/* c6 闭包不能跨线程/任务（C96） */

// EXPECT test_async: error: a closure cannot cross a thread or a task: V2 closures are confined to the thread that creates them
// EXPECT test_go: error: a closure cannot cross a thread or a task: V2 closures are confined to the thread that creates them

#if defined test_async
async int work(closure int() c) { return c(); }
int main(void) { return 0; }
#elif defined test_go
int work(closure int() c) { return c(); }
int main(void)
{
    int n = 1;
    closure int() c = int() { return n; };
    go work(c);
    c = NULL;
    return 0;
}
#else
int main(void) { return 0; }
#endif
