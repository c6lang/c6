/* ------------------------------------------------------------------------
   c6 runtime prelude for the C99 back-end.
   This text is appended to every generated file that uses c6 features.
   It is ordinary C99 (plus POSIX threads/select for async and go).
   ------------------------------------------------------------------------ */
#include <stdlib.h>

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
# define C99_RT_POSIX 1
# include <pthread.h>
# include <sys/select.h>
#else
# define C99_RT_POSIX 0
#endif

#if defined(__GNUC__) || defined(__clang__)
# define C99_RT_UNUSED __attribute__((unused))
# define __c99_rt_linkage __attribute__((weak))
#else
# define C99_RT_UNUSED
# define __c99_rt_linkage static
#endif

/* c6 extension runtime: defer / errdefer / err                          */
/*                                                                           */
/* Deferred statements are recorded on a per-thread stack.  Each function     */
/* stores the stack top at entry ("frame base") in a local slot and passes it */
/* to every cleanup request, so a cleanup never touches records that belong   */
/* to the caller's frames.  Scope cleanups additionally filter by lexical     */
/* depth, which makes conditional registration and loops work.               */
/*                                                                           */
/* Every OS thread owns its defer stack, error slot and coroutine pool, so a  */
/* thread that runs c6 code cannot corrupt the state of another one.  The     */
/* per-thread block is found through a pthread key: '-run' rejects TLS        */
/* sections, so '__thread' cannot be used.  A thread allocates its block      */
/* lazily, the first time it touches the c6 runtime.                         */

#define C99_DEFER_MAX     8192
#define C99_DEFER_CTX_MAX 512

#define C99_DEFER_KIND_DEFER    0
#define C99_DEFER_KIND_ERRDEFER 1

#define C99_DEFER_MODE_SCOPE 0   /* run records of scopes deeper than depth */
#define C99_DEFER_MODE_FUNC  1   /* run all records of this function frame */
#define C99_DEFER_MODE_ERR   2   /* run only errdefer records, frame wide   */

typedef struct C99DeferRec {
    int id;      /* cleanup site id (> 0) */
    int depth;   /* lexical scope depth at registration */
    int kind;    /* C99_DEFER_KIND_* */
} C99DeferRec;

typedef struct C99DeferCtx {
    int mode;    /* C99_DEFER_MODE_* */
    int depth;   /* target scope depth (scope mode) */
    int resume;  /* resume id (reported as -resume when finished) */
    int base;    /* frame base: never pop records below this index */
} C99DeferCtx;

typedef struct C99CoPool C99CoPool;   /* completed with the async runtime */
typedef struct C99ThRec C99ThRec;     /* threads started by this thread */
typedef struct C99GcHdr C99GcHdr;     /* completed with the gc runtime */

typedef struct C99ThCtx {
    C99DeferRec *dstack;  int dsp, stack_cap;
    C99DeferCtx *dctx;    int dcsp, ctx_cap;
    int err_flag, err_id;
    const char *err_msg;
    C99GcHdr *gc_pending; /* objects whose count reached zero */
    C99GcHdr *gc_pending_tail;
    int gc_pending_n;
    int gc_draining;      /* a drain loop is already running on this thread */
    C99CoPool *co;
    C99ThRec *th_list;    /* children, joined when the thread exits */
    int th_freed;
} C99ThCtx;


/* a thread started by 'go', always joined by the thread that started it */
struct C99ThRec {
    C99ThRec *next;
#if C99_RT_POSIX
    pthread_t tid;
#else
    void *handle;
#endif
};

C99_RT_UNUSED __c99_rt_linkage void __c99_trap(void)
{
    volatile int *p = (volatile int *)0;
    *p = 0;
}

C99_RT_UNUSED __c99_rt_linkage void *__c99_alloc(unsigned long size)
{
    void *p = calloc(1, size);

    if (!p)
        __c99_trap();
    return p;
}

C99_RT_UNUSED __c99_rt_linkage void __c99_co_free(C99CoPool *co);
C99_RT_UNUSED __c99_rt_linkage void __c99_gc_free_all(C99ThCtx *th);

/* Join every thread this thread started.  A 'go' statement has no place
   where it could be joined, so the starter reaps its threads when it exits;
   the main thread does it from exit(), early enough that -run can still tear
   the JIT memory down afterwards. */
C99_RT_UNUSED __c99_rt_linkage void __c99_join_children(C99ThCtx *th)
{
    C99ThRec *r;

    while ((r = th->th_list) != 0) {
        th->th_list = r->next;
#if C99_RT_POSIX
        pthread_join(r->tid, (void **)0);
#endif
        free(r);
    }
}

C99_RT_UNUSED __c99_rt_linkage void __c99_th_free(C99ThCtx *th)
{
    if (!th || th->th_freed)
        return;
    th->th_freed = 1;
    __c99_gc_free_all(th);
    __c99_join_children(th);
    if (th->dstack)
        free(th->dstack);
    if (th->dctx)
        free(th->dctx);
    if (th->co)
        __c99_co_free(th->co);
    free(th);
}

#if !C99_RT_POSIX
C99_RT_UNUSED __c99_rt_linkage C99ThCtx __c99_th_single;

C99_RT_UNUSED __c99_rt_linkage C99ThCtx *__c99_th(void)
{
    return &__c99_th_single;
}
#else
C99_RT_UNUSED __c99_rt_linkage pthread_key_t __c99_th_key;
C99_RT_UNUSED __c99_rt_linkage pthread_once_t __c99_th_once;

C99_RT_UNUSED __c99_rt_linkage void __c99_th_key_init(void)
{
    if (pthread_key_create(&__c99_th_key, (void (*)(void *))__c99_th_free) != 0)
        __c99_trap();
}

/* The per-thread block of the calling thread, allocated on first use. */
C99_RT_UNUSED __c99_rt_linkage C99ThCtx *__c99_th(void)
{
    C99ThCtx *th;

    if (pthread_once(&__c99_th_once, __c99_th_key_init) != 0)
        __c99_trap();
    th = (C99ThCtx *)pthread_getspecific(__c99_th_key);
    if (!th) {
        th = (C99ThCtx *)__c99_alloc(sizeof *th);
        if (pthread_setspecific(__c99_th_key, th) != 0)
            __c99_trap();
    }
    return th;
}
#endif

/* The argument block of one 'go f(a);'.  The thread that runs the call
   releases it, so a failed start releases it here. */
