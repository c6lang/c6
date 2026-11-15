/* expect: 10 */
int main(void) { int i = 0; int s = 0; while (i < 5) { s = s + i; i = i + 1; } return s; }
