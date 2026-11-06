/* c6 extension: diagnostics for structured interfaces.
   A struct implements an interface by having the methods; the check
   happens at the first conversion, so every branch below converts. */
struct Circle { int r; };

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
/* the conversion below cannot see a method that is declared only later */
int main(void) { struct Circle c = {1}; return use(c); }
int area(struct Circle *self) { return self->r; }
#else
interface Shape { int area(); }
int area(struct Circle *self) { return self->r; }
int main(void) { struct Circle c = {1}; Shape s = {0}; s = c; return s.area(); }
#endif
