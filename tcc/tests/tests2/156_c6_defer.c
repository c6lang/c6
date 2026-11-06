/* c6 extension: defer */
#include <stdio.h>

static char log_buf[512];
static int log_pos;

static void log_reset(void) { log_pos = 0; log_buf[0] = '\0'; }
static void log_add(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    if (log_pos + n < (int)sizeof(log_buf)) {
        int i;
        for (i = 0; i < n; i++)
            log_buf[log_pos++] = s[i];
        log_buf[log_pos] = '\0';
    }
}
static void log_print(const char *tag) { printf("%s: %s\n", tag, log_buf); }

/* 1: basic LIFO order in a block */
static void t_basic(void)
{
    log_reset();
    {
        defer log_add("a");
        defer log_add("b");
        defer log_add("c");
        log_add("body");
    }
    log_print("basic");
}

/* 2: defer runs when the block is left by break/continue */
static void t_loop(void)
{
    int i;
    log_reset();
    for (i = 0; i < 3; i++) {
        defer log_add("d");
        if (i == 1)
            continue;
        log_add("i");
        if (i == 2)
            break;
    }
    log_print("loop");
}

/* 3: defer runs on return, and the return value is preserved */
static int t_return(int x)
{
    log_reset();
    defer log_add("one");
    if (x > 0) {
        defer log_add("two");
        return x * 2;
    }
    defer log_add("three");
    return -1;
}

/* 4: goto out of a block runs the defers of the scopes being left */
static void t_goto(void)
{
    log_reset();
    {
        defer log_add("outer");
        {
            defer log_add("inner");
            goto out;
        }
    }
out:
    log_print("goto");
}

/* 5: recursion keeps each frame's defers separate */
static int rec(int n)
{
    defer log_add("r");
    if (n <= 0)
        return 0;
    return n + rec(n - 1);
}

static void t_rec(void)
{
    int r;
    log_reset();
    r = rec(3);
    printf("rec: %d %d\n", r, (int)(log_pos / 1));
}

int main(void)
{
    t_basic();
    t_loop();
    printf("ret1: %d ", t_return(5));
    log_print("ret1log");
    printf("ret2: %d ", t_return(-5));
    log_print("ret2log");
    t_goto();
    t_rec();
    return 0;
}
