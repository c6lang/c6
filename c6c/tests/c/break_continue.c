/* expect: 11 */
int main(void) {
    int s = 0;
    int i = 0;
    while (i < 20) {
        i = i + 1;
        if (i % 2 == 0) { continue; }
        if (i > 5) { break; }
        s = s + i;
    }
    return s;
}
