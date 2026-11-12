// 异步编程基准：Rust
//
// 两种机制：
//   async    async/await + 手写的最小执行器（epoll + Waker，不用 tokio：
//            离线环境里没有 crates.io，而 tokio 就是这么一个执行器）
//   threads  std::thread（Rust 标准库自带的并发写法）
//
// 指标与 bench.c6 / bench_c.c / bench_cpp.cpp / bench_go.go 对齐：
//   create  建 200000 个任务并跑完，报每个多少纳秒
//   switch  一次「挂起在 fd 上 → 内核就绪 → 恢复」的往返，跑 50000 轮
//   conns   1000 路并发连接，执行器一次把 1000 路服务完，报每路多少微秒
//           + 每个挂起任务的 RSS 增量
//
// 编译/运行：
//   rustc -O -o build/bench_rs bench/async/bench_rs.rs
//   BENCH_SEC=conns ./build/bench_rs            # async
//   BENCH_SEC=conns ./build/bench_rs threads    # std 线程
use std::collections::HashMap;
use std::future::Future;
use std::pin::Pin;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::task::{Context, Poll, Wake, Waker};
use std::time::Instant;

const CREATE_N: usize = 200_000;
const SWITCH_N: usize = 50_000;
const CONN_N: usize = 1000;

const EPOLLIN: u32 = 0x001;
const EPOLLONESHOT: u32 = 0x4000_0000;
const EPOLL_CTL_ADD: i32 = 1;
const EPOLL_CTL_MOD: i32 = 3;
const RLIMIT_NOFILE: i32 = 7;

#[repr(C, packed)]
#[derive(Clone, Copy)]
struct EpollEvent {
    events: u32,
    data: u64,
}

#[repr(C)]
struct Rlimit {
    cur: u64,
    max: u64,
}

extern "C" {
    fn epoll_create1(flags: i32) -> i32;
    fn epoll_ctl(epfd: i32, op: i32, fd: i32, ev: *mut EpollEvent) -> i32;
    fn epoll_wait(epfd: i32, evs: *mut EpollEvent, max: i32, timeout: i32) -> i32;
    fn pipe(fds: *mut i32) -> i32;
    fn read(fd: i32, buf: *mut u8, n: usize) -> isize;
    fn write(fd: i32, buf: *const u8, n: usize) -> isize;
    fn getrlimit(res: i32, rl: *mut Rlimit) -> i32;
    fn setrlimit(res: i32, rl: *const Rlimit) -> i32;
}

static EPFD: OnceLock<i32> = OnceLock::new();
static ARMED: OnceLock<Mutex<HashMap<i32, bool>>> = OnceLock::new();
static WAKERS: OnceLock<Mutex<HashMap<i32, Waker>>> = OnceLock::new();

fn epfd() -> i32 {
    *EPFD.get_or_init(|| unsafe { epoll_create1(0) })
}

fn armed() -> &'static Mutex<HashMap<i32, bool>> {
    ARMED.get_or_init(|| Mutex::new(HashMap::new()))
}

fn wakers() -> &'static Mutex<HashMap<i32, Waker>> {
    WAKERS.get_or_init(|| Mutex::new(HashMap::new()))
}

fn raise_fds() {
    let mut rl = Rlimit { cur: 0, max: 0 };

    unsafe {
        if getrlimit(RLIMIT_NOFILE, &mut rl) != 0 {
            return;
        }
        if rl.cur >= 4096 {
            return;
        }
        rl.cur = if rl.max < 4096 { rl.max } else { 4096 };
        setrlimit(RLIMIT_NOFILE, &rl);
    }
}

fn rss_kb() -> i64 {
    let s = match std::fs::read_to_string("/proc/self/statm") {
        Ok(s) => s,
        Err(_) => return -1,
    };
    let mut it = s.split_whitespace();
    let _total = it.next();
    match it.next().and_then(|v| v.parse::<i64>().ok()) {
        Some(pages) => pages * 4,
        None => -1,
    }
}

/* 把 fd 挂到 epoll 上等可读（EPOLLONESHOT：内核触发一次后自动解除，
   下次 await 用 EPOLL_CTL_MOD 重新装上——和 c6 的运行时同一个做法） */
