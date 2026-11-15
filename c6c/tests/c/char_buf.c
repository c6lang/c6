/* expect: 194 */
int main(void) {
    char buf[8] = "abc";
    return buf[0] + buf[2];
}
