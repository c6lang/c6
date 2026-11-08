/* ABI 兼容：模块导出的就是普通的、带前缀的 C 符号。
 *
 * 纯 C 一侧（module_plain.c，不带 -std=c6）只声明
 *   struct math__Point, math__add, math__Point_area, math__counter
 * 就能调用，不需要知道 module/import 的存在。
 */
module math;

struct Point { int x, y; };
typedef struct Point Point;
enum Color { RED, GREEN };

int counter = 7;

int add(int a, int b) { return a + b; }
int area(Point *p) { return p->x * p->y; }

// EXIT 0
