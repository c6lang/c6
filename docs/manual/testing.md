# 测试：test 块与完整回归

> **状态**：现行规范（`C6_EXT` + `.c6` 后缀） · **索引**：[文档索引](../README.md) · **实现状态**：[status.md](../history/status.md)
>
> `test "描述" { ... }` 是语言内建的测试块，`-test` 跑；诊断用 `// EXPECT` 语料；提交前跑 `cmake --build build --target check-all`。
>
> 相关：用例目录 `tests/`；运行器 `tests/run-tests.sh`、`tests/run-errbase.sh`

---

## test 块

```c
test "add works" {
    if (add(1, 2) != 3)
        return err(1, "1 + 2 != 3");
}
```

```sh
./tcc/c6 -B tcc -test tests/slice.c6               # 运行全部 test 块
./tcc/c6 -B tcc -test -test-filter=切片 tests/slice.c6
```

运行器输出 `PASS:` / `FAIL:` 行与 `N test(s), M failure(s)`，全部通过时退出码为 0。


## 测试

提交前只跑一条命令就够：`cmake --build build --target check-all`（= `test-all` + `errcheck` + C99 后端
两套用例 + 三条自举路线），拆开看是下面「完整回归」里那些目标。

这条命令并发跑：`tests/` 的两套宿主用例、`errcheck`、C99 后端两套、三条自举
路线、`tests2` 全量、freestanding/裸机那一组互不相干，交给 make 一起跑；只有
**共享 `build/` 下固定路径**的那一组（`stdtest` 和八十多个 `std-*` 示例）整组
串行 —— 它们的用例会往 `build/fs-test/`、`build/std-static/`、
`build/mmap-test.bin`、`build/echo.sock` 这些固定路径写临时文件。输出按目标
成块（`--output-sync=target`），不会混成一团；`cmake -S . -B build -DC6_CHECK_JOBS=1 && cmake --build build --target check-all`
可以退回整轮串行。目标列表写在 `Makefile` 的 `STD_TARGETS`（共享路径，串行）
和 `CHECK_PARALLEL`（其余，并发）里；CMake 的 `check-std-serial` 目标递归调用
`make -j1` 把这一组串行跑完，CMake 是唯一入口，Makefile 只保存用例定义。

c6 的测试全部在 `tests/` 下，都用 c6 自己的 `test` 块写：

```
tests/
├── run-tests.sh      运行器（cmake --build build --target selftest 调它）
├── run-errbase.sh    errbase.def 的检查（cmake --build build --target errcheck 调它）
├── defer.c6           defer 的时机、顺序与作用域
├── defer_scope.c6     defer 与块作用域、goto 跨作用域的清理
├── defer_reent.c6     递归/重入时的 defer 链
├── defer_thread.c6    线程里的 defer（裸机跑不了，boot/runtests.sh 跳过）
├── err.c6             err / errdefer / 清理顺序
├── err_ids.c6         错误码号段：模块的 enum Error、std 内置号段（tu/err_ids.c6）
├── try.c6             try / catch
├── test-block.c6      test 块与 -test 模式本身
├── macro.c6           expr/stmt/ident/type 宏与名字卫生
├── module.c6          module / import：限定名、带前缀的 C 符号、-emit-header
├── method.c6          结构体方法（含不同类型的同名方法与 libc 方法）
├── interface.c6       结构化接口
├── iface_agg.c6       接口方法签名里的 struct / 元组
├── slice.c6           切片
├── tuple.c6           元组（与 tu/tuple.c 一起跑）
├── tuple_error.c6     元组 / struct 作 !error 的返回类型
├── tuple_async.c6     元组 + async / await
├── tuple_thread.c6    元组 + spawn（OS 线程）
├── typename.c6        裸 tag 类型名（省略 struct / union / enum）
├── enum_scope.c6      枚举成员的限定名 Color.RED
├── foreach.c6         foreach：数组 / 切片 / 区间
├── foreach_head.c6    foreach：`(T v = a)` / `((T i, T v) = a)` / `(T i = lo:hi)`
├── foreach_iter.c6    foreach：has()/next() 迭代器协议
├── fstr.c6            字符串插值 f".." / @f".."
├── fn_lit.c6          函数字面量
├── closure.c6         捕获式闭包（V2）
├── generic.c6         泛型：模板、实例、体内方法、值形参
├── generic_fn.c6      |T| 函数 / typedef 模板
├── generic_scope.c6   模板实例的作用域边界（字段名 vs 同名方法、形参不漏出、跨 TU 弱符号）
├── async.c6           async / await / co / task
├── async_frame.c6     帧住在任务里：&local 跨 await、weak 形参
├── thread.c6          spawn（OS 线程）
├── thread_handle.c6   线程句柄 h->join()/done()/value()
├── task_method.c6     任务句柄 t->wait()/done()/kind()
├── weak.c6            weak 引用
├── gc.c6              托管引用：分配、存储、作用域释放、数组
├── gc_field.c6        字段释放、嵌套结构体、长链迭代释放
├── gc_type.c6         shared / weak / own 的类型语义
├── gc_drop.c6         析构方法 drop（含数组元素与 spawn 线程）
├── gc_thread.c6       shared 跨线程：实参块持有、交接、并发加减
├── gc_async.c6        async 任务的参数/返回值持有 + 销毁挂起任务时回卷
├── gc_own.c6          own 独占链/子树 + move()
├── gc_return.c6       返回托管引用的计数归属（新建/借字段/形参/循环）
├── c6ext/            .c6 后缀自测：不加任何开关，靠扩展名开启扩展
├── tu/               跨翻译单元的第二个 .c6（同名的主文件会一起编译）
├── abi/              ABI 兼容：`.c6`（c6）和 `_plain.c`（纯 C）各编译一个 TU，
│                     链接后运行（`// EXIT <n>` 声明期望的退出码）
├── exit/             退出码（`// EXIT <n>` 声明期望的进程退出码）
└── errors/           编译期诊断（每个片段一个 -D 开关）
```

```sh
cmake --build build --target selftest                       # 跑全部
tests/run-tests.sh                  # 同上
tests/run-tests.sh slice module     # 只跑这几个
tests/run-tests.sh --list           # 列出会跑的文件
```

单个文件也可以直接跑：

```sh
./tcc/c6 -B tcc -test tests/err.c6
./tcc/c6 -B tcc -test tests/slice.c6 tests/tu/slice.c6
./tcc/c6 -B tcc -test -test-filter=切片 tests/slice.c6   # 按描述过滤
```

输出形如：

```
ok   err: 18 test(s), 0 failure(s)
ok   errors/slice: 13 个诊断片段
...
run-tests: 96 通过, 0 失败
```

`-test` 模式下 `main` 可有可无（运行器不调用它）。

## 编译期诊断怎么写

`tests/errors/*.c6` 用 `#if defined <片段名>` 把每个报错场景放在一个片段里，
用注释声明该片段必须产生的诊断，不需要维护行号：

```c
// EXPECT test_void_element: error: '[:]' cannot have a 'void' element type

#if defined test_void_element
void f(void[:] s);
int main(void) { return 0; }
#else
int main(void) { return 0; }      /* 不带 -D 时必须编译并运行通过 */
#endif
```

运行器为每个 `EXPECT` 片段跑一次 `-D<片段>`，检查 stderr 含声明的文本，
再检查不带 `-D` 时该文件能编译并运行通过。

两条额外的约定：

- `tests/tu/<同名>.c6` 存在时会作为**第二个翻译单元**一起编译（跨 TU 的环
  检测这类诊断要两个单元才看得见；`-run` 只接一个源文件，所以这种情况下
  运行器改用编译 + 链接）；
- `// NATIVE-ONLY <片段>` 标记该片段只在本机后端成立，`BACKEND=c99` 时跳过
  （例如跨 TU 环检查 —— C99 后端只输出 C 源码，没有链接期）。

