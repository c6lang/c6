/* superc 自测：package / import / pkg.member
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=tcc-ext -test tests/package.c tests/tu/package.c
 */
#include <stdio.h>
#include <string.h>

package math {
    int counter = 0;
    int add(int a, int b) { return a + b; }
    /* 包内可以直接用限定写法，不需要 import */
    int twice(int a) { return math.add(a, a); }
    int hidden_helper(int a) { return a + 1; }
    struct Point { int x, y; };
    struct Point origin = {3, 4};
    int bump(void) { return ++counter; }
}

/* import 也可以写在包块里，作用范围是块的剩余部分 */
package text {
    import math;
    int len(const char *s)
    {
        int n = 0;
        while (s[n])
            n++;
        return n;
    }
    int len_plus(int k) { return len("ab") + math.add(k, 1); }
}

/* 多级包名只是词法前缀：'acme.util' 就是 'acme__util' */
package acme.util {
    int counter = 0;
    int add(int a, int b) { return a + b; }
    int twice(int a) { return acme.util.add(a, a); }
}

/* 嵌套包块把前缀拼起来 */
package acme {
    package util {
        int len(const char *s)
        {
            int n = 0;
            while (s[n])
                n++;
            return n;
        }
    }
}

import math;
import text;
import acme.util;

/* 限定的包函数就是普通符号，取地址也可以用 */
int (*add_ref)(int, int) = math.add;

/* ---- 跨翻译单元（tests/tu/package.c）---- */
int plain_double(int x);
package remote {
    int add(int a, int b);
    int bump(void);
    extern int counter;
}
import remote;

/* 文件级形式给文件的剩余部分命名（所以必须放在最后） */
package acme.crc;
int sum(int a, int b) { return a + b; }

/* ---- 测试块 ---- */

test "import 之后可以不带限定地调用包函数" {
    if (add(2, 3) != 5)
        err(1, "add 不对");
    if (twice(21) != 42)
        err(2, "twice 不对");
    if (len("hello") != 5)
        err(3, "len 不对");
}

test "包成员符号就是普通的 C 全局符号" {
    if (math__add(10, 20) != 30)
        err(1, "math__add 不对");
    if (text__len("abc") != 3)
        err(2, "text__len 不对");
    if (math__hidden_helper(41) != 42)
        err(3, "math__hidden_helper 不对");
}

test "pkg.member 限定访问" {
    if (math.add(2, 3) != 5)
        err(1, "math.add 不对");
    if (add(4, 5) != 9)
        err(2, "import 之后的 add 不对");
    if (math.twice(21) != 42)
        err(3, "math.twice 不对");
    if (text.len("hello") != 5)
        err(4, "text.len 不对");
    if (text.len_plus(1) != 4)
        err(5, "text.len_plus 不对");
}

test "包全局变量：读、取地址、赋值" {
    if (math.counter != 0)
        err(1, "counter 的初值不为 0");
    if (math.bump() != 1)
        err(2, "第一次 bump 不对");
    if (math.bump() != 2)
        err(3, "第二次 bump 不对");
    math.counter = 10;
    if (math.counter != 10)
        err(4, "counter 赋值没生效");
    math__counter = 42;
    if (math.counter != 42)
        err(5, "mangled 名字和限定访问不是同一个对象");
}

test "包里的结构体全局可以逐字段访问" {
    if (math.origin.x != 3 || math.origin.y != 4)
        err(1, "origin 的初值不对");
    *&math.origin.x = 8;
    if (math.origin.x != 8)
        err(2, "取地址再写入没有生效");
}

test "限定的包函数可以取地址" {
    if (add_ref(20, 22) != 42)
        err(1, "函数指针调用不对");
}

test "可见的对象优先于包名：普通 C 成员访问" {
    struct Point math = {7, 8};
    if (math.x != 7)
        err(1, "同名对象没有遮蔽包名");
}

test "多级包名：acme.util 与嵌套包块等价" {
    if (acme.util.add(2, 3) != 5)
        err(1, "acme.util.add 不对");
    if (acme.util.twice(21) != 42)
        err(2, "acme.util.twice 不对");
    if (acme.util.len("hello") != 5)
        err(3, "嵌套包块里的 len 不对");
    if (acme__util__add(1, 2) != 3)
        err(4, "mangled 名字 acme__util__add 不对");
}

test "文件级 package 声明给剩余部分命名" {
    if (acme.crc.sum(20, 22) != 42)
        err(1, "acme.crc.sum 不对");
    if (acme__crc__sum(3, 4) != 7)
        err(2, "acme__crc__sum 不对");
    acme.util.counter = 7;
    if (acme.util.counter != 7)
        err(3, "acme.util.counter 赋值没生效");
}

test "包成员对普通 C 单元就是一个普通符号" {
    if (plain_double(21) != 42)
        err(1, "普通 C 单元按 mangled 名字调用包函数不对");
}

test "包成员跨翻译单元共享" {
    if (remote.add(2, 3) != 5)
        err(1, "另一个 TU 的 remote.add 不对");
    remote.counter = 5;
    if (remote.counter != 5)
        err(2, "另一个 TU 的 remote.counter 赋值没生效");
    if (remote.bump() != 6)
        err(3, "另一个 TU 的 remote.bump 不对");
}
