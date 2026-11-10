/* module 的编译期诊断。
 *
 * 运行（仓库根目录）：
 *   ./tests/run-tests.sh errors/module
 */
// EXPECT MODMAIN: 'main' cannot be declared in a module
// EXPECT MODFIRST: 'module' must be the first declaration in the file
// EXPECT MODDUP: module 'm' already declared in this file
// EXPECT MODTYPE: 'math.Nope' undeclared

#if defined(MODMAIN)
module m;
int main(void) { return 0; }

#elif defined(MODFIRST)
int x = 0;
module late;

#elif defined(MODDUP)
module m;
module m;

#elif defined(MODTYPE)
import math;
int f(void) { math.Nope x = 1; return x; }

#else
int main(void) { return 0; }
#endif
