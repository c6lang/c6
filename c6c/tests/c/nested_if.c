/* expect: 7 */
int classify(int n) {
    if (n < 0) { return 1; }
    else if (n == 0) { return 2; }
    else if (n < 10) { return 3; }
    else { return 4; }
}
int main(void) { return classify(5) + classify(0) + classify(-2) + classify(100); }
