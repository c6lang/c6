/* expect: 46 */
int main(void) {
    int s = 0;
    int i = 0;
    for (i = 0; i < 10; i = i + 1) { s = s + i; }
    return s + 1;
}
