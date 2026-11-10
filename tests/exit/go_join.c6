/* 'spawn' 的线程在 main 的清理链里 join：join 跑在返回之前，但不能影响
   main 的返回值，也不能抢在 defer 前面。线程自己的输出用 stderr，免得
   和测试运行器的 stdout 比对上。 */
// EXIT 7

#include <unistd.h>

static int worker(int x)
{
    write(2, "worker ran\n", 11);
    return x;
}

int main(void)
{
    defer write(2, "defer ran\n", 10);
    spawn worker(3);
    return 7;
}
