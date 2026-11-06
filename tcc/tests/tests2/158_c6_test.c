/* c6 extension: 'test "desc" { ... }' blocks and -test mode */
#include <stdio.h>

int __tcc_err_flag_get(void);
int __tcc_err_id_get(void);
void __tcc_err_clear(void);

test "arithmetic works" {
    if (1 + 1 != 2)
        err(1, "1 + 1 != 2");
}

test "defer runs inside a test block" {
    int n = 0;
    {
        defer n++;
        n += 2;
    }
    if (n != 3)
        err(2, "defer did not run");
}

test "errdefer is skipped on the normal path" {
    errdefer printf("not reached\n");
    if (0)
        err(3, "never");
}

test "strings and loops" {
    int i = 0, sum = 0;
    const char *s = "abcd";
    for (i = 0; s[i]; i++)
        sum += s[i];
    if (sum != 'a' + 'b' + 'c' + 'd')
        err(4, "bad sum");
}

/* a helper used by a test; a failing helper propagates via 'try' */
static int!error helper(int x)
{
    if (x < 0)
        err(5, "negative");
    return x * 2;
}

test "helpers propagate errors" {
    if ((try helper(3)) != 6)
        err(6, "helper(3) != 6");
    helper(-1) catch e {
        if (e.id != 5)
            err(7, "wrong error id");
        return 0;           /* handled here: the test itself passes */
    };
    err(8, "helper did not fail");
}

int main(void)
{
    /* -test mode never calls main */
    printf("main should not run\n");
    return 0;
}