fn arm(fd: i32, waker: Waker) {
    let mut ev = EpollEvent {
        events: EPOLLIN | EPOLLONESHOT,
        data: fd as u64,
    };
    let known = armed().lock().unwrap().contains_key(&fd);
    let ep = epfd();
    let r = unsafe {
        if known {
            epoll_ctl(ep, EPOLL_CTL_MOD, fd, &mut ev)
        } else {
            epoll_ctl(ep, EPOLL_CTL_ADD, fd, &mut ev)
        }
    };
    if r != 0 {
        unsafe {
            epoll_ctl(ep, EPOLL_CTL_ADD, fd, &mut ev);
        }
    }
    armed().lock().unwrap().insert(fd, true);
    wakers().lock().unwrap().insert(fd, waker);
}

/* 一次 epoll_wait，返回就绪的 fd（-1 = 没有） */
fn wait_fd() -> i32 {
    let mut ev = EpollEvent { events: 0, data: 0 };
    let n = unsafe { epoll_wait(epfd(), &mut ev, 1, -1) };

    if n < 1 {
        return -1;
    }
    let data = { ev.data };
    data as i32
}

/* 一次 epoll_wait 并唤醒对应的任务 */
fn wait_and_wake() {
    let fd = wait_fd();

    if fd < 0 {
        return;
    }
    let w = wakers().lock().unwrap().remove(&fd);
    if let Some(w) = w {
        w.wake();
    }
}

struct Flag {
    woken: AtomicBool,
}

impl Wake for Flag {
    fn wake(self: Arc<Self>) {
        self.woken.store(true, Ordering::Relaxed);
    }
    fn wake_by_ref(self: &Arc<Self>) {
        self.woken.store(true, Ordering::Relaxed);
    }
}

/* 等 fd 可读：poll 时挂到 epoll 上，就绪后把字节读走 */
struct ReadReady {
    fd: i32,
    parked: bool,
}

impl Future for ReadReady {
    type Output = isize;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<isize> {
        let me = self.get_mut();

        if me.parked {
            let mut b = [0u8; 1];
            let n = unsafe { read(me.fd, b.as_mut_ptr(), 1) };
            return Poll::Ready(n);
        }
        arm(me.fd, cx.waker().clone());
        me.parked = true;
        /* 已经可读的话内核立刻就会报，下一次 poll 就完成 */
        Poll::Pending
    }
}

fn block_on<F: Future>(fut: F) -> F::Output {
    let flag = Arc::new(Flag {
        woken: AtomicBool::new(false),
    });
    let waker = Waker::from(flag.clone());
    let mut cx = Context::from_waker(&waker);
    let mut fut = fut;
    let mut pinned = unsafe { Pin::new_unchecked(&mut fut) };

    loop {
        match pinned.as_mut().poll(&mut cx) {
            Poll::Ready(v) => return v,
            Poll::Pending => wait_and_wake(),
        }
    }
}

async fn trivial(v: usize) -> usize {
    v + 1
}

/* ---- 三项指标 ---- */

fn bench_create() {
    let t0 = Instant::now();
    let mut acc = 0usize;

    for i in 0..CREATE_N {
        acc = acc.wrapping_add(block_on(trivial(i)));
    }
    println!(
        "create_ns_per_task,{:.1}",
        t0.elapsed().as_nanos() as f64 / CREATE_N as f64
    );
    if acc == 0 {
        println!("create_note,acc 0");
    }
}

fn bench_switch() {
    let mut fds = [0i32; 2];

    unsafe {
        if pipe(fds.as_mut_ptr()) != 0 {
            return;
        }
    }
    let t0 = Instant::now();
    for _ in 0..SWITCH_N {
        let b = [b'x'];
        unsafe {
            write(fds[1], b.as_ptr(), 1);
        }
        let _n = block_on(ReadReady {
            fd: fds[0],
            parked: false,
        });
    }
    println!(
        "switch_ns_per_round,{:.1}",
        t0.elapsed().as_nanos() as f64 / SWITCH_N as f64
    );
}

