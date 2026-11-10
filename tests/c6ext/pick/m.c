/* 故意与 m.c6 同名：import 的优先级回归用，正常情况下不应被选中。
 * 这里写 module 只是为了让它成为合法的 c6 模块源；它不会被单独编译
 * （tests/run-tests.sh 只 glob 上一层的 tests/c6ext/*.c6）。 */
module pick.m;

enum { PICK = 22 };
