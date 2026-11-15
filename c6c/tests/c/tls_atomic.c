/* expect: 33 */
/* `__thread` 和 `_Atomic`：镜像里各放一份，TLS 的地址走 %fs 基址 + 镜像偏移，
 * 原子量在单线程下就是普通读写（多线程走运行期加锁后端）。
 * 期望：a(11+4=15) + b(3*2=6) + c(5+7=12) = 33。 */
static __thread int a = 11;
__thread long b = 3;
_Atomic int c = 5;

int main(void)
{
    __thread int *p = &a;

    a += 4;
    b *= 2;
    c += 7;
    if (p != &a)
        return 1;
    return (int)a + (int)b + (int)c;
}