fn bench_conns() {
    let mut rfds = vec![0i32; CONN_N];
    let mut wfds = vec![0i32; CONN_N];

    for i in 0..CONN_N {
        let mut fds = [0i32; 2];

        unsafe {
            if pipe(fds.as_mut_ptr()) != 0 {
                println!("conns_error,pipe {}", i);
                return;
            }
        }
        rfds[i] = fds[0];
        wfds[i] = fds[1];
    }
    let before = rss_kb();
    let mut idx: HashMap<i32, usize> = HashMap::new();
    let mut futs: Vec<Pin<Box<ReadReady>>> = Vec::with_capacity(CONN_N);

    for i in 0..CONN_N {
        idx.insert(rfds[i], i);
        futs.push(Box::pin(ReadReady {
            fd: rfds[i],
            parked: false,
        }));
    }
    let flag = Arc::new(Flag {
        woken: AtomicBool::new(false),
    });
    let waker = Waker::from(flag.clone());
    let mut cx = Context::from_waker(&waker);
    for f in futs.iter_mut() {
        let _ = f.as_mut().poll(&mut cx); /* 全部挂到 epoll 上 */
    }
    let after = rss_kb();
    let t0 = Instant::now();
    for i in 0..CONN_N {
        let b = [b'x'];
        unsafe {
            write(wfds[i], b.as_ptr(), 1);
        }
    }
    let mut done = 0usize;
    while done < CONN_N {
        let fd = wait_fd();

        if fd < 0 {
            break;
        }
        if let Some(&i) = idx.get(&fd) {
            if let Poll::Ready(_) = futs[i].as_mut().poll(&mut cx) {
                done += 1;
            }
        }
    }
    let dt = t0.elapsed().as_nanos() as f64;
    println!("conns_us_per_conn,{:.3}", dt / 1000.0 / CONN_N as f64);
    if before > 0 && after > 0 {
        println!(
            "rss_bytes_per_task,{:.1}",
            (after - before) as f64 * 1024.0 / CONN_N as f64
        );
    } else {
        println!("rss_bytes_per_task,-1");
    }
}

/* ---- std::thread 模式 ---- */

fn bench_create_threads() {
    let t0 = Instant::now();
    for i in 0..CREATE_N {
        let h = std::thread::spawn(move || i + 1);
        let _ = h.join();
    }
    println!(
        "create_ns_per_task,{:.1}",
        t0.elapsed().as_nanos() as f64 / CREATE_N as f64
    );
}

fn bench_switch_threads() {
    let (to_child, child_rx) = std::sync::mpsc::channel::<u8>();
    let (to_parent, parent_rx) = std::sync::mpsc::channel::<u8>();
    let h = std::thread::spawn(move || {
        for _ in 0..SWITCH_N {
            if child_rx.recv().is_err() {
                break;
            }
            if to_parent.send(1u8).is_err() {
                break;
            }
        }
    });
    let t0 = Instant::now();
    for _ in 0..SWITCH_N {
        let _ = to_child.send(1u8);
        let _ = parent_rx.recv();
    }
    println!(
        "switch_ns_per_round,{:.1}",
        t0.elapsed().as_nanos() as f64 / SWITCH_N as f64
    );
    let _ = h.join();
}

fn bench_conns_threads() {
    let mut rfds = vec![0i32; CONN_N];
    let mut wfds = vec![0i32; CONN_N];

    for i in 0..CONN_N {
        let mut fds = [0i32; 2];

        unsafe {
            if pipe(fds.as_mut_ptr()) != 0 {
                println!("conns_error,pipe {}", i);
                return;
            }
        }
        rfds[i] = fds[0];
        wfds[i] = fds[1];
    }
    let before = rss_kb();
    let mut hs = Vec::with_capacity(CONN_N);
    for i in 0..CONN_N {
        let fd = rfds[i];
        hs.push(std::thread::spawn(move || {
            let mut b = [0u8; 1];
            unsafe {
                read(fd, b.as_mut_ptr(), 1);
            }
        }));
    }
    std::thread::sleep(std::time::Duration::from_millis(50));
    let after = rss_kb();
    let t0 = Instant::now();
    for i in 0..CONN_N {
        let b = [b'x'];
        unsafe {
            write(wfds[i], b.as_ptr(), 1);
        }
    }
    for h in hs {
        let _ = h.join();
    }
    let dt = t0.elapsed().as_nanos() as f64;
    println!("conns_us_per_conn,{:.3}", dt / 1000.0 / CONN_N as f64);
    if before > 0 && after > 0 {
        println!(
            "rss_bytes_per_task,{:.1}",
            (after - before) as f64 * 1024.0 / CONN_N as f64
        );
    } else {
        println!("rss_bytes_per_task,-1");
    }
}

fn main() {
    let threads = std::env::args().nth(1).as_deref() == Some("threads");
    let sec = std::env::var("BENCH_SEC").unwrap_or_default();
    let want = |name: &str| sec.is_empty() || sec == "all" || sec == name;

    println!(
        "lang,{}",
        if threads {
            "rust(std 线程)"
        } else {
            "rust(async 手写执行器)"
        }
    );
    raise_fds();
    if threads {
        if want("create") {
            bench_create_threads();
        }
        if want("switch") {
            bench_switch_threads();
        }
        if want("conns") {
            bench_conns_threads();
        }
    } else {
        if want("create") {
            bench_create();
        }
        if want("switch") {
            bench_switch();
        }
        if want("conns") {
            bench_conns();
        }
    }
}
