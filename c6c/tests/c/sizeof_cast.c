/* expect: 48 */
int main(void) {
    int a = 300;
    char c = (char)a;
    int n = sizeof(int) + sizeof(char) + sizeof(long);
    return c + n;
}
