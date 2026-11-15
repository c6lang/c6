/* expect: 15 */
int main(void) {
    int i = 0;
    int s = 0;
    do { s = s + i; i = i + 1; } while (i < 6);
    return s;
}
