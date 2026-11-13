// bench/async/tokio_bench —— Rust 最流行的异步框架 tokio 的同一套基准
//
// 指标与 bench.c6 / bench_c.c / bench_cpp.cpp / bench_rs.rs / bench_go.go 对齐：
//   create  建 200000 个任务并跑完，报每个多少纳秒
//   switch  一次「写一个字节 → await 读回来」的往返，跑 50000 轮（走 tokio 的
//           epoll netpoller + read 系统调用）
//   conns   1000 路并发连接（1000 个 tokio 任务各自 await 读），一次性全部喂满，
//           报每路多少微秒 + 每个挂起任务的 RSS 增量
//
// 两种运行时：
//   ct（默认）  current_thread：单线程执行器，和 c6 的 __c6_co_run() 同口径
//   mt          multi_thread(4)：tokio 的默认形态，work-stealing 4 线程
//
// 编译/运行：
//   cargo build --release            # 依赖已在 ~/.cargo 里，加 --offline 也行
//   BENCH_SEC=conns ./target/release/bench_tokio      # current_thread
//   BENCH_SEC=conns ./target/release/bench_tokio mt   # multi_thread(4)
use std::time::Instant;

use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::UnixStream;

const CREATE_N: usize = 200_000;
const SWITCH_N: usize = 50_000;
const CONN_N: usize = 1000;
const RLIMIT_NOFILE: i32 = 7;

#[repr(C)]
struct Rlimit {
    cur: u64,
    max: u64,
}

extern "C" {
    fn getrlimit(res: i32, rl: *mut Rlimit) -> i32;
    fn setrlimit(res: i32, rl: *const Rlimit) -> i32;
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

/* ---- 1. 任务创建 ---- */

async fn bench_create() {
    let t0 = Instant::now();
    let mut acc = 0usize;

    for i in 0..CREATE_N {
        let h = tokio::spawn(async move { i + 1 });
        acc = acc.wrapping_add(h.await.unwrap_or(0));
    }
    let dt = t0.elapsed().as_nanos() as f64;
    println!("create_ns_per_task,{:.1}", dt / CREATE_N as f64);
    if acc == 0 {
        println!("create_note,acc 0");
    }
}

/* ---- 2. 一次「写 → await 读」往返 ---- */

async fn bench_switch() {
    let (mut a, mut b) = match UnixStream::pair() {
        Ok(p) => p,
        Err(_) => {
            println!("switch_error,pair");
            return;
        }
    };
    let mut buf = [0u8; 1];
    let t0 = Instant::now();
    for _ in 0..SWITCH_N {
        if a.write_all(&[b'x']).await.is_err() {
            println!("switch_error,write");
            return;
        }
        match b.read(&mut buf).await {
            Ok(1) => {}
            _ => {
                println!("switch_error,read");
                return;
            }
        }
    }
    let dt = t0.elapsed().as_nanos() as f64;
    println!("switch_ns_per_round,{:.1}", dt / SWITCH_N as f64);
}

/* ---- 3. 1000 路并发 + RSS 密度 ---- */

async fn bench_conns() {
    let mut pairs: Vec<(UnixStream, UnixStream)> = Vec::with_capacity(CONN_N);

    for i in 0..CONN_N {
        match UnixStream::pair() {
            Ok(p) => pairs.push(p),
            Err(_) => {
                println!("conns_error,pair {}", i);
                return;
            }
        }
    }
    let before = rss_kb();
    let mut writers: Vec<UnixStream> = Vec::with_capacity(CONN_N);
    let mut handles = Vec::with_capacity(CONN_N);

    for (a, mut b) in pairs.drain(..) {
        writers.push(a);
        handles.push(tokio::spawn(async move {
            let mut buf = [0u8; 1];

            b.read(&mut buf).await.unwrap_or(0)
        }));
    }
    /* 让 1000 个任务都跑到第一次 Pending（挂到 epoll 上）再量 RSS */
    tokio::time::sleep(std::time::Duration::from_millis(50)).await;
    let after = rss_kb();
    let t0 = Instant::now();
    for w in writers.iter_mut() {
        if w.write_all(&[b'x']).await.is_err() {
            println!("conns_error,write");
            return;
        }
    }
    let mut done = 0usize;
    for h in handles {
        if h.await.unwrap_or(0) == 1 {
            done += 1;
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
    if done != CONN_N {
        println!("conns_note,done {}", done);
    }
}

async fn sections(sec: &str) {
    let want = |name: &str| sec.is_empty() || sec == "all" || sec == name;

    if want("create") {
        bench_create().await;
    }
    if want("switch") {
        bench_switch().await;
    }
    if want("conns") {
        bench_conns().await;
    }
}

fn main() {
    let mode = std::env::args().nth(1).unwrap_or_default();
    let sec = std::env::var("BENCH_SEC").unwrap_or_default();
    let multi = mode == "mt";

    println!(
        "lang,{}",
        if multi {
            "rust(tokio multi_thread×4)"
        } else {
            "rust(tokio current_thread)"
        }
    );
    raise_fds();
    let sec_owned = sec.clone();
    if multi {
        let rt = tokio::runtime::Builder::new_multi_thread()
            .worker_threads(4)
            .enable_all()
            .build()
            .expect("tokio runtime");
        rt.block_on(async move { sections(&sec_owned).await });
    } else {
        let rt = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .expect("tokio runtime");
        rt.block_on(async move { sections(&sec_owned).await });
    }
}
