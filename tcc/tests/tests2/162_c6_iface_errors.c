/* c6 extension: diagnostics for malformed 'impl' blocks */
#if defined test_missing_method
interface Shape { int area(); void dump(int n); }
struct Circle { int r; };
impl Shape for Circle {
    int area(Circle *self) { return self->r; }
}
int main(void) { return 0; }
#elif defined test_wrong_param_count
interface Shape { int area(); }
struct Circle { int r; };
impl Shape for Circle {
    int area(Circle *self, int extra) { return self->r + extra; }
}
int main(void) { return 0; }
#elif defined test_wrong_return_type
interface Shape { int area(); }
struct Circle { int r; };
impl Shape for Circle {
    char *area(Circle *self) { return 0; }
}
int main(void) { return 0; }
#elif defined test_incompatible_param
interface Shape { int scale(int n); }
struct Circle { int r; };
impl Shape for Circle {
    int scale(Circle *self, char *n) { return self->r + (n != 0); }
}
int main(void) { return 0; }
#elif defined test_unknown_interface
struct Circle { int r; };
impl Nope for Circle {
    int area(Circle *self) { return self->r; }
}
int main(void) { return 0; }
#elif defined test_duplicate_impl
interface Shape { int area(); }
struct Circle { int r; };
impl Shape for Circle { int area(Circle *self) { return 1; } }
impl Shape for Circle { int area(Circle *self) { return 2; } }
int main(void) { return 0; }
#else
int main(void) { return 0; }
#endif
