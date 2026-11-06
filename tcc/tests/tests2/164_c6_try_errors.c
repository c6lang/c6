/* c6 extension: diagnostics for malformed 'try' / 'catch' / 'T!error' */
static int ok_int(void) { return 1; }
static int!error fails(int n) { if (n) err(1, "x"); return 0; }
struct S { int a; };

#if defined test_try_in_plain_fn
static int plain(void) { try fails(0); return 0; }
int main(void) { return plain(); }
#elif defined test_try_not_fallible
static int!error bad(void) { try ok_int(); return 0; }
int main(void) { return bad(); }
#elif defined test_try_not_a_call
static int!error bad(int n) { try n; return 0; }
int main(void) { return bad(1); }
#elif defined test_try_assignment
static int!error bad(void) { int v; try v = 1; return v; }
int main(void) { return bad(); }
#elif defined test_catch_not_fallible
static int bad(void) { int v = ok_int() catch e { return -1; }; return v; }
int main(void) { return bad(); }
#elif defined test_catch_falls_through
static int!error bad(void) { int v = fails(0) catch e { v = 1; }; return v; }
int main(void) { return bad(); }
#elif defined test_bare_catch
static int bad(void) { catch e { return -1; } return 0; }
int main(void) { return bad(); }
#elif defined test_fallible_not_a_function
static int!error x = 1;
int main(void) { return x; }
#elif defined test_fallible_struct_return
static struct S!error bad_ret(void) { struct S s; return s; }
int main(void) { return bad_ret().a; }
#elif defined test_try_in_defer
static int!error bad(void) { defer { try fails(0); } return 0; }
int main(void) { return bad(); }
#elif defined test_catch_in_defer
static int!error bad(void) { defer { int v = fails(0) catch e { return -1; }; } return 0; }
int main(void) { return bad(); }
#else
int main(void) { return 0; }
#endif