C99_RT_UNUSED __c99_rt_linkage void *__tcc_sp_args(unsigned long size)
{
    return __c99_alloc(size);
}

typedef struct C99ThStart {
    void *(*fn)(void *);
    void *arg;
} C99ThStart;

/* Thread entry: unpack the call, release the block, drop the result. */
C99_RT_UNUSED __c99_rt_linkage void *__c99_th_tramp(void *p)
{
    C99ThStart st = *(C99ThStart *)p;
    void *ret;

    free(p);
    ret = st.fn(st.arg);
    free(st.arg);
    return ret;
}

/* __tcc_th_start(&trampoline, block): run the trampoline on a new thread.
   The block belongs to the thread from here on, and the thread is joined by
   the thread that started it.  Returns 0 when the thread was started. */
C99_RT_UNUSED __c99_rt_linkage int __tcc_th_start(void *(*fn)(void *), void *arg)
{
#if !C99_RT_POSIX
    (void)fn;
    (void)arg;
    __c99_trap();              /* the PE port has no thread support yet */
    return -1;
#else
    C99ThCtx *th = __c99_th();
    C99ThRec *r;
    C99ThStart *st;
    pthread_t tid;

    st = (C99ThStart *)__c99_alloc(sizeof *st);
    st->fn = fn;
    st->arg = arg;
    if (pthread_create(&tid, (const void *)0, __c99_th_tramp, st) != 0) {
        free(st);
        free(arg);
        return -1;
    }
    r = (C99ThRec *)__c99_alloc(sizeof *r);
    r->tid = tid;
    r->next = th->th_list;
    th->th_list = r;
    return 0;
#endif
}

/* Wait for the threads this thread started.  A 'go' statement has no place
   where it could be joined, so the compiler calls this at the end of main
   and the test runner calls it when the tests are done; a thread that was
   started by another threads is joined when that thread exits.  Doing it from
   the program keeps -run safe: the JIT memory is only released afterwards. */
C99_RT_UNUSED __c99_rt_linkage void __c6_th_join_all(void)
{
    __c99_join_children(__c99_th());
}

C99_RT_UNUSED __c99_rt_linkage C99DeferRec *__c99_dstack(C99ThCtx *th)
{
    if (!th->dstack) {
        th->dstack = (C99DeferRec *)__c99_alloc(C99_DEFER_MAX
                                                * sizeof(C99DeferRec));
        th->stack_cap = C99_DEFER_MAX;
    }
    return th->dstack;
}

C99_RT_UNUSED __c99_rt_linkage C99DeferCtx *__c99_dctx(C99ThCtx *th)
{
    if (!th->dctx) {
        th->dctx = (C99DeferCtx *)__c99_alloc(C99_DEFER_CTX_MAX
                                              * sizeof(C99DeferCtx));
        th->ctx_cap = C99_DEFER_CTX_MAX;
    }
    return th->dctx;
}

/* Called once at function entry: remember where the frame starts.  The
   result is stored through OUT because helper functions are void-typed. */
C99_RT_UNUSED __c99_rt_linkage void __tcc_defer_func_enter(int *out)
{
    *out = __c99_th()->dsp;
}

C99_RT_UNUSED __c99_rt_linkage void __tcc_defer_push(int id, int depth, int kind)
{
    C99ThCtx *th = __c99_th();
    C99DeferRec *r;

    if (th->dsp >= C99_DEFER_MAX)
        return;
    r = &__c99_dstack(th)[th->dsp++];
    r->id = id;
    r->depth = depth;
    r->kind = kind;
}

C99_RT_UNUSED __c99_rt_linkage void __tcc_defer_begin_scope(int depth, int resume, int base)
{
    C99ThCtx *th = __c99_th();
    C99DeferCtx *c;

    if (th->dcsp >= C99_DEFER_CTX_MAX)
        return;
    c = &__c99_dctx(th)[th->dcsp++];
    c->mode = C99_DEFER_MODE_SCOPE;
    c->depth = depth;
    c->resume = resume;
    c->base = base;
}

C99_RT_UNUSED __c99_rt_linkage void __tcc_defer_begin_func(int mode, int resume, int base)
{
    C99ThCtx *th = __c99_th();
    C99DeferCtx *c;

    if (th->dcsp >= C99_DEFER_CTX_MAX)
        return;
    c = &__c99_dctx(th)[th->dcsp++];
    c->mode = mode;
    c->depth = 0;
    c->resume = resume;
    c->base = base;
}

/* Store the next cleanup site id to run (> 0), or -resume when the current
   cleanup request is finished, through OUT so generated code needs no
   return-value handling. */
C99_RT_UNUSED __c99_rt_linkage void __tcc_defer_step(int *out)
{
    C99ThCtx *th = __c99_th();

    *out = -1;
    while (th->dcsp > 0) {
        C99DeferCtx *c = &__c99_dctx(th)[th->dcsp - 1];
        while (th->dsp > c->base) {
            C99DeferRec *r = &__c99_dstack(th)[th->dsp - 1];
            if (c->mode == C99_DEFER_MODE_SCOPE && r->depth <= c->depth)
                break;              /* belongs to an enclosing scope */
            th->dsp--;
            if (r->kind == C99_DEFER_KIND_ERRDEFER
                && c->mode != C99_DEFER_MODE_ERR)
                continue;           /* errdefer only runs on the error path */
            *out = r->id;
            return;
        }
        th->dcsp--;
        *out = -c->resume;
        return;
    }
}

C99_RT_UNUSED __c99_rt_linkage void __tcc_err_set(int id, const char *msg)
{
    C99ThCtx *th = __c99_th();

    th->err_flag = 1;
    th->err_id = id;
    th->err_msg = msg;
}

C99_RT_UNUSED __c99_rt_linkage int __tcc_err_flag_get(void)
{
    return __c99_th()->err_flag;
}

/* Same value, but stored through OUT: generated code can call this without
   having to materialize the return value of a helper (helpers are treated
   as void-typed everywhere in the code generator). */
C99_RT_UNUSED __c99_rt_linkage void __tcc_err_flag_peek(int *out)
{
    *out = __c99_th()->err_flag;
}

C99_RT_UNUSED __c99_rt_linkage int __tcc_err_id_get(void)
{
    return __c99_th()->err_id;
}

C99_RT_UNUSED __c99_rt_linkage const char *__tcc_err_msg_get(void)
{
    return __c99_th()->err_msg;
}

