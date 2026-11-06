/* built for plain C (no -std=c6): the package exports are ordinary
   symbols, so declaring the mangled name is enough to call them. */
int math__add(int a, int b);

int plain_double(int x) { return math__add(x, x); }