## 完整回归

```sh
cmake --build build --target check-all     # 提交/发布前跑这一条：test-all + errcheck + C99 后端两套用例 + 三条自举路线
                   # （除共享 build/ 路径的 std 家族整组串行外都并发；CHECK_JOBS=1 整轮串行）
cmake --build build --target test-all      # check-all 的第一段：selftest + tests2 + c6selfcheck + 标准库 + freestanding/裸机
ctest --test-dir build -R c6-tests2          # 扩展模式：上游 tests2 全量
cmake --build build --target test-plain    # 关闭 C6_EXT 重建后再跑一遍，验证“扩展关闭零行为变化”
cmake --build build --target c6selfcheck   # 把 tcc 自己的源码拷成 .c6，用 c6 规则编译，编出可运行的编译器
cmake --build build --target errcheck      # errbase.def：钉号表本身和 C126/C127/C128 真的会报错
cmake --build build --target stdtest       # 标准库 std/（七个子目录）每个模块的 test 块
cmake --build build --target std-demo      # 标准库集成示例（examples/wordcount.c6）
cmake --build build --target std-tour      # 新模块集成示例（examples/stdtour.c6，自带自检）
```

自举（编译器编译自己，产物在 `build/selfhost/`，`cmake --build build --target c6-clean` 会一起删掉）：

```sh
cmake --build build --target selfhost          # c6 路线：tcc 编译 tcc.c -> stage1，stage1 再编一次 -> stage2
cmake --build build --target c99selfhost       # C99 后端路线：tcc 生成 C -> 宿主 gcc 编出 stage2 -> stage3
cmake --build build --target c99selfhost-tcc   # 同上，但生成的 C 也由 tcc 自己编（整条链不需要 gcc）
```

三条路线都会把两次编译得到的目标文件 `cmp` 一遍（bootstrap fixpoint），
再用最终的 stage2/stage3 编译并运行一个小程序：只有目标文件逐字节相同、
编出来的编译器真能编程序，自举才算通过。

`cmake --build build --target c6selfcheck` 是"变量必须初始化"那条规则最严格的回归测试：tcc 自己
的源码也要满足它，所以把 `tcc/tcc.c` 拷成 `build/selfhost/c6/selfcheck.c6`
后用 `-DC6_EXT` 编译一遍（原生目标文件 + C99 后端产物；带 `C6_EXT` 才会把
扩展实现那半边也纳入检查），再把编出来的编译器连起来跑 `-v`。两步编译都
不许有诊断输出，出现警告即失败。

> 切换模式会先 `cmake --build build --target c6-clean` 重建，因为 `C6_EXT` 是编译期开关。
> 三个目标都会先跑 `bootstrap`，只在 `tcc/` 还没配置时执行一次 `./configure`。
