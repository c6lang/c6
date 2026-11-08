/* c6 自测：全局根在程序退出时释放（引用计数 GC，阶段 P4）
 *
 * 运行（仓库根目录）：
 *   tests/run-tests.sh exit/drop_exit
 *
 * 托管全局的槽要活到程序结束，所以编译器为每个含托管全局的翻译单元发
 * 一对构造/析构函数：构造时把槽地址交给运行时（'__c6_gc_root'），析构
 * 时调用 '__c6_gc_shutdown' 逐个释放。'new T' 不能出现在静态初始化式里，
 * 所以全局只能先给空值、在函数里赋值。
 *
 * 这里用自定义的析构函数来验收：它跑在进程退出时，无论是编译器发的那
 * 个析构先跑，还是它自己补调 '__c6_gc_shutdown'（幂等），只要全局根的
 * 释放是好的，就能看到 drop 跑过；否则用退出码 9 报失败。
 */
// EXIT 0

#include <unistd.h>

struct Global { int v; };

static int dropped = 0;

void drop(struct Global *self)
{
    dropped = self->v;
}

@Global keep = NULL;

/* 'fini_array' 反向走：编译器发的析构在本文件末尾登记，通常先跑 */
static void check(void) __attribute__((destructor));
static void check(void)
{
    if (dropped == 8)
        return;
    __c6_gc_shutdown();
    if (dropped != 8)
        _exit(9);
    if (keep != NULL)
        _exit(10);
}

int main(void)
{
    keep = new Global;
    keep->v = 8;
    if (__c6_gc_live() != 1)
        return 3;
    return 0;
}
