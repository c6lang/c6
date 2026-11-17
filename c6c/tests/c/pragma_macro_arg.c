/* expect: 42 */
/* `_Pragma` 的操作数常是宏展开出来的字符串字面量，不是直接的字面量：
 * 内核 include/linux/compiler-gcc.h:118 就是
 *     #define __diag(s) _Pragma(__diag_str(GCC diagnostic s))
 * 再被 `__diag_ignore_all(...)` 套一层。以前 `_Pragma` 只用 raw_next 读操作数，
 * 拿到的还是未展开的宏名，整条 pragma 漏到语法分析里报"顶层声明缺名字"。 */
#define S1(s)		#s
#define S(s)		S1(s)
#define D(s)		_Pragma(S(GCC diagnostic s))

D(push)
D(ignored "-Wunused")

static int v = 42;

D(pop)

int main(void)
{
	_Pragma(S(GCC diagnostic push))
	v += 0;
	_Pragma(S(GCC diagnostic pop))
	return v;
}
