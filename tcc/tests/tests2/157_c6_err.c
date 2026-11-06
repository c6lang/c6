/* c6 extension: errdefer + err */
#include <stdio.h>
#include <string.h>

int __tcc_err_flag_get(void);
int __tcc_err_id_get(void);
const char *__tcc_err_msg_get(void);
void __tcc_err_clear(void);

#define SENT (-1)

static char buf[256];
static int blen;

static void log_reset(void) { blen = 0; buf[0] = '\0'; }
static void log_add(const char *s)
{
    while (*s && blen < (int)sizeof(buf) - 2)
        buf[blen++] = *s++;
    buf[blen] = '\0';
}
static void log_print(const char *tag) { printf("%s: %s\n", tag, buf); }

/* errdefer is skipped on the normal path and runs in reverse on the
   error path; plain defer always runs */
static int e1(int fail)
{
    defer log_add("1");
    errdefer log_add("2");
    errdefer log_add("3");
    if (fail)
        err(7, "boom");
    log_add("4");
    return 0;
}

static int inner(int fail)
{
    defer log_add("A");
    errdefer log_add("B");
    if (fail)
        err(9, "inner");
    return 0;
}

/* propagation through two frames with the sentinel */
static int outer(int fail)
{
    defer log_add("C");
    errdefer log_add("D");
    if (inner(fail) == SENT)
        err();              /* re-raise after the local cleanups */
    log_add("E");
    return 0;
}

/* err inside a loop leaves the whole frame */
static void loop_err(int fail)
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

static int *ptr_err(int fail)
{
    defer log_add("p");
    if (fail)
        err(11, "ptr");
    return 0;
}

int main(void)
{
    log_reset();
    printf("e1-normal=%d ", e1(0));
    log_print("e1-normal");
    printf("flag=%d\n", __tcc_err_flag_get());

    __tcc_err_clear();
    log_reset();
    printf("e1-error=%d ", e1(1));
    log_print("e1-error");
    printf("id=%d msg=%s\n", __tcc_err_id_get(), __tcc_err_msg_get());

    __tcc_err_clear();
    log_reset();
    printf("outer-error=%d ", outer(1));
    log_print("outer-error");
    printf("id=%d msg=%s\n", __tcc_err_id_get(), __tcc_err_msg_get());

    __tcc_err_clear();
    log_reset();
    printf("outer-normal=%d ", outer(0));
    log_print("outer-normal");
    printf("flag=%d\n", __tcc_err_flag_get());

    __tcc_err_clear();
    log_reset();
    loop_err(1);
    log_print("loop-error");
    printf("id=%d\n", __tcc_err_id_get());

    __tcc_err_clear();
    log_reset();
    loop_err(0);
    log_print("loop-normal");

    __tcc_err_clear();
    log_reset();
    printf("ptr=%d ", ptr_err(1) == (int *)(long)SENT);
    log_print("ptr");
    printf("id=%d msg=%s\n", __tcc_err_id_get(), __tcc_err_msg_get());

    __tcc_err_clear();
    printf("final-flag=%d\n", __tcc_err_flag_get());
    return 0;
}
