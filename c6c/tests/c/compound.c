/* expect: 46 */
int main(void) {
    int s = 0;
    int i = 0;
    while (i < 10) { s += i; i += 1; }
    return s + 1;
}
