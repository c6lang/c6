/* c6 extension: errdefer + err + try */
#include <stdio.h>
#include <string.h>

int __tcc_err_flag_get(void);
int __tcc_err_id_get(void);
const char *__tcc_err_msg_get(void);
void __tcc_err_clear(void);

#define SENT (-1)

static char buf[256];
static int blen;

/* the last error observed by a 'catch' handler (the slot itself is cleared
   by 'catch', so the values have to be copied out) */
static int last_id;
static const char *last_msg;

static void log_reset(void) { blen = 0; buf[0] = '\0'; }
static void log_add(const char *s)
{
    while (*s && blen < (int)sizeof(buf) - 2)
        buf[blen++] = *s++;
    buf[blen] = '\0';
}
static void log_print(const char *tag) { printf("%s: %s\n", tag, buf); }

static void note(int id, const char *msg)
{
    last_id = id;
    last_msg = msg;
}

/* errdefer is skipped on the normal path and runs in reverse on the
   error path; plain defer always runs */
static int!error e1(int fail)
{
    defer log_add("1");
    errdefer log_add("2");
    errdefer log_add("3");
    if (fail)
        err(7, "boom");
    log_add("4");
    return 0;
}

static int!error inner(int fail)
{
    defer log_add("A");
    errdefer log_add("B");
    if (fail)
        err(9, "inner");
    return 0;
}

/* propagation through two frames: 'try' hands the error back after running
   the local cleanups, the statement form drops the value */
static int!error outer(int fail)
{
    defer log_add("C");
    errdefer log_add("D");
    try inner(fail);
    log_add("E");
    return 0;
}

/* err inside a loop leaves the whole frame */
static void!error loop_err(int fail)
{
    int i;
    for (i = 0; i < 3; i++) {
        defer log_add("d");
        if (fail && i == 1)
            err(12, "loop");
        log_add("i");
    }
    log_add("z");
}

static int!error *ptr_err(int fail)
{
    defer log_add("p");
    if (fail)
        err(11, "ptr");
    return 0;
}

/* callers that only want "value or sentinel" catch and report SENT */
static int run_e1(int fail)
{
    int v = e1(fail) catch e {
        note(e.id, e.msg);
        return SENT;
    };
    return v;
}

static int run_outer(int fail)
{
    int v = outer(fail) catch e {
        note(e.id, e.msg);
        return SENT;
    };
    return v;
}

static int run_loop(int fail)
{
    loop_err(fail) catch e {
        note(e.id, e.msg);
        return SENT;
    };
    return 0;
}

static int *run_ptr(int fail)
{
    int *v = ptr_err(fail) catch e {
        note(e.id, e.msg);
        return (int *)(long)SENT;
    };
    return v;
}

int main(void)
{
    log_reset();
    printf("e1-normal=%d ", run_e1(0));
    log_print("e1-normal");
    printf("flag=%d\n", __tcc_err_flag_get());

    __tcc_err_clear();
    log_reset();
    printf("e1-error=%d ", run_e1(1));
    log_print("e1-error");
    printf("id=%d msg=%s\n", last_id, last_msg);

    __tcc_err_clear();
    log_reset();
    printf("outer-error=%d ", run_outer(1));
    log_print("outer-error");
    printf("id=%d msg=%s\n", last_id, last_msg);

    __tcc_err_clear();
    log_reset();
    printf("outer-normal=%d ", run_outer(0));
    log_print("outer-normal");
    printf("flag=%d\n", __tcc_err_flag_get());

    __tcc_err_clear();
    log_reset();
    run_loop(1);
    log_print("loop-error");
    printf("id=%d\n", last_id);

    __tcc_err_clear();
    log_reset();
    run_loop(0);
    log_print("loop-normal");

    __tcc_err_clear();
    log_reset();
    printf("ptr=%d ", run_ptr(1) == (int *)(long)SENT);
    log_print("ptr");
    printf("id=%d msg=%s\n", last_id, last_msg);

    __tcc_err_clear();
    printf("final-flag=%d\n", __tcc_err_flag_get());
    return 0;
}
