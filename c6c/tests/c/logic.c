/* expect: 1 */
int main(void) {
    int a = 3;
    int b = 4;
    int t = (a < b) && (b > 0);
    int f = (a > b) || (a == 0);
    int n = !(a == 3);
    return t * 1 + f * 0 + n * 0;
}
