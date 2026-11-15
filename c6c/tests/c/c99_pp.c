/* C99 预处理器的边角（6.10）：函数式宏的递归抑制、`#`/`##`（含两级展开）、
 * 变参宏与空实参、`#if` 里的算术与 `defined`、`#elif` 嵌套、字符常量在 `#if`
 * 里、`#undef` 后重定义、`__LINE__`/`__FILE__`、`#line`、未知 `#pragma` 要
 * 忽略、`#pragma once`、多行宏的续行、注释在宏里。
 *
 * 输出要和 gcc -std=c99 逐字节一致（三后端也一样）。 */
#include <stdio.h>
#include <string.h>

#define STR(x) #x
#define XSTR(x) STR(x)
#define VSTR(...) #__VA_ARGS__
#define CAT(a, b) a##b
#define XCAT(a, b) CAT(a, b)
#define SUM(a, b) ((a) + (b))
#define EMPTY
#define VARIADIC(fmt, ...) printf(fmt, __VA_ARGS__)
#define VARIADIC0(...) printf(__VA_ARGS__)
#define COUNT_ARGS(...) COUNT_(__VA_ARGS__, 5, 4, 3, 2, 1)
#define COUNT_(a, b, c, d, e, n, ...) n
#define EMPTYARG(x, ...) ((x) + 0)
#define NESTED(...) SUM(__VA_ARGS__)
#define CONCAT3(a, b, c) a##b##c
#define APPLY(f, x) f(x)
#define DOUBLE(x) ((x) * 2)

#if defined(__STDC__) && __STDC_VERSION__ >= 199901L
#define HAVE_C99 1
#else
#define HAVE_C99 0
#endif

#if defined(NOT_DEFINED) || 0
#define BRANCH "bad"
#elif (2 + 3) * 2 == 10 && !defined(__cplusplus)
#define BRANCH "ok"
#else
#define BRANCH "bad"
#endif

#if 'A' == 65 && '\n' == 10
#define CHARC_OK 1
#else
#define CHARC_OK 0
#endif

#define VERSION_MAJOR 2
#define VERSION_MINOR 7
#define VERSION XSTR(VERSION_MAJOR) "." XSTR(VERSION_MINOR)

/* 先定义、再用 #undef 换掉：第二次展开必须用新定义 */
#undef SUM
#define SUM(a, b) ((a) * 10 + (b))

#pragma some_unknown_pragma 1 2 3

#line 500 "fake.c"

int main(void)
{
	int CAT(var, 1) = 10;
	int CAT(var, 2) = 20;
	int line = __LINE__;
	const char *file = __FILE__;
	long XCAT(va, r) = 4;
	{
		int cont = 1 + \
			   2;
		/* 多行注释里再来一行注释符：*/ int after = 5; /*
		*/

		printf("cont=%d after=%d\n", cont, after);
	}

	printf("str=%s xstr=%s cat=%d\n", STR(hello world), XSTR(CONCAT3(1, 2, 3)),
	       var1 + var2);
	printf("version=%s branch=%s c99=%d charc=%d\n", VERSION, BRANCH, HAVE_C99,
	       CHARC_OK);
	printf("sum=%d nested=%d\n", SUM(2, 3), NESTED(4, 5));
	printf("apply=%d empty=%d\n", APPLY(DOUBLE, 6), EMPTYARG(7, 8));
	printf("count=%d %d %d\n", COUNT_ARGS(a), COUNT_ARGS(a, b),
	       COUNT_ARGS(a, b, c));
	printf("vstr=%s %d\n", VSTR(a, b, c), (int)strlen(VSTR(a, b, c)));
	VARIADIC("var=%d %d\n", 1, 2);
	VARIADIC0("var0=%d\n", 3);
	printf("line=%d file=%s\n", line, file);
	printf("var=%ld\n", var);
	return 0;
}
