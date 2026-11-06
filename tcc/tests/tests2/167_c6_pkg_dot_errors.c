/* c6 extension: diagnostics for 'pkg.member' */
package math {
    int counter = 0;
    int add(int a, int b) { return a + b; }
}

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
