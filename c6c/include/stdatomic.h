/* c6c 自带 stdatomic.h
 *
 * 原子操作走前端内建：src/parse.c6 认得 `__atomic_*` 这些名字，把它们翻成
 * lib/rt.c6 里 __c6_at_rmw / __c6_at_xchg / __c6_at_cas 的调用（全局自旋锁
 * 里一起做完「读 → 算 → 写」）。所以这个头文件**不要**再把它们定义成
 * `*(p) += (v)` 这种宏：单线程下等价，多线程下就是普通读改写，丢更新
 * （tcc tests2/124_atomic_counter 里 16 个真线程各加一万次，三行全 FAILURE）。
 *
 * 内存序参数仍然要**求值**（哪怕值被忽略）——真实代码会传有副作用的表达式
 * （tcc tests2 的 151_atomic_header 就在数这个），内建那边会把参数全求值。
 */
#ifndef C6C_STDATOMIC_H
#define C6C_STDATOMIC_H

/* wchar_t 由 stddef.h 提供（`typedef int wchar_t;`）。不先引进来，
 * `typedef wchar_t atomic_wchar_t;` 就会变成"unknown type name"——
 * 而且只在独立包含 stdatomic.h 时炸，别的翻译单元里看不出来。 */
#include <stddef.h>

typedef enum {
    memory_order_relaxed = 0,
    memory_order_consume = 1,
    memory_order_acquire = 2,
    memory_order_release = 3,
    memory_order_acq_rel = 4,
    memory_order_seq_cst = 5
} memory_order;

typedef _Bool atomic_bool;
typedef char atomic_char;
typedef signed char atomic_schar;
typedef unsigned char atomic_uchar;
typedef short atomic_short;
typedef unsigned short atomic_ushort;
typedef int atomic_int;
typedef unsigned int atomic_uint;
typedef long atomic_long;
typedef unsigned long atomic_ulong;
typedef long long atomic_llong;
typedef unsigned long long atomic_ullong;
typedef void *atomic_ptr;
typedef unsigned long atomic_size_t;
typedef long atomic_ptrdiff_t;
typedef long atomic_intptr_t;
typedef unsigned long atomic_uintptr_t;
typedef short atomic_short;
typedef wchar_t atomic_wchar_t;

#define __ATOMIC_RELAXED 0
#define __ATOMIC_CONSUME 1
#define __ATOMIC_ACQUIRE 2
#define __ATOMIC_RELEASE 3
#define __ATOMIC_ACQ_REL 4
#define __ATOMIC_SEQ_CST 5

#define _Atomic(T) T
#define ATOMIC_VAR_INIT(v) (v)
#define ATOMIC_FLAG_INIT {0}
#define atomic_init(p, v) (*(p) = (v))

/* 栅栏做成真函数：有些用例会取它的地址 */
static void atomic_thread_fence(memory_order mo) { (void)mo; }
static void atomic_signal_fence(memory_order mo) { (void)mo; }
#define __atomic_thread_fence(mo) ((void)(mo))
#define __atomic_signal_fence(mo) ((void)(mo))
#define __atomic_always_lock_free(sz, p) 1
#define __atomic_is_lock_free(sz, p) 1
#define kill_dependency(x) (x)

/* GCC 风格 __atomic_* 是**内建**（src/parse.c6 + lib/rt.c6），这里刻意
 * 不写 `#define __atomic_fetch_add(...)`，写了就把内建挡住了。 */
#define atomic_load(p) __atomic_load_n(p, __ATOMIC_SEQ_CST)
#define atomic_load_explicit(p, mo) __atomic_load_n(p, mo)
#define atomic_store(p, v) __atomic_store_n(p, v, __ATOMIC_SEQ_CST)
#define atomic_store_explicit(p, v, mo) __atomic_store_n(p, v, mo)
#define atomic_exchange_explicit(p, v, mo) __atomic_exchange_n(p, v, mo)

#define atomic_fetch_add_explicit(p, v, mo) __atomic_fetch_add(p, v, mo)
#define atomic_fetch_sub_explicit(p, v, mo) __atomic_fetch_sub(p, v, mo)
#define atomic_fetch_and_explicit(p, v, mo) __atomic_fetch_and(p, v, mo)
#define atomic_fetch_or_explicit(p, v, mo) __atomic_fetch_or(p, v, mo)
#define atomic_fetch_xor_explicit(p, v, mo) __atomic_fetch_xor(p, v, mo)
#define atomic_fetch_add(p, v) atomic_fetch_add_explicit(p, v, memory_order_seq_cst)
#define atomic_fetch_sub(p, v) atomic_fetch_sub_explicit(p, v, memory_order_seq_cst)
#define atomic_fetch_and(p, v) atomic_fetch_and_explicit(p, v, memory_order_seq_cst)
#define atomic_fetch_or(p, v) atomic_fetch_or_explicit(p, v, memory_order_seq_cst)
#define atomic_fetch_xor(p, v) atomic_fetch_xor_explicit(p, v, memory_order_seq_cst)

#define atomic_compare_exchange_strong(p, e, d) \
    __atomic_compare_exchange_n(p, e, d, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#define atomic_compare_exchange_weak(p, e, d) \
    __atomic_compare_exchange_n(p, e, d, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#define atomic_compare_exchange_strong_explicit(p, e, d, mo, f) \
    __atomic_compare_exchange_n(p, e, d, 0, mo, f)
#define atomic_compare_exchange_weak_explicit(p, e, d, mo, f) \
    __atomic_compare_exchange_n(p, e, d, 1, mo, f)

/* atomic_flag 是可读的结构体（`->v`）；运行期 __c6_xchg8 就是一条 xchg
 * 指令（硬件原子），不走那把全局锁——这个接口的典型用法是自旋锁，
 * 等待者每圈都来抢一次。 */
long __c6_xchg8(void *p, long v);

typedef struct {
    unsigned char v;
} atomic_flag;

#define atomic_flag_test_and_set(p) ((int)__c6_xchg8(&(p)->v, 1))
#define atomic_flag_test_and_set_explicit(p, mo) \
    ((void)(mo), (int)__c6_xchg8(&(p)->v, 1))
#define atomic_flag_clear(p) ((void)((p)->v = 0))
#define atomic_flag_clear_explicit(p, mo) ((void)(mo), (void)((p)->v = 0))

#define atomic_is_lock_free(p) 1

#endif /* C6C_STDATOMIC_H */
