/* 全局闭包是一个 GC 根：退出时由运行时释放。 */
closure int() g = NULL;

// EXIT 0
int main(void)
{
    int n = 7;
    closure int() f = int() { return n; };
    g = f;
    f = NULL;
    if (g() != 7)
        return 1;
    return 0;
}
