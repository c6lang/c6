/* c6 编译期诊断：结构化接口
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-errors.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过。
 */
struct Circle { int r; };

// EXPECT test_missing_method: error: 'Circle' does not implement interface 'Shape': missing method 'dump'
// EXPECT test_wrong_param_count: error: method 'area' of 'Shape' has the wrong number of parameters
// EXPECT test_wrong_return_type: error: method 'area' of 'Shape' has an incompatible return type
// EXPECT test_incompatible_param: error: method 'scale' of 'Shape' has an incompatible parameter
// EXPECT test_duplicate_method: error: duplicate method 'area' for 'Circle'
// EXPECT test_method_after_use: error: 'Circle' does not implement interface 'Shape': missing method 'area'
// EXPECT test_proto_scope_tag: error: method 'put' of 'Shape' has an incompatible parameter: the interface's 'struct P' is a prototype-scope tag

#if defined test_missing_method
interface Shape { int area(); void dump(int n); }
int area(struct Circle *self) { return self->r; }
static int use(Shape s) { return s.area(); }
int main(void) { struct Circle c = {1}; return use(c); }
#elif defined test_wrong_param_count
interface Shape { int area(); }
int area(struct Circle *self, int extra) { return self->r + extra; }
static int use(Shape s) { return s.area(); }
int main(void) { struct Circle c = {1}; return use(c); }
#elif defined test_wrong_return_type
interface Shape { int area(); }
char *area(struct Circle *self) { return 0; }
static int use(Shape s) { return s.area(); }
int main(void) { struct Circle c = {1}; return use(c); }
#elif defined test_incompatible_param
interface Shape { int scale(int n); }
int scale(struct Circle *self, char *n) { return self->r + (n != 0); }
static int use(Shape s) { return s.scale(2); }
int main(void) { struct Circle c = {1}; return use(c); }
#elif defined test_duplicate_method
package a { int area(struct Circle *self) { return self->r; } }
package b { int area(struct Circle *self) { return self->r + 1; } }
int main(void) { return 0; }
#elif defined test_method_after_use
interface Shape { int area(); }
static int use(Shape s) { return s.area(); }
/* 转换点看不到之后才声明的方法 */
int main(void) { struct Circle c = {1}; return use(c); }
int area(struct Circle *self) { return self->r; }
#elif defined test_proto_scope_tag
/* 'struct P' 第一次出现在 interface 的签名里：按 C 的规则那是原型作用域里
   的 tag，和随后文件作用域的 struct P 不是同一个类型。诊断要说清这一点，
   而不是笼统地报“参数不兼容”。 */
interface Shape { void put(struct P p); int area(); }
struct P { int x, y; };
void put(struct Circle *self, struct P p) { (void)self; (void)p; }
int area(struct Circle *self) { return self->r; }
int main(void)
{
    struct Circle c = {1};
    Shape s = {0};
    s = c;
    return s.area() == 1 ? 0 : 1;
}
#else
interface Shape { int area(); }
int area(struct Circle *self) { return self->r; }
int main(void)
{
    struct Circle c = {1};
    Shape s = {0};
    s = c;
    return s.area() == 1 ? 0 : 1;
}
#endif
