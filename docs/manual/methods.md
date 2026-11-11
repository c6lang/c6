# 结构体方法与 interface

> **状态**：现行规范（`C6_EXT` + `.c6` 后缀） · **索引**：[文档索引](../README.md) · **实现状态**：[status.md](../history/status.md)
>
> `ret f(struct T *self, ...)` 就是 `T` 的方法（符号 `T_f`），`p->f()` / `v.f()` 都是调用；`interface` 是胖指针 + 自动生成的 vtable。
>
> 相关：用例 `tests/method.c6`、`tests/interface.c6`、`tests/iface_agg.c6`

---

## 结构体方法

```c
struct Vec { int x, y; };

int vec_sum(struct Vec *self) { return self->x + self->y; }
int vec_twice(struct Vec *self) { return self->vec_sum() * 2; }

/* 不同类型可以同名：符号分别是 Circle_area / Rect_area */
struct Circle { int r; };
struct Rect { int w, h; };
int area(struct Circle *c) { return 3 * c->r * c->r; }
int area(struct Rect *r) { return r->w * r->h; }

int main(void)
{
    struct Vec v = {3, 4};
    struct Rect r = {2, 3};
    return v.vec_twice() + r.area();   /* 等价于 vec_twice(&v) + Rect_area(&r) */
}
```

- 文件作用域的函数声明/定义，只要**第一个参数是指向结构体（或联合体）的指针**，
  就注册为该类型的方法；调用 `p->f(a)` 被改写为 `T_f(p, a)`；
- 方法就是普通函数：**调用约定不变**，接收者就是第一个实参；符号名是
  `<结构体>_<方法>`，源码里写的名字另外发一个**弱别名**，所以
  `f(p, a)` 这种普通调用、手写 C、纯 C 翻译单元都照旧能链接；
- 不同类型用同名方法**不需要**命名空间（`Circle_area` 与 `Rect_area` 各自独立）；
  弱别名只指向本 TU 里第一个取到该名字的方法，真有歧义时写 `p->area()`；
- 普通名已经被别的函数占着时（同名普通函数，或 `getpid` 这类 libc 头里的声明）
  就不再发别名：`p->getpid()` 照旧可用，普通调用 `getpid()` 仍然是原来的函数；
- 不改名的情形：`static` / `inline` 函数、`__` 开头的名字（编译器和 libtcc1 专用）、
  系统头文件里的声明（`p->fflush()`、`p->gettimeofday()` 这类 libc 成员照旧可用）、
  匿名结构体的方法；
- 接收者可以写成指针，也可以写成**值**：`p->f(a)` 与 `v.f(a)` 都改写为
  `T_f(接收者, a)`，其中 `v.f(a)` 传的是 `&v`（和 `f(&v, a)` 是同一个调用，
  所以方法里改的确实是 `v`），`v` 必须是可作左值的对象；`p->f` / `v.f`（不带
  调用）报 `method 'f' must be called`；
- 结构体同名字段优先于方法；方法须在**使用点之前**可见（TCC 单趟编译）；
- 模块里的方法也按接收者取名，模块前缀加在最外层（`geo__shape__Point_area`），
  所以同名方法放进不同模块、或干脆不进模块，都能共存；
- 返回结构体的方法也支持（隐藏的 `self` 排在 sret 缓冲之后）；
- `T!error` 方法照常工作：`int!error f(struct T *self)` 可用 `try p->f()` / `p->f() catch e { ... }`。


## interface

```c
interface Shape {
    int area();
    const char *name();
}

struct Circle { int r; };

/* 结构体「拥有」接口声明的方法，就实现了这个接口；不再有 impl 语法 */
int area(struct Circle *self) { return 3 * self->r * self->r; }
const char *name(struct Circle *self) { return "circle"; }

int describe(Shape s) { return s.area(); }   /* 结构体值隐式转换为胖指针 */

int main(void)
{
    struct Circle c = {2};
    return describe(c) == 12 ? 0 : 1;
}
```

- 接口值是 `{void *self; const VT *vt;}` 胖指针，`s.area()` 通过 vtable 分派；
- **没有 `impl` 声明**：结构体是否实现接口，由「是否声明了接口要求的全部方法」
  决定；检查是惰性的——第一次发生结构体到接口的转换（函数实参、赋值、显式强转）
  时才比对方法集合与签名，缺方法、参数个数/类型、返回类型不一致都会编译报错，
  诊断指出具体的接口与方法；
- 转换点的前提是方法**在此之前**已经声明或定义；结构体到接口的转换在
  **文件作用域初始化器**里仍然不支持（没有运行时代码可生成），接口到接口是普通拷贝；
- vtable 是一个 `static const` 对象，每个 TU 对「用到的 (类型, 接口) 对」各生成一份，
  因此两个 TU 可以同时转换同一个类型而不会符号冲突；
- `impl` 现在是普通标识符，`int impl = 1;` 合法。
