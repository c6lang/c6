/* expect: 10 */
int main(void) {
    int a[6];
    int *p = a;
    int i = 0;
    int s = 0;
    for (i = 0; i < 6; i = i + 1) { *(p + i) = i + 1; }
    for (i = 0; i < 6; i = i + 1) { s = s + p[i]; }
    return s - 11;
}