C99_RT_UNUSED __c99_rt_linkage void __tcc_iface_init(void *self, const void *vt, void *dst)
{
    void **d = (void **)dst;
    d[0] = self;
    d[1] = (void *)vt;
}

C99_RT_UNUSED __c99_rt_linkage void __tcc_err_clear(void)
{
    C99ThCtx *th = __c99_th();

    th->err_flag = 0;
    th->err_id = 0;
    th->err_msg = 0;
}

/* c6 'error' value: layout must match the 'error' typedef emitted by
   tcc_predefs(), i.e. 'struct __tcc_error { int id; const char *msg; }'. */
typedef struct C99ErrorValue {
    int id;
    const char *msg;
} C99ErrorValue;

/* Snapshot the error slot into OUT (may be NULL to just clear it) and then
   clear the slot: this is what 'catch' does before running its body, so the
   error never leaks to the enclosing function. */
C99_RT_UNUSED __c99_rt_linkage void __tcc_error_take(void *out)
{
    C99ThCtx *th = __c99_th();

    if (out) {
        C99ErrorValue *e = (C99ErrorValue *)out;
        e->id = th->err_id;
        e->msg = th->err_msg;
    }
    __tcc_err_clear();
}

/* Safety net emitted at the end of every 'catch' body: the body must leave
   the handler by itself (return / err() / goto), so reaching the end is a
   path the compiler could not prove unreachable.  libtcc1 is freestanding
   (no libc), so trap with a deliberate null store instead of abort(). */
C99_RT_UNUSED __c99_rt_linkage void __tcc_catch_fallthrough(void)
{
    volatile int *p = (volatile int *)0;
    *p = 0;
}


/* ------------------------------------------------------------------------- */
/* c6 extension runtime: slices 'T[:]'                                   */
/*                                                                           */
/* A slice value is '{ void *data; size_t len; }' with the two words          */
/* adjacent, so the compiler only hands over a pointer to such a pair and    */
/* the helpers never depend on the element type: everything is byte          */
/* arithmetic.  A bounds violation is part of the language, so it must stop  */
/* the program: libtcc1 is freestanding (no libc), hence the same deliberate */
/* null store that __tcc_catch_fallthrough() uses.                          */

typedef unsigned long C99SliceSize;

#define C99_SLICE_LEN(slice) \
    (*(const C99SliceSize *)((const char *)(slice) + sizeof(void *)))

C99_RT_UNUSED __c99_rt_linkage void __c99_slice_trap(void)
{
    volatile int *p = (volatile int *)0;
    *p = 0;
}

/* Write the address of element IDX of SLICE through OUT. */
C99_RT_UNUSED __c99_rt_linkage void __tcc_slice_index(void **out, const void *slice,
                       C99SliceSize idx, C99SliceSize elem_size)
{
    const char *base = *(const char * const *)slice;
    C99SliceSize len = C99_SLICE_LEN(slice);

    if (idx >= len)
        __c99_slice_trap();
    *out = (void *)(base + idx * elem_size);
}

/* Write the [LO, HI) sub-slice of SLICE through OUT, which must address two
   adjacent machine words (a fresh slice value). */
C99_RT_UNUSED __c99_rt_linkage void __tcc_slice_sub(void *out, const void *slice, C99SliceSize lo,
                     C99SliceSize hi, C99SliceSize elem_size)
{
    const char *base = *(const char * const *)slice;
    C99SliceSize len = C99_SLICE_LEN(slice);

    if (lo > hi || hi > len)
        __c99_slice_trap();
    *(void **)out = (void *)(base + lo * elem_size);
    *(C99SliceSize *)((char *)out + sizeof(void *)) = hi - lo;
}

/* ------------------------------------------------------------------------- */
/* c6 extension runtime: async / await (stackless coroutines)                */
/*                                                                           */
/* An 'async' function is compiled to 'int __c6_body_NAME(void *task)'.  The */
/* body runs on the caller's stack; every suspension ("await" that has to    */
/* wait) copies the used part of the native frame into the task and returns  */
/* to whoever drives the task.  Resuming re-enters the body, copies the      */
/* saved frame back to the same offsets and jumps to the recorded resume      */
/* point.  'co f();' makes a coroutine top-level: it is put on the ready list */
/* below and stepped by __c99_co_pump(), which is the scheduler.  Task objects   */
/* live in this file so that 'struct __c6_task'                              */
/* has exactly the layout that tcc_predefs() declares to compiled programs;  */
/* generated code only ever touches it through the __c6_co_* accessors.      */

#define C6_CO_MAX        64     /* concurrent tasks (frames + slots) */
#define C6_CO_FRAME_MAX  4096   /* largest native frame an async body may use */
#define C6_CO_TASK_MAX   1024   /* largest 'struct NAME_task' */
#define C6_CO_DEFER_MAX  256    /* parked defer records per task */

#define C6_CO_KIND_DONE   0
#define C6_CO_KIND_IO     1
#define C6_CO_KIND_CHILD  2
#define C6_CO_KIND_ERR    3

#define C6_CO_STATE_NEW   0
#define C6_CO_STATE_DONE  (-1)

/* lays out exactly like the predef in tccpp.c:tcc_predefs() */
typedef struct C99CoTask {
    int state, kind;
    int defer_max, defer_base, defer_cap, defer_csp;
    int wait_fd, wait_ev, wait_rev;
    int err_flag, err_id, __pad0;
    void *desc, *frame, *defer_arena, *parent, *child;
    const char *err_msg;
    int frame_size, __pad1;
    void *frame_addr;
    void *snext;                /* next task on the top-level ready list */
    int busy;                   /* currently being stepped (re-entrancy) */
    int waiting;                /* parked on wait_fd, waiting for select() */
} C99CoTask;

typedef struct C99CoDesc {
    unsigned size, align;
    int defer_max;
    int (*entry)(void *);
    void (*release)(void *);    /* give back what the task slot owns */
} C99CoDesc;

/* One slot per task, big enough for 'struct NAME_task' (the header plus the
   declared return value and parameters).  'long long' keeps every slot
   8-byte aligned.  The whole pool is per thread and allocated on first use:
   most threads never create a task and should not pay for the 512 KiB. */
