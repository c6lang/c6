/* expect: 42 */
/* 聚合按值传参：3 / 5 / 2 字节宽的三种规模都必须整块拷进被调方。
 * 曾经形参槽里只存了个指针，被调方按 8 字节读，结果全是垃圾。 */
typedef struct { long a; long b; long c; } S3;
typedef struct { long a; long b; long c; long d; long e; } S5;
typedef struct { char k; unsigned short l; } SM;

static long f3(S3 s) { return s.a + s.b + s.c; }
static long f5(S5 s) { return s.a + s.d + s.e; }
static long fm(SM s) { return s.k + s.l; }
static S3 mk(int i) { S3 r = {0}; r.a = i; r.b = i * 2; r.c = i * 3; return r; }

int main(void) {
    S3 a = {1, 2, 3};
    S5 b = {1, 2, 3, 4, 5};
    SM m = {0};
    S3 r = {0};

    m.k = 1;
    m.l = 2;
    r = mk(4);
    return (int)(f3(a) * 10 + f5(b) + fm(m) + r.c - 123);
}
