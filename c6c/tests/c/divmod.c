/* expect: 37 */
int main(void) {
    int a = 17;
    int b = 5;
    int c = a / b;
    int d = a % b;
    return c * 10 + d + 2;
}