struct C99CoPool {
    long long task_mem[C6_CO_MAX][C6_CO_TASK_MAX / 8];
    char frames[C6_CO_MAX][C6_CO_FRAME_MAX];
    C99DeferRec parked[C6_CO_MAX][C6_CO_DEFER_MAX];
    char live[C6_CO_MAX];
    /* Top-level coroutines started with 'co': they belong to no scope and no
       parent, so the scheduler below owns them from creation to completion. */
    void *runlist;
    /* Failures of tasks that finished inside the 'co' statement itself: they
       are gone by the time anyone can call __c6_co_run(), so the count has to
       be remembered here. */
    int eager_errs;
};

#define C6_CO_TASK(co, i) ((C99CoTask *)(void *)(co)->task_mem[i])

C99_RT_UNUSED __c99_rt_linkage void __c99_co_free(C99CoPool *co)
{
    free(co);
}

C99_RT_UNUSED __c99_rt_linkage C99CoPool *__c99_th_co(C99ThCtx *th)
{
    if (!th->co)
        th->co = (C99CoPool *)__c99_alloc(sizeof *th->co);
    return th->co;
}


C99_RT_UNUSED __c99_rt_linkage void __c99_co_trap(void)
{
    volatile int *p = (volatile int *)0;
    *p = 0;
}

C99_RT_UNUSED __c99_rt_linkage void __c99_co_copy(void *dst, const void *src, int n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n-- > 0)
        *d++ = *s++;
}

C99_RT_UNUSED __c99_rt_linkage int __c99_co_index(C99CoPool *co, C99CoTask *t)
{
    long off = (long)((char *)t - (char *)(void *)co->task_mem);
    int i;

    if (off < 0 || off >= (long)C6_CO_MAX * C6_CO_TASK_MAX
        || off % C6_CO_TASK_MAX)
        __c99_co_trap();
    i = (int)(off / C6_CO_TASK_MAX);
    if (!co->live[i])
        __c99_co_trap();
    return i;
}

/* Allocate a task.  The returned pointer is both the 'void *task' handle and
   the address of its 'struct __c6_task head', which is field 0 of the
   generated 'struct NAME_task', so the frame prefix needs no fixups. */
C99_RT_UNUSED __c99_rt_linkage void *__c6_co_new(const C99CoDesc *d)
{
    C99ThCtx *th = __c99_th();
    C99CoPool *co = __c99_th_co(th);
    int i;
    C99CoTask *t;

    for (i = 0; i < C6_CO_MAX; i++)
        if (!co->live[i])
            break;
    if (i >= C6_CO_MAX)
        __c99_co_trap();          /* too many live tasks */
    if (d->size > C6_CO_TASK_MAX)
        __c99_co_trap();          /* frame prefix larger than the pool */
    co->live[i] = 1;
    t = C6_CO_TASK(co, i);
    {
        char *p = (char *)t;
        int n = (int)d->size;
        while (n-- > 0)
            *p++ = 0;
    }
    t->state = C6_CO_STATE_NEW;
    t->kind = C6_CO_KIND_DONE;
    t->desc = (void *)d;
    t->defer_max = d->defer_max;
    if (t->defer_max > C6_CO_DEFER_MAX)
        t->defer_max = C6_CO_DEFER_MAX;
    return t;
}

/* Link CHILD under SELF.  Returns 0 when the child has already finished (the
   caller can read its value without suspending), 1 when it must wait. */
C99_RT_UNUSED __c99_rt_linkage int __c6_co_await(void *child, void *self)
{
    C99CoTask *c = (C99CoTask *)child;
    C99CoTask *t = (C99CoTask *)self;

    t->child = c;
    c->parent = t;
    return c->state == C6_CO_STATE_DONE ? 0 : 1;
}

/* Park the native frame, the defer records of this frame and the error slot,
   then record where the body has to resume. */
C99_RT_UNUSED __c99_rt_linkage void __c6_co_suspend(void *self, int kind, int state,
                     char *base, int size, int defer_base)
{
    C99ThCtx *th = __c99_th();
    C99CoPool *co = __c99_th_co(th);
    C99CoTask *t = (C99CoTask *)self;
    int idx = __c99_co_index(co, t);
    int i, n;

    if (size < 0 || size > C6_CO_FRAME_MAX)
        __c99_co_trap();
    t->frame_size = size;
#ifdef C6_CO_CHECK_FRAME_ADDR
    t->frame_addr = base - size;
#endif
    __c99_co_copy(co->frames[idx], base - size, size);
    t->frame = co->frames[idx];
    t->state = state;
    t->kind = kind;

    t->defer_base = defer_base;
    n = th->dsp - defer_base;
    if (n < 0)
        n = 0;
    if (n > t->defer_max)
        n = t->defer_max;
    for (i = 0; i < n; i++)
        co->parked[idx][i] = __c99_dstack(th)[defer_base + i];
    t->defer_cap = n;
    t->defer_csp = th->dcsp;
    if (th->dsp > defer_base)
        th->dsp = defer_base;
    th->dcsp = 0;

    t->err_flag = th->err_flag;
    t->err_id = th->err_id;
    t->err_msg = th->err_msg;
    th->err_flag = 0;
    th->err_id = 0;
    th->err_msg = 0;
}

/* Put the parked native frame back where it belongs and re-establish the
   task's defer and error state (the generated prologue calls this before it
   jumps to the resume point). */
C99_RT_UNUSED __c99_rt_linkage void __c6_co_restore(void *self, char *base)
{
    C99ThCtx *th = __c99_th();
    C99CoPool *co = __c99_th_co(th);
    C99CoTask *t = (C99CoTask *)self;
    int idx = __c99_co_index(co, t);
    int i, n = t->defer_cap;

#ifdef C6_CO_CHECK_FRAME_ADDR
    if (t->frame_addr && t->frame_addr != base - t->frame_size)
        __c99_co_trap();
#endif
    __c99_co_copy(base - t->frame_size, co->frames[idx], t->frame_size);
    for (i = 0; i < n; i++)
        __c99_dstack(th)[t->defer_base + i] = co->parked[idx][i];
    th->dsp = t->defer_base + n;
    th->dcsp = t->defer_csp;
    th->err_flag = t->err_flag;
    th->err_id = t->err_id;
    th->err_msg = t->err_msg;
    th->err_flag = 0;           /* a resumed body starts from a clean slot */
    th->err_id = 0;
    th->err_msg = 0;
}

C99_RT_UNUSED __c99_rt_linkage void __c6_co_finish(void *self, int kind)
{
    C99CoTask *t = (C99CoTask *)self;
    t->state = C6_CO_STATE_DONE;
    t->kind = kind;
}

