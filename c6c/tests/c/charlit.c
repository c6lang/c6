/* expect: 8 */
int main(void) {
    int a = 'A';
    int n = '\n';
    int z = 'z';
    if (a == 65) { return z - a + (n == 10) - 48; }
    return 1;
}
