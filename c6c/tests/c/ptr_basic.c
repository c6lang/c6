/* expect: 18 */
int main(void) {
    int x = 7;
    int *p = &x;
    *p = 9;
    return *p + x;
}
