/* C99 **多维**变长数组：`int b[n][m]`（二维）、`int c[n][m][k]`（三维），
 * 以及常量维和运行期维混着写（`int d[n][4]`、`int e[3][m]`）。
 *
 * 为什么要有这个用例：`b[i]` 的行 stride 是**运行期**算出来的（`m * sizeof(int)`），
 * 不能按 sizeof(元素) 缩放。以前多维 VLA 要么直接报"暂不支持"，要么
 * （`int b[n][4]` 这种内层是常量的）静默按 sizeof(int) 缩放，越界写到别的
 * 栈对象上——同一个函数里 `int b[n][4]` 和 `int x` 一起用就会踩。
 *
 * 只按**退出码**判对错（不打 printf，LLVM 后端打浮点还有别的坑），所以四个
 * 后端都能跑：native / C99 / LLVM 三家输出必须逐字节一致，gcc 当 oracle。 */
/* expect: 0 */

/* 一行（`int *`）：验证 `b[i]` 这个"行"能当普通指针用（它的值就是行地址） */
static int row_sum(int n, int *row)
{
    int i = 0;
    int s = 0;

    for (i = 0; i < n; i++)
        s += row[i];
    return s;
}

/* 整块摊平成一维读：行 stride 乘对了，整块就是连续排下来的 */
static int flat_sum(int total, int *p)
{
    int i = 0;
    int s = 0;

    for (i = 0; i < total; i++)
        s += p[i];
    return s;
}

int main(void)
{
    int n = 0;

#define CHK(x) do { if (!(x)) return (1000 + ++n); } while (0)

    /* 1) 二维 VLA：读写 + sizeof 是运行期的整块字节数 */
    {
        int r = 3;
        int c = 4;
        int b[r][c];
        int i = 0;
        int j = 0;
        int s = 0;

        for (i = 0; i < r; i++)
            for (j = 0; j < c; j++)
                b[i][j] = i * 10 + j;
        for (i = 0; i < r; i++)
            for (j = 0; j < c; j++)
                s += b[i][j];
        CHK(s == 138);                      /* 每行 0+1+2+3 加上 i*10 的偏移 */
        CHK(sizeof b == 48);
        CHK(sizeof(b) == 48);
        CHK(sizeof b == sizeof(b));
        CHK((long)sizeof b == 48);
        CHK(b[0][0] == 0);
        CHK(b[1][2] == 12);
        CHK(b[2][3] == 23);
        /* 行地址和元素地址都是简单的线性关系 */
        CHK(&b[2][3] == (int *)b + 11);
        CHK(&b[1][0] == (int *)b + 4);
        /* 把一行当普通 `int *` 用 */
        CHK(row_sum(c, b[1]) == 46);
        {
            int *row = b[2];

            CHK(row[0] == 20);
            CHK(row[3] == 23);
        }
        /* 整块摊平 */
        CHK(flat_sum(r * c, (int *)b) == 138);
    }
    /* 2) 长度是**声明那一刻**定下来的：以后改 c 不影响这块的 sizeof */
    {
        int m = 5;
        int t[m][m];

        CHK(sizeof t == 100);
        m = 7;
        CHK(sizeof t == 100);
        t[4][4] = 42;
        CHK(t[4][4] == 42);
        CHK(&t[4][4] == (int *)t + 24);
    }
    /* 3) 常量维和运行期维混着写 */
    {
        int r = 3;
        int c = 4;
        int d[r][4];                /* 内层是常量 */
        int e[3][c];                /* 外层是常量 */
        int i = 0;
        int j = 0;

        CHK(sizeof d == 48);
        CHK(sizeof e == 48);
        for (i = 0; i < r; i++)
            for (j = 0; j < 4; j++)
                d[i][j] = i + j;
        for (i = 0; i < 3; i++)
            for (j = 0; j < c; j++)
                e[i][j] = i * j;
        CHK(d[2][3] == 5);
        CHK(&d[2][1] == (int *)d + 9);
        CHK(e[2][3] == 6);
        CHK(&e[2][1] == (int *)e + 9);
        CHK(row_sum(4, d[1]) == 10);
    }
    /* 4) 三维 VLA */
    {
        int p = 2;
        int q = 3;
        int r = 4;
        int c3[p][q][r];
        int i = 0;
        int j = 0;
        int k = 0;
        int s = 0;

        CHK(sizeof c3 == 96);
        for (i = 0; i < p; i++)
            for (j = 0; j < q; j++)
                for (k = 0; k < r; k++)
                    c3[i][j][k] = i * 100 + j * 10 + k;
        for (i = 0; i < p; i++)
            for (j = 0; j < q; j++)
                for (k = 0; k < r; k++)
                    s += c3[i][j][k];
        CHK(s == 1476);
        CHK(c3[1][2][3] == 123);
        CHK(&c3[1][2][3] == (int *)c3 + 23);
        CHK(row_sum(r, c3[1][2]) == 486);
        CHK(c3[0][1][2] == 12);
        {
            int *row = c3[1][0];

            CHK(row[0] == 100);
            CHK(row[3] == 103);
        }
    }
    /* 5) 循环里反复声明不同长度的 VLA：每次都要重新算 stride，不能残留 */
    {
        int i = 0;

        for (i = 1; i <= 4; i++) {
            int d[i][i + 1];
            int a = 0;
            int b = 0;
            int s = 0;

            CHK(sizeof d == i * (i + 1) * 4);
            for (a = 0; a < i; a++)
                for (b = 0; b < i + 1; b++)
                    d[a][b] = a * 10 + b;
            for (a = 0; a < i; a++)
                for (b = 0; b < i + 1; b++)
                    s += d[a][b];
            CHK(d[i - 1][i] == (i - 1) * 10 + i);
            CHK(&d[i - 1][i] == (int *)d + i * (i + 1) - 1);
            CHK(row_sum(i + 1, d[i - 1]) == (i - 1) * 10 * (i + 1) + (i * (i + 1)) / 2);
            CHK(s == flat_sum(i * (i + 1), (int *)d));
        }
    }
    /* 6) 嵌套块 + 和普通局部量挨着放：stride 算错就会踩到旁边那个 int */
    {
        int m = 2;
        int guard = 0x5a5a;
        int v[m][3];
        int i = 0;
        int j = 0;

        for (i = 0; i < m; i++)
            for (j = 0; j < 3; j++)
                v[i][j] = i * 3 + j;
        CHK(guard == 0x5a5a);
        CHK(v[1][2] == 5);
        CHK(sizeof v == 24);
    }
    return 0;
}