C99_RT_UNUSED __c99_rt_linkage int __c6_co_state(void *self)
{
    return ((C99CoTask *)self)->state;
}

C99_RT_UNUSED __c99_rt_linkage int __c6_co_kind(void *self)
{
    return ((C99CoTask *)self)->kind;
}

C99_RT_UNUSED __c99_rt_linkage void __c6_co_set_io(void *self, int fd, int ev)
{
    C99CoTask *t = (C99CoTask *)self;
    t->wait_fd = fd;
    t->wait_ev = ev;
    t->wait_rev = 0;
}

C99_RT_UNUSED __c99_rt_linkage int __c6_co_rev(void *self)
{
    return ((C99CoTask *)self)->wait_rev;
}

/* Wait for FD to become ready for EV (1 = readable, 2 = writable). */
C99_RT_UNUSED __c99_rt_linkage int __c99_co_select(int fd, int ev)
{
#if C99_RT_POSIX
    fd_set r, w, e;
    int res, rev = 0;

    if (fd < 0 || fd >= FD_SETSIZE)
        return -1;
    FD_ZERO(&r); FD_ZERO(&w); FD_ZERO(&e);
    if (ev & 1)
        FD_SET(fd, &r);
    if (ev & 2)
        FD_SET(fd, &w);
    res = select(fd + 1, &r, &w, &e, (struct timeval *)0);
    if (res <= 0)
        return res;
    if (FD_ISSET(fd, &r))
        rev |= 1;
    if (FD_ISSET(fd, &w))
        rev |= 2;
    if (FD_ISSET(fd, &e))
        rev |= 3;
    return rev;
#else
    (void)fd; (void)ev;
    return -1;
#endif
}

/* 'go' wants "run until you have to wait" and nothing more specific */
#define C6_CO_KIND_SKIP   4

C99_RT_UNUSED __c99_rt_linkage void __c6_co_destroy(void *self);

C99_RT_UNUSED __c99_rt_linkage C99CoTask *__c99_co_leaf(C99CoTask *t)
{
    while (t->child && ((C99CoTask *)t->child)->state != C6_CO_STATE_DONE)
        t = (C99CoTask *)t->child;
    return t;
}

C99_RT_UNUSED __c99_rt_linkage void __c99_co_link(C99CoTask **head, C99CoTask *t)
{
    t->snext = *head;
    *head = t;
}

C99_RT_UNUSED __c99_rt_linkage void __c99_co_unlink(C99CoTask **head, C99CoTask *t)
{
    while (*head) {
        if (*head == t) {
            *head = (C99CoTask *)t->snext;
            t->snext = 0;
            return;
        }
        head = (C99CoTask **)&(*head)->snext;
    }
}

/* Run one step of the leaf of T: call its body until it suspends or ends.
   Never blocks.  Returns the kind the body reported, or C6_CO_KIND_SKIP when
   there is nothing to do right now (re-entered, or parked on its fd and the
   caller has not waited for it yet). */
C99_RT_UNUSED __c99_rt_linkage int __c99_co_step_once(C99CoTask *t)
{
    C99CoTask *leaf = __c99_co_leaf(t);
    C99CoDesc *d;
    int k;

    if (leaf->state == C6_CO_STATE_DONE)
        return C6_CO_KIND_DONE;
    if (leaf->busy)
        return C6_CO_KIND_SKIP;         /* re-entered from a nested pump */
    if (leaf->waiting)
        return C6_CO_KIND_IO;           /* still parked on wait_fd */
    d = (C99CoDesc *)leaf->desc;
    leaf->busy++;
    k = d->entry(leaf);
    leaf->busy--;
    switch (k) {
    case C6_CO_KIND_DONE:
    case C6_CO_KIND_ERR:
        leaf->state = C6_CO_STATE_DONE;
        leaf->kind = k;
        break;
    case C6_CO_KIND_IO:
        leaf->waiting = 1;              /* the pump has to select() for it */
        break;
    case C6_CO_KIND_CHILD:
        break;
    default:
        __c99_co_trap();
    }
    return k;
}

/* One round over the top-level ready list: step every task once, then wait for
   whatever is parked on a descriptor and hand each one its readiness mask.
   Finished tasks are released here; ERRS, when not NULL, collects how many of
   them ended with 'err'.  Returns 0, or -1 when select() failed. */
C99_RT_UNUSED __c99_rt_linkage int __c99_co_pump(int *errs)
{
    C99CoPool *co = __c99_th_co(__c99_th());
    int fds[C6_CO_MAX], evs[C6_CO_MAX];
    C99CoTask *leafs[C6_CO_MAX];
    C99CoTask *t, **pp;

    for (;;) {
        int nio = 0, maxfd = -1, ran = 0, i;

        pp = (C99CoTask **)&co->runlist;
        while ((t = *pp) != 0) {
            int k;

            if (t->state == C6_CO_STATE_DONE) {
                *pp = (C99CoTask *)t->snext;
                t->snext = 0;
                if (errs && t->kind == C6_CO_KIND_ERR)
                    (*errs)++;
                __c6_co_destroy(t);
                continue;
            }
            k = __c99_co_step_once(t);
            if (k == C6_CO_KIND_IO && nio < C6_CO_MAX) {
                C99CoTask *leaf = __c99_co_leaf(t);
                int fd = leaf->wait_fd;
                if (fd >= 0 && fd < 1024) {
                    fds[nio] = fd;
                    evs[nio] = leaf->wait_ev;
                    leafs[nio] = leaf;
                    nio++;
                    if (fd > maxfd)
                        maxfd = fd;
                } else {
                    /* c6_io() with an fd select() cannot wait for: fail the
                       task and release it, like __c6_co_wait() returning -1.
                       Leaving it in the list would park it forever. */
                    *pp = (C99CoTask *)t->snext;
                    t->snext = 0;
                    if (errs)
                        (*errs)++;
                    __c6_co_destroy(t);
                    continue;
                }
            } else if (k != C6_CO_KIND_SKIP) {
                ran = 1;
            }
            pp = (C99CoTask **)&t->snext;
        }
        if (!nio) {
            if (!ran)
                return 0;               /* nothing left to do */
            continue;                   /* someone made progress: go again */
        }
#if C99_RT_POSIX
        {
            fd_set r, w, e;
            FD_ZERO(&r); FD_ZERO(&w); FD_ZERO(&e);
            for (i = 0; i < nio; i++) {
                if (evs[i] & 1)
                    FD_SET(fds[i], &r);
                if (evs[i] & 2)
                    FD_SET(fds[i], &w);
            }
            if (select(maxfd + 1, &r, &w, &e, (struct timeval *)0) <= 0)
                return -1;
            for (i = 0; i < nio; i++) {
                int rev = 0;
                if (FD_ISSET(fds[i], &r))
                    rev |= 1;
                if (FD_ISSET(fds[i], &w))
                    rev |= 2;
                if (!rev)
                    continue;       /* not this one yet: keep it parked */
                leafs[i]->wait_rev = rev;
                leafs[i]->waiting = 0;
            }
        }
#else
        (void)i; (void)maxfd;
        return -1;
#endif
    }
}

