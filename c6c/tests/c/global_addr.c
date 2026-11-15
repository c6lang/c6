/* 全局/静态初始化器里的**地址常量**（C99 6.6p7 / 6.6p9：全局对象或函数的
 * 地址 + 整数常量偏移）。三种后端（native / --emit-c / --emit-llvm）都要
 * 和 gcc 算出同一个地址。
 *
 * 每个指针都同时验两件事：`p == &已知子对象`（地址本身对不对）和 `*p`
 * 读出来的内容对不对。只比 `*p` 会在"偏移错但恰好落在另一个同值成员上"时
 * 蒙混过去。
 *
 * expect: 225（没装 gcc 时的兜底口径，和 gcc 的结果一致） */
struct P {
    int x, y;
};
struct Q {
    int tag;
    struct P p[2];
    int tail[3];
};

int A[5] = {10, 20, 30, 40, 50};
int M[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
struct P tbl[3] = {{1, 2}, {3, 4}, {5, 6}};
struct Q q = {7, {{8, 9}, {10, 11}}, {12, 13, 14}};
char txt[8] = "abcdefg";

/* 数组名退化 + 常量偏移 */
int *p_arr2 = A + 2;
int *p_arr0 = &A[0];
int *p_arr4 = &A[4];
int *p_sub = &A[4] - 1;
int *p_lead = 2 + A;                   /* 整数在左的加法 */
/* 成员偏移（含嵌套） */
int *p_mem = &tbl[1].y;
int *p_nest = &q.p[1].y;
int *p_tail = &q.tail[2];
int *p_tag = &q.tag;
/* 成员里的数组成员 */
int *p_row = &M[1][2];
int *p_row2 = &M[2][0];
char *p_ch = &txt[3];
/* 索引表达式可以是任意整数常量表达式 */
int *p_expr = &A[2 * 2];
/* 二维数组的行指针：`M + 1` 按 sizeof(int[4]) 缩放 */
int (*p_rowp)[4] = M + 1;
/* 函数指针 / 函数地址 */
int forty(void) { return 40; }
int (*p_fn)(void) = forty;
int (*p_fn2)(void) = &forty;

/* 聚合初始化器里的地址常量带偏移（`gen_glob_obj` 那条路） */
struct Pair {
    int *a;
    int *b;
    char *c;
};
struct Pair gpair = {&A[1], A + 3, &txt[6]};
int *ptab[3] = {&A[0], &tbl[2].x, &q.tail[1]};
void *mixed[2] = {&A[2], forty};

/* 块的静态量：走的是同一条全局初始化路径 */
int static_test(void)
{
    static int B[3] = {100, 200, 250};
    static int *pb = &B[1];
    static int *pb2 = B + 2;

    if (pb != &B[1])
        return 90;
    if (pb2 != &B[2])
        return 91;
    return (*pb + *pb2) % 256;         /* 200 + 250 = 450 -> 194 */
}

int main(void)
{
    if (p_arr2 != &A[2])
        return 81;
    if (p_arr0 != A || p_arr4 != &A[4])
        return 82;
    if (p_sub != &A[3] || p_lead != &A[2])
        return 83;
    if (p_mem != &tbl[1].y || p_nest != &q.p[1].y)
        return 84;
    if (p_tail != &q.tail[2] || p_tag != &q.tag)
        return 85;
    if (p_row != &M[1][2] || p_row2 != &M[2][0])
        return 86;
    if (p_ch != &txt[3] || p_expr != &A[4])
        return 87;
    if (p_rowp != &M[1] || (*p_rowp)[1] != 6)
        return 88;
    if (p_fn != forty || p_fn2 != &forty || p_fn() != 40)
        return 89;
    if (gpair.a != &A[1] || gpair.b != &A[3] || gpair.c != &txt[6])
        return 92;
    if (ptab[0] != A || ptab[1] != &tbl[2].x || ptab[2] != &q.tail[1])
        return 93;
    if (mixed[0] != (void *)&A[2] || mixed[1] != (void *)forty)
        return 94;
    return (*p_arr2 + *p_arr0 + *p_arr4 + *p_sub + *p_lead      /* 30 10 50 40 30 */
            + *p_mem + *p_nest + *p_tail + *p_tag                /* 4 11 14 7 */
            + *p_row + *p_row2 + *p_ch                           /* 7 9 'd'=100 */
            + *p_expr + *gpair.a + *gpair.b + *gpair.c           /* 50 20 40 'g' */
            + *ptab[1] + *ptab[2]                                /* 5 13 */
            + static_test()) % 256;                              /* 194 */
}
