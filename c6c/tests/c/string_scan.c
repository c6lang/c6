/* expect: 5 */
int main(void) {
    char *s = "hello";
    int n = 0;
    while (s[n] != 0) { n = n + 1; }
    return n;
}
