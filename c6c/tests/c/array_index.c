/* expect: 15 */
int main(void) {
    int a[5];
    int i = 0;
    int s = 0;
    for (i = 0; i < 5; i = i + 1) { a[i] = i * 2; }
    for (i = 0; i < 5; i = i + 1) { s = s + a[i]; }
    return s;
}
