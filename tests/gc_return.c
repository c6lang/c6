/* c6 自测：返回托管引用时的计数归属
 *
 * 三种返回形态都要把“调用者拿到一个引用”这件事算准：
 *   - 新建对象返回（`return new R`）：调用者拿到计数 1；
 *   - 借字段返回（`return self->r`）：调用者多一个计数，原对象不受影响；
 *   - 形参返回（`return p`）：同上。
 *
 * 每个块结束（或返回的引用离开作用域）后计数必须回到基线——原来的实现
 * 每返回一次就多算一个计数，`docs/gc-design.md` §17 记过这条；现在由这个
 * 文件守住。
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/gc_return.c
 */
struct R { int v; };
struct S { @R r; };

static @R fresh(void)
{
    @R p = new R;
    p->v = 7;
    return p;
}

static @R borrow_field(struct S *self)
{
    return self->r;
}

static @R pass_through(@R p)
{
    return p;
}

test "新建对象返回：调用者拿到计数 1" {
    {
        @R r = fresh();
        if (r->v != 7)
            err(1, "返回值不对");
        if (__c6_gc_refcount(r) != 1)
            err(2, "调用者拿到的计数不是 1");
        if (__c6_gc_live() != 1)
            err(3, "活对象数不是 1");
    }
    if (__c6_gc_live() != 0)
        err(4, "返回值离开作用域后没有释放");
}

test "借字段返回：只给调用者加计数" {
    @S s = new S;
    s->r = new R;
    if (__c6_gc_refcount(s->r) != 1)
        err(1, "字段初始计数不是 1");
    {
        @R a = borrow_field(s);
        if (a->v != 0)
            err(2, "借出来的对象不对");
        if (__c6_gc_refcount(s->r) != 2)
            err(3, "调用者没有拿到新计数");
    }
    if (__c6_gc_refcount(s->r) != 1)
        err(4, "返回值泄漏了一个计数");
}

test "形参返回：只给调用者加计数" {
    @R p = new R;
    if (__c6_gc_refcount(p) != 1)
        err(1, "初始计数不是 1");
    {
        @R q = pass_through(p);
        if (__c6_gc_refcount(p) != 2)
            err(2, "调用者没有拿到新计数");
    }
    if (__c6_gc_refcount(p) != 1)
        err(3, "返回值泄漏了一个计数");
}

test "多次调用不累积计数" {
    int i = 0;

    @S s = new S;
    s->r = new R;
    for (i = 0; i < 8; i++) {
        @R a = borrow_field(s);
        @R b = pass_through(a);
        if (__c6_gc_refcount(s->r) != 3)
            err(1, "循环里的计数不对");
    }
    if (__c6_gc_refcount(s->r) != 1)
        err(2, "循环结束后计数没有回到 1");
}
