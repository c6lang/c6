# 宏：expr / stmt / ident / type

> **状态**：现行规范（`C6_EXT` + `.c6` 后缀） · **索引**：[文档索引](../README.md) · **实现状态**：[status.md](../history/status.md)
>
> 像函数一样写宏：角色词说明展开结果怎么用，体内局部变量自动卫生重命名。
>
> 相关：用例 `tests/macro.c6`、`tests/errors/macro.c6`

---

## 宏：`expr` / `stmt` / `ident` / `type`

宏写在文件作用域，长得像函数：开头的角色词说清展开结果怎么用，每个形参前面
也带自己的角色（都不能省）：

```c
stmt swap(expr a, expr b) {          /* 当一条语句用：展开成 { ... } */
    typeof(a) tmp = (a);             /* tmp 自动认成宏的局部变量，不会捕获调用者变量 */
    (a) = (b);
    (b) = tmp;
}

expr imax(expr a, expr b) { a > b ? a : b; }   /* 当值用：展开成 ( ... ) */
stmt twice(stmt s) { s; s; }                   /* 实参就是一条语句 */
ident cat(ident a, ident b) { a ## b }         /* 原样重放：int cat(ab, cd) → int abcd; */
type ptr(type T) { T * }                       /* 原样重放：ptr(int) p → int *p; */
```

- `expr` 的体只有一个表达式（结尾的 `;` 可有可无），实参自动加括号：
  `sq(a + 1)` 就是 `((a + 1) * (a + 1))`；要多条语句就用 `stmt`。
- 宏体里自己声明的局部变量自动做卫生重命名（展开成
  `<宏名>__<变量名>__h<展开号>`）；想手动指定就写 `__local`。
- 角色词是软关键字：只在文件作用域、后面紧跟标识符、且没有同名可见符号时才
  开始一个宏定义，别的位置它们仍是普通标识符（`int expr = 1;` 照常）。
- 定义期只检查形状和「`#` / `##` 的操作数必须是 `ident` 角色」。
- 旧的 `#macro` 指令已删除，写它会得到一条迁移诊断。