/* 'co f();' : make T a top-level coroutine and run it up to its first real
   suspension (or to completion), then return to the 'co' statement.  A task
   that is already finished has no observer and no handle, so it is released
   right here -- otherwise a loop of non-suspending 'co' calls would fill the
   task pool. */
C99_RT_UNUSED __c99_rt_linkage void __tcc_co_spawn(void *task)
{
    C99CoPool *co = __c99_th_co(__c99_th());
    C99CoTask *t = (C99CoTask *)task;

    if (!t)
        return;
    t->parent = 0;
    __c99_co_link((C99CoTask **)&co->runlist, t);
    /* Run it until it has to wait for a descriptor: awaiting a child is not
       waiting, so keep descending.  A task that is finished by the time the
       'co' statement returns has no handle and no observer, so it is released
       here -- otherwise a loop of short 'co' calls would fill the pool. */
    for (;;) {
        int k = __c99_co_step_once(t);
        if (k == C6_CO_KIND_IO || k == C6_CO_KIND_SKIP)
            break;                      /* the pump waits for the fd */
        if (t->state == C6_CO_STATE_DONE) {
            __c99_co_unlink((C99CoTask **)&co->runlist, t);
            if (t->kind == C6_CO_KIND_ERR)
                co->eager_errs++;
            __c6_co_destroy(t);
            break;
        }
        /* KIND_CHILD: step the awaited child next */
    }
}

/* Drain the top-level ready list; returns how many tasks ended with 'err'. */
C99_RT_UNUSED __c99_rt_linkage int __c6_co_run(void)
{
    C99CoPool *co = __c99_th_co(__c99_th());
    int errs = co->eager_errs;

    co->eager_errs = 0;
    if (__c99_co_pump(&errs) < 0)
        return -1;
    return errs;
}

/* Drive the task tree rooted at SELF until it finishes, letting the top-level
   coroutines make progress in between: 0 = done, -1 = error. */
C99_RT_UNUSED __c99_rt_linkage int __c6_co_wait(void *self)
{
    int ignored;

    if (!self)
        return -1;
    for (;;) {
        C99CoTask *root = (C99CoTask *)self;
        int k;

        if (root->state == C6_CO_STATE_DONE)
            break;
        k = __c99_co_step_once(root);
        if (k == C6_CO_KIND_IO) {
            C99CoTask *leaf = __c99_co_leaf(root);
            int rev = __c99_co_select(leaf->wait_fd, leaf->wait_ev);
            if (rev < 0)
                return -1;
            leaf->wait_rev = rev;
            leaf->waiting = 0;
        }
        if (__c99_co_pump(&ignored) < 0)
            return -1;
    }
    return ((C99CoTask *)self)->kind == C6_CO_KIND_ERR ? -1 : 0;
}

/* Release a task and everything it owns.  A still-suspended body is dropped
   without running its remaining 'defer' statements. */
C99_RT_UNUSED __c99_rt_linkage void __c6_co_destroy(void *self)
{
    C99CoPool *co = __c99_th_co(__c99_th());
    C99CoTask *t = (C99CoTask *)self;

    if (!t)
        return;
    if (t->child)
        __c6_co_destroy(t->child);
    t->child = 0;
    t->parent = 0;
    /* the task owned one reference per managed parameter (and the return
       value): give them back before the slot is reused */
    if (t->desc && ((C99CoDesc *)t->desc)->release)
        ((C99CoDesc *)t->desc)->release(t);
    __c99_co_unlink((C99CoTask **)&co->runlist, t);
    co->live[__c99_co_index(co, t)] = 0;
}

/* 'await c6_io(fd, ev)' is rewritten by the compiler and never calls this;
   the definition only keeps a stray direct call linkable. */
C99_RT_UNUSED __c99_rt_linkage int c6_io(int fd, int ev)
{
    (void)fd;
    (void)ev;
    return -1;
}

/* c6 extension runtime: reference counted managed references '@T'           */
/*                                                                           */
/* Every object made by 'new T' carries a 32 byte header: the reference      */
/* count, the element count together with the object flags, the type          */
/* descriptor and either the list of weak references pointing at it or the    */
/* link in the per-thread free queue.  Dropping a reference only queues the   */
/* object; the queue is drained in batches, so freeing a long chain does not  */
/* need one C stack frame per link.                                          */

#define C99_GC_HDR_SIZE   32
#define C99_GC_DEAD       (1UL << 63)
#define C99_GC_FRESH      1
#define C99_GC_ATOMIC     2
#define C99_GC_WEAK       4
#define C99_GC_OWN        8
#define C99_GC_INIT       16

#define C99_GCK_STRONG 1
#define C99_GCK_WEAK   2
#define C99_GCK_OWN    3
#define C99_GCK_EMBED  4

#define C99_GC_META_COUNT 0xffffffffffUL
#define C99_GC_META_FLAGS(meta) ((int)((meta) >> 40))

#define C99_GC_DRAIN_BUDGET 256

#define C99_GC_A_ACQ_REL 4
#define C99_GC_A_ACQUIRE 2

typedef struct C99GcWeak {
    void **slot;
    struct C99GcWeak *next;
} C99GcWeak;

/* the descriptor the compiler's type metadata uses */
struct __c6_gc_desc;

typedef struct C99GcField {
    unsigned off, kind, count, stride;
    const struct __c6_gc_desc *sub;
} C99GcField;

struct __c6_gc_desc {
    unsigned size, align, nfields, flags;
    void (*drop)(void *);
    const C99GcField *fields;
};

struct C99GcHdr {
    unsigned long rc;
    unsigned long meta;
    const struct __c6_gc_desc *desc;
    union {
        C99GcWeak *weak;
        C99GcHdr *next;
    } u;
};

C99_RT_UNUSED __c99_rt_linkage unsigned long __c99_gc_live_count;

