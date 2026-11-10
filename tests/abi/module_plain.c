/* tests/abi/module.c6 的纯 C 一侧（纯 C：`.c` 后缀，扩展不生效）。 */

struct math__Point { int x, y; };
int math__add(int a, int b);
int math__Point_area(struct math__Point *p);
extern int math__counter;

int main(void)
{
    struct math__Point p = { 3, 4 };

    if (math__add(1, 2) != 3)
        return 1;
    if (math__Point_area(&p) != 12)
        return 2;
    if (math__counter != 7)
        return 3;
    return 0;
}
