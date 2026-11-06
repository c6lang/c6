/* main 的返回值必须原样传出去：c6 会在 main 退出时跑清理链（defer 和
   'go' 线程的 join），这些调用不能把返回寄存器踩掉。 */
// EXIT 42

int main(void)
{
    return 42;
}
