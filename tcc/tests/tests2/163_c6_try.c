/* c6 extension: 'T!error' functions, 'try' and 'catch' */
#include <stdio.h>
#include <string.h>

int __tcc_err_flag_get(void);
int __tcc_err_id_get(void);
const char *__tcc_err_msg_get(void);
void __tcc_err_clear(void);

static char buf[256];
static int blen;

static void log_reset(void) { blen = 0; buf[0] = '\0'; }
static void log_add(const char *s)
{
    while (*s && blen < (int)sizeof(buf) - 2)
        buf[blen++] = *s++;
    buf[blen] = '\0';
}

/* a fallible function: 'err()' behaves like 'return err()' */
static int!error fetch(int n)
{
    if (n < 0)
        err(100, "negative");
    if (n == 0)
        err(101, "zero");
    return n * 10;
}

/* the marker may also be written after the declarator, and a prototype
   marked '!error' also marks the later definition */
static int fetch2(int n)!error;

static int fetch2(int n)
{
    if (n > 99)
        err(102, "too big");
    return n + 1;
}

/* a fallible function without a value */
static void!error check(int n)
{
    if (n != 0)
        err(200, "check failed");
}

/* 1) 'try' hands the value through on the success path and propagates the
   error otherwise */
static int!error propagates(int n)
{
    int v = (try fetch(n));
    log_add("A");
    return v + 1;
}

static int call_propagates(int n)
{
    int v = propagates(n) catch e {
        printf("propagates(%d) caught %d/%s\n", n, e.id, e.msg);
        return -1;
    };
    return v;
}

/* 2) 'catch' snapshots the error and clears the slot */
static int catches(int n)
{
    int v = fetch(n) catch e {
        printf("caught %d %s\n", e.id, e.msg);
        printf("slot-in-handler flag=%d id=%d\n",
               __tcc_err_flag_get(), __tcc_err_id_get());
        return -1000 + e.id;
    };
    return v;
}

/* 3) the handler may omit the variable and the parentheses */
static int catches_plain(int n)
{
    int v = fetch(n) catch {
        printf("plain handler, slot=%d\n", __tcc_err_flag_get());
        return -2;
    };
    return v;
}

static int catches_paren(int n)
{
    int v = fetch(n) catch (e) {
        printf("paren handler %d\n", e.id);
        return -3;
    };
    return v;
}

/* 4) a handler may also leave with 'goto' */
static int catches_goto(int n)
{
    int v = fetch(n) catch e {
        goto failed;
    };
    return v;
failed:
    return -4;
}

/* 5) 'try' on a void function, and 'catch' on a void function */
static void!error void_try(int n)
{
    try check(n);
    log_add("V");
}

static int use_void(int n)
{
    void_try(n) catch e {
        printf("void handler %d/%s\n", e.id, e.msg);
        return -5;
    };
    log_add("W");
    return 0;
}

/* 6) 'try' inside a loop: one failing iteration aborts the whole function */
static int!error loop(int n)
{
    int i, sum = 0;
    for (i = 0; i < n; i++) {
        if (i == 3)
            err(300, "loop");
        sum += (try fetch(i + 1));
    }
    return sum;
}

static int call_loop(int n)
{
    int v = loop(n) catch e {
        printf("loop caught %d\n", e.id);
        return -6;
    };
    return v;
}

/* 7) nested try: either failing call propagates out of 'nested' */
static int!error nested(int n)
{
    return (try fetch(n)) + (try fetch2(n));
}

static int call_nested(int n)
{
    int v = nested(n) catch e {
        printf("nested caught %d/%s\n", e.id, e.msg);
        return -7;
    };
    return v;
}

int main(void)
{
    int a, b, c;

    log_reset();
    a = call_propagates(2);
    printf("1 ok=%d log=%s\n", a, buf);
    printf("1 err=%d\n", call_propagates(-1));
    printf("1 flag=%d\n", __tcc_err_flag_get());

    log_reset();
    a = catches(3);
    printf("2 ok=%d log=%s\n", a, buf);
    printf("2 err=%d\n", catches(0));
    printf("2 flag=%d\n", __tcc_err_flag_get());

    a = catches_plain(1);
    b = catches_plain(0);
    printf("3 plain-ok=%d plain-err=%d\n", a, b);
    a = catches_paren(1);
    b = catches_paren(0);
    printf("3 paren-ok=%d paren-err=%d\n", a, b);
    a = catches_goto(1);
    b = catches_goto(0);
    printf("3 goto-ok=%d goto-err=%d\n", a, b);

    log_reset();
    a = use_void(0);
    printf("5 ok=%d log=%s\n", a, buf);
    printf("5 err=%d\n", use_void(1));

    log_reset();
    a = call_loop(3);
    printf("6 ok=%d log=%s\n", a, buf);
    printf("6 err=%d\n", call_loop(5));

    a = call_nested(1);
    b = call_nested(0);
    printf("7 ok=%d err=%d\n", a, b);
    printf("7 too-big=%d\n", call_nested(120));

    a = call_propagates(3);
    printf("8 proto-ok=%d\n", a);
    printf("8 small=%d\n", fetch2(4));
    printf("8 flag=%d\n", __tcc_err_flag_get());

    c = __tcc_err_flag_get();
    printf("9 flag=%d id=%d\n", c, __tcc_err_id_get());
    __tcc_err_clear();
    printf("final flag=%d\n", __tcc_err_flag_get());
    return 0;
}
