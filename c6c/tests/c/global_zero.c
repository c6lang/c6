/* expect: 7 */
int tbl[4];
int main(void) {
    tbl[1] = 3;
    tbl[2] = 4;
    return tbl[0] + tbl[1] + tbl[2];
}
