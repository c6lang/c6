/* 一段"像真的 C 程序"的综合用例：qsort + 回调、二维数组当形参、矩阵乘、
 * 字符串切分（手写 strtok 风格）、指针数组与二级指针、链表遍历、
 * switch 贯穿/嵌套 + goto 跳出、strcpy/strncpy/memset/strchr/strstr、
 * union 按字节看、静态数组的地址。三后端 stdout 要逐字节一致。 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct Item { int key; char name[8]; };
union Val { int i; float f; char b[4]; };
struct Node { int v; struct Node *next; };

static int cmp_item(const void *a, const void *b)
{
	const struct Item *x = a;
	const struct Item *y = b;

	return x->key - y->key;
}

static void mat_mul(int n, int a[][2], int b[][2], int out[][2])
{
	int i, j, k;

	for (i = 0; i < n; i++)
		for (j = 0; j < 2; j++) {
			out[i][j] = 0;
			for (k = 0; k < 2; k++)
				out[i][j] += a[i][k] * b[k][j];
		}
}

static int str_split(char *s, char **out, int max)
{
	int n = 0;
	char *p = s;

	while (*p && n < max) {
		out[n++] = p;
		while (*p && *p != ',')
			p++;
		if (*p)
			*p++ = 0;
	}
	return n;
}

int main(void)
{
	struct Item items[4] = {{3, "c"}, {1, "a"}, {4, "d"}, {2, "b"}};
	union Val v;
	struct Node n3 = {3, NULL}, n2 = {2, &n3}, n1 = {1, &n2};
	int m1[2][2] = {{1, 2}, {3, 4}};
	int m2[2][2] = {{5, 6}, {7, 8}};
	int mo[2][2];
	char buf[] = "aa,bb,cc";
	char *parts[4];
	int np = 0, i = 0;
	int **pp = NULL;
	int *ptrs[3];
	int **tab = ptrs;
	int sum = 0;

	v.i = 0x41424344;
	printf("union=%d %d %d\n", v.b[0], v.b[3], (int)sizeof v);

	qsort(items, 4, sizeof items[0], cmp_item);
	for (i = 0; i < 4; i++)
		printf("item%d=%d%s ", i, items[i].key, items[i].name);
	printf("\n");

	mat_mul(2, m1, m2, mo);
	printf("mat=%d %d %d %d\n", mo[0][0], mo[0][1], mo[1][0], mo[1][1]);

	np = str_split(buf, parts, 4);
	printf("split=%d", np);
	for (i = 0; i < np; i++)
		printf(" %s", parts[i]);
	printf("\n");

	{
		static int store[3] = {7, 8, 9};

		for (i = 0; i < 3; i++)
			tab[i] = &store[i];
		pp = tab;
		printf("tab=%d %d %d\n", *pp[0], **pp, sum = *pp[1] + *pp[2]);
		printf("tab2=%d\n", sum);
	}
	{
		struct Node *cur = &n1;

		while (cur) {
			printf("node=%d", cur->v);
			cur = cur->next;
		}
		printf("\n");
	}
	{
		/* switch 贯穿 + 嵌套 + goto 跳出 */
		int t = 0, k;

		for (k = 0; k < 5; k++) {
			switch (k) {
			case 0:
			case 1:
				t += 1;
				/* fallthrough */
			case 2:
				t += 10;
				if (k == 4)
					goto done;
				break;
			default:
				switch (k % 2) {
				case 0:
					t += 100;
					break;
				default:
					t += 1000;
				}
			}
		}
done:
		printf("switch=%d\n", t);
	}
	{
		char a[16], b[16];

		strcpy(a, "hello");
		strncpy(b, a, 3);
		b[3] = 0;
		printf("str=%d %d %d %d\n", (int)strlen(a), strcmp(a, "hello"),
		       (int)strlen(b), strchr(a, 'l') - a);
		memset(b, 'z', 5);
		b[5] = 0;
		printf("memset=%s %d\n", b, strstr("haystack", "st") ? 1 : 0);
	}
	return 0;
}
