/* tests/typename.c 的第二个翻译单元：裸 tag 跨 TU。
 *
 * 两个 TU 各自写同样的 'struct Vec { int x, y; };'（这里用裸写法），
 * 布局相同，类型互相兼容，不需要额外声明。
 */
struct Vec { int x, y; };

Vec tu_scale(Vec v, int k)
{
    Vec r = { v.x * k, v.y * k };
    return r;
}