C99_RT_UNUSED __c99_rt_linkage C99GcHdr *__c99_gc_hdr(void *p)
{
    return (C99GcHdr *)p - 1;
}

C99_RT_UNUSED __c99_rt_linkage int __c99_gc_is_atomic(C99GcHdr *h)
{
    return (C99_GC_META_FLAGS(h->meta) & C99_GC_ATOMIC) != 0;
}

C99_RT_UNUSED __c99_rt_linkage void __c99_gc_inc(C99GcHdr *h)
{
    if (__c99_gc_is_atomic(h))
        __atomic_fetch_add(&h->rc, 1UL, C99_GC_A_ACQ_REL);
    else
        h->rc++;
}

/* defined below: the field release runs inside the free path */
C99_RT_UNUSED __c99_rt_linkage void __c99_gc_drop(C99GcHdr *h);
C99_RT_UNUSED __c99_rt_linkage void __c99_gc_destroy(C99GcHdr *h);
C99_RT_UNUSED __c99_rt_linkage void __c99_gc_weak_unlink(void **slot, void *p);

C99_RT_UNUSED __c99_rt_linkage void __c99_gc_release_slot(void **slot, int kind)
{
    void *p = *slot;

    if (!p)
        return;
    if (kind == C99_GCK_WEAK) {
        __c99_gc_weak_unlink(slot, p);
        *slot = 0;
        return;
    }
    *slot = 0;
    if (kind == C99_GCK_OWN)
        __c99_gc_destroy(__c99_gc_hdr(p));
    else
        __c99_gc_drop(__c99_gc_hdr(p));
}

C99_RT_UNUSED __c99_rt_linkage void __c99_gc_release_block(char *base,
                                                           const struct __c6_gc_desc *d)
{
    unsigned k;

    if (!d)
        return;
    for (k = 0; k < d->nfields; k++) {
        const C99GcField *f = &d->fields[k];
        unsigned i;

        for (i = 0; i < f->count; i++) {
            char *p = base + f->off + (size_t)i * f->stride;
            if (f->kind == C99_GCK_EMBED)
                __c99_gc_release_block(p, f->sub);
            else
                __c99_gc_release_slot((void **)p, (int)f->kind);
        }
    }
}

C99_RT_UNUSED __c99_rt_linkage void __c99_gc_free_one(C99GcHdr *h)
{
    const struct __c6_gc_desc *d = h->desc;

    if (d) {
        unsigned long n = h->meta & C99_GC_META_COUNT, i;
        for (i = 0; i < n; i++) {
            char *base = (char *)(h + 1) + i * (size_t)d->size;
            if (d->drop)
                d->drop(base);
            if (d->nfields)
                __c99_gc_release_block(base, d);
        }
    }
    if (__c99_gc_live_count)
        __c99_gc_live_count--;
    free(h);
}

C99_RT_UNUSED __c99_rt_linkage void __c99_gc_drain(C99ThCtx *th, int budget)
{
    int n = 0;

    if (th->gc_draining)
        return;
    th->gc_draining = 1;
    while (th->gc_pending && n < budget) {
        C99GcHdr *h = th->gc_pending;
        th->gc_pending = h->u.next;
        if (!th->gc_pending)
            th->gc_pending_tail = 0;
        th->gc_pending_n--;
        n++;
        __c99_gc_free_one(h);
    }
    th->gc_draining = 0;
}

C99_RT_UNUSED __c99_rt_linkage void __c99_gc_free_all(C99ThCtx *th)
{
    while (th->gc_pending) {
        C99GcHdr *h = th->gc_pending;
        th->gc_pending = h->u.next;
        th->gc_pending_n--;
        __c99_gc_free_one(h);
    }
    th->gc_pending_tail = 0;
}

C99_RT_UNUSED __c99_rt_linkage void __c99_gc_die(C99GcHdr *h)
{
    C99ThCtx *th;
    C99GcWeak *w;

    h->rc = C99_GC_DEAD;
    w = h->u.weak;
    h->u.weak = 0;
    while (w) {
        C99GcWeak *next = w->next;
        *w->slot = 0;
        free(w);
        w = next;
    }
    th = __c99_th();
    h->u.next = 0;
    if (th->gc_pending_tail)
        th->gc_pending_tail->u.next = h;
    else
        th->gc_pending = h;
    th->gc_pending_tail = h;
    th->gc_pending_n++;
    __c99_gc_drain(th, C99_GC_DRAIN_BUDGET);
}

C99_RT_UNUSED __c99_rt_linkage void __c99_gc_drop(C99GcHdr *h)
{
    unsigned long old;

    if (__c99_gc_is_atomic(h)) {
        old = __atomic_fetch_sub(&h->rc, 1UL, C99_GC_A_ACQ_REL);
        if (old != 1)
            return;
    } else {
        if (--h->rc != 0)
            return;
    }
    __c99_gc_die(h);
}

C99_RT_UNUSED __c99_rt_linkage void __c99_gc_destroy(C99GcHdr *h)
{
    if (h->rc & C99_GC_DEAD)
        return;
    h->rc = 1;
    __c99_gc_die(h);
}

C99_RT_UNUSED __c99_rt_linkage void __c99_gc_weak_unlink(void **slot, void *p)
{
    C99GcWeak **pp;

    for (pp = &__c99_gc_hdr(p)->u.weak; *pp; pp = &(*pp)->next) {
        C99GcWeak *w = *pp;
        if (w->slot == slot) {
            *pp = w->next;
            free(w);
            return;
        }
    }
}

C99_RT_UNUSED __c99_rt_linkage void __c99_gc_weak_add(void **slot, void *p)
{
    C99GcHdr *h = __c99_gc_hdr(p);
    C99GcWeak *w = (C99GcWeak *)__c99_alloc(sizeof *w);

    w->slot = slot;
    w->next = h->u.weak;
    h->u.weak = w;
}

C99_RT_UNUSED __c99_rt_linkage void *__c6_gc_new(unsigned long size,
                                                 unsigned long count,
                                                 const void *desc, int flags)
{
    unsigned long total;
    C99GcHdr *h;

    if (size == 0)
        size = 1;
    if (count == 0)
        count = 1;
    if (count > ((unsigned long)-1 - C99_GC_HDR_SIZE) / size)
        __c99_trap();
    total = size * count;
    h = (C99GcHdr *)__c99_alloc(C99_GC_HDR_SIZE + total);
    h->rc = 1;
    h->meta = count & C99_GC_META_COUNT;
    if (flags & C99_GC_ATOMIC)
        h->meta |= (unsigned long)C99_GC_ATOMIC << 40;
    h->desc = (const struct __c6_gc_desc *)desc;
    __c99_gc_live_count++;
    return h + 1;
}

