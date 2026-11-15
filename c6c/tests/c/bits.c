/* expect: 21 */
int main(void) {
    int x = 6;
    int y = 3;
    int and_v = x & y;
    int or_v = x | y;
    int xor_v = x ^ y;
    int shl_v = x << 2;
    int shr_v = 48 >> 3;
    int not_v = ~x & 7;
    return and_v + or_v + xor_v + shl_v + shr_v + not_v - 20;
}
