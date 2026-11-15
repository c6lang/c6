/* expect: 6 */
int g = 5;
int bump(void) { g = g + 1; return g; }
int main(void) { return bump(); }
