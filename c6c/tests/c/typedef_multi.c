/* expect: 45 */
/* 一条 typedef 里可以有**多个**声明符（C89 6.5.7）。以前只认第一个，
 * 遇到逗号就报"这里要 ';'（当前是 ','）"，而且那个名字没登记上，后面
 * 整段代码全被连锁带错——内核 lib/dhry.h:344 的
 * `typedef struct { … } Rec_Type, *Rec_Pointer;` 就是这个形状。
 * 文件作用域和块作用域两条路都要支持。 */
typedef int A, B;
typedef struct {
	int i;
} Rec_Type, *Rec_Pointer;
typedef int Scalar, Arr2[2];
typedef enum { E1, E2 } Enum_A, *Enum_P;

int main(void)
{
	typedef unsigned long ULong, *ULongP;
	A a = 5;
	B b = 6;
	Rec_Type r = { 7 };
	Rec_Pointer rp = &r;
	Arr2 arr = { 8, 9 };
	Scalar s = 10;
	Enum_A e = E2;
	Enum_P ep = &e;
	ULong u = s;
	ULongP up = &u;

	if (*ep != E2)
		return 1;
	if (*up != 10)
		return 2;
	if (rp->i != 7)
		return 3;
	return a + b + rp->i + arr[0] + arr[1] + (int)u;
}
