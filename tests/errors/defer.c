/* c6 编译期诊断：defer / errdefer
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-errors.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过。
 *
 * 清理点的运行期状态住在函数帧里（docs/grammar.md 8.2），所以上限也是
 * 编译期的：一个函数 128 个清理点（C101），清理体最多嵌套 8 层（C102）。
 * 超出报错而不是运行期悄悄丢——这正是 old 每线程记录栈的毛病。
 */
static int g = 0;

/* 一次编译最多 128 个清理点：129 个就报错 */
#define S1  { defer g += 1; }
#define S4  S1 S1 S1 S1
#define S16 S4 S4 S4 S4
#define S64 S16 S16 S16 S16
#define S128 S64 S64

// EXPECT test_site_limit: error: a function cannot have more than 128 cleanup sites
// EXPECT test_body_nesting: error: 'defer' bodies nested too deeply
// EXPECT test_return_in_defer: error: 'return' is not allowed in a defer body
// EXPECT test_break_in_defer: error: 'break' is not allowed in a defer body
// EXPECT test_continue_in_defer: error: 'continue' is not allowed in a defer body
// EXPECT test_goto_in_defer: error: 'goto' is not allowed in a defer body
// EXPECT test_err_in_defer: error: 'err' is not allowed in a defer body

#if defined test_site_limit
/* 129 个清理点：第 129 个报 C101 */
static void too_many(void)
{
    S128
    S1
}
int main(void) { too_many(); return g; }
#elif defined test_body_nesting
/* 9 层清理体：第 9 层报 C102（8 层是允许的） */
static void too_deep(void)
{
    defer {
        defer {
            defer {
                defer {
                    defer {
                        defer {
                            defer {
                                defer {
                                    defer {
                                        g += 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
int main(void) { too_deep(); return g; }
#elif defined test_return_in_defer
static int bad(void) { defer { return 1; } return 0; }
int main(void) { return bad(); }
#elif defined test_break_in_defer
static void bad(void) { while (1) { defer { break; } } }
int main(void) { bad(); return 0; }
#elif defined test_continue_in_defer
static void bad(void) { while (1) { defer { continue; } } }
int main(void) { bad(); return 0; }
#elif defined test_goto_in_defer
static void bad(void) { defer { goto out; } out: ; }
int main(void) { bad(); return 0; }
#elif defined test_err_in_defer
static int!error bad(void) { defer { err(1, "x"); } return 0; }
int main(void) { return bad(); }
#else
/* 正常路径：正好 128 个清理点、正好 8 层清理体，都必须编译通过 */
static void just_enough(void)
{
    S128
}

static void just_deep(void)
{
    defer {
        defer {
            defer {
                defer {
                    defer {
                        defer {
                            defer {
                                defer {
                                    g += 1;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

int main(void)
{
    just_enough();
    just_deep();
    return 0;
}
#endif
