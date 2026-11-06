/* c6 编译期诊断：import 与 pkg.member
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-errors.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过。
 */
package math {
    int counter = 0;
    int add(int a, int b) { return a + b; }
}

// EXPECT test_not_imported: error: 'math' is not an imported package (use 'import math;')
// EXPECT test_not_imported_multi: error: 'acme.util' is not an imported package (use 'import acme.util;')
// EXPECT test_parent_only_import: error: 'acme.util' is not an imported package (use 'import acme.util;')
// EXPECT test_unknown_member: error: 'math.nope' undeclared
// EXPECT test_unknown_member_call: warning: implicit declaration of function 'math.nope'
// EXPECT test_unknown_member_call: error: unresolved reference to 'math__nope'
// EXPECT test_missing_member_name: error: identifier expected after '.'
// EXPECT test_arrow_is_not_package_access: error: 'math' undeclared
// EXPECT test_import_after_use: error: 'math' is not an imported package (use 'import math;')

#if defined test_not_imported
int main(void) { return math.add(1, 2); }
#elif defined test_not_imported_multi
package acme { package util { int f(void) { return 1; } } }
int main(void) { return acme.util.f(); }
#elif defined test_parent_only_import
package acme { package util { int f(void) { return 1; } } }
import acme;
int main(void) { return acme.util.f(); }
#elif defined test_unknown_member
import math;
int main(void) { return math.nope; }
#elif defined test_unknown_member_call
import math;
int main(void) { return math.nope(1, 2); }
#elif defined test_missing_member_name
import math;
int main(void) { return math.; }
#elif defined test_arrow_is_not_package_access
import math;
int main(void) { return math->add(1, 2); }
#elif defined test_import_after_use
int main(void) { return math.add(1, 2); }
import math;
#else
import math;
int main(void) { return math.add(1, 2) == 3 ? 0 : 1; }
#endif
