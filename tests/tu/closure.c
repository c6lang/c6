/* 第二个翻译单元：闭包按值跨 TU 传递（同签名共享同一套布局） */
int closure_tu_call(closure int(int) f, int x)
{
    return f(x);
}
