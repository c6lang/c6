/* expect: 42 */
/* `__builtin_strlen` 在**常量上下文**里要能折成常数。
 *
 * 内核 include/linux/moduleparam.h 的 MODULE_INFO 里有一条
 *     _Static_assert(sizeof(__stringify(s)) - 1 == strlen(__stringify(s)),
 *                    "MODULE_INFO(...) contains embedded NUL byte")
 * 而 include/linux/string.h 把 strlen 指到 __builtin_strlen 上
 * （CONFIG_FORTIFY 关闭时 `#define strlen __builtin_strlen`）。
 * lib/btree.c 的 MODULE_AUTHOR("Joern Engel <joern@logfs.org>") 就撞在这条断言上。
 *
 * 同时钉住非常量实参：那一路必须仍然能编、能跑（c6c 以前把它宏展开成
 * 真正的 strlen() 调用，改动后要保持同样的运行期行为）。
 */
#include <stdio.h>
#include <string.h>

/* 常量上下文：静态断言与数组长度 */
_Static_assert(__builtin_strlen("") == 0, "empty");
_Static_assert(__builtin_strlen("abc") == 3, "abc");
_Static_assert(__builtin_strlen("Joern Engel <joern@logfs.org>") == 29, "author");
/* 相邻字面量拼接后长度 */
_Static_assert(__builtin_strlen("ab" "cd") == 4, "concat");
_Static_assert(sizeof("abc") - 1 == __builtin_strlen("abc"), "sizeof 一致");

static char buf[__builtin_strlen("hello") + 1];

int main(void)
{
	const char *s = "hello";

	if (sizeof(buf) != 6)
		return 1;
	/* 非常量实参：走运行期 */
	if (__builtin_strlen(s) != 5)
		return 2;
	if (__builtin_strlen("") != 0)
		return 3;
	if (__builtin_strlen("abcdef") != 6)
		return 4;
	return 42;
}
