/* c6 编译期诊断：诊断位置归属
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过。
 *
 * `sizeof(type)` / `alignof(type)` 的类型位置会先把整个括号组扫一遍，再决定
 * 它是不是元组类型。扫描结束把 token 放回去时，组后面的声明仍然必须算在真正
 * 写它的文件里：行号来自 token 串里的行标记，文件名来自当时的 BufferedFile，
 * 两者错位就会报出“源文件:头文件里的行号”这种假诊断。
 */
// EXPECT test_sizeof_in_header: loc.h:2: error: variable 'loc_hg' must be initialized

#if defined test_sizeof_in_header
#include "loc.h"
int main(void) { return loc_hf(); }
#else
int main(void) { return 0; }
#endif