C99_RT_UNUSED __c99_rt_linkage void *__c6_gc_store(void **slot, void *nv,
                                                   int mode)
{
    int init = (mode & C99_GC_INIT) != 0;
    void *old = init ? 0 : *slot;

    if (mode & C99_GC_WEAK) {
        if (!init && old == nv)
            return nv;
        if (old)
            __c99_gc_weak_unlink(slot, old);
        *slot = nv;
        if (nv)
            __c99_gc_weak_add(slot, nv);
        return nv;
    }
    if (!init && old == nv)
        return nv;
    if (nv && !(mode & C99_GC_FRESH))
        __c99_gc_inc(__c99_gc_hdr(nv));
    *slot = nv;
    if (old) {
        if (mode & C99_GC_OWN)
            __c99_gc_destroy(__c99_gc_hdr(old));
        else
            __c99_gc_drop(__c99_gc_hdr(old));
    }
    return nv;
}

C99_RT_UNUSED __c99_rt_linkage void *__c6_gc_own_store(void **slot, void *nv,
                                                       int mode)
{
    int init = (mode & C99_GC_INIT) != 0;
    void *old = init ? 0 : *slot;

    if (!init && old == nv)
        return nv;
    *slot = nv;
    if (old)
        __c99_gc_destroy(__c99_gc_hdr(old));
    return nv;
}

C99_RT_UNUSED __c99_rt_linkage void __c6_gc_release(void **slot, int mode)
{
    void *p = *slot;

    if (!p)
        return;
    *slot = 0;
    if (mode & C99_GC_WEAK)
        __c99_gc_weak_unlink(slot, p);
    else if (mode & C99_GC_OWN)
        __c99_gc_destroy(__c99_gc_hdr(p));
    else
        __c99_gc_drop(__c99_gc_hdr(p));
}

C99_RT_UNUSED __c99_rt_linkage void *__c6_gc_move(void **slot)
{
    void *p = *slot;

    *slot = 0;
    return p;
}

C99_RT_UNUSED __c99_rt_linkage void *__c6_gc_incref(void *p)
{
    if (p)
        __c99_gc_inc(__c99_gc_hdr(p));
    return p;
}

C99_RT_UNUSED __c99_rt_linkage void *__c6_gc_drop(void *p)
{
    if (p)
        __c99_gc_drop(__c99_gc_hdr(p));
    return 0;
}

C99_RT_UNUSED __c99_rt_linkage void *__c6_gc_weak_store(void **dst,
                                                          void **weak_slot,
                                                          int mode)
{
    void *p = *weak_slot;
    C99GcHdr *h;

    if (p) {
        h = __c99_gc_hdr(p);
        if (__c99_gc_is_atomic(h)) {
            unsigned long expected, desired;
            for (;;) {
                expected = h->rc;
                if (expected & C99_GC_DEAD)
                    break;
                desired = expected + 1;
                if (__atomic_compare_exchange(&h->rc, &expected, &desired, 0,
                                              C99_GC_A_ACQ_REL,
                                              C99_GC_A_ACQUIRE))
                    goto owned;
            }
        } else if (!(h->rc & C99_GC_DEAD)) {
            h->rc++;
            goto owned;
        }
        p = 0;                      /* died while we looked at it */
    }
owned:
    return __c6_gc_store(dst, p, (mode & ~C99_GC_WEAK) | C99_GC_FRESH);
}

C99_RT_UNUSED __c99_rt_linkage unsigned long long __c6_gc_live(void)
{
    return (unsigned long long)__c99_gc_live_count;
}

C99_RT_UNUSED __c99_rt_linkage unsigned long long __c6_gc_refcount(void *p)
{
    if (!p)
        return 0;
    return (unsigned long long)(__c99_gc_hdr(p)->rc & ~C99_GC_DEAD);
}

C99_RT_UNUSED __c99_rt_linkage void __c6_gc_drain_budget(int n)
{
    C99ThCtx *th = __c99_th();

    if (n <= 0)
        n = C99_GC_DRAIN_BUDGET;
    __c99_gc_drain(th, n);
}

/* managed globals: released at exit, see libtcc1.c */
typedef struct C99GcRoot {
    void *block;
    int kind;
    unsigned long count, stride;
} C99GcRoot;

static C99GcRoot *__c99_gc_roots;
static int __c99_nb_gc_roots, __c99_cap_gc_roots;
static int __c99_gc_shut_down;

C99_RT_UNUSED __c99_rt_linkage void __c99_gc_release_slot(void **slot,
                                                           int kind);

C99_RT_UNUSED __c99_rt_linkage void __c6_gc_root(void *block, int kind,
                                                 unsigned long count,
                                                 unsigned long stride)
{
    if (!block || !count)
        return;
    if (__c99_nb_gc_roots == __c99_cap_gc_roots) {
        int cap = __c99_cap_gc_roots ? __c99_cap_gc_roots * 2 : 16;
        C99GcRoot *nr = (C99GcRoot *)realloc(__c99_gc_roots,
                       (unsigned long)cap * sizeof *nr);
        if (!nr)
            return;
        __c99_gc_roots = nr;
        __c99_cap_gc_roots = cap;
    }
    __c99_gc_roots[__c99_nb_gc_roots].block = block;
    __c99_gc_roots[__c99_nb_gc_roots].kind = kind;
    __c99_gc_roots[__c99_nb_gc_roots].count = count;
    __c99_gc_roots[__c99_nb_gc_roots].stride = stride;
    __c99_nb_gc_roots++;
}

static void __c99_gc_release_roots(void)
{
    while (__c99_nb_gc_roots > 0) {
        C99GcRoot *r = &__c99_gc_roots[--__c99_nb_gc_roots];
        unsigned long i;

        for (i = 0; i < r->count; i++)
            __c99_gc_release_slot((void **)((char *)r->block + i * r->stride),
                                  r->kind);
    }
}

C99_RT_UNUSED __c99_rt_linkage void __c6_gc_shutdown(void)
{
    if (__c99_gc_shut_down)
        return;
    __c99_gc_shut_down = 1;
    __c99_gc_release_roots();
    __c99_gc_free_all(__c99_th());
}

