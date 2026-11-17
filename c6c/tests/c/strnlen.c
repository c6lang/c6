/* expect: 42 */
/* <string.h> 里的 strnlen：内核 scripts/dtc/libfdt/fdt_ro.c 用它在
 * `#include <string.h>` 之后直接调。c6c 自带的 string.h 以前只声明了
 * memchr/strlen 两个，编到一半报"未声明的标识符 'strnlen'"。 */
#include <string.h>

int main(void)
{
	const char *s = "hello";
	int r = 0;

	if (strnlen(s, 100) == 5)
		r += 1;
	if (strnlen(s, 3) == 3)
		r += 2;
	if (strnlen("", 10) == 0)
		r += 4;
	if (strnlen(s, 0) == 0)
		r += 8;
	if (strnlen(s, 5) == 5)
		r += 16;
	if (strnlen(s, 6) == 5)
		r += 11;
	return r;
}
