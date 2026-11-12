// 异步编程基准：Go
//
// goroutine + 运行时 netpoller（GOMAXPROCS=1，和 c6/C epoll 一样是单线程
// 事件循环的量级），指标与 bench.c6 / bench_c.c / bench_cpp.cpp /
// bench_rs.rs 对齐：
//
//	create  建 200000 个 goroutine 并等它们结束，报每个多少纳秒
//	switch  一次「等待 → 另一方就绪 → 醒来」的往返，跑 50000 轮
//	conns   1000 路并发连接（每路一对管道、一个等着的 goroutine），
//	        一次把 1000 路服务完，报每路多少微秒 + 每个挂起任务的 RSS 增量
//
// 运行：go run bench/async/bench_go.go
package main

import (
	"fmt"
	"os"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

const (
	createN = 200000
	switchN = 50000
	connN   = 1000
)

func rssKB() int64 {
	b, err := os.ReadFile("/proc/self/statm")
	if err != nil {
		return -1
	}
	f := strings.Fields(string(b))
	if len(f) < 2 {
		return -1
	}
	res, err := strconv.ParseInt(f[1], 10, 64)
	if err != nil {
		return -1
	}
	return res * 4
}

func raiseFds() {
	var rl syscall.Rlimit
	if syscall.Getrlimit(syscall.RLIMIT_NOFILE, &rl) != nil {
		return
	}
	if rl.Cur >= 4096 {
		return
	}
	rl.Cur = 4096
	if rl.Max < 4096 {
		rl.Cur = rl.Max
	}
	syscall.Setrlimit(syscall.RLIMIT_NOFILE, &rl)
}

func benchCreate() {
	var wg sync.WaitGroup
	t0 := time.Now()
	wg.Add(createN)
	for i := 0; i < createN; i++ {
		go func() { wg.Done() }()
	}
	wg.Wait()
	fmt.Printf("create_ns_per_task,%.1f\n", float64(time.Since(t0).Nanoseconds())/createN)
}

// 一次往返：主 goroutine 发一个字节，另一个 goroutine 收到后回一个字节
func benchSwitch() {
	ping := make(chan struct{})
	pong := make(chan struct{})
	done := make(chan struct{})
	go func() {
		for i := 0; i < switchN; i++ {
			<-ping
			pong <- struct{}{}
		}
		close(done)
	}()
	t0 := time.Now()
	for i := 0; i < switchN; i++ {
		ping <- struct{}{}
		<-pong
	}
	d := time.Since(t0)
	<-done
	fmt.Printf("switch_ns_per_round,%.1f\n", float64(d.Nanoseconds())/switchN)
}

func benchConns() {
	var wg sync.WaitGroup
	rf := make([]*os.File, connN)
	wf := make([]*os.File, connN)
	for i := 0; i < connN; i++ {
		r, w, err := os.Pipe()		// os.Pipe 走 netpoller：非阻塞 + epoll 注册
		if err != nil {
			fmt.Printf("conns_error,pipe %d\n", i)
			return
		}
		rf[i] = r
		wf[i] = w
	}
	before := rssKB()
	wg.Add(connN)
	for i := 0; i < connN; i++ {
		go func(f *os.File) {
			buf := make([]byte, 1)
			f.Read(buf)
			wg.Done()
		}(rf[i])
	}
	time.Sleep(50 * time.Millisecond)	// 让 1000 个 goroutine 都停在 netpoller 上
	after := rssKB()
	t0 := time.Now()
	for i := 0; i < connN; i++ {
		wf[i].Write([]byte("x"))
	}
	wg.Wait()
	d := time.Since(t0)
	fmt.Printf("conns_us_per_conn,%.3f\n", float64(d.Nanoseconds())/1000.0/connN)
	if before > 0 && after > 0 {
		fmt.Printf("rss_bytes_per_task,%.1f\n", float64(after-before)*1024.0/connN)
	} else {
		fmt.Printf("rss_bytes_per_task,-1\n")
	}
}

func main() {
	// BENCH_SEC 选跑哪一段：create / switch / conns（默认全跑），
	// 每个指标单独一个进程，RSS 增量才是干净的
	runtime.GOMAXPROCS(1)
	fmt.Println("lang,go(goroutine+netpoller)")
	raiseFds()
	sec := os.Getenv("BENCH_SEC")
	if sec == "" || sec == "all" || sec == "create" {
		benchCreate()
	}
	if sec == "" || sec == "all" || sec == "switch" {
		benchSwitch()
	}
	if sec == "" || sec == "all" || sec == "conns" {
		benchConns()
	}
}
