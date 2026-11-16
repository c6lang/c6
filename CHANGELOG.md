# 变更史

> **状态**：现行 · **索引**：[文档索引](docs/README.md) · **实施状态**：[docs/history/status.md](docs/history/status.md)
>
> 这里是**按特性归档的变更史**（不是逐条 commit 日志）。每条给一个代表性提交的短
> hash，完整历史用 `git log` 查。日期取该特性的收尾提交。

## 未发布

### 裸字段接后缀链之后按链尾的类型算：回归 58 → 59 / 92

方法体里的裸字段（接收者的成员）解析完之后还要走 `gen_postfix_ops` 把
`->next` / `[...]` 接上去；那条路走完，收尾仍然按**成员本身**的类型取值和
回填 `GEN_TY`：

```
T next() { T x = d->data[(d->head + i) % d->cap]; }
```

`d` 是 `Deque@T *`，`d->head` 已经是 `int`，可收尾时还拿 `Deque@int *` 去
`load_lval`（8 字节）并把 `GEN_TY` 设回指针类型——于是 `d->head + i` 被当成
指针加法，步长取 `sizeof(Deque@int) = 24`，下标 `(head + i*24) % cap` 恒等于 0：
deque 迭代永远返回第一个元素（std-tour 的 `sum` 恒为 6）。

修法：收尾按 `PEND_TY`（后缀链留下的类型）取值/回填；链上一个后缀都没接时
它本来就等于成员类型，行为不变。

- `std-tour` 全绿（73 条断言），58 → 59 / 92，无回退

### 方法体里的裸名字不再被实例化点的局部量遮住：回归 57 → 58 / 92

模板实例的体是在**实例化点**展开的，那里调用方函数的局部量都在作用域链上。
方法体里裸写的成员名（`i++`、`d->n`）本该指向接收者的字段，可 `sym_find`
先撞上调用方的同名局部量：

```
struct DIter@T { Deque@T *d; int i; ... }     /* 方法体里裸写 d / i */
void tour_deque(void) { @Deque@int d = ...; int i = 0; ... }
foreach (int x = d->iter())                   /* 实例在这里展开 */
```

于是 `i++` 改的是 `tour_deque` 的循环变量、`has()` 读的也是它——std-tour 的
deque 迭代永远吐第一个元素（`sum` 恒为 6）。`It@T` 里 `b->n` 那种同样中招：
读的是调用方局部量 `b` 的拷贝（局部量是值，取成员读到的全是 0）。

修法：给方法体记一个作用域边界（`METHOD_SCOPE_BASE`，`pa_func_def` 里
`scope_enter()` 之后），裸名字查到的符号如果**不在这条链上**（登记在更外层的
作用域里，即调用方的局部量），就按接收者的成员走；方法自己的形参/局部量
照旧遮蔽字段。

- 新增全绿：`std-net`；`std-tour` 的迭代器 `i` 从"永不推进"变成正常推进
- 回归 57 → 58 / 92，无回退（`trend`：连 tcc 侧用例都没动）
- 剩下的 deque 断言是另一回事：`Deque@T` 的成员在实例里被解析成了 `T` 的
  指针（下标 stride 变成 `i * 24`），记在 `c6c/docs/todo.md` 第 216 轮

### 下标赋值不再把普通标量当托管引用：std-tour 从段错误到 73 条断言

`data[i] = 0` 里 `data` 是 `@char`（托管指针）时，下标这层的 `PEND_MGD`
沿用了"托管引用"的标记，赋值就按托管引用走：**先取旧值当指针 release，再存**。
旧值是那个字节本身——`strbuf.truncate` 写结尾 NUL 时旧值是 `'d'`（100），
运行时拿 `100 - 头长 = 60` 当引用计数头，`lock xadd` 直接段错误。std-tour 的
第一段 `strbuf` 就挂在这里。

判据收窄成"基址是数组/切片**且元素是指针**（元素本身是引用）才把标记传给元素"。
第一版按"成员 kind 2（引用数组）"判，`tests/foreach_iter` 的局部 `@MNode a[2]`
就掉了——局部引用数组没有成员 kind，元素照样是托管引用：

```
std-tour   段错误 → 73 check(s), 1 failure(s)（只剩 deque 迭代顺序）
std-kit    段错误 → 跑过 num 段，崩在后面（下一个问题）
foreach_iter  局部 @T a[2] 的 a[i] = v 仍是托管赋值（第一版判据漏了这条）
```

新增回归口径不变（三居用例还没全绿），所以基线仍是 57 / 92。

### 跨 TU 的模板/方法符号：回归 52 → 57 / 92

`std-*` 里有一整类用例挂在「未定义的符号」上，名字分两副面孔：`p_atom$2`
这种带 TU 后缀的，和 `std__container__vec__Iter_int_has` 这种带模块前缀的。
查下来是同一个区域的两处：

- **方法式函数的前向声明**（`static int poke(Bucket *b);` 先声明、后面才定义）：
  声明时按短名建了一条 ELF 符号，调用点已经按它发过重定位；定义时发射名换成
  `<接收者>_<方法>`（再套模块前缀 / TU 后缀），旧的那条就悬空了。定义处现在
  记一笔账（`meth_alias_register`），等所有 TU 解析完、代码都发出来了，
  由 `alias_apply()` 统一接到真名上——**不能当场接**：`x64_alias_idx` 复制的是
  偏移，定义处这会儿代码还没生成，接过去的是 0（先按当场接写的第一版就让
  c6c 编出来的 tcc 一启动就段错误：tests2 从 118 掉到 0）。这一处收掉了 `std-async3`
  （`co_step$2`）、`std-ub`（`ps_typespec$2`）、`std-text`（`p_atom$2`）、
  `std-box`（`jp_value$2`）、`std-mw`（`take_n`）、`std-more`（`next_set`）、
  `std-mesh`（`next`）、`std-docs`（`add_irregular`）。
- **嵌套的模板实例化**：模板 A 的方法签名里出现 `B@T`，A 的"只登记签名"那一遍
  就会把 B 实例化（只有签名）；外层第二遍重放同样的实参时缓存直接短路，B 的
  方法体再没机会展开。现在嵌套实例在自己的那一遍里一次展开完
  （`PA_SIG_ONLY` 在嵌套重放期间清零）。`Vect@T.iter()` 返回 `Iter@T` 正是这个
  形状，`std-demo`/`std-kit`/`std-tour` 从"链接不上"变成"能跑但段错误"——
  离全绿还差运行期那一步（实例身份/布局），留到下一轮。
- 新 check：`tmplcheck`（两 TU 的模块 fixture：编译链接、无未定义符号、跑对结果、
  模块 `-c` 单编）。
- CI 稳定：`tests/thread.c6` 的两处空转上界从 20M 提到 200M。同一份二进制本地
  20/20 通过，CI 的 2 vCPU runner（cfs quota）上线程可能整段时间没被调度上，
  `thread` 因此报过一次"跑不通"。`run-tests.sh` 现在还会把跑不通用例的最后几行
  输出捎到 stderr，CI 上不用再猜。
- 回归：52 → 57 / 92（`std-async3`、`std-docs`、`std-mesh`、`std-mw`、`std-ub`），
  没有一条从绿变红。
### 构建：c6 与 c6c 的安装元数据收敛到 `cmake/C6Metadata.cmake`

- `.pc` 生成、`find_package(...)` 包配置、`LD_LIBRARY_PATH` 注入三件事原本各自
  在多个模块里重复：`.pc` 的 `${pcfiledir}` 前缀反推写过一遍又一遍，包配置的四
  件套（`configure_package_config_file` + `write_basic_package_version_file` +
  `configure_file(Targets)` + `install`）在 `C6LibraryConfig.cmake` 与
  `C6CInstall.cmake` 各有一份 30 行样板，执行构建树的 c6 要补的环境变量在
  `C6Testing.cmake` / `C6Wasm.cmake` / `C6C.cmake` 写了三遍。现在统一成
  `c6_ld_env()` / `c6_pcfile_prefix()` / `c6_install_pc()` /
  `c6_install_cmake_package()` 四个函数。
- 四个 pkgconf 包（`c6` / `c6lib` / `libtcc` / `c6c`）共用 `cmake/c6.pc.in`，
  **删掉** `cmake/c6c.pc.in`；`tccdir=` 行与"变量"注释参数化，c6c 不再输出无意义
  的 `tccdir=`。`c6.pc` / `c6lib.pc` / `libtcc.pc` 的输出与改造前逐字节相同。
- 并发数不再算两遍：`C6C.cmake` 直接复用 `C6Options.cmake` 的 `C6_TCC_JOBS`。
- `check-install` / `pkgcheck` 里的落点变量（`C6_PACKAGE_DIR`、
  `C6C_PACKAGE_DIR`）显式保留，未生成包配置时留空值。
- 验证：`pkgcheck` 10/10、`check-install` 通过、`check-all-c6c` 全绿（fixpoint
  逐字节相同、std 381/0、tests2 117/0 意外失败、c-testsuite 149/150 且 0 失败、
  前端回归 52/0、c6c 回归 52/0）；ON（tcc 引导）与 OFF（宿主 cc 引导）都
  configure + build 通过。

### 安装：c6c 有了自己的 `find_package(c6c)` 与 `c6c.pc`；`C6C.cmake` 拆成五个模块

- **c6c 的包元数据**：此前无论哪种 flavour，安装树里只有 `c6.pc` / `c6lib.pc` /
  `libtcc.pc` 与 `find_package(c6)`（c6c 包下还得写 `pkgconf --libs c6`），c6c
  自己没有名字。现在装 c6c 就同时给：
  - `find_package(c6c COMPONENTS compiler libc6c)` → `c6c::compiler`（`bin/c6c`）、
    `c6c::libc6c`（静态，默认）/ `c6c::libc6c_shared`；变量 `c6c_EXECUTABLE`、
    `c6c_LIBC6C_STATIC` / `_SHARED`、`c6c_INCLUDE_DIR`、`c6c_VERSION`；
  - `c6c.pc`（`pkgconf --cflags --libs c6c` → `-lc6c`，`--variable=c6c` 给编译器路径）。
  c6 那套名字照旧安装、不冲突：`c6.pc` 在两种 flavour 下内容不同，`c6c.pc` 只
  描述自举编译器本身（`cmake/C6CInstall.cmake` 里有取舍说明）。
- **`cmake/C6C.cmake` 417 行拆成五个模块**（顶层 include 不变，target 名与行为
  逐个保持）：`C6C.cmake`（总控：路径/工具/`_c6c_ld_env`/源码 GLOB/llc 发现）、
  `C6CBuild.cmake`（c6c / c6c-opt / libc6c 与 stage0 两条路）、
  `C6CCheck.cmake`（`c6c_target()` + 全部验收 target）、`C6CGate.cmake`（fast /
  check-all-c6c / check-all-perf / CTest / check-c6c）、`C6CInstall.cmake`（库与头
  文件安装 + 上面那套包元数据）。
- 顺带抽出 `c6_pcfile_prefix()`（`cmake/C6Options.cmake`）：`.pc` 的 `prefix` 用
  `${pcfiledir}` 反推，`c6.pc` / `c6c.pc` 共用一条算法，不再各写一遍。
- 契约测试同步：`pkgcheck` 从 7/7 扩到 **10/10**（安装树含 `c6c.pc` 与
  `lib/cmake/c6c/*`、`find_package(c6c COMPONENTS compiler libc6c)` 的消费方真编
  真跑、`pkgconf c6c` 的旗子 `-Wall -Werror` 编得过）；`check-install` 在默认
  flavour 下也验 `c6c.pc` 的 `prefix` 重定位与 `find_package(c6c)` 消费方。
- 验证：`pkgcheck` 10/10、`check-install` 通过、`tests2-check` 117/0 意外失败、
  `testflag` 19/0、`regression-c6c` 52/0、`diagtest` 316/0；ON（tcc）与 OFF（宿主
  CC 引导）两种 flavour 都能 configure + build。

### 修复：隐式 int 告警只在"非函数声明"发；宏体里的 `__LINE__`/`__FILE__` 报调用点

- `parse_typespec` 的隐式 int 告警收缩到 tcc 的真实口径：形参表里不发
  （`int f(int x, ...)` 的省略号、K&R `int f(a, b)` 的裸标识符都走
  `parse_typespec` 的兜底旧路径，实测 tcc 零告警），并且延迟到声明符解析完、
  只对**非函数**声明发（tcc `decl()` 的 `else if (oldint)` 前面是 VT_FUNC
  分支）——`f();` / `f(){}` 不告警。此前 `c6c/include/stdio.h` 的变参原型
  每条多一行 `type defaults to int`，tests2 118 条全红、`testflag` 的
  "stderr 干净 / 过滤空 / 没有 test 块"三条同因挂掉。
- `pp.c6`：宏展开时记下**最外层**调用点的 file/off，宏体里的 `__LINE__` /
  `__FILE__` 用它而不是 `#define` 那一行（tests2/129_scopes 的 `myassert`
  宏在函数体里展开，expect 逐行要调用点行号）。
- `tcc-na.txt` 补两条架构性 n/a 并写死卡点：`102_alignas`（`_Alignas` 只
  解析不落地，全局布局与 `__alignof__` 都不认用户对齐）、`129_scopes`
  （`__LINE__` 已修，剩下 `sizeof(enum{…})` 常量泄漏到外层块、两块遮蔽与
  静态初始化的作用域语义差）。
- 验证：tests2 记分板 117 通过 / 10 n/a / 0 意外失败；testflag 19/0；
  前端回归 52/0；c6c 回归 47/0；编译期诊断 316 报错 / 0 漏报。

### 格式化与二进制转义对齐 glibc：回归 47 → 52 / 92

两条"看起来无关"的线，其实是同一类事：**文本输出和字节字面量的口径**。它们
在 std 的用例里表现为"自检就差一两条"，很难顺着错误信息找到根。

- `c6c/lib/rt.c6` 的 `c6c_vfmt` / `c6c_fmt_double` 逐项对齐 glibc：
  `%.0d` 的 0 什么都不印（不是 `0`）、`%#x` 的值 0 不加 `0x`、浮点也认 `+`/空格
  标志、正好 `.5` 时**取偶**（`%.0f` 的 2.5 → `2`，`%.1f` 的 2.25 → `2.2`）、
  `%g` 只剥小数部分的尾零（`100000` 曾被剥成 `1`）、`-0.0` 靠符号位认出来、
  `%f` 的整数部分超过 2^64 时走小 bignum 精确展开（`printf("%f", 1e20)` 一位不差）。
  `c6c/tests/printf-matrix.c` + `printfcheck` 把 68 条格式钉死：同一份源码用宿主
  `cc` 和 c6c 各跑一遍逐行 diff。
- `c6c/src/parse.c6`：窄字面量里**只有** `\u`/`\U` 和源码里直接写的 UTF-8 才该
  重新编码，`\xNN`/`\NNN` 是**字节**转义。此前凡是解出来 >= 0x80 的都当码点又
  UTF-8 编码了一遍——`"\xde"` 变成 `0xc3 0x9e` 两个字节，拿字符串拼二进制数据
  的 std 用例（hexdump/数据库/图形/词表/协议）全被带偏。`esccheck` 钉 16 条。
- 回归：47 → 52 / 92（`std-db`、`std-gfx`、`std-pack`、`std-proto2`、`std-words`），
  没有一条从绿变红。
- 已知没动的：`float_lit_bits` 的十进制→double 还差 1 ulp（`1e300` 的位模式末位
  与 glibc 不同），这是解析侧正确舍入的问题，不是打印侧。
- 顺带：`...` 在声明符的形参表里被当成"隐式 int 的形参"，每个变参原型都多一条
  `type defaults to int` 警告（c6c 自己的 `stdio.h` 4 条、`unistd.h` 还有）。在
  `parse_typespec` 之前把省略号拦下来，编译干净了，`testflag` 的"全部通过时
  stderr 干净 / 过滤空 / 没有 test 块"三条也一起转绿（16/3 → 19/0）——CI 的
  `c6c-check-all` 就是被这三条挂着的。

### 许可：仓库根新增 `LICENSE`（LGPL-2.1，逐字继承 tcc）

- `tcc/COPYING`（LGPL-2.1 全文）逐字复制为根 `LICENSE`：c6 是 TinyCC 的分支，
  主编译器（`tcc/`）及其修改按 tcc 原许可继承；GitHub 许可识别与 README 的
  License 徽章从此有据可依。`c6c/` 是自举重写、不复制 tcc 源码，仍按自己的
  MIT（`c6c/LICENSE`）；打包侧许可随 flavour 分发（`cmake/C6Install.cmake`），
  仓库根 `LICENSE` 也作为总许可声明随两种包安装；README「上游与许可」一节
  同步双许可作用域。

### 修复：tests2 全线飘红（隐式 int 告警误伤 `unsigned` 组合）与记分板死因点名

- `parse_typespec` 的隐式 int warning（tcc 口径 `type defaults to int`，为
  tests2/102_alignas 补的）原先挂在 `if (!base)` 分支上——该分支同时也是
  `unsigned` / `long` / `unsigned long` 这类**只选宽度**的组合的必经之路，
  于是 `stddef.h` 的 `typedef unsigned long size_t;` 也被当隐式 int 告警：
  每个 TU 的每个头文件 `unsigned` 族声明都多一条 warning（00_assignment 多
  220 行、101_cleanup 多 65797 行），tests2 118 条全红，`testflag` 的
  "stderr 干净 / 过滤空 / 没有 test 块"三条也被同一噪声打挂。改为只在
  `!saw_int`（完全不带类型说明符的真隐式 int）且非 c6 TU 时告警。
- `c6c/tests/tcc-tests2.sh` 完善错误提示：运行期超时/被信号杀死的用例不再
  只留 shell 一句 "Segmentation fault"，退出码被接住后在汇总里逐条点名
  （`运行期被信号 11 杀死（139=SIGSEGV…）`）；判定口径不变（上游只比输出），
  FAIL 结论的 diff 预览从 2 行加到 3 行。

### `-l<name>` 收下 + 数学/组库/线程同步的边角：回归 45 → 47 / 92

- `c6c/src/main.c6` 接受 `-l<name>`（`-lm` 最常见）并**不生效**：c6c 的产物是
  静态的，libc/libm 的常用面由 `c6c/lib/rt.c6` 自带；真缺符号时链接期照旧报
  「未定义的符号」，不会变成运行时炸。此前整批用例挂在「未知选项 '-lm'」上。
- `c6c/lib/rt.c6`：补 `atan2`（std/geo 卡在它）、`exp2`、`fdim`、`nextafter`；
  `getgrgid`（解析 `/etc/group`，`struct group` 按 glibc ABI 摆）；
  `sem_trywait`/`sem_timedwait`/`sem_getvalue`、`pthread_rwlock_tryrdlock`/
  `trywrlock`、`pthread_barrier_*`、`pthread_spin_*`。
- 回归：45 → 47 / 92（新增 `std-linalg`、`std-math3`，无回退）。`std-sys` /
  `std-thread` / `std-geo` 从"链不上"变成"跑起来但自检有差"。

### 运行时第二批 + `errno`：回归 42 → 45 / 92

- `c6c/src/main.c6` 预定义 `_GNU_SOURCE 1`（和 tcc 一个口径）：glibc 把
  `pthread_setname_np`、`cfmakeraw`、`strsignal` 这些藏在 `__USE_GNU` 后面，
  不开就「未声明的标识符」。
- `c6c/lib/rt.c6` 补第二批（能弱定义的一律 `weak`）：`opendir`/`readdir`/
  `closedir`/`rewinddir`（自己 decode `getdents64`）、`getrusage`、
  `tcsetattr`/`tcflush`/`tcdrain`、`pthread_self`（`gettid`）/`equal`/`detach`/
  `once`/`key_create`/`setspecific`/`getspecific`/`mutex_trylock`/`timedlock`/
  `cond_timedwait`/`rwlock_*`/`attr_setstacksize`/`setdetachstate`/
  `setname_np`（自己走 `prctl`，别的线程写 `/proc/self/task/<tid>/comm`）/
  `getname_np`、`sched_yield`、`symlink`/`link`/`readlink`/`fcntl`/`msync`、
  `getpwuid`/`getpwnam`、真的 `sigset_t` 位操作（`sigemptyset`/`sigfillset`/
  `sigaddset`/`sigdelset`/`sigismember`）、`sigpending`、
  `getaddrinfo`/`freeaddrinfo`/`gai_strerror`/`inet_addr`/`inet_ntoa`/
  `inet_pton`/`inet_ntop`（只认数字地址与 `localhost`，真域名如实返回
  `EAI_NONAME`）、`ferror`/`feof`/`clearerr`/`fileno`。
- **`errno` 真的会设了**：六个 `c6c_sys*` 薄壳统一在返回负数时
  `errno = -r`（返回值口径不变）。在这之前按 `errno == EINPROGRESS`/`EAGAIN`/
  `EINTR` 分支的代码永远走失败那条路——`std/net/tcp.c6` 的非阻塞 connect 就是
  这么被卡死的。`errno` 的定义按 `#ifdef errno` 分岔：系统 `<errno.h>` 是宏
  （`(*__errno_location())`）时用系统那份，c6c 自己的 `<errno.h>` 下给一个
  `weak` 定义。
- 回归：42 → 45 / 92，新增 `std-fsx` / `std-h2` / `std-wire` 全绿，没有回退。

### 回归换编译器跑：`C6_CC` + c6c 侧 42 个用例

- `tests/regression.sh` 的被测编译器从写死的 `$C6_TCC_DIR/c6` 改成 `$C6_CC`
  （默认值不变），98 处调用点统一替换；selfhost / c99selfhost 那几个拿 tcc
  **源码**自举的用例仍直接用 `$C6_TCC_DIR/c6`。于是 `C6_CC=c6c/build/c6c
  tests/regression.sh <用例>` 就整批换编译器跑。
- `c6c/tests/regression-c6c.list` 是「c6c 已经跑绿」的单一数据源，
  `c6c/tests/regression-c6c.sh` 跑这张表（`--all` 跑全部 std-* 量基线，也可以
  点名几个），CMake 目标 `cmake --build build --target regression-c6c`。
- `C6_ENABLE_TCC=OFF` 的构建不再一个回归用例都不注册：`cmake/C6Testing.cmake`
  把这张表注册成 CTest 用例（label `regression-c6c`，同时带 `c6c`）。
- `c6c/lib/rt.c6` 补了一批 POSIX 面，全部 `weak`（程序里同名强定义会盖掉，
  对应 tcc 走 libc.so.6 时的符号覆盖）：`poll` `nanosleep` `uname` `rmdir`
  `tcgetattr` `chdir` `sysinfo` `mkfifo` `fsync` `ftruncate`、
  `ntohs`/`ntohl`/`htons`/`htonl`、`cfmakeraw`，以及整族 socket（`socket`
  `bind` `listen` `accept` `connect` `send` `recv` `sendto` `recvfrom`
  `setsockopt` `getsockopt` `getsockname` `getpeername` `socketpair` `shutdown`）。
- 实测基线 38 → 42 / 92（新增 `std-async` / `std-async2` / `std-async5` /
  `std-crypto`）；缺口四类与下一轮顺序写在 `c6c/docs/todo.md` 第 209 轮。

### 共享库：`c6c -shared`（ET_DYN）

- `c6c -shared a.o b.o -o libx.so` 出共享库：`ET_DYN`、基址 0（VA 就是段内偏移，
  加载基址由 ld.so 选）、没有入口与 `PT_INTERP`、程序头 4 个。输入必须是 `.o`
  （`-shared lib.c6` 直接报错）；`-fPIC` / `-fpic` 收下不报错——native 后端出来的
  代码本来就位置无关（全局地址走 `lea rip+rel32` + 重定位），不需要另一套 PIC
  代码生成。
- `c6c/src/ld.c6`：`LD_BASE_VA` 从宏改成变量（静态 `0x400000` / 共享 `0`），新增
  `LD_SHARED` 开关；共享模式下跳过「找 `main`」「合成 `_start`」「把 ctor/dtor
  补进 `_start` 的 call」三步。数据里的绝对地址（字符串指针、初始化器、
  `.init_array` 的函数指针）变成 `.rela.dyn` 的 `R_X86_64_RELATIVE`（`ld_apply`
  记账 → `ld_dyn_plan` 定尺寸 → 落盘时逐条写）。定义的全局/弱符号进 `.dynsym`
  （函数 `STT_FUNC`、数据 `STT_OBJECT`，`st_shndx` 按 `.text`/`.data`/`.bss` 填），
  名字进 `.dynstr`，`DT_SONAME` = 输出文件名，`DT_INIT_ARRAY`/`DT_FINI_ARRAY`
  让 `__attribute__((constructor))` 在 `dlopen` 那一刻就跑。
- 产物末尾补一张 16 项**节头表**（`.text` / `.rodata` / `.dynstr` / `.dynsym` /
  `.hash` / `.rela.plt` / `.plt` / `.rela.dyn` / `.got.plt` / `.data` /
  `.init_array` / `.fini_array` / `.bss` / `.dynamic` / `.shstrtab`）：ld.so 不看
  它，但 GNU ld 把 `.so` 当**输入**时只认节头（没有就 `file in wrong format`），
  `readelf --dyn-syms` / `nm -D` 也靠它。
- 修好一处一直错着的 SysV `.hash`：`nchain` 少写一个字、`chain` 每位写成 `j+2`
  （应为 `j+1`）。静态可执行文件没人查这张表所以从没露馅，`dlopen` 一上来
  `dlsym` 就返回 NULL。
- 边界如实拒绝：共享库里的外部**数据**符号（`extern int optind;`）要走 GOT
  间接寻址，这一版还没做，直接报错而不是悄悄写 0；外部函数照旧走 PLT。
- 新增回归 `c6c/tests/shared-check.sh`（19 条，`cmake --build build --target
  sharedcheck`）：`ET_DYN` / 入口 0 / 无 `PT_INTERP` / 程序头 4 个 / `DT_SONAME` /
  `DT_INIT_ARRAY` / `RELATIVE` / 节头表齐全 / 导出符号 `T`+`D` /
  `dlopen`+`dlsym` 真调通且数据读写一致 / 构造器在 `dlopen` 时跑 /
  `cc -L … -l` 直接链也能跑 / 源文件与外部数据符号两条边界 / `-fPIC` 收下且
  默认仍是 `ET_EXEC`。

### 嵌入 API：`tcc_*` 兼容面补齐

- `c6c/include/tcc-compat.h` 以前只声明 13 个 `tcc_*`，现在与 tcc 公开嵌入头对齐
  （23 个 + 回调 typedef）：`tcc_set_error_func`（诊断整行交给回调）、
  `tcc_add_sysinclude_path`（等价 `-I`）、`tcc_list_symbols`（镜像里的符号与 VA）、
  `tcc_set_lib_path`（认 `<dir>/lib/rt.c6` 与 `<dir>/rt.c6`）都是真实现；
  `tcc_set_realloc` / `tcc_set_backtrace_func` 收下不动（arena 不可换、没有 `-b` 的
  运行期）；`tcc_add_library_path` / `tcc_add_library` / `tcc_add_symbol` 返回 -1 并把
  原因写进 `c6_strerror`（链接器没有 `-L`/`-l`、宿主地址注入要 JIT 重定位）。
  `tcc_relocate` 改成变参，一参数的新写法与老的两参数写法都收。
- `c6c/src/diag.c6` 新增 `diag_emit`：`diag_error`/`diag_warn` 与解析器的 `pa_error`、
  `pa_warn_tcc`（含「错误太多，停止」和「N 个错误，不写输出」那两行）全部改走它，
  所以设了回调之后编译诊断不再漏到 stderr；没设回调时输出字节与以前一致。
- 新增进程级守卫：同一个 `C6State` 编第二次一直是被拒的，但换一个新 state 再编会在
  `x64_sym` 里段错误（前端/后端/符号表都是文件级全局量）。现在同样如实拒绝，并提示
  「多次编译请每个编译 fork 一个子进程」。
- `c6c/tests/embed-check.sh` 的兼容面用例按 stage 分进程跑（`tcc_set_error_func` /
  系统头目录 + 列符号 / 如实拒绝的三件 / `tcc_set_lib_path` 真的换掉运行时），
  6 条 → 10 条，全绿。

### 打包：默认包里带上自举编译器 `c6c`

- `cmake/C6Install.cmake` 把 c6c 的数据文件从 `C6_ENABLE_TCC=OFF` 分支里挪出来，
  默认配置（tcc 在）也把自举编译器装成 `bin/c6c`，与 tcc 版 `c6` 并存；没有 tcc
  时它仍是 `bin/c6`，另外多一个同内容的 `bin/c6c`，两种配置的落点因此一致。
  以前 DEB / RPM / TGZ 三种包里根本没有 c6c，只有 `-DC6_ENABLE_TCC=OFF` 才装得出来。
- 新开关 `C6_INSTALL_C6C`（默认 `ON`，只在 tcc 在时起作用）：关掉就只装 tcc 版 `c6`。
- 二进制必须跟它自己的数据成套：c6c 按 `argv[0]` 找 `<bin>/../lib/c6/include`
  （自带头文件）与 `<bin>/../lib/c6/rt.c6`（自带运行时），所以 `lib/c6/` 跟着
  `bin/c6c` 一起装——只装二进制的话它连 hello 都编不出来，只会报「找不到自带
  运行时 lib/rt.c6」。
- `cmake/c6-check-install.cmake.in` 除了核对落点，还用暂存树里的 `bin/c6c` 真编一个
  程序并运行（退出码 91，同时用到自带头文件与 rt.c6）；`c6c/tests/pkgcheck.sh` 的
  安装清单加上 `bin/c6c` 并单独验一遍它的自找路径；CI 的打包步骤对 DEB / RPM / TGZ
  逐个断言 `usr/bin/c6c` 与 `usr/lib/c6/rt.c6` 在包里。

### 打包：没有 tcc 也能出包（`c6c` flavour）

- `cmake/C6Packaging.cmake` 以前在 `C6_ENABLE_TCC=OFF` 下直接 `return()`——「没有
  tcc 就没有可打的包」。现在两种 flavour 都能打：默认出 `c6` 包（tcc 版编译器 +
  `libtcc`），`-DC6_ENABLE_TCC=OFF` 出 `c6c` 包（自举编译器 + `libc6c` + `lib/c6/`，
  没有一行 tcc）。包名、描述、许可证（LGPL-2.1 vs MIT）、`/usr/share/doc/<名>` 与
  `Provides` / `Conflicts` 都跟着 flavour 走；新开关 `C6_PACKAGE_NAME` 可覆盖包名。
- 文档安装从 tcc 分支里挪出来：`C6_ENABLE_TCC=OFF` 以前一份文档都不装，现在 c6c 包
  也带 `share/doc/c6c/`（`docs/` 的 33 篇 + `README.md` / `CHANGELOG.md` /
  `copyright`，其中 copyright 是 c6c 自己的 MIT，不是 tcc 的 `COPYING`）。
- CI 新增 `package c6c-only (DEB|RPM|TGZ)` 作业：构建 c6c、打包、逐项断言「有
  `usr/bin/c6`、`usr/bin/c6c`、`lib/c6/rt.c6`、`libc6c.a`、`libc6c.so`、
  `share/doc/c6c/docs/README.md`」与「没有 `usr/lib/tcc`、`libtcc.a`、`libtcc1.a`、
  `usr/bin/tcc`、`tcc-doc`、`tcc.1`」，产物作 `c6c-packages-*` artifact 上传。
  Release 里发的仍是 `c6` 包：等 c6c 的能力缺口（`-shared`/`-fPIC`、libtcc 兼容面、
  `tests/regression.sh` 的 91 个用例迁移）补齐再换默认。

## 0.6.2 — 2026-11-16

### CI：发版说明直接取自版本提交的正文

- release job 的正文改成取**版本提交**（主题正好是 `c6 <VERSION>`）的正文：发版
  时在那条提交里写「改了什么、怎么验收的」，CI 只做取用、排版，末尾追加产物清单
  与 sha256sum 校验方法。找不到版本提交时退到最后一个动 VERSION 的提交；正文为空
  时退到**附注 tag** 的注解（轻量 tag 的 `%(contents)` 会回落到它指向的提交消息，
  那不叫版本描述，所以只认附注 tag）。标题仍是 tag 注解的首行。
- package job 把「版本提交存在且正文非空」提到打包前校验——三种包都打完才在最后
  一步红太浪费。CHANGELOG 段落的校验保留：它仍是给人看的变更史，并且随包装进
  `/usr/share/doc/c6/`。
- 发版四件事写进文档（README「打包与发布」、`docs/reference/cli.md`、
  `docs/README.md` 维护约定）：根 `VERSION`、CHANGELOG 一段、`c6 <版本>` 提交、
  `v<版本>` tag。

### 打包侧：Arch build log 的三类告警清零

- `c6c/src/parse.c6` 里 `AGG_CAP0` 被定义了两次（1024 给聚合成员表、64 给
  struct 体成员数组）——后者改名 `AGG_MEM_CAP0`，两个不同的初始容量不再共用
  一个名字（改一个不会悄悄影响另一个）。
- `c6c/src/lex.c6` 的 `Tok.syn` 改成 `const char *`、`tok_syn()` 的参数跟着改：
  `-Wdiscarded-qualifiers` 那条告警消失（token 的文本从不原地改写），顺手去掉
  三处 `(char *)` 强转。
- `tcc/tcc-doc.texi` 的 Top 节点去掉 `(dir)` 前后指针：texinfo 7 生成
  `tcc-doc.html` 时不再报「no HTML cross-references entry found for `dir'」。

验收：本机 cmake 全量构建（stage0 = tcc/c6）零告警、`ctest -L c6c -LE perf`
5/5；Arch 容器（gcc 16.2.1 + texinfo 7.3）重编 build log 零告警、
`tcc-doc.html` 照常生成。

## 0.6.1 — 2026-11-16

### 修 `async` / `async_frame` 的段错误：退役的协程槽把还在用的栈给了下一个协程

- 现象：CI 的 `no-tcc` job 和 Arch 打包的 `check()` 都在 `c6c-check-all` 上红，
  `c6c: 跑不通的： async async_frame`；core 里 `rip = 0`，崩点落在一段协程栈上。
- 根因：`c6c_co_retire()` 只看 `exited` 标志——那是协程线程自己在 epilogue 里置
  的，置完它还要**站在自己的栈上**跑 `c6c_co_give()` 和 `exit`。这时候槽已经挂回
  空闲链，下一个协程把同一份栈从顶上铺开，旧线程 `ret` 出去的返回地址就成了 0。
  负载越高越容易撞上（多任务抢 CPU 时，"置完标志"到"真的退出"这个窗口被拉长）。
  0.6.0 里把它当成"限流没做好"的次生现象，其实限流只影响撞上的概率。
- 复现：本机 20 个实例并发跑 `tests/async.c6` → 8/20 段错误（顺序跑 20/20 通
  过）；把"退役时先不复用槽"当实验 → 40/40 通过，确认就是栈复用。
- 修法：
  - `__c6_co_spawn` 多收一个 `ctid`，同时当 clone 的 ptid/ctid：协程线程带
    `CLONE_CHILD_SETTID|CLONE_CHILD_CLEARTID` 起，线程一退出内核就把 `c->tid`
    清零并 futex 唤醒。
  - `c6c_co_retire()` 等 `c->tid` 变 0（每轮最多 10 ms、上限 200 轮）才把槽挂回
    空闲链；等不到就不挂回——漏一个槽好过两个线程共用一份栈。
  - `__c6_co_entry` 交令牌前先把 `resumer` 存进本地，不再读可能已被复用的槽字段。
- 验证：48 个实例并发跑 `tests/async.c6` → 48/48；CPU 打满时顺序跑 12/12；
  `async_frame` 32 并发 → 32/32；`ctest -L c6c -LE perf` 全绿（含 `c6c-check-all`）。

### CI：两条"只在一台机器上红"的假失败

- `dyn-check.sh` 解析 `readelf` 的 `Elf file type is` 时没锁 locale：开发机是
  `zh_CN.UTF-8`（输出 `Elf 文件类型为 EXEC`），runner 是 C locale，同一条断言在
  一处红一处绿。现在 `readelf` 走 `LC_ALL=C` 包装，正则只认英文口径。
- `ci.yml` 把 `CMAKE_INSTALL_PREFIX: /usr` 设成了**全局环境变量**：CMake 会把它
  当默认 prefix 带进 `pkgcheck` 的嵌套 configure，Debian 的 GNUInstallDirs 于是把
  libdir 算成 `lib/x86_64-linux-gnu`（期望 `lib`），`pkgcheck` 报"安装树缺 7 个
  文件"。现在每个 job 在自己的 configure 里显式传 `-DCMAKE_INSTALL_PREFIX`，
  `pkgcheck` 里再钉一个 `-DCMAKE_INSTALL_LIBDIR=lib`。

### 打包侧：失败要能查，门槛要留余量

- `tests/tcc-tests2.sh` 的"意外失败 N"原来只有 `VERBOSE=1` 才打名字，AUR 的日志
  里就只剩一个数字。现在无条件打出每条的名字和结论（`LINK` / `CERR` / `FAIL`）。
- `bench/thresholds.json` 的 `max_rss_mb` 32 → 40：32 是在 Ubuntu/gcc 13 上定的
  绝对口径，Arch（gcc 15 -O3 + glibc 2.42）实测 `native:tcc.c` 33 MB 直接假红。
  40 MB 仍能挡住 2 倍级别的真退步。

### 构建：`c6c/Makefile` 全部转成 CMake target

- `make -C c6c check-all` 这一套入口没了，改成 `cmake --build build --target
  fast` / `--target check-all-c6c` / `--target dyncheck` …：脚本、数字、失败口径
  都不变，只是入口收敛到 CMake。打包脚本按 target 名调用即可（下面两条新门禁就是
  这批 target 的聚合）。

### 新增 `fast` / `check-all-c6c`：c6c 替换 c6 的两条门禁（含第三方语料）

- 现状：仓库的门禁只有 c6 那一条（`cmake --build build --target check-all`，
  被测编译器是 `tcc/c6`）；c6c 只有 `check-c6c` 一个 ctest 入口，且没有"改一行
  先跑一遍"的快车道。
- 新增两个 CMake 聚合目标，被测编译器换成 **c6c 自己**：
  - `cmake --build build --target fast` —— 快速门禁，实测 ≈40 s：`lint` +
    `unit`/`irtest`/`hello` + `ctest`/`runflag`/`testflag` + 外部语料 `c99suite`
    （`third_party/c-testsuite` 150 条）+ 上游 `tests1-check`/`tests2-check` +
    `llvmtest`。
  - `cmake --build build --target check-all-c6c` —— 全量门禁，实测 ≈4 min：
    `c6c/Makefile` 的 `check-all`（自举 fixpoint / 三后端 / std 377 / tests /
    诊断 316 / `c99suite` / `build-tcc` 里 c6c 编出上游 tcc 再跑上游 tests2 与
    第三代 selfhost）再加 `tests1-check`、`tests2-check`。
- 第三方测试集是这两条门禁的一部分，不是可选装饰：c-testsuite（`c99suite`）、
  tcc 上游 `tests1` / `tests2`，以及 c6c 编出的 tcc 再跑上游 tests2。
- 实现：CMake 的 `fast` target（`check-all` 依赖的子集，注释里列了挪出去
  哪几条及各自耗时；0.6.0 时是 `c6c/Makefile` 的 `fast`，见上面"构建"一节），所以 `make -C c6c fast` 与
  `--target fast` 跑的是同一套脚本、同一批数字；`ctest -R '^c6c-'` 那套
  （`check-c6c`）不变。

### `make -C c6c check-all` 不再卡死：`-j` 以前在 dash 下等于没限流

- 现象：`check-all` 在小机器/CI 上偶发"卡死"，或者跑到一半出现一整轮
  "跑不通的： async"；`-j N` 调小也看不出区别。
- 根因（两条，都跟并发有关）：
  1. 五个用例驱动的限流写的是 `if [ "$(jobs -p | wc -l)" -ge "$JOBS" ]`。
     `$(…)` 是**子 shell**，dash（Ubuntu 的 `/bin/sh`）里它看不见父 shell 的
     作业表，`jobs -p` **恒为空** → 计数恒为 0 → 限流从来不会触发。于是
     `c99suite` 把 150 条用例、`test` 把 130 多个 `.c6`、`stdtest` 把 377 个模块
     一次性全起起来；每条背后还有 gcc/cc/被测程序。44 核开发机上看不出来，
     2~4 核的 runner 或笔记本上直接把机器拖进 swap——表现就是"卡死"，
     对负载敏感的 `async` 系列还会真的段错误（本机复现：`-j 2` 峰值 21 个
     c6c 同时跑，`async` 挂）。
  2. 编译器调用、被测程序的运行**没有任何时间上限**（只有少数几处有
     `timeout`，而且都是不带 `-k` 的）。任何一个 `c6c` 在输入上死循环、
     或者被测程序接了/屏蔽了 SIGTERM，`timeout` 的 SIGTERM 就不生效，
     整轮验收永久挂在那里。
- 修法：
  - 五个驱动（`c99-suite` / `run-tests` / `diag-tests` / `std-tests` /
    `tcc-tests2`）把限流换成**自己数批次数**（`nrun` 累加、满一批 `wait`
    后清零），POSIX sh 下都成立；实测 `-j 3` 峰值就是 3 个 c6c 进程。
  - 所有 `timeout` 一律带 `-k`（先 SIGTERM，几秒后 SIGKILL）；`c99-suite`
    / `run-tests` / `diag-tests` / `std-tests` / `tcc-tests2` / `tcc-self-check`
    的**编译器调用**也加了上限（`C6C_TMO`，默认 120 s；`tcc-self-check` 编
    tcc.c 是 300 s），`c99-diff` / `llvm-diff` 的三步编译和两边程序运行同样
    有上限。卡死会被判成一条失败，而不是把 check-all 挂住。
- 顺手修掉：
  - `c99-suite.sh`：结论缺失（子进程被打死）以前会被 `[ -f "$v" ] || continue`
    静默跳过——挂掉的用例从总数里消失，闸门反而变绿。现在先占位 `MISS`，
    汇总时单列并挡闸门；编译超时单列 `TMO`（也挡闸门），运行超时不再笼统
    记成"退出码 124"。
  - `std-tests.sh`：编译失败但没有诊断输出（多半是超时）时，`r.$i` 是空文件，
    汇总按"通过"算——现在算失败。
  - `run-tests.sh` / `diag-tests.sh`：没有结论的用例单列出来并算失败
    （以前直接 `continue`）。
  - `llvm-diff.sh`：`tests/c/llvm-only.txt` 非空时 `skip=$((skip+1))` 撞上
    `set -u`，脚本在第一条上就 "skip: unbound variable" 死掉。

## 0.6.0 — 2026-11-15

这个发布号的内容是**语言层 C99 收口**：先引一份外部标准用例当闸门，再把闸门和
自查扫出来的"非库层面"缺陷逐类修掉——每类都留了回归用例，三后端逐字节对齐，
自举 fixpoint 不变。

### 外部标准用例闸门：`third_party/c-testsuite` + `make c99suite`

- 引入 scc 的 c-testsuite（`single-exec` 150 条 portable 单文件用例，ISC，
  上游 `5c72756`），原样口径：编出可执行 → 跑 → stdout+stderr 逐字节比。
- `c6c/tests/c99-suite.sh` 把每条判成 PASS / LANG（前端编不过）/ OUT（输出不符
  或退出码非 0）/ LIB（链接缺符号）/ STALE（gcc 也对不上）/ SKIP / N/A（超出口径，
  登记在 `tests/c99-out-of-scope.txt`），**闸门只看 LANG+OUT**，接进 `check-all`。
- 当前：通过 **149** / 语言层失败 0 / 输出不符 0 / 超出口径 1（`00046` 是 C11 的
  匿名成员）。`1f26c69`

### 语言特性补齐

- **`_Complex`**（C99 6.2.5p11）：表示成两个分量的普通结构体，sizeof/对齐/按值
  传参/返回全走现成的聚合路径，**三个后端一行没改**；`+ - * /`、一元负号、
  `==`/`!=`、实数↔复数转换、复合赋值、初始化器（局部/全局）、GNU 的
  `__real__`/`__imag__`（可读可写）。`_Imaginary` 和 gcc 一样明确报"没实现"。`aab7186`
- **变长数组**：`sizeof v`（不带括号也要能过）、多维 VLA（块作用域：stride 在
  声明处算成 vreg 记进 `Sym`，`b[i][j]` 按运行期 stride 缩放）、多维 VLA **形参**
  （`int a[n][m]`：形状在声明符上留一份，函数入口按形参值现算 stride）。`0d64333`
  `6529d65` `b24b574`
- **三字符组**（6.4.6）：读文件那一刻就地改写源缓冲，行长拼接/注释/字符串/`#`
  指令/诊断行号看到的都是换好的文本。`0bfca23`
- **`-std=c89|c99|c11`**（含 gnu 变体）：`__STDC_VERSION__` 跟着给，c89 里不定义。
- **宏实参个数**（6.10.3p4）：多给少给都报诊断；`E()`、单形参宏的 `F()`、变参宏
  不给可变部分这三个边界按"空实参"放行。`0bfca23`
- digraph、标识符 UCN、`_Pragma` 同样是这一轮补的。

### 语义修正（都是"合法 C99 但算错/被拒"）

- `_Bool` 的转换（6.3.1.2）：任何标量转 `_Bool` 都是"和 0 比"，浮点源用浮点比较。
  `0d64333` `9d8baa9`
- 位域成员初始化器：按位宽/位偏移落值，不再"写整个字节"；指定初始化器按位偏移
  挑叶子。`0d64333`
- 常用算术转换里的除法/取模：`long / unsigned int` 是**有符号**除法（6.3.1.8）。
  `ae6c544`
- union 的指定初始化器按**那个成员**的类型落值（`.f = 1.0f` 存位模式）。`ae6c544`
- 全局/静态初始化器里的地址常量：`&A[i]`、`A + 2`、`&s.x`。`0cc349a`
- 复合字面量的后缀：`(struct P){1,2}.y`、`(int[]){7,8}[1]`。`7ff99c6`
- 下标交换律：`1[a]`、`i[a]` 等价 `a[i]`（6.5.2.1p2）。`7ff99c6`
- 宽字符串 `sizeof(L"abc")` 按 wchar_t 宽度；单字符常量 `'\377'` 是 -1
  （6.4.4.4p10）。`7ff99c6`
- 一元 `+` 的整型提升（6.5.3.3p2），`_Bool` 也提升。`bfd7169`

### 三后端一致性（后端缺陷）

- **LLVM 后端的变参**：以前 `va_start`/`va_arg` 落在"函数入口自己的 `%ap`"上，
  前端那个 `va_list` 从没初始化——`printf("%d", 7)` 印 0、转发 va_list 直接崩、
  `llc -O0` 段错误。现在 va_start 作用在前端对象上，va_arg 手写 SysV 取字。`65fecc4`
  另外 emit 的是**不带版本后缀**的 `llvm.va_start`：`.p0` 那个名字只有 LLVM 19
  认，CI 上的 llvm-18 会把它当外部函数、51 条用例全挂在链接阶段。`e87f0f5`
- **C99 输出后端的聚合按值返回**：以前返回被调方帧里那个临时对象的地址，
  调用方拿到就失效（`gcc -O1` 起读出垃圾），改走"调用者给槽"。顺带修好零形参
  函数返回聚合（`struct P f(void)`）那条最常见的写法。`c2dca23` `6529d65`
- **C99/LLVM 后端的窄整型载入**：char/short/int 必须符号扩展，否则 `-1` 变
  4294967295，除法/右移/int→double 一起错。`e9a8c6d`
- LLVM 后端的"终结指令后面接指令"（cleanup/goto 密集的代码编不出 IR）。`df9fe9c`

### 语料与验收

- `c6c/tests/c/` 扩到 **51** 条，覆盖上面每一类：`complex` `vla_multi` `vla_param`
  `struct_return` `trigraph` `varargs` `c99-semantics` `arith_conv` `union_init`
  `postfix_lit` `widecharlit` `global_addr` `realworld` `c99_pp` `c99_init`
  `regress_all`（把本轮十条真缺陷合成一条回归）等；`llvm-only.txt` 变成空表。
- `make -C c6c check-all` 一条命令：自举 fixpoint 逐字节相同、tests 前端 124/0 +
  跑通 67/0、std 377/377、编译期诊断 316/316、C99 51/51、LLVM 51/51、
  c-testsuite 闸门绿、c6c 编出的 tcc 跑 tests2 通过 118。
- 文档：`c6c/docs/acceptance.md` 新增"语言层 C99 的收官状态"，`docs/todo.md`
  把每一类修复和**仍然不支持**的形态都记了卡点。`d4c9dba`

### 仍然不支持（明确报错，不是静默算错）

- 指向变长数组的指针 `int (*p)[m]`：运行期 stride 没有地方放；建议写数组形参
  `int p[][m]` 或自己按 `p[i * m + j]` 算。
- 多维 VLA 形参里 stride 是复杂表达式（目前只支持形参名/常量）。
- 库层面（"库不管"口径）：runtime printf 的 `%f` 定点格式化在 |值| ≥ 2^64 时印
  `0.0`（同一个值用 `%e`/`%g` 是对的）。

### 顺手修掉 v0.3.0 tag 上 CI 红的两处



v0.3.0 tag 的流水线挂在两处，都是"本机能过、CI 上才炸"的口径，这次一起修掉：

### shared libtcc 形态下 c6c 编不动（`tcc/c6` 找不到 `libtcc.so`）

- 现象：`-DC6_TCC_SHARED_LIBTCC=ON` 的矩阵里 `cmake --build` 走到 c6c 就报
  `tcc/c6: error while loading shared libraries: libtcc.so`，打包 job（同为
  shared 形态）跟着跑不起来，`release` 因此永远等不到包。
- 原因：引导编译器 tcc/c6 在 shared 形态下自己就链到 `libtcc.so`，而构建树里
  的 c6 没有 rpath、`libtcc.so` 又不在默认搜索路径上；编译期没有任何地方给它
  注 `LD_LIBRARY_PATH`（注入只做在 CTest 用例的 ENVIRONMENT 里）。
- 修法：`cmake/C6C.cmake` 里凡是**要执行** c6 的地方（c6c 的自定义命令 +
  `c6c-build` / `c6c-check-all` / `c6c-tests1` / `c6c-tests2` / `c6c-bench`）
  统一补 `LD_LIBRARY_PATH`，静态形态下什么都不加。
- 顺手：`tcc/libtcc.so` 补进 `tcc/.gitignore`（这个形态的构建产物原来会把工作树
  弄脏）。

### c6c 释放长链是递归的：8 MB 默认栈段错误

- 现象：c6c job 的 `make test` 在 CI 上挂 `gc_field`（"跑不通的： gc_field"）。
  本机 64 MB 栈能过，CI 上栈小就直接段错误。
- 原因：`lib/rt.c6` 的 `__c6_gc_release` 命中 `rc==0` 后就地销毁字段，字段的
  release 再递归进来；`tests/gc_field.c6` 的 20001 节点长链要几千帧，约 30 MB 栈。
- 修法：加待销毁队列——release 归零只入队，由一个循环逐个销毁（`tcc` 侧
  `lib/libtcc1.c` 的 drain 队列同口径）。栈深与链长无关，1 MB 栈也能跑完长链。
- 顺带把这条口径钉住：`c6c/tests/run-tests.sh` 现在默认在 **8 MB 栈**下跑每个
  用例（`C6C_TEST_STACK=0` 关掉，非 Linux 自动关掉），免得开发机的大栈再把
  这类问题藏到 CI。

### `tests/defer_scope.c6` 深递归条数 20000 → 10000

- 那一档只是为了证明"没有 8192 条记录的上限"，而每帧约 500 B 栈：20000 层在
  8 MB 栈上会真段错误（那是递归吃栈，不是记录上限）。10000 层仍远大于 8192。

## 0.3.0 — 2026-11-15

这个发布号的全部内容是一个东西：独立子目录 **`c6c/`** ——用 c6 语言自己写的、
脱离 tcc 的自举编译器。

### 新增 c6c：用 c6 写的自举编译器（native x86-64 / C99 / LLVM IR 三后端）

- 目录 `c6c/`（约 3.3 万行 c6）：`src/` 前端与三后端、`lib/rt.c6` 自带运行时、
  `include/` 头文件、`tests/` 记分板与回归语料、`bench/` 性能基准、
  `docs/` 计划/设计/并发/模块/todo/验收六份文档。
- 三后端共用一套 IR（16 字节一条 op）与同一套 vreg 口径；native 直接落 ELF，
  C99 与 LLVM IR 生成本机编译器可编译的中间产物，三者跑同一套用例逐字节对齐。
- 自带运行时（`lib/rt.c6`）：堆分配/GC、TLS、原子操作、协程、内联汇编跳板、
  系统调用，不依赖 tcc 或 libc 的启动代码。
- `-run`：编到 `$TMPDIR` 的临时 ELF 再 `exec`（不是 mmap+重定位的 JIT）；
  程序参数与退出码原样透传（被信号杀按 shell 惯例 `128+sig`），临时文件在子
  进程结束后删除；首行 `#!` 当空白，所以 `#!/path/to/c6c -run` 可直接当脚本
  解释器。回归 `make -C c6c runflag`。
- `-test` / `-test-filter=子串`：跑 `test "…" { }` 块、不执行用户 main（合成
  入口叫 `c6c_test_main`，用户的 `main` 留在符号表里但不调用）。输出协议与
  tcc 的 native 运行器逐字一致：每块一行 `PASS: 描述` / `FAIL: 描述: 错误消息`
  （FAIL 走 stderr）、收尾 `N test(s), M failure(s)`、失败数当退出码；零块或
  过滤空了静默退出 0。不给 `-o` 就编到临时 ELF 再 `exec`（和 `-run` 同一条
  路），给了 `-o` 只写文件。回归 `make -C c6c testflag`（19 条）。
- 验收（仓库内一条命令 `make -C c6c check-all`）：
  - 自举 fixpoint 逐字节相同（`c6c2 == c6c3`）；
  - tests 前端 123/0、跑通 67/0，std 模块 377/377，编译期诊断 314/314；
  - C99 后端 31/31、LLVM IR 后端 29/29 与 native 输出一致；
  - c6c 能编出上游 tcc 并让它跑通 tests2 **118/144**，且**第三代 selfhost**
    （编出来的 tcc 再编一遍 tcc.c）出体、抽查用例全对。
- 直接跑 tcc 上游用例（`make -C c6c tests2-check`）：**通过 112 / 架构性
  n/a 10 / 上游只编不跑 8 / 上游 expect 过期 14 / 意外失败 0（共 144）**。
  那张 n/a 表（`c6c/tests/tcc-na.txt`）逐条写了具体卡点，不在表里的失败一律
  算意外失败——所以它是闸门，不是进度条。
- 性能（`make -C c6c bench-c6c`，gcc -O3 编的 c6c-opt，best-of-5）：
  编 tcc.c **112 ms / 25 MB**、自编译 **88 ms / 22 MB**、预处理 57 ms，
  每行成本随规模收敛到 1.0（线性度断言通过）。
- `c6c --version` 打 `c6c 0.3.0（c6 的自举编译器；native / C99 / LLVM IR
  三后端）`，版本号跟仓库根 `VERSION` 的发布号走（以前是第一阶段留下的
  "0.0.1 / P1 骨架"）。
- 顺手修掉两类会让结果悄悄错的问题：vreg/标号超过 16 位上限现在**报错**
  （以前静默绕回、算错结果）；`ty_qual()`/`ty_unqual()` 把 `nkid`/`a` 当
  `nbits` 传给 interning 的潜伏空指针。

### `tcc/tcc.h`：纯 C 构建（不带 `C6_EXT`）也要有 `C6_ROLE_*`

`tccpp.c` 里判断"参数是不是 expr 角色"的那行在两种构建下都会编译，而
`C6_ROLE_*` 原来只在 `C6_EXT` 分支里定义。c6c 编 tcc 走的是纯 C 路径，
缺这几个宏就编不过；补上定义（值本身无关紧要，不带 `C6_EXT` 时没有任何宏会被
标成 expr 角色）。

### 构建与 CI：c6c 接进 CMake，并进流水线

- 新增 `cmake/C6C.cmake`：`cmake --build` 顺带把 c6c 编出来（目标 `c6c`，挂在
  `ALL` 上、依赖 `tcc-build` 产出的引导编译器 `tcc/c6`）；验收注册成 4 条
  CTest（`c6c-build` / `c6c-check-all` / `c6c-tests2` / `c6c-tests1`，label
  `c6c`），性能门槛单独一条（label `perf`）。一条命令的入口：
  `cmake --build build --target check-c6c`（再加 `check-c6c-perf` 就是验收 4）。
- `c6c/Makefile` 保留：验收本身是"跑测试、对输出、算 fixpoint"的脚本集合，
  Make 里的二十多个目标不是"编译一个目标"能表达的，所以 CMake 只管编译与入口。
- CI 新增 `c6c` job（push/PR 都跑）：`cmake --build` → `ctest -L c6c -LE perf`，
  外加一条 `continue-on-error` 的 `ctest -L perf`；`release` 的 `needs` 加上
  它，c6c 的验收挂了就不发版。

## 0.1.0 — 2026-11-13

第一个正式发布号。此前 c6 的「版本」只有 `tcc/VERSION`（内嵌上游 TinyCC 的
`0.9.28rc`）这一个来源：`c6 -v`、`pkgconf --modversion c6`、DEB/RPM/TGZ 包名
全都印的是上游版本，c6 自己的发布号无处可写。

### 版本源拆成两个：根 `VERSION` = c6 发布版本，`tcc/VERSION` = 上游兼容版本

- 新增仓库根 `VERSION`（= `0.1.0`）。`CMakeLists.txt` 的 `project(c6 VERSION)`
  改读它并设 `C6_VERSION_RAW`；`cmake/C6Tcc.cmake` 不再拿 `TCC_VERSION` 覆盖
  `C6_VERSION_RAW`，`tcc/VERSION` 读回的值只当兼容版本（`C6_TCC_VERSION`）。
- 影响面：`c6 -v` 多打一行 `c6 0.1.0 (TinyCC 0.9.28rc)`（让 c6 侧把
  `C6_RELEASE_VERSION` 传进 `tcc/CMakeLists.txt`，生成 `config.h` 里的
  `C6_VERSION` 宏；独立构建 `tcc/` 时没有这个宏，输出与以前逐字一致）；
  `c6.pc` / `libtcc.pc` 的 `Version`、`c6Config.cmake` 的 `C6_VERSION`、CPack 的
  `CPACK_PACKAGE_VERSION` 与产物名（`c6-0.1.0-<release>-<arch>`）全部跟着走。
- 发布链路不变：push 一个 `v*` tag，`.github/workflows/ci.yml` 的 release job
  会跑完整回归、打 DEB/RPM/TGZ 三个包并附到 GitHub Release；发布号与 tag 现在
  都来自根 `VERSION`，两者不会再对不上（`package` job 新增一步校验
  `tag == v$(cat VERSION)`，不一致就直接失败，不会出现 tag 与包版本错位）。

### 修 CI：GNU make 4.3 上 `tccdefs_.h: No such file or directory`

- GitHub Actions 的四个 `test (...)` 全挂在 Build 第一步：`tccpp.c:4430: fatal error:
  tccdefs_.h: No such file or directory`，报错的两个目标是 `<builtin>: libtcc.o` 和
  `<builtin>: tcc.o`。根因不在源码，在 `tcc/Makefile` 的标志记录 `tcc/.c6-flags`：
  它由 makefile **解析期**的 `$(shell ...)` 写出来，而 GNU make **4.3**（Ubuntu
  22.04 / 24.04，也就是 CI 上的 make）看不到「解析期刚创建」的文件，于是
  `%.o : %.c $(LIBTCC_INC) $(C6FLAGS_STAMP)` 的前置判成不可生成、规则被丢弃，make
  退到内置的 `%.o : %.c` —— 没有 `$(DEFINES)` / `$(CFLAGS)`、没有 `-DONE_SOURCE=0`、
  也没有 `tccdefs_.h` 依赖，于是 `libtcc.c` 与 `tcc.c` 一起撞上同一个 fatal error。
  本机 GNU make 4.4.1 看得见这个文件，所以本地一直是绿的。
- 修法：给 `$(C6FLAGS_STAMP)` 补一条自己的规则（缺了就照同样内容写一份）。文件在的
  时候规则不跑，不会带出多余重编；缺了也不会让对象规则失效，内置规则再没有机会顶上来。
- 验证：GNU make 4.3（`-DCMAKE_MAKE_PROGRAM=` 指向 Debian bookworm 的 make 4.3），
  `shared=ON` / `shared=OFF` 两种构型全新检出都能编通，`c6` / `libtcc.a` /
  `libtcc.so` 齐全，第二次构建无动作，改 `EXTRA-DEFS` 仍会全量重编。

### 构建标志变了就全量重编（修 `tcc: error: no input files`）

- `tcc/` 里的目标文件现在都依赖一份标志记录 `tcc/.c6-flags`（`CC` / `CFLAGS` /
  `DEFINES` / `LDFLAGS` / `LIBS`）。标志一不同，下一次 `make` 就把对象全部重编，
  **不管 make 是怎么被调起来的**。起因是 `make check-all` 在共享检出里挂在第一步：
  `make -C tcc/lib` 用 `tcc/c6` 编 `libtcc1.c`，每个文件都只回一句
  `tcc: error: no input files`。根因不是源码，而是产物混编：`-DC6_EXT` 会在
  `tcc.h` 里给 `struct TCCState` 加字段（就在 `files` / `nb_files` 前面），有人用
  不带 `EXTRA-DEFS` 的裸 `make -C tcc` 重编了 `tcc.o`（`config.mak` 的
  `-Wall -O2`，没有 `-g`），其余对象还是 `make build` 的扩展构型；两边链接没意见，
  驱动读 `s->nb_files` 读到了错位的槽位，于是所有输入都被当成「没有输入文件」。
- `tcc/.c6-flags` 进 `.gitignore`，`make -C tcc clean` 一并删掉。
- 顺带修掉上游模式（不定义 `C6_EXT`）真正的编译回归：`tccpp.c` 里
  `s->c6_role` / `C6_ROLE_EXPR` 和 `hbody` / `wbody` 的声明、`tccgen.c` 里
  `TOK_C6_ERR` 那个 `case` 漏了 `#ifdef C6_EXT`，`make build-plain` 根本编不过。
  裸 `make -C tcc` 走的就是这条路线，所以现在两种构型都能自洽地编出来。
- README、[docs/reference/cli.md](docs/reference/cli.md#构建) 各记一句：别手工删一部分
  产物来「加快」构建，`make clean` 才是那个开关。

### `make check-all` 改成并发跑（共享路径的那组串行）

- 完整验收里只有**共享 `build/` 下固定临时路径**的那一组（`stdtest` 和八十多个
  `std-*` 示例）必须串行 —— 它们的用例会往 `build/fs-test/`、`build/std-static/`、
  `build/mmap-test.bin`、`build/echo.sock` 这些固定路径写文件。于是把这组交给
  `check-std-serial`（一条 `make -j1` 的递归 make），其余目标交给 make 并发；
  同一台 44 核机器上，一轮验收从 122s 降到 50s 上下。输出按目标成块
  （`--output-sync=target`），想整轮串行就 `make check-all CHECK_JOBS=1`。
- 并发顺手暴露并修掉两个真问题：裸机五连（boot / tests / go / handle / smp）
  过去共用 `build/{payload.*,boot.*,bare.img}`，同时跑会互相盖掉（报
  `_start not defined`、qemu 退出码 5）——现在每条一个目录
  （`examples/boot/build.sh` 认 `BARE_BUILD`）；`fstest-qemu` 跑的是 `fstest` /
  `fstest-go` / `fstest-noos` 编出来的程序，却没写依赖，并发时会赶在产出之前启动。
- 标准库那一组目标合并成一个变量 `STD_TARGETS`（`test-all` 与 `check-all` 共用），
  以后加模块少一处要同步。

### 标准库收尾：覆盖面盘点

- 新增 [docs/stdlib-coverage.md](docs/stdlib-coverage.md)：377 个模块的规模分布、
  逐族覆盖矩阵（完整 / 部分 / 不做）、**明确「不做」清单及理由**（QUIC、TLS 1.3
  完整握手、SNMP v3、LDAP/RADIUS/SIP、Office/Parquet/7z、GPX/KML/GeoJSON、
  base36/62 变体、词典级断词/排序），以及「什么时候到头」的三条判定标准与结论。

### 标准库第九十二批：HTTP/2（HPACK / 帧层 / h2c 客户端 / h2c 服务端）

- **`std/encoding/hpack.c6`**：HPACK 头压缩（RFC 7541）。61 项静态表 + 动态表（尺寸更新、
  淘汰、按尺寸清空）、1..8 位前缀整数、字符串与 Huffman（按码长分桶的扁平偏移表，解码
  不建树，EOS 出现在数据里直接判错）、整块 `put_block`/`decode_block`；编码端一律不插
  动态表（同一份头块永远编成同一串字节），解码端按对端指令插；`authorization`/`cookie`/
  `set-cookie`/`proxy-authorization` 强制 never-indexed，头名自动转小写。RFC 7541 的
  C.2/C.3/C.4 向量（含 Huffman 与动态表尺寸计算）都在 test 块里。
- **`std/net/http2.c6`**：帧层（RFC 9113）。帧头读写（半包给 0）、`PADDED`/`PRIORITY`
  前缀剥离、preface 校验、SETTINGS / WINDOW_UPDATE / RST_STREAM / PING / GOAWAY 编解码；
  `H2Pipe` 把「读满一帧 / 写满一帧 / 读负载 / 丢负载」收成一套同步原语，客户端与服务端
  共用，拿 `socketpair` 就能互测。
- **`std/net/http2c.c6`**：h2c（明文）客户端。连上先发 preface + SETTINGS、等对端 SETTINGS
  落地再发请求；SETTINGS/PING 自动应答、DATA 自动还 WINDOW_UPDATE、GOAWAY 与 RST_STREAM
  让本次请求失败（原因在 `why()`）；一条连接串行发请求、流号 1/3/5…，CONTINUATION 与填充
  都拼装，请求体超首窗在本地拒绝而不是挂着等 WINDOW_UPDATE。
- **`std/net/http2d.c6`**：h2c 服务端骨架。`next_request` 返回 1（请求收全，头已解、body
  已在调用方缓冲）/0（对端收工）/−1（协议错），`respond`/`respond_text` 写响应；连接第一
  件事必须是 preface（HTTP/1 报文判协议错），一次一个流，并发的流回
  `RST_STREAM(REFUSED_STREAM)`。
- 有意的边界（写在模块注释里）：只做 h2c + prior knowledge，**不做** ALPN、不把 TLS 记录层
  塞进来（那是 `std.net.https`/`std.net.tlsrec` 的活）、不做服务端推送与并发流、请求/响应体
  不跨窗口排队。协议本身完整：CONTINUATION 拼装、填充、优先前缀、SETTINGS 协商、
  WINDOW_UPDATE、RST_STREAM、PING、GOAWAY 都处理。
- 集成示例 `examples/stdh2.c6`（`cmake --build build --target std-h2`，49 条检查）+
  `tests/regression.sh` 的 `std-h2` 用例（同时进 `C6_STD_TARGETS` / `C6_TEST_ALL`）+ `stdtest`
  的 `deps` 映射；真机互通方向用 `curl --http2-prior-knowledge`（nghttp2）打过 `http2d` 的
  服务端：200/404、响应头、60000 字节响应体、POST 回显，curl 收工时的 GOAWAY 也正常收尾。
  `std/` 到 **381** 个模块（`encoding` 54 / `net` 56）。
- 顺手改定位：[docs/stdlib-coverage.md](docs/stdlib-coverage.md) 第 4 节原来的「只收
  判断型/编解码型、新模块只能是变体」删掉，换成「开发者常用就收 / 代价可控 / 零分配风格一致」
  三条；QUIC 与 HTTP/3 仍然不做，但理由从「类型学不符」改成「等于随包一个传输栈」的代价判断。

### 标准库第九十一批：网段、字节序、排序键、断词

- **`std/net/cidr.c6`**：IPv4/IPv6 CIDR。解析后主机位清零，`contains_text` 判归属，
  `network_str`/`broadcast_str`/`host_count` 给网段信息；`SubnetDoc` 把网段拆成更小
  前缀的子网（带 `next`/`count`/`reset` 游标，最多一次展开 2^20 个）。
- **`std/core/endian.c6`**：字节序读写游标。大端/小端都支持 u8/16/32/64 与按位宽读，
  越界立即进入失败态；写入游标 + `bswap16/32/64` + `host_is_little`，不依赖 libc。
- **`std/text/collate.c6`**：排序键。大小写折叠、忽略标点、数字按数值比，
  键是字节串可直接 `strcmp`，排序一堆字符串时先算键缓存即可。
- **`std/text/hyphen.c6`**：断词（VCCV 启发式 + 常见前后缀 + 最小前后缀约束），
  给出断点下标或直接插入连字符；明确不是 TeX 断词字典。
- 集成示例 `examples/stdutil.c6`（`make std-util`，45 条检查）+ `STD_UTIL_SRCS` +
  err 号段 0x6175…0x6178；`std/` 到 **377** 个模块。

### 标准库第九十批：网络管理与可观测

- **`std/net/prometheus.c6`**：Prometheus 文本暴露格式（语法层）。样本/`# HELP`/
  `# TYPE`/注释都交出来，标签与数值按原文本偏移返回；构造器 `help/type/sample`。
- **`std/net/gemini.c6`**：Gemini 协议报文层。请求行解析（host/port/path/query）、
  响应状态分类（2x/3x/4x/5x/6x）与 META/正文，两侧都能构造。
- **`std/net/snmp.c6`**：SNMPv2c + 最小 BER。解析 version/community/PDU/
  request-id/error 字段与 varbind 列表；值按原始字节透传，`int_value` 解数值；
  构造器支持 `set_pdu`（Get/GetNext/Response/Set/GetBulk）与任意类型 varbind。
- **`std/net/nntp.c6`**：NNTP 报文层。命令行（512 字节上限、参数禁止空白）、
  响应状态分类、多行正文边界 `body_end`、行首点双写还原 `undot`。
- 集成示例 `examples/stdmgmt.c6`（`make std-mgmt`，53 条检查）+ `STD_MGMT_SRCS` +
  err 号段 0x6171…0x6174；`std/` 到 **373** 个模块。

### 标准库第八十九批：文档与邮件

- **`std/text/plural.c6`**：英文单复数——不规则表（person/child/mouse…）、不可数表、
  `-es/-ies/-ves` 规则，`count_str` 直接给「3 files」这种短语，可自定义增补。
- **`std/data/mbox.c6`**：Unix mailbox 分卷（行首 `From ` 切分、`>From` 不作分隔、
  续行折叠），消息游标 + 头部游标（`next_header`/`get_header`/`header_value`）。
- **`std/data/kdl.c6`**：KDL v1 文档——节点/参数/属性、嵌套、行/块注释、`/-` 软注释、
  引号转义，两个游标（`next` 给节点、`next_arg` 给参数）。
- **`std/data/rtf.c6`**：RTF 纯文本提取——控制字剥离、`\{ \} \\ \'hh` 转义、
  `\uN` → UTF-8、整组忽略 `fonttbl`/`info`/`pict`/`\*` 目标。
- 集成示例 `examples/stddocs.c6`（`make std-docs`，48 条检查）+ `STD_DOCS_SRCS` +
  err 号段 0x616d…0x6170；`std/` 到 **369** 个模块。

### 标准库第八十八批：网络运维与媒体清单

- **`std/net/netflow.c6`**：NetFlow v5（24 字节头 + N×48 记录），版本与长度校验，
  `FlowDoc` 游标；`Flow` 上带 `src_str/dst_str/octets_per_packet/duration_ms`；
  构造器按 `count` 校验记录数。
- **`std/net/wol.c6`**：Wake-on-LAN 魔术包（6 个 0xFF + 16 份 MAC + 可选 SecureOn
  密码），解析要求 16 份 MAC 完全一致；`mac_str`/`mac_parse`（大小写与 `-` 分隔都收）。
- **`std/data/vtt.c6`**：WebVTT（`WEBVTT` 头、NOTE/STYLE/REGION 跳过、可选 cue id、
  `-->` 时间戳、settings、多行正文），`parse_ts`/`ts_str` 处理 `HH:MM:SS.mmm`
  与省略小时的写法。
- **`std/data/zone.c6`**：DNS 区域文件（`;` 注释、跨行括号、省略 owner、
  `$TTL`/`$ORIGIN` 伪记录、`[TTL] [IN] type rdata`），未知类型与 `$INCLUDE`
  保守判非法。
- 集成示例 `examples/stdops.c6`（`make std-ops`，62 条检查）+ `STD_OPS_SRCS` +
  err 号段 0x6169…0x616c；`std/` 到 **365** 个模块。

### 标准库第八十七批：发现与配置协议 + 文本折叠

- **`std/net/coap.c6`**（RFC 7252）：4 字节头 + token + 选项串（13/14 扩展
  delta/length）+ `0xFF` payload；`CoapMsg.parse` 解析、`CoapDoc` 游标逐条走选项、
  `CoapBuilder` 自动算 delta 并强制递增。
- **`std/net/dhcp.c6`**（RFC 2131/2132）：236 字节头 + magic cookie + TLV 选项，
  `find` 按编号查不影响游标；构造器补 END 并填充到 300 字节；`ip4_str/ip4_parse`。
- **`std/net/ssdp.c6`**（UPnP 发现）：`M-SEARCH`/`NOTIFY`/应答三种起始行，头部名
  大小写不敏感（`get`），构造器自动写多播 `HOST` 并补空行。
- **`std/text/translit.c6`**：UTF-8 → 近似 ASCII（拉丁去音符、符号换 ASCII、全角
  转半角、组合符号丢弃），标志位 `TR_REPLACE_UNKNOWN`/`TR_FOLD_CASE`，流式句柄
  `Translit.put/finish/dropped`。
- 集成示例 `examples/stdproto4.c6`（`make std-proto4`，56 条检查）+ `STD_PROTO4_SRCS`
  + err 号段 0x6165…0x6168；`std/` 到 **361** 个模块。
- **文档补一条硬约束**：跨模块同名的方法（首参结构体指针）编成 `<Tag>_<name>`
  不会撞，但首参不是结构体指针的自由函数是普通全局符号——两个模块都导出同名
  函数时会被静默合并，必须做成方法或加模块前缀。

### 标准库第八十六批：服务端中间件语义（限流 / 缓存 / 邮件编码 / 模糊匹配）

- **`std/net/ratelimit.c6`**：令牌桶（`Bucket.take/take_n/retry_after`，整数毫秒
  补充、允许突发）与滑动窗口（`Window.allow/count`，10 个 100 ms 槽，窗口边界不
  双倍突发）两种口径。
- **`std/net/httpcache.c6`**：RFC 9111 的判断要点——`CacheControl` 解析
  （s-maxage 优先、no-store/no-cache/private 不可存）、新鲜度边界、ETag 弱/强比较，
  以及定容缓存槽 `Cache.put/lookup/etag_of/expire`（近似 LFU 淘汰）+ `CacheDoc` 游标。
- **`std/encoding/quotedprintable.c6`**：RFC 2045 QP 编解码。只有一份编码状态机：
  `qp_encode()` 是流式 `QpEnc` 一次性喂完，`qp_encoded_len()` 是同一状态机只计数，
  所以「算出来的长度」和「写出来的字节」永远一致；行尾空白按 `=20`/`=09` 编码。
- **`std/text/strdist.c6`**：Levenshtein / OSA / Hamming / LCS / Jaro / Jaro-Winkler，
  相似度一律千分制整数；`Matcher` 句柄把「阈值 + 逐个候选打分 + 记住最佳」做成方法。
- 集成示例 `examples/stdmw.c6`（`make std-mw`，60 条检查）+ `STD_MW_SRCS` +
  err 号段 0x6161…0x6164；`std/` 到 **357** 个模块。

### 标准库第八十五批：密钥交换 + 传输安全策略 + 抓取许可 + 45 进制

- **`std/net/x25519.c6`**：Curve25519 密钥交换（RFC 7748），`Dh.init/agree/is_ready`
  句柄式入口；16×16 位肢域运算、常数时间蒙太奇阶梯（条件交换用掩码）。
- **`std/net/hsts.c6`**：HSTS 策略库（RFC 6797）——`hsts_parse` 解响应头，
  `HstsStore.add/match/expire/prune` 判策略，`HstsDoc` 游标遍历；子域、IP 字面量、
  时钟倒退、`max-age=0`（撤销）这些边界都在测试里。
- **`std/net/robots.c6`**：robots.txt（RFC 9309）——`Robots.load/allowed/sitemap`
  + `RobotsDoc` 逐行游标；最长匹配、同长 Allow 赢、`*`/`$` 通配、具体 agent 组优先。
- **`std/encoding/base45.c6`**：Base45（RFC 9285）一次性编解码 + 流式 `B45Enc`；
  空格是字符表成员（不能当分隔符），只有 `\t\n\r\v\f` 跳过。
- 集成示例 `examples/stdsec.c6`（`make std-sec`，75 条检查）+ `STD_SEC_SRCS` +
  err 号段 0x615d…0x6160；`std/` 到 **353** 个模块。

### 性能与基准

- **一次性 epoll 注册 + O(1) 唤醒**：协程运行时的 fd 注册集改成按 fd 索引的
  数组，等待用 `EPOLLONESHOT` 装填（触发后内核自动解除，下一次 `await` 用
  `EPOLL_CTL_MOD` 重装），唤醒用「fd → 叶子下标」直接命中：
  1000 路并发连接一轮收完从 10.7 µs/连接降到 **4.95 µs/连接**，
  单次挂起+恢复从 3.95 µs 降到 **2.84 µs**（同一台机器，`make bench-async`）。
- **六语言异步编程横向基准**：`bench/async/`（c6 `async`/`await`、C `epoll`
  状态机与 `pthread`、C++20 协程、Go goroutine、Rust `async` 与 `std::thread`、
  Uya `@async_fn`/`@await`）+ `make bench-async`；三项指标（create / switch /
  conns）与 RSS 密度同口径可比。实测：c6 146 ns/任务、2836 ns/往返、369 B/
  挂起任务；C++ 22 ns、2092 ns、168 B；Uya 421 ns、2545 ns；Go 879 ns、542 ns、
  2.6 KB；pthread 64 µs、9.2 µs、8.5 KB。分析见新增的
  [docs/async-comparison.md](docs/async-comparison.md)。

### 文档

- **补齐第十一批的号段与状态**：`docs/reference/grammar.md` §8.1 的 std 号段表
  补上 `std.core.crc`=0x6035…`std.encoding.frame`=0x6038（第十一批漏更新），
  `docs/history/status.md` 补写第十一批小节；第十二批（regex/diff/fuzzy/wrap）
  的号段与状态一并写入。

- **新增 [docs/async-comparison.md](docs/async-comparison.md)**：同一件事（等一个
  fd 可读）在 c6 / C / C++20 / Go / Rust / Uya 里的写法、机制、内存密度与实测
  数据，含「哪种场景选哪种」的结论表和每条指标的口径说明。
- **标准库 API 形态要求**：`docs/stdlib.md` 的写法约定增加硬性条款——能用结构体方法
  表达的 API 必须写成方法，调用侧只写 `v.m()`/`p->m()`；自由函数只保留工厂、无接收者
  算法、一次性便捷入口和 `foreach` 的 `has`/`next`。
- **文档 IA 重构**：`README.md` 从 1996 行瘦身为落地页（特性总表 + 快速开始 + 目录结构 +
  文档地图）；「使用」章拆成 `docs/manual/` 一特性一篇（14 篇，统一「状态 → 一句话 →
  例子 → 速查 → 陷阱」模板）。
- **新增参考文档**：`docs/reference/diagnostics.md`（C1–C134 约束与诊断文本，
  从 `grammar.md` §7 抽出）、`docs/reference/cli.md`（编译器选项 + 全部 make 目标 +
  环境变量）、`docs/reference/c99-backend.md`（C99 后端，原文为英文，已中文化）、
  `docs/design/implementation-notes.md`（运行时/ABI + `grammar.md` §11 实现位置表）、
  `docs/history/status.md`（`plan.md` §11 状态表）、`docs/README.md`（三条读者路径 +
  维护约定）。
- **目录分层**：`docs/{manual,reference,design,history}/`；全仓库 `docs/...` 引用同步更新
  （含 C 源码注释），零断链。
- **去重复**：模块数 / `test` 块数 / 自检条数不再散落在 README、`stdlib.md`、
  `status.md` 多处，改为「以 `make stdtest` 输出为准」；状态数字只在 `status.md` 留快照。
- **修正过期状态**：`gc-design.md` 的「设计定稿待编码」改为已落地（P0–P8）。
- **`make help`** 从打印 20 行注释改为打印完整目标清单（37 个目标，按用途分组）。

### 语言与编译器

- **裸机引导器：payload 直接用 ATA PIO 读进 0x400000**（`examples/boot/boot.S`）。
  原先实模式用 INT 13h 把 payload 读到 0x10000 再搬过去，目标地址受「段:偏移」
  约 1 MiB 的限制；payload 超过 ~576 KiB 时还会压到 0xA0000 起的 VGA/ROM 空洞
  （写进去读出来是 0xFF）。`tests/data_zero.c6`（855 KiB payload）因此在裸机上
  3/4 失败。现在进 32 位保护模式后用 ATA PIO（主通道、28 位 LBA、轮询）一次
  一个扇区直接写进链接地址 0x400000，不再需要中转和搬运；`make fstest-bare`,
  `fstest-bare-tests`（38 通过 0 失败）、`fstest-bare-go`、`fstest-bare-handle`、
  `fstest-bare-smp` 全过，`make check-all` 绿。

- **`!error` 支持浮点返回类型**：`float` / `double` / `long double` 现在可以作
  `!error` 的返回类型（C9 原先直接拒绝）。失败路径的返回值仍然只承载成功路径、
  永远不可观测：整数类标量继续用 `-1` 哨兵，聚合类型和浮点标量改为清零（全零
  位型对 IEEE 浮点就是 `+0.0`），native 与 C99 后端、普通函数与 `async` 体走
  同一条规则。数组不是 C 的返回类型，`A!error f(void)` 仍在声明处报
  `'!error' does not support a 'int [3]' return type`。`tests/try.c6` 加了
  double / float / long double 的成功、失败与 `try` 传播用例，
  `tests/errors/try.c6` 把原来的 float 拒绝用例换成数组拒绝用例；
  `docs/manual/errors.md`、`grammar.md` R22、`diagnostics.md` C9 同步。

### 语言 / 运行时

- **协程个数与 fd 号不再有固定上限**：运行时的就绪等待从 `select(2)` 换成 `epoll(7)`
  （没有 epoll 的平台退回 `poll(2)`；`tcc/lib/libtcc1.c` 与 C99 端运行时镜像
  `tcc/c99-rt_.h`），注册集与事件数组随挂起的任务数增长，`c6_io` 只拒绝负 fd——
  `fd >= FD_SETSIZE(1024)` 的任务不再被当成失败回收；
  `std/async/aio.c6` 里同样的 1024 限制一并去掉。`tests/async.c6` 新增「同时存活 300 个
  协程」与「fd 号 2000 也能挂起」两条语言层回归。

### 标准库

- **`stdub` 支持目录递归**：命令行给目录就往下钻（`std.os.walk`），只收 `*.c6`、
  不跟随符号链接、最多 8192 个文件、深度 32；`c6 --ub DIR` 同样可用。`make ubcheck`
  因此简化成一句 `./build/stdub -W std`（369 个模块、约 6 秒），顺便当这个功能的回归。

- **跨函数分析：调用点内联（深度 3）+ 摘要兜底**：调用点把被调函数体放到当前上下文
  里再走一遍，实参的堆状态、堆块字节数、常量值都带进去，回来写回「释放了哪个实参」
  「写过哪个 `*out`」。于是两层传递的 use-after-free（`drop2 → drop1`）、被调函数里
  的常量下标越界（`take(p, 9)`，调用方知道 p 只有 4 字节）、经出参释放
  （`release(&p)` 里的 `free(*slot)`）都能追到；递归、大函数（>48 条语句）和超出
  预算时退回「无条件释放了第几个参数」的摘要。另加谓词/返回值契约
  （`if (!ok(p)) return;`、`r = peek(p); if (r == 1)` ⇒ p 非空）压低误报。
  复核：`std/` 365 个模块 + `examples/` + `tests/` 全部 0 条发现；ub 用例 82 条。

- **`c6 --ub`：编译器的 UB 检查入口**：`c6 --ub FILE...`（别名 `-fub`）不编译，直接把
  源码转给检查器 `stdub`（`--ub -W` 安全门、`--ub -S` 健全模式），参数原样透传、退出码
  带回；查找顺序 `$C6_STDUB` → `build/stdub` → `stdub`，找不到直说怎么装。检查器本身仍
  是用 c6 写的程序（`make std-ub`），编译器只做转发。

- **静态检查加固：别名 / 跨函数 / 范围 / 安全门**：`std.analysis.ub` 从「函数内常量
  判定」扩到「一层函数摘要 + 别名链 + 循环范围 + 守卫识别」——`q = p` 之后 `free(p)`
  再用 q、`drop(p)` 里释放回来再用、`for (i = 0; i < 7; i++) a[i]`、`strcpy(b, "hello")`
  塞不进 `char b[4]`、`*(a + 6)`、`free(&x)` 现在都能报（新增 `UB-BADFREE`，共 14 类）；
  位宽按类型名认（`u64`/`size_t`），说不准就不报。`examples/stdub.c6` 增 `-W/--strict`
  安全门（「可能」级与「没看懂」的构造都算失败），`Makefile` 增 `make ubcheck`：
  用严格模式把整个 `std/` 过一遍，349 个模块全通过、0 条误报。见
  [docs/ubcheck.md](docs/ubcheck.md) §4（分析能力）与 §8（安全级别与 Rust 级的差距）。
  再补两个可判定项：`q = realloc(p, n)` 之后旧指针作废（`UB-UAF`，原地 `p = realloc(p, n)`
  不算）、全局变量地址进线程入口 → `UB-RACE`（可能级，修法指向锁/原子量），共 15 类。
  运行时层落地成 `make safegate`：`ubcheck` 之外再用 `c6 -b` 跑 `tests/ub/ubsmoke.c6`
  （下标来自参数、静态判不出的越界），必须被 `-b` 当场抓住才算通过。
  报告里再补两笔「没有结论」的账：`N 处没看懂`（解析器没认出来的构造）与
  `另有 N 处访问说不准`（`ub_unproven()`：下标/解引用说不准，交给 `c6 -b` 兜）——
  两条都为 0 才算真干净，静态层的边界不再藏着。
- **`stdub -S/--sound`（健全模式）与 L4/L5 的实测边界**：`--sound` 把每处「说不准」
  的访问都报成 `UB-UNAUDITED` 并算失败，要求补边界判断或标 `/// c6ub-proven`——
  「没证明过的不许当成没问题」。实测整个 `std/`：365 个模块只有 9 个能过，
  其余 356 个共 15 380 处访问没有证明；配合 `c6 -b`（实测能抓堆/栈越界、释放后使用、
  重复释放、`memcpy` 超长，抓不到未初始化读/泄漏/竞争）。结论写进
  [docs/ubcheck.md](docs/ubcheck.md) §8：L4 靠「静态门 + `-b`」拼，L5（Rust 级）要
  语言长出所有权与借用检查，不是 lint 的开关。

- **标准库第八十四批（用 c6 分析 c6：静态分析三件套）**：`std.analysis.lex`（流式
  分词器，`Lexer` 是纯值、整结构体赋值即快照，零分配）、`std.analysis.parse`
  （务实递归下降解析器，AST 全放调用方 `Arena`，**认不出的构造只记账不中断**：
  `Program.nerr` + 前 32 处 `errl/errc`）、`std.analysis.ub`（13 类未定义行为检查，
  每条带「一定/可能」两档可信度与一个 `Fix{shape,line,text,code}` —— 修法给到
  「改哪行、怎么说、可直接粘贴的整行代码」，说不出具体改法的问题不报；支持
  `/// c6ub-ignore [UB-XXX]` 抑制），示例 `examples/stdub.c6`（`make std-ub`，
  `文件:行:列: 一定|可能 [UB-XXX] 标题` + 源码 + `修法 |` + `代码 |`，退出码 0/1/2），
  新增文档 [docs/ubcheck.md](docs/ubcheck.md)（检查清单、可信度口径、抑制写法、
  三件套 API、诚实限制清单）。`defer free(p)` 只算「排队释放」，它之后的使用不报
  use-after-free，只有与显式 `free` 撞车才算重复释放。错误号段 `std.analysis.lex`=0x615a、
  `std.analysis.parse`=0x615b、`std.analysis.ub`=0x615c。

- **标准库第八十三批（异步框架横切面）**：`std.async.abudget`（时间预算 +
  `await ayield()`/`aspin`）、`std.async.agroup`（任务组 `g.add`/`g.join`）、
  `std.async.apool`（并发额度 `p.acquire`/`p.release`）、`std.async.acancel`（协作取消 +
  `c.link` 父子联动 + `async astep`），示例 `examples/stdasync5.c6`（`make std-async5`），
  并新增文档 [docs/async-framework.md](docs/async-framework.md)（语言级异步框架整体：
  分层、启动方式、实测开销、限制清单）。错误号段 0x6156–0x6159。

- **标准库第八十二批（同步/异步 HTTPS + async 性能指标）**：`std.net.tlskdf`
  （TLS 1.3 密钥调度：`expand_label`/`derive_secret`/`traffic_key`/`traffic_iv` 与
  `Tls13` 的方法，向量对齐 RFC 8446）、`std.net.tlsrec`（TLS 1.3 记录层 +
  `TLS_CHACHA20_POLY1305_SHA256`）、`std.net.https`（**同步** HTTP/1.1(S) 客户端）、
  `std.net.httpsa`（**异步**版本，语言级 `async`/`await`/`c6_io` + `co` 批量）、
  `std.async.abench`（async 框架开销指标：`run_null`/`run_io`/`run_co` + `report`），
  示例 `examples/stdhttps.c6`（`make std-https`）。错误号段
  `std.net.tlskdf`=0x6151 … `std.async.abench`=0x6155。

- **标准库第八十一批（三维网格与材质）**：`std.data.obj`（Wavefront OBJ 游标解析：
  顶点/法向/纹理/面，负数索引）、`std.data.stl`（二进制与 ASCII 自动识别，面积/体积）、
  `std.data.ply`（ASCII PLY 头解析 + 顶点列号 + list 面）、`std.data.mtl`
  （MTL 材质块游标 + 按名字查），示例 `examples/stdmesh.c6`（`make std-mesh`）。
  错误号段 `std.data.obj`=0x614d … `std.data.mtl`=0x6150。

- **标准库第八十批（语言级 async 的库支持）**：`std.async.aio`（`await c6_io` 之上的
  `aio_wait`/`aio_read`/`aio_read_exact`/`aio_write_all`/`aio_read_line`）、
  `std.async.achan`（`AChan` + `achan_send`/`achan_recv`，长度前缀帧）、
  `std.async.art`（`Rt`：`__c6_co_wait`/`__c6_co_run` 的方法封装 + 统计）、
  `std.async.apump`（fd→fd / 限量 / 按行 / 通道→fd 的 async 转发），
  示例 `examples/stdasync4.c6`（`make std-async4`）。`async` 函数不能是方法，
  故句柄参数为 `void *`。错误号段 `std.async.aio`=0x6149 … `std.async.apump`=0x614c。

- **标准库第七十九批（计算几何）**：`std.core.hull`（Andrew 单调链凸包 + 面积/周长/
  点内）、`std.core.polygon`（简单多边形：面积/凸性/点内/形心/边游标）、
  `std.core.segment`（线段相交/共线重叠/点到线段距离/投影）、`std.core.closest`
  （分治最近点对 + 暴力对拍），集成示例 `examples/stdgeo2.c6`（`make std-geo2`）。
  错误号段 `std.core.hull`=0x6145 … `std.core.closest`=0x6148
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第七十八批（co / async-await 框架）**：新目录 `std/async/`——`std.async.co`
  （co 运行器：立即值/Future/定时器三种挂起点）、`std.async.await`（`AW_BEGIN`/`AW_SLEEP`/
  `AW_WAIT`/`AW_YIELD`/`AW_END` 宏，把状态机藏起来写线性代码）、`std.async.scope`
  （结构化并发：spawn/cancel/fail/join）、`std.async.limiter`（并发额度与额度转交），
  集成示例 `examples/stdasync3.c6`（`make std-async3`）。错误号段 `std.async.co`=0x6141 …
  `std.async.limiter`=0x6144（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第七十七批（图算法进阶）**：`std.container.mst`（Kruskal + Prim 最小生成树）、
  `std.container.flow`（Edmonds-Karp 最大流）、`std.container.scc`（Tarjan 强连通分量）、
  `std.container.match`（Kuhn 二分图最大匹配），集成示例 `examples/stdgraph2.c6`
  （`make std-graph2`）。错误号段 `std.container.mst`=0x613d … `std.container.match`=0x6140
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第七十六批（检索与关键词）**：`std.text.inverted`（倒排索引 + AND 查询）、
  `std.text.bm25`（BM25 相关度打分）、`std.text.keyword`（词频 Top-N 与停用词过滤）、
  `std.text.stem`（轻量英文词干化），集成示例 `examples/stdsearch2.c6`
  （`make std-search2`）。错误号段 `std.text.inverted`=0x6139 … `std.text.stem`=0x613c
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第七十五批（线性代数）**：`std.core.lu`（PLU 分解，反复求解/求逆/行列式）、
  `std.core.eigen`（Jacobi 特征分解 + 幂法）、`std.core.tridiag`（Thomas 三对角求解）、
  `std.core.sparse`（CSR 稀疏矩阵 SpMV/转置），集成示例 `examples/stdlinalg.c6`
  （`make std-linalg`）。错误号段 `std.core.lu`=0x6135 … `std.core.sparse`=0x6138
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第七十四批（数值方法与拟合）**：`std.core.solve`（二分/牛顿/割线求根）、
  `std.core.ode`（Euler/中点/RK4 定步长积分）、`std.core.optimize`（黄金分割/三分/
  网格求一维极小）、`std.core.least`（直线与多项式最小二乘），集成示例
  `examples/stdmath3.c6`（`make std-math3`）。错误号段 `std.core.solve`=0x6131 …
  `std.core.least`=0x6134（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第七十三批（async 之上再盖一层）**：`std.core.executor`（事件循环执行器）、
  `std.os.asio`（异步按行读）、`std.core.retry`（指数退避重试）、`std.core.awaitgroup`
  （完成计数组），集成示例 `examples/stdasync2.c6`（`make std-async2`）。错误号段
  `std.core.executor`=0x612d … `std.core.awaitgroup`=0x6130（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第七十二批（异步编程基础）**：`std.core.future`（Future/Promise 与
  all/any 组合）、`std.core.timer`（定时器小顶堆）、`std.core.task`（协作式任务调度）、
  `std.os.reactor`（poll + 定时器的事件循环），集成示例 `examples/stdasync.c6`
  （`make std-async`）。错误号段 `std.core.future`=0x6129 … `std.os.reactor`=0x612c
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第七十一批（匹配与对齐）**：`std.text.aho`（Aho-Corasick 多模式匹配）、
  `std.text.rabin`（Rabin-Karp 滚动哈希）、`std.container.interval`（闭区间集合）、
  `std.text.align`（Needleman-Wunsch 全局比对），集成示例 `examples/stdmatch.c6`
  （`make std-match`）。错误号段 `std.text.aho`=0x6125 … `std.text.align`=0x6128
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第七十批（系统配置文件）**：`std.data.dotenv`（`.env` 解析）、
  `std.data.hosts`（`/etc/hosts`）、`std.data.resolvconf`（`/etc/resolv.conf`）、
  `std.data.knownhosts`（SSH `known_hosts`），集成示例 `examples/stdcfg.c6`
  （`make std-cfg2`）。错误号段 `std.data.dotenv`=0x6121 … `std.data.knownhosts`=0x6124
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第六十九批（文本分析）**：`std.text.words`（切词游标）、
  `std.text.sentences`（切句游标）、`std.text.ngram`（字符 n-gram 池）、
  `std.text.readability`（音节估计 + Flesch 指数与年级），集成示例
  `examples/stdtextan.c6`（`make std-textan`）。错误号段 `std.text.words`=0x611d …
  `std.text.readability`=0x6120（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第六十八批（轻量标记语言）**：`std.text.rst`（reST 标题/指令/列表）、
  `std.text.org`（Org 标题层级/TODO/标签/链接）、`std.text.tex`（LaTeX 章节/环境/
  转义）、`std.text.pod`（POD 段落与格式化码剥离），集成示例
  `examples/stdmarkup.c6`（`make std-markup`）。错误号段 `std.text.rst`=0x6119 …
  `std.text.pod`=0x611c（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第六十七批（小型文本协议）**：`std.net.irc`（IRC 消息解析/构造）、
  `std.net.gopher`（Gopher 菜单游标）、`std.net.whois`（WHOIS 字段游标）、
  `std.net.ident`（RFC 1413 查询/应答），集成示例 `examples/stdnet2.c6`
  （`make std-proto3`）。错误号段 `std.net.irc`=0x6115 … `std.net.ident`=0x6118
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第六十六批（老音频格式头）**：`std.data.aiff`（FORM 块 + 80 位扩展浮点
  采样率）、`std.data.au`（Sun `.au` 编码表）、`std.data.voc`（Creative Voice 块遍历）、
  `std.data.ircam`（IRCAM `SF` 浮点采样率），集成示例 `examples/stdlegacy2.c6`
  （`make std-audio2`）。错误号段 `std.data.aiff`=0x6111 … `std.data.ircam`=0x6114
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第六十五批（字符串算法）**：`std.text.kmp`（KMP + 可重叠计数）、
  `std.text.bmh`（Boyer-Moore-Horspool）、`std.text.suffix`（后缀数组 + 二分检索 +
  Kasai LCP）、`std.text.wordplay`（回文/最长回文/倒序单词/异位词指纹），集成示例
  `examples/stdsearch.c6`（`make std-search`）。错误号段 `std.text.kmp`=0x610d …
  `std.text.wordplay`=0x6110（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第六十四批（统计小工具）**：`std.core.histogram`（等宽直方图与众数箱）、
  `std.core.moving`（SMA/EMA 滑动平均）、`std.core.series`（均值/方差/协方差/相关）、
  `std.core.quantile`（分位数、中位数、四分位距），集成示例
  `examples/stdstats2.c6`（`make std-stats`，链接带 `-lm`）。错误号段
  `std.core.histogram`=0x6109 … `std.core.quantile`=0x610c
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第六十三批（基础容器补充）**：`std.container.stack`（i64 栈）、
  `std.container.queue`（环形 FIFO）、`std.container.slots`（位图槽位分配器）、
  `std.container.bag`（i64 多重集合，墓碑删除），集成示例
  `examples/stdcontainers.c6`（`make std-cont`）。错误号段
  `std.container.stack`=0x6105 … `std.container.bag`=0x6108
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第六十二批（颜色与单位）**：`std.core.color`（RGBA 与 HSL/HSV、hex 解析
  生成、混合、亮度与对比度）、`std.data.palette`（GIMP `.gpl` 与 hex 列表）、
  `std.text.colorname`（40 个 CSS 命名颜色 + 最近匹配）、`std.core.units`（长度/
  质量/温度/字节换算），集成示例 `examples/stdcolor.c6`（`make std-color`）。
  错误号段 `std.core.color`=0x6101 … `std.core.units`=0x6104
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第六十一批（文本清洗与标注）**：`std.text.sanitize`（四项开关的清洗器）、
  `std.text.tabs`（制表符展开/还原）、`std.text.quote`（shell 引用与去引用）、
  `std.text.nato`（NATO/ICAO 拼读与解码），集成示例 `examples/stdclean.c6`
  （`make std-clean`）。错误号段 `std.text.sanitize`=0x60fd … `std.text.nato`=0x6100
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第六十批（二进制与数值工具）**：`std.core.binio`（LE/BE 整数读写游标）、
  `std.core.bitio`（按位读写，MSB-first）、`std.core.half`（fp16 与 bfloat16 转换）、
  `std.core.align`（对齐/2 的幂/按倍数取整），集成示例 `examples/stdbin2.c6`
  （`make std-bin2`，链接带 `-lm`）。错误号段 `std.core.binio`=0x60f9 …
  `std.core.align`=0x60fc（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第五十九批（Linux 配置文本）**：`std.os.mounts`（/proc/mounts + 八进制
  反转义）、`std.os.fstab`（4~6 字段与默认值）、`std.os.osrelease`（键值与引号剥离）、
  `std.os.systemd`（unit 文件分段与取值），集成示例 `examples/stdlinux.c6`
  （`make std-linux`）。错误号段 `std.os.mounts`=0x60f5 … `std.os.systemd`=0x60f8
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第五十八批（校验与纠错）**：`std.encoding.luhn`（Luhn/ISBN-10/ISBN-13/
  Verhoeff 校验位）、`std.encoding.crc16`（CRC-8 与四种 CRC-16，流式方法）、
  `std.encoding.hamming`（汉明 (7,4) 编解码与纠 1 位错）、`std.encoding.classic`
  （凯撒/ROT13/Atbash/维吉尼亚/异或/栅栏），集成示例 `examples/stdcheck.c6`
  （`make std-check`）。错误号段 `std.encoding.luhn`=0x60f1 … `std.encoding.classic`=0x60f4
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第五十七批（目标文件与包）**：`std.data.ar`（Unix 归档，含 GNU/BSD 长名）、
  `std.data.coff`（MS COFF 头/节/符号表）、`std.data.class`（Java class 常量池与
  方法表）、`std.data.rpm`（lead + 主头索引表），集成示例 `examples/stdpkg.c6`
  （`make std-pkg`）。四个模块都提供游标方法。错误号段 `std.data.ar`=0x60ed …
  `std.data.rpm`=0x60f0（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第五十六批（地理编码）**：`std.core.geo`（haversine 距离、方位、目的地、
  半径包围盒）、`std.data.geohash`（编码/解码/边界/邻居）、`std.data.maidenhead`
  （2/4/6/8 位网格）、`std.data.dms`（度分秒解析与格式化，带 `to_deg`/`from_deg`/
  `format` 方法），集成示例 `examples/stdgeo.c6`（`make std-geo`，链接带 `-lm`）。
  错误号段 `std.core.geo`=0x60e9 … `std.data.dms`=0x60ec（`c6_err_std_slots[]`
  追加，`grammar.md` §8.1 同步）。

- **标准库第五十五批（凭据与配置）**：`std.data.netrc`（`.netrc` 凭据 + `find` 兜底）、
  `std.data.sshconfig`（`Host` 分块、`*`/`?` 与 `!` 匹配、块内取键）、
  `std.data.keys`（`authorized_keys` 行，含引号选项）、`std.data.crontab`（定时行 +
  `@reboot` 别名 + 环境变量双游标），集成示例 `examples/stdcreds.c6`
  （`make std-creds`）。四个模块都提供游标方法。错误号段 `std.data.netrc`=0x60e5 …
  `std.data.crontab`=0x60e8（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第五十四批（字幕与播放列表）**：`std.data.srt`（SubRip + WebVTT）、
  `std.data.ass`（ASS/SSA 脚本信息与事件）、`std.data.lrc`（LRC 歌词与元信息）、
  `std.data.m3u`（M3U/M3U8，含 `#EXTINF` 与 HLS 判断），集成示例
  `examples/stdsubs.c6`（`make std-subs`）。四个模块都提供**游标方法**
  （`doc.open`/`doc.next`/`doc.count`/`doc.seek`/`doc.rewind`）。错误号段
  `std.data.srt`=0x60e1 … `std.data.m3u`=0x60e4（`c6_err_std_slots[]` 追加）。

- **标准库：状态型模块统一方法化**：`std.container.fenwick`、`segtree`、`dsu`、
  `bloom`、`treap`、`heap`、`skiplist`、`counter` 与 `std.os.log` 的函数改为裸方法名
  （`f.add`、`t.put`、`h.push`、`lg.dump` 等），`test` 块与
  `examples/stdstruct.c6`、`examples/stdordered.c6`、`examples/stddb.c6` 一并改为
  方法调用；顺带解决字段与同名方法的冲突（`Dsu.nsets`、`Bloom.set_bits`、
  `Log.nfilt/ndrop`、`Counter.sum`）。

- **标准库第五十三批（变换式压缩积木）**：`std.encoding.bwt`（Burrows-Wheeler 双向
  变换）、`std.encoding.mtf`（Move-To-Front）、`std.encoding.delta`（字节与 i64
  差分）、`std.encoding.huffman`（规范 Huffman 建表/编码/解码），集成示例
  `examples/stdcmp.c6`（`make std-bwt`）。错误号段 `std.encoding.bwt`=0x60dd …
  `std.encoding.huffman`=0x60e0（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第五十二批（有序容器）**：`std.container.treap`（随机化 BST + 排名/第 k
  小）、`std.container.heap`（二叉堆，含 pushpop 与原地堆化）、
  `std.container.skiplist`（8 层跳表 + 有序遍历）、`std.container.counter`（字符串
  频次计数），集成示例 `examples/stdordered.c6`（`make std-ordered`）。错误号段
  `std.container.treap`=0x60d9 … `std.container.counter`=0x60dc
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第五十一批（非加密散列补齐）**：`std.encoding.fnv`（FNV-1/1a 32/64 位 +
  流式）、`std.encoding.adler32`（流式 + 合并）、`std.encoding.murmur3`
  （32 位与 x64 128 位）、`std.encoding.siphash`（2-4 / 1-3，带官方向量），集成
  示例 `examples/stdhash2.c6`（`make std-hash2`）。错误号段 `std.encoding.fnv`=0x60d5 …
  `std.encoding.siphash`=0x60d8（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第五十批（日志与数据库）**：`std.os.log`（定长环形日志，级别过滤）、
  `std.data.sqlite`（文件头 + B-tree 页 + 单元 + 变长整数）、`std.net.postgres`
  （v3 报文、启动包、错误字段）、`std.net.mysql`（报文拆帧、HandshakeV10、
  长度编码整数、COM_QUERY），集成示例 `examples/stddb.c6`（`make std-db`）。
  错误号段 `std.os.log`=0x60d1 … `std.net.mysql`=0x60d4（`c6_err_std_slots[]`
  追加，`grammar.md` §8.1 同步）。

- **标准库第四十九批（网络报文）**：`std.net.ntp`（NTPv4 报文与时间戳换算）、
  `std.net.syslog`（RFC 5424 / 3164 解析与构造）、`std.net.stun`（STUN 头/属性/
  XOR-MAPPED-ADDRESS）、`std.net.tftp`（RRQ/WRQ/DATA/ACK/ERROR/OACK），集成示例
  `examples/stdpacket.c6`（`make std-packet`）。错误号段 `std.net.ntp`=0x60cd …
  `std.net.tftp`=0x60d0（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第四十八批（时间工具）**：`std.os.calendar`（历法换算 / 星期 / ISO 周）、
  `std.os.iso8601`（解析与格式化，带偏移换算）、`std.os.duration`（时长串解析与
  格式化）、`std.os.cron`（五字段表达式 + 下次运行时间），集成示例
  `examples/stdtime.c6`（`make std-time`）。错误号段 `std.os.calendar`=0x60c9 …
  `std.os.cron`=0x60cc（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第四十七批（数值工具）**：`std.core.vector`（Vec2/Vec3 运算）、
  `std.core.polynomial`（求值/求导/加减乘/二次求根）、`std.core.fft`（基 2 迭代
  FFT，正逆变换）、`std.core.spline`（贝塞尔/Hermite/Catmull-Rom），集成示例
  `examples/stdnumeric.c6`（`make std-math2`，链接带 `-lm`）。错误号段
  `std.core.vector`=0x60c5 … `std.core.spline`=0x60c8（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第四十六批（文本变换）**：`std.text.slug`（URL slug，UTF-8 + 去音标）、
  `std.text.casing`（snake/kebab/camel/Pascal/title 互转）、`std.text.roman`
  （罗马数字往返 + 标准形式校验）、`std.text.phonetic`（Soundex + 简化 Metaphone），
  集成示例 `examples/stdwords.c6`（`make std-words`）。错误号段
  `std.text.slug`=0x60c1 … `std.text.phonetic`=0x60c4（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第四十五批（基础数据结构）**：`std.container.fenwick`（树状数组 + 前缀
  定位）、`std.container.segtree`（迭代线段树，求和/最值）、`std.container.dsu`
  （并查集，路径减半 + 按大小合并）、`std.container.bloom`（布隆过滤器），集成
  示例 `examples/stdstruct.c6`（`make std-ds`）。错误号段
  `std.container.fenwick`=0x60bd … `std.container.bloom`=0x60c0
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第四十四批（文本行协议）**：`std.net.smtp`（应答/命令/DATA 点填充）、
  `std.net.ftp`（应答/PASV/LIST/MLSD）、`std.net.redis`（RESP 编解码）、
  `std.net.telnet`（IAC 协商与去转义），集成示例 `examples/stdproto2.c6`
  （`make std-proto2`）。错误号段 `std.net.smtp`=0x60b9 … `std.net.telnet`=0x60bc
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第四十三批（音频容器）**：`std.data.mp3`（ID3v2/v1 + MPEG 帧头）、
  `std.data.ogg`（页头 + Vorbis/Opus/Speex/FLAC 识别）、`std.data.flac`（STREAMINFO
  与 VORBIS_COMMENT）、`std.data.midi`（MThd/MTrk + 变长量事件），集成示例
  `examples/stdaudio.c6`（`make std-audio`）。错误号段 `std.data.mp3`=0x60b5 …
  `std.data.midi`=0x60b8（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第四十二批（老式文本编码）**：`std.encoding.base91`（basE91）、
  `std.encoding.uu`（uuencode/uudecode）、`std.encoding.yenc`（yEnc 含 `=ybegin`
  头部字段）、`std.encoding.ebcdic`（CP037 双向表），集成示例
  `examples/stdtranscode.c6`（`make std-transcode`）。错误号段
  `std.encoding.base91`=0x60b1 … `std.encoding.ebcdic`=0x60b4
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第四十一批（分区与文件系统）**：`std.data.mbr`（MBR + GPT）、
  `std.data.fat`（FAT12/16/32 BPB 与目录项）、`std.data.iso`（ISO 9660 主卷与
  目录记录）、`std.data.ext2`（超级块 + 块组描述符 + inode），集成示例
  `examples/stddisk.c6`（`make std-disk`）。错误号段 `std.data.mbr`=0x60ad …
  `std.data.ext2`=0x60b0（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第四十批（字体与矢量）**：`std.data.bdf`（BDF 位图字体）、
  `std.data.woff`（WOFF 1.0 容器）、`std.data.svg`（路径规范化 + 包围盒）、
  `std.data.ttf`（表目录 + cmap/hmtx 度量），集成示例 `examples/stdfont.c6`
  （`make std-font`，链接带 `-lm`）。错误号段 `std.data.bdf`=0x60a9 …
  `std.data.ttf`=0x60ac（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第三十九批（老图形格式）**：`std.data.pcx`（RLE + 调色板/24 位/1 位）、
  `std.data.hdr`（Radiance RGBE，新老两种扫描线，含 `hdr_to_rgb`）、
  `std.data.xbm`（C 源码位图）、`std.data.xpm`（C 源码彩图 + 透明键），集成示例
  `examples/stdlegacy.c6`（`make std-legacy`）。错误号段 `std.data.pcx`=0x60a5 …
  `std.data.xpm`=0x60a8（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第三十八批（哈希与校验）**：`std.encoding.sha3`（SHA3-224/256/384/512 +
  SHAKE128/256）、`std.encoding.blake2s`（输出 1~32 字节）、`std.encoding.crc32c`
  （CRC-32C，含分段接口）、`std.encoding.xxhash`（XXH32/XXH64，一次性 + 流式），
  集成示例 `examples/stdhash.c6`（`make std-hash`）。错误号段
  `std.encoding.sha3`=0x60a1 … `std.encoding.xxhash`=0x60a4（`c6_err_std_slots[]`
  追加，`grammar.md` §8.1 同步）。

- **标准库第三十七批（系统视图）**：`std.os.sysinfo` / `std.os.resource` /
  `std.os.user` / `std.os.pty` 四个 libc 薄封装模块，集成示例
  `examples/stdsys.c6`（`make std-sys`）。sysinfo 给 uname/运行时长/负载/CPU 数/
  页大小；resource 给 rlimit 与 getrusage（毫秒 CPU 时间、峰值 RSS、缺页、上下文
  切换）；user 给 passwd/group 查询（查不到 0、出错 -1）；pty 给伪终端一对 +
  fork 子进程 + 带超时的收发 + 窗口大小。错误号段 `std.os.sysinfo`=0x609d …
  `std.os.pty`=0x60a0（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第三十六批（序列化与标记）**：`std.data.bson` / `std.data.ubjson` /
  `std.data.plist` / `std.text.markdown` 四个零分配模块，集成示例
  `examples/stdser.c6`（`make std-ser`）。BSON 逐元素切视图并做 int32 符号扩展；
  UBJSON 走事件流 + 容器栈，支持 `$`/`#` 优化容器；plist 解 `bplist00` 的偏移表
  与引用号（支持 null/bool/int/real/date/data/ASCII/UTF-16/array/dict/uid）；
  markdown 把常用子集渲染成 HTML（标题/段落/围栏代码/引用/列表/分隔线 +
  行内代码、粗斜体、链接、自动链接）。错误号段 `std.data.bson`=0x6099 …
  `std.text.markdown`=0x609c（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第三十五批（二进制容器格式）**：`std.data.cpio` / `std.data.pe` /
  `std.data.macho` / `std.data.wasm` 四个零分配结构解析模块，集成示例
  `examples/stdexec.c6`（`make std-exec`）。cpio 认 newc/crc；pe 走 DOS 头到节表
  并做 RVA→文件偏移映射（PE32/PE32+ 分开处理）；macho 认 thin 32/64 位与 fat
  外壳，抓 `LC_UUID`/`LC_MAIN`；wasm 逐节遍历并解导出表。错误号段
  `std.data.cpio`=0x6095 … `std.data.wasm`=0x6098（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第三十四批（网络协议编解码）**：`std.net.sse` / `std.net.socks5` /
  `std.net.mqtt` / `std.encoding.snappy` 四个零分配字节层编解码模块，集成示例
  `examples/stdproto.c6`（`make std-proto`）。sse 按空行派发事件、多行 data 拼进
  调用方缓冲；socks5 构建问候/认证/请求并解析应答（IPv4/IPv6/域名）；
  mqtt 处理 3.1.1 的固定头与变长剩余长度、CONNECT/PUBLISH/SUBSCRIBE 与主题
  通配；snappy 实现块格式编解码（重叠复制、哈希表贪心匹配）。错误号段
  `std.net.sse`=0x6091 … `std.encoding.snappy`=0x6094（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第三十三批（结构化文本格式）**：`std.data.html` / `std.data.yaml` /
  `std.data.vcf` / `std.data.ics` 四个零分配读取器，集成示例
  `examples/stddoc.c6`（`make std-doc`）。html 是六种 token 的分词器（属性按需
  扫、`script` 走原文、实体解码）；yaml 是扁平事件 + `depth`，支持注释与引号
  但不管流式与块标量；vcf/ics 都是「先摊平折行，再逐行读」。错误号段
  `std.data.html`=0x608d … `std.data.ics`=0x6090（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第三十二批（图像与 LZW）**：`std.encoding.lzw` / `std.data.gif` /
  `std.data.tiff` / `std.data.ico` 四个零分配模块，集成示例
  `examples/stdgfx.c6`（`make std-gfx`）。lzw 是 GIF 变体（LSB 打包、clear/eoi、
  码宽 2..12）；gif 支持全局/局部调色板、透明色、隔行扫描与多帧；tiff 支持
  II/MM 字节序、STRIP、8 位、none/Deflate(zlib)/PackBits 压缩与 RGB/灰度；
  ico 支持 DIB（1/4/8/24/32 位）与内嵌 PNG 负载。顺带给 `std.data.png` 补了
  像素解码 `png_decode`（颜色类型 0/2/3/4/6、五种滤波、tRNS）。错误号段
  `std.encoding.lzw`=0x6089 … `std.data.ico`=0x608c（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第三十一批（编码与凭据）**：`std.encoding.asn1` / `std.data.x509` /
  `std.data.jwt` / `std.encoding.otp` 四个零分配模块，集成示例
  `examples/stdenc.c6`（`make std-enc`）。asn1 是 DER 读取器（多字节 tag、长
  长度、INTEGER/OID/BIT STRING/时间/字符串解码）；x509 在它上面解析 v3 证书
  （版本/序列号/签名 OID/issuer·subject 的 CN·O·C/有效期/公钥位数/签名视图，
  不验签）；jwt 验 HS256/HS384/HS512 紧凑令牌（常量时间比较，`alg=none` 与非
  HMAC 拒绝）；otp 给 RFC 4226 的 HOTP 与 RFC 6238 的 TOTP（带时间窗口校验）。
  `std.encoding.sha512` 顺带补 `hmac_sha384`。错误号段 `std.encoding.asn1`=0x6085
  … `std.encoding.otp`=0x6088（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第三十批（图像解码）**：`std.data.bmp` / `std.data.tga` /
  `std.data.qoi` / `std.data.pnm` 四个模块，集成示例 `examples/stdimg.c6`
  （`make std-img`）。四个解码器都输出 RGBA8888、零分配：bmp 支持 24/32 位
  真彩与 8 位调色板、上下行序与 4 字节行对齐；tga 支持类型 2/10（未压缩/
  RLE）与四种原点；qoi 既能解码全套操作码（INDEX/DIFF/LUMA/RUN/RGB/RGBA）
  也能编码（RUN/INDEX/RGB/RGBA）；pnm 支持 P5/P6、注释、maxval 1..65535
  与 8/16 位大端样本缩放。错误号段 `std.data.bmp`=0x6081 … `std.data.pnm`=0x6084
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第二十九批（二进制文件格式）**：`std.data.wav` / `std.data.png` /
  `std.data.elf` / `std.data.zip` 四个只读解析模块，集成示例
  `examples/stdbin.c6`（`make std-bin`）。wav 解析 RIFF/WAVE 的 `fmt `/`data`
  块（跳过未知块、奇数补齐、fmt 必须在 data 前）；png 遍历签名 + 块并校验
  IEEE CRC-32（`png_parse` 直接给 IHDR 的宽/高/位深/颜色类型）；elf 支持
  ELF32/64 与小端/大端，读头 + 节表并从 `shstrndx` 字符串表取节名；zip 从
  EOCD 找中央目录、按本地头取数据，stored 直接拷贝、deflate 走
  `std.encoding.deflate`，并校验 CRC-32 与大小。四个模块零分配、只读不拷贝。
  错误号段 `std.data.wav`=0x607d … `std.data.zip`=0x6080（`c6_err_std_slots[]`
  追加，`grammar.md` §8.1 同步）。

- **标准库第二十八批（压缩与归档）**：`std.encoding.deflate` /
  `std.encoding.zlib` / `std.encoding.gzip` / `std.encoding.tar` 四个模块，
  集成示例 `examples/stdarch.c6`（`make std-arch`）。deflate 的 `inflate` 完整
  支持 stored/固定 Huffman/动态 Huffman 三种块（回溯直接读输出缓冲），压缩侧
  给 `deflate_stored` 与「贪心 LZ77 + 固定 Huffman」的 `deflate_fixed`；zlib
  是 `78 01` 头 + DEFLATE + Adler-32（`adler32`/`adler32_update`）；gzip 是
  10 字节头 + DEFLATE + CRC-32/ISIZE（`gzip_crc32` 走 `std.core.crc`，
  `gzip_decompress` 会跳过 FEXTRA/FNAME/FCOMMENT/FHCRC 并校验页脚）；tar 是
  ustar 的读写（checksum、prefix/name 拆分、512 对齐、结尾两个零块）。四个
  模块都是零分配、调用方给缓冲。错误号段 `std.encoding.deflate`=0x6079 …
  `std.encoding.tar`=0x607c（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第二十七批（数值工具）**：`std.core.geometry` / `std.core.interp` /
  `std.core.stats` / `std.core.matrix` 四个模块，集成示例 `examples/stdcalc.c6`
  （`make std-calc`）。geometry 是 Vec2/Vec3/Rect（自由函数、纯值、牛顿迭代
  开方不链 libm）；interp 是 lerp/inv_lerp/remap/smoothstep 与 11 条缓动曲线
  （端点严格 0→1，sin 用 15 阶泰勒）加一维 Catmull-Rom；stats 是 Welford 流式
  均值/方差/最值，加中位数/分位数（就地排序 + 线性插值）/直方图/Pearson 相关；
  matrix 是行主序矩阵（`mat_strided` 支持子矩阵视图）与由调用方给 scratch 的
  行列式、线性方程组。四个模块都是纯函数、零分配、自成一体。错误号段
  `std.core.stats`=0x6075 … `std.core.interp`=0x6078（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第二十六批（数据格式）**：`std.data.bencode` / `std.data.msgpack` /
  `std.data.cbor` / `std.data.pem` 四个模块，集成示例 `examples/stddata.c6`
  （`make std-data`）。bencode 是 BitTorrent 的元信息格式（写整数/字节串/
  列表/字典，读端给标记流，拒绝前导零与溢出）；msgpack 与 cbor 是两种紧凑
  二进制（写端覆盖 nil/bool/整数/浮点/字符串/二进制/数组/映射，读端给
  `MpTag`/`CborTag`；msgpack 拒绝扩展类型与保留码，cbor 拒绝不定长、标签与
  `f7`/`f8`，半/单精度都转 f64）；pem 是 RFC 7468 的 BEGIN/END + Base64 封装
  （`pem_encode` 每行 64 列、`pem_reader` 多块迭代、`decode_der` 走
  `std.encoding.base64`）。四个模块都是纯函数、零分配、调用方给缓冲。错误号段
  `std.data.msgpack`=0x6071 … `std.data.pem`=0x6074（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第二十五批（对称加密基础）**：`std.encoding.aes` /
  `std.encoding.chacha20` / `std.encoding.poly1305` / `std.encoding.random`
  四个模块，集成示例 `examples/stdcipher.c6`（`make std-cipher`）。aes 是
  FIPS 197 的分组密码（`aes_key` 支持 128/192/256 位、10/12/14 轮，
  `encrypt_block`/`decrypt_block`/`ctr_xor`，S 盒与逆 S 盒由 GF(2^8) 生成）；
  chacha20 是 RFC 8439 的流密码（`chacha20_block`/`chacha20_xor`，12 字节
  nonce、32 位计数器）；poly1305 是一次性 MAC（5×26 位肢体做 130 位模运算、
  u64 乘积、无 128 位整数）；random 从 `/dev/urandom` 取安全随机
  （`rand_bytes`/`rand_u64`/`rand_below` 用拒绝采样避免取模偏置/`rand_hex`）。
  三者都只给原语：CTR/流密码未认证，组合方式（encrypt-then-MAC）写在文档与
  示例里。错误号段 `std.encoding.aes`=0x606d … `std.encoding.random`=0x6070
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第二十四批（文件系统进阶）**：`std.os.walk` / `std.os.temp` /
  `std.os.lock` / `std.os.mmap` 四个模块，集成示例 `examples/stdfsx.c6`
  （`make std-fsx`）。walk 把 `std.os.fs` 的单层 `Dir` 包成整棵树
  （`walk`/`walk_stat` 带深度/符号链接/目录回调选项与文件/目录/字节计数，
  回调返回 0 跳过子树，`remove_tree` 后序删除）；temp 是 `mkstemp`/`mkdtemp`
  的安全封装（`temp_file`/`temp_file_named`/`temp_dir`，0600 + `FD_CLOEXEC`，
  不自动删除）；lock 是 `fcntl` 建议锁（`acquire` 的阻塞/非阻塞/超时三态、
  同一进程两个句柄不冲突、跨进程才互斥）；mmap 做文件与匿名映射
  （`map_file` 可写走 `MAP_SHARED`、`map_anon` 清零、`sync`/`read_at`/`write_at`
  带边界检查、`unmap` 可重复）。错误号段 `std.os.walk`=0x6069 …
  `std.os.mmap`=0x606c（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第二十三批（消息摘要与密钥派生）**：`std.encoding.md5` /
  `std.encoding.sha512` / `std.encoding.hkdf` / `std.encoding.pbkdf2` 四个模块，
  集成示例 `examples/stdcrypto.c6`（`make std-crypto`）。md5 是兼容用的校验和
  （`Md5` 的 `init`/`update`/`final`、`md5`/`md5_hex`/`md5_eq`，RFC 1321 的 T 表，
  明确标注不安全）；sha512 补 SHA-512/SHA-384 与 HMAC-SHA-512（128 位长度计数、
  `init384` 换 IV、`hmac_sha512` 的 128 字节块）；hkdf 在 HMAC-SHA-256 上做
  `extract`/`expand`（RFC 5869，空 salt 按 32 个零字节、expand 上限 8160 字节）；
  pbkdf2 是带迭代的口令 KDF（`pbkdf2_sha1`/`pbkdf2_sha256`，RFC 6070/8018，
  `iters <= 0` 直接拒绝）。错误号段 `std.encoding.md5`=0x6065 …
  `std.encoding.pbkdf2`=0x6068（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第二十二批（Web 服务端）**：`std.net.httpd` / `std.net.router` /
  `std.net.assets` / `std.net.session` 四个模块，集成示例
  `examples/stdserve.c6`（`make std-serve`）。httpd 把 `std.net.http` 的解析和
  `std.net.tcp` 的收发接成服务端骨架（`httpd_listen`/`accept_conn`、`Conn` 的
  `read_request` 三态与 `begin`/`send_body`/`respond`/`respond_text`、
  `Server.serve_once` 的处理器回调；一次连接一个请求，chunked 请求直接 400）；
  router 是固定 32 条的路由表（`:name` 段参数、`*`/`*name` 尾段通配、方法
  大小写不敏感、`"*"` 任意方法、先注册先赢、按 `/` 严格切段）；assets 把一个
  root 目录当静态资源发（百分号解码后拒绝 `..`/NUL、归一后必须仍在 root 下、
  默认 `lstat` 拒符号链接、8 KiB 流式写出、Content-Type 走 `std.net.mime`）；
  session 是 16 槽的内存会话表（`create`/`find`/`destroy`/`expire`、
  `key=value\n` 数据行、`sid` cookie、id 来自 `std.core.rand` 且明确非密码学）。
  错误号段 `std.net.httpd`=0x6061 … `std.net.session`=0x6064（`c6_err_std_slots[]`
  追加，`grammar.md` §8.1 同步）。

- **标准库第二十一批（网络客户端）**：`std.net.udp` / `std.net.uds` /
  `std.net.resolver` / `std.net.httpc` 四个模块，集成示例
  `examples/stdclient.c6`（`make std-client`）。udp 是数据报薄封装
  （`udp_bind`/`udp_connect`、`send_to`/`send_bytes`/`recv_bytes`/`recv_from`、
  `set_timeout`/`set_broadcast`，超时 -1、空报文 0、缓冲区小就截断）；uds 是
  AF_UNIX 本地套接字（`uds_pair`、`uds_listen`/`uds_connect`/`accept_client`、
  `shutdown_write` 之后对端读到 EOF=0，路径 socket 的 `unlink` 交给调用方）；
  resolver 把 `std.net.dns` 的报文层和 `std.net.udp` 的传输层接起来
  （`resolver_open` 读 `/etc/resolv.conf`、`resolver_from_server`、`resolve_a`/
  `resolve_aaaa`/`resolve_ip`、`parse_reply` 校验 id/TC/rcode 并提取记录，
  `resolver_conf_parse`/`resolver_conf_file` 单独可测，`ipv4_format`/
  `ipv6_format`）；httpc 在 `std.net.tcp`+`std.net.url` 上做 HTTP/1.1 客户端
  （`build_request` 先量再写、`fetch` 一次到位、`parse_response` 的 1/0/-1 三态、
  chunked 解码含扩展与 trailer、`Response.header`/`header_int` 大小写不敏感，
  只做明文 `http://`）。错误号段 `std.net.udp`=0x605d … `std.net.httpc`=0x6060
  （`tcc/tccgen.c` 的 `c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。
  另在 `std/core/err.c6` 里加 `#undef NO_DATA`：glibc 的 `<netdb.h>` 把这个
  名字定义成宏，会污染 `enum Error` 的成员名（import 是在导入方的宏环境里展开
  的）。

- **标准库第二十批（终端与进程控制）**：`std.os.tty` / `std.os.signal` /
  `std.os.pipe` / `std.os.poll` 四个模块，集成示例 `examples/stdterm.c6`
  （`make std-term`）。tty 是 termios 薄封装（`is_tty`、`size`/`width`/`height`、
  `TtyMode` 的 `raw`/`restore`/`is_active`，失败不动输出参数）；signal 包
  `sigaction`/`sigprocmask`/`raise`/`kill`（`install` 带 `SA_RESTART`、`ignore`/
  `reset`、`block`/`unblock`/`is_blocked`/`is_pending`、`send_self`/`send_to`、
  `signal_name`/`signal_number`，名字统一成动词以避开 libc 同名函数）；pipe 是
  匿名管道的 `Pipe`（`init`/`init_nonblock`、`read_bytes` 的 0=EOF、`read_exact`、
  `write_bytes` 返回实际字节数、`close_read`/`close_write`/`close_all`、
  `would_block()`，读写都重试 `EINTR`、fd 带 `FD_CLOEXEC`）；poll 把 `poll(2)`
  包成 32 槽零分配的 `PollSet`（`add` 同 fd 改事件、`wait` 返回就绪个数、
  `is_ready`/`ready`/`fd_at`）与单 fd 的 `wait_readable`/`wait_writable`。
  错误号段 `std.os.tty`=0x6059 … `std.os.poll`=0x605c（`c6_err_std_slots[]` 追加）。

- **标准库第十九批（并发与同步）**：`std.core.atomic` / `std.os.sync` /
  `std.os.thread` / `std.container.chan` 四个模块，集成示例
  `examples/stdthread.c6`（`make std-thread`）。atomic 是 `Atomic`/`Atomic32`
  的原子读写与读改写（默认 seq_cst，另有 `load_relaxed`/`store_release`
  发布口）、屏障与 TAS 自旋锁 `SpinLock`，走编译器的 `__atomic_*` 内建、
  不依赖 pthread；sync 是 pthread 薄封装的值类型 `Mutex`/`Cond`/`RWLock`/
  `Once`/`Semaphore`/`Barrier`（`init`/`destroy` 方法、`try_*`、限时等待）；
  thread 是线程身份/`cpu_count`/让步/改名与固定 worker 的 `ThreadPool`
  （任务队列由调用方给，`submit` 满时背压、`wait` 等空闲、`stop` join）；
  chan 是建在 Mutex/Cond 上的有界通道（元素按字节块拷贝，阻塞收发 +
  `try_*`/限时，`close_chan` 收尾）。错误号段 `std.core.atomic`=0x6055 …
  `std.container.chan`=0x6058（`tcc/tccgen.c` 的 `c6_err_std_slots[]` 追加）。

- **标准库第十八批（算法与容器）**：`std.container.graph` / `std.container.lru` /
  `std.container.trie` / `std.container.region` 四个模块，集成示例
  `examples/stdalgo.c6`（`make std-algo`）。graph 是前向星表示的图算法（BFS 无权
  最短路、迭代 DFS、Kahn 拓扑排序、并查集连通分量、非负权 Dijkstra + 前驱链）；
  lru 是链式哈希 + 双向链表的 O(1) 缓存（命中/未命中/淘汰统计）；trie 是任意
  字节键的前缀树（节点池由调用方给，池满不写坏结构）；region 是自动合并的
  半开区间集合（包含/重叠/找空隙，带溢出保护）。四个模块都零分配，错误号段
  `std.container.graph`=0x6051 … `std.container.region`=0x6054（`tcc/tccgen.c` 的
  `c6_err_std_slots[]` 追加）。

- **标准库第十七批（网络协议基础）**：`std.encoding.sha1` / `std.net.ws` /
  `std.net.dns` / `std.net.tcp` 四个模块，集成示例 `examples/stdwire.c6`
  （`make std-wire`）。sha1 是流式 + 一次性的 SHA-1 / HMAC-SHA1（含 RFC 2202
  与官方 100 万字节向量，文档写明只用于协议互操作）；ws 是 RFC 6455 的握手
  accept-key 与帧编解码（7/16/64 位长度、客户端掩码、区分「还不够」与「坏帧」）；
  dns 是 RFC 1035 的报文解析/查询构建（压缩指针防环、A/AAAA/CNAME、`*next`
  遍历）；tcp 是 POSIX 套接字薄封装（getaddrinfo、内核挑端口、poll 超时连接、
  收发/超时/半关闭，方法名避开 libc 的 send/recv/close/accept）。错误号段
  `std.encoding.sha1`=0x604d … `std.net.tcp`=0x6050（`tcc/tccgen.c` 的
  `c6_err_std_slots[]` 追加）。

- **标准库第十六批（命令行工具基础）**：`std.os.args` / `std.text.table` /
  `std.text.natural` / `std.os.proc` 四个模块，集成示例 `examples/stdcli.c6`
  （`make std-cli`）。args 是按规格表迭代的选项解析（长/短名、带值/开关、
  `--name=value`、`--` 终止、位置参数任意穿插，未知选项/缺值报错）；table 是按
  显示列宽对齐的表格（CJK 2 列、边框/竖线/内边距可关、左/右/居中）；natural 是
  自然排序（`file2 < file10`）与人类可读化（KiB/MiB、`1h 2m 3s`、千分位，且能
  反向解析）；proc 是 POSIX 子进程封装（fork+execvp+waitpid、退出码/信号、
  `/bin/sh -c`、PATH 查找）。错误号段 `std.os.args`=0x6049 … `std.os.proc`=0x604c
  （`tcc/tccgen.c` 的 `c6_err_std_slots[]` 追加）。

- **标准库第十五批（配置与文档格式）**：`std.data.toml` / `std.data.xml` /
  `std.data.query` / `std.text.template` 四个模块，集成示例
  `examples/stdconf.c6`（`make std-conf`）。toml 是事件式的 TOML 子集读取器
  （表/表数组、注释、基本与字面量字符串、十进制/0x/0o/0b 整数、浮点、布尔、
  跨行数组；语法错误给行号且能继续读）；xml 是零分配扫描器（START/END/SELF/TEXT、
  按需解析属性、CDATA 当文本、跳过注释/PI/DOCTYPE、32 层标签配对校验、实体解码）；
  query 是 form-urlencoded 的解析与构建（`+` 表空格、只按第一个 `=` 切、unreserved
  编码）；template 是 `{{name}}` 模板（`{{{{` 转义、缺变量严格/宽松两种模式、
  去重收集变量名）。错误号段 `std.data.toml`=0x6045 … `std.text.template`=0x6048
  （`tcc/tccgen.c` 的 `c6_err_std_slots[]` 追加）。

- **标准库第十四批（压缩与文本编码）**：`std.encoding.lz4` / `std.encoding.rle` /
  `std.encoding.base85` / `std.encoding.hexdump` 四个模块，集成示例
  `examples/stdpack.c6`（`make std-pack`）。lz4 是标准 LZ4 块格式的零分配实现
  （哈希表与输出缓冲都由调用方给；手写标准向量 + 独立参考实现双向交叉验证）；
  rle 是 PackBits 游程与一阶差分（模 256，缓变采样差分后压到 1/40）；
  base85 是 Ascii85（Adobe，`z` 简写/部分组/`~>`）与 Z85（ZeroMQ，长度约束）
  两套字母表，与 Python `base64.a85encode` 做了随机交叉验证；hexdump 按固定
  布局排版并能反向解析（只把「8 位十六进制 + 空白」当偏移）。错误号段
  `std.encoding.lz4`=0x6041 … `std.encoding.hexdump`=0x6044（`tcc/tccgen.c` 的
  `c6_err_std_slots[]` 追加）。

- **标准库第十三批（数值类型）**：`std.core.bignum` / `std.core.decimal` /
  `std.core.rational` / `std.core.complex` 四个模块，集成示例
  `examples/stdnum.c6`（`make std-num`）。bignum 是 1024 位的值类型大整数
  （加减乘、二进制长除法、u32 快速幂、2..36 进制互转，超限/除零/解析失败走
  `err`）；decimal 是 9 位小数的十进制定点，**乘除用自带的 128 位小工具**
  （tcc 没有 `__int128`）再按 half-up 收尾，`0.1 + 0.2` 精确等于 `0.3`，带 5
  种舍入模式与「恰好 N 位小数」的格式化，并与 Python `decimal` 做了随机交叉
  验证；rational 是始终最简的精确分数，`cmp` 用连分数比较（不经过 double）；
  complex 是 f64 复数（四则/共轭/模/归一化/整数幂，除零按 IEEE）。错误号段
  `std.core.bignum`=0x603d … `std.core.complex`=0x6040（`tcc/tccgen.c` 的
  `c6_err_std_slots[]` 追加）。

- **标准库第十二批（文本算法）**：`std.text.regex` / `std.text.diff` /
  `std.text.fuzzy` / `std.text.wrap` 四个模块，集成示例
  `examples/stdtext.c6`（`make std-text`）。regex 是回溯式正则：UTF-8 按码点
  匹配、位置给字节，捕获组/分支/字符类/`{m,n}` 与懒惰量词都有，单字符量词和
  「定长链 + 一层捕获组」走迭代快路径（`.*` 在 200KB 文本上是线性的），固定
  容量在编译期报错、匹配深度 4096 防爆栈；diff 是 Hirschberg 线性空间 LCS +
  `diff -u` 同形 hunk（先剥公共前后缀，工作区走 `Arena`，放不下不写半截）；
  fuzzy 是编辑距离/OSA/千分比相似度/子序列打分与 `fuzzy_best`（按码点，
  `FUZZY_MAX`=256）；wrap 按显示列折行（CJK 2 列、CJK 之间可断行、超长词硬切）
  并带整段缩进与居中。错误号段 `std.text.regex`=0x6039 … `std.text.wrap`=0x603c
  （`tcc/tccgen.c` 的 `c6_err_std_slots[]` 追加）。

- **标准库方法化**：`std/` 的普通结构体方法去掉 `reader_`/`writer_`/`cfile_`/`sha256_`/
  `arena_`/`random_`/`uuid_`/`inet_` 之类的接收者前缀，统一成 `pr->read_line(...)`、
  `b->write(...)`、`r.next_u64()`、`a.json_parse(...)`、`ip.is_private()`。方法符号
  仍是 `<Tag>_<name>`；模块里的裸名弱别名带模块前缀，所以 `close`/`ok`/`next`/`format`
  这些同名方法在不同结构体、不同模块之间可以共存，`foreach` 的 `has`/`next` 保持
  字面名不变。工厂和一次性函数（`reader_open`/`rand_seed`/`bw_open`/`csv_reader`/
  `ini_open`）仍是自由函数。`std/`、`examples/std*.c6`、`docs/stdlib.md`、`README.md`
  已全部改用方法语法。

- **标准库第十一批（嵌入式基础）**：`std.core.crc` / `std.core.fixed` /
  `std.core.pool` / `std.encoding.frame` 四个模块，集成示例
  `examples/stdembed.c6`（`make std-embed`）。crc 补 CRC-8（SMBUS/1-Wire）、
  CRC-16（CCITT-FALSE/XMODEM/MODBUS/ARC）、CRC-32C 与 Fletcher-16/LRC/XOR/
  sum8/sum16，全部逐位不带表并带流式 `*_update`；fixed 提供 Q16.16 的 `Fix`
  （饱和四则、开方、Taylor sin/cos，误差 < 1e-5）与 Q15 的 `Q15`（饱和乘法）；
  pool 是借用调用方缓冲的定长块分配器（O(1) alloc/free、块对齐、坏指针拒绝）；
  frame 是 COBS 与 SLIP（RFC 1055）的零分配分帧/转义（先量再写，坏格式不写
  半截）。四个模块都只依赖 `std.core.types`，不链 libc/libm、不分配，可以直接
  用在 freestanding/裸机程序里。错误号段 `std.core.crc`=0x6035 …
  `std.encoding.frame`=0x6038（`tcc/tccgen.c` 的 `c6_err_std_slots[]` 追加）。

- **标准库第十批**：`std.data.props` / `std.net.multipart` / `std.encoding.punycode` /
  `std.text.width` 四个模块，集成示例 `examples/stdform.c6`（`make std-form`）。
  props 读 `.properties`/`.env`（三种分隔符、`export`、续行、`\uXXXX` 与代理对、
  语法错误给行号，值视图保留转义原文、`decode` 才解）；multipart 零分配解析
  `multipart/form-data`（boundary 引号、伪分隔符当 preamble、无头 part、CRLF/LF）；
  punycode 覆盖 RFC 3492 与 IDNA 的 `xn--` 层（纯 ASCII 带结尾 `-`、63/255 字节
  上限、解码溢出检查）；width 用 Unicode 15.0 的 EastAsianWidth/通用类别表算
  终端列数并做裁剪/补齐（不做字素簇合并）。错误号段
  `std.data.props`=0x6031 … `std.text.width`=0x6034。

- **标准库第九批**：`std.container.list` / `std.container.ring` / `std.text.utf16` /
  `std.text.ansi` 四个模块，集成示例 `examples/stdnode.c6`（`make std-node`）。
  侵入式双向链表不拥有节点、remove 先确认归属；环形队列借用调用方数组、满时不
  覆盖；UTF-16 处理代理对/非法代理/截断；ANSI 构造器拼 SGR、256 色与光标移动，
  满了置 ok=0。错误号段 `std.container.list`=0x602d … `std.text.ansi`=0x6030。

- **标准库第八批**：`std.encoding.base58` / `std.encoding.varint` / `std.text.semver` /
  `std.net.cookie` 四个模块，集成示例 `examples/stdnext.c6`（`make std-next`）。
  Base58 走 Bitcoin 字母表、前导零写 `1`；varint 提供 LEB128 无符号/有符号
  （zigzag）编解码，解码区分成功/截断/非法；semver 严格按 SemVer 2.0.0 解析与
  比较优先级；cookie 零分配解析 Set-Cookie / Cookie:，属性名大小写不敏感。
  错误号段 `std.encoding.base58`=0x6029 … `std.net.cookie`=0x602c。

- **标准库第七批**：`std.container.bitset` / `std.text.glob` / `std.encoding.base32` /
  `std.net.http` 四个模块，集成示例 `examples/stdmore.c6`（`make std-more`）。
  bitset 借用调用方 `u64` 缓冲，末字多余位始终清零；glob 是 `Glob` 对象上的
  `match`/`match_path`，支持 `*`/`?`/字符类/转义；base32 覆盖 RFC 4648 标准表与
  base32hex，填充策略显式、拒绝非零填充位；http 零分配解析请求/响应，头字段名
  大小写不敏感、不接受 obs-fold。错误号段 `std.container.bitset`=0x6025 …
  `std.net.http`=0x6028；模块、示例、文档都按 `docs/stdlib.md` 的「API 形态：
  硬性要求」写成结构体方法。

- **标准库分目录**：`std/` 从平铺改成按领域分七个子目录。模块名就是路径，所以
  模块名多了一段（`std.vec` → `std.container.vec`、`std.err` → `std.core.err`）。
  分组：
  - `std/core/`：`types` `err` `mem` `math` `bits` `hash` `rand`
  - `std/text/`：`str` `strbuf` `fmt` `utf8` `ascii` `num`
  - `std/container/`：`vec` `span` `map` `set` `deque` `pqueue` `range` `sort`
  - `std/os/`：`io` `fs` `path` `env` `time`
  - `std/encoding/`：`codec` `base64` `sha256` `bitbuf` `uuid`
  - `std/data/`：`json` `csv` `ini`
  - `std/net/`：`inet` `url` `mime`

  编译器里的 std 错误号段表只改了名字，**槽位数值一个没动**（`std.core.err`
  仍是 0x6000），`errbase.def` 不动；`make stdtest` 改为遍历 `std/` 下的子目录。
- **标准库第三批**：`std.text.ascii` / `std.text.num` / `std.encoding.codec` / `std.core.rand` / `std.os.fs` /
  `std.container.set` 六个模块，集成示例 `examples/stdkit.c6`（`make std-kit`）；顺手修掉
  `std/container/map` 扩容复活墓碑的 bug（`std/container/set` 的对应用例钉住）。错误号段
  `std.text.ascii`=0x6013 … `std.container.set`=0x6018。
- **标准库第六批**：`std.net.inet` / `std.encoding.bitbuf` / `std.container.pqueue` / `std.data.ini` 四个模块，
  集成示例 `examples/stdnet.c6`（`make std-net`）。inet 的 IPv6 输出按 RFC 5952
  规范化（最长零段压缩、同长取第一个、v4-mapped 写点分），解析对前导零/多余冒号
  一律严格；bitbuf 是 MSB-first 的位读写器，越界只报 `FULL`/`SHORT` 而不写半截，
  `patch` 支持不移动游标的回填；pqueue 是托管二叉堆，比较器与 `std.container.sort` 通用，
  带 `push_pop`（top-k）与 `heapify`；ini 是零分配迭代读取器，坏行给准确行号。
  错误号段 `std.net.inet`=0x6021 … `std.data.ini`=0x6024。
- **标准库第五批**：`std.encoding.base64` / `std.net.url` / `std.encoding.uuid` / `std.net.mime` 四个模块，
  集成示例 `examples/stdweb.c6`（`make std-web`）。Base64 把「表」与「填充」变成显式
  参数（标准表/URL 安全表不能混用）；URL 是 RFC 3986 §5.2 的完整相对解析（§5.4 的
  23 条 normal + abnormal 用例钉死）；UUID 覆盖 v4（随机）/v5（SHA-1 名字派生，已知
  向量与 Python `uuid5` 一致）/v7（48 位毫秒时间戳）；MIME 是扩展名表 + 分类 + 魔数
  嗅探。错误号段 `std.encoding.base64`=0x601d … `std.net.mime`=0x6020。
- **标准库第四批**：`std.data.json` / `std.encoding.sha256` / `std.data.csv` / `std.os.env` 四个模块，
  集成示例 `examples/stdbox.c6`（`make std-box`）。JSON 是 arena 上的 DOM（数字保留
  原文、严格 RFC 8259、代理对解码）；SHA-256/HMAC 的 FIPS 180-4 与 RFC 4231 向量
  钉死在用例里；CSV 是零分配读取器 + 写调用方缓冲；env 管环境变量与 `argc/argv`。
  错误号段 `std.data.json`=0x6019 … `std.os.env`=0x601c。

## 2026-11-11

- **LLVM IR 后端**：按输出后缀 `.ll` 选择，复用整个前端 — `a6543f2`
- **交叉编译一条命令**：`--target=<triple>` / `--cc` / `--emit-llvm`，wasm 目标可跑 — `c36d1f6`
- **工具链改名**：构建产物统一叫 `tcc/c6`（原 `tcc/tcc`）
- **标准库 `std/` 首批**：一个文件一个 module，八个模块 + wordcount 集成示例 — `48c9193`
- **标准库扩充**：sort/mem/io/math/time + vec/str/map 增补 — `01ad893`
- **标准库第二批**：strbuf/utf8/bits/hash/path/deque，并加固 str/vec/fmt — `e5976f5`

## 2026-11-10

- **闭包 V2 收尾**：捕获式闭包 P0–P5 全部落地，文档与用例跟上 — `1364fe9`
- **`.c6` 后缀成为唯一开关**：删除 `-std=c6`，`.c` 文件永远按普通 C 编译 — `e066b5b`

## 2026-11-09

- **字符串插值**：`f".."` 是 `const char *`，`@f".."` 是托管对象 — `eb7df04`
- **引用计数 GC**：P0–P8 全流程 + 跨 TU 环检测 + 多环报告 — `e5b284e`
- **函数字面量**：`void(int x) { ... }` 是表达式，值就是函数指针 — `5404367`
- **闭包 P0/P1**：`closure` 类型、捕获扫描、标量捕获与调用改写 — `a077afd`
- **裸机**：自己写的 512 字节引导器把 c6 程序拉起来（`make fstest-bare`）— `455832c`

## 2026-11-08

- **module / import**：命名空间隔离，模块名就是它的路径 — `4be263a`

## 2026-11-07

- **元组**：`(T1, T2, ...)` 多返回值与解构 — `fd76ea9`
- **foreach**：数组 / 切片 / 整数区间遍历 — `ff1dc97`
- **async / await**：无栈协程 — `2e6fc87`
- **OS 线程**：`go` 改为真线程，协程派生改用 `co`（后来 `go` 更名为 `spawn`）— `6100ea8`
- **C99 输出后端**：`-c99` 把 c6 源码生成为可移植 C99 — `c2c279f`

## 2026-11-06

- **仓库初始化**：规范、补丁、构建脚本 — `8f77011`
- **变量必须初始化**：所有变量定义必须带初始化器（VLA 豁免）— `d113da7`
- **defer / errdefer / err**：用法说明与可运行示例 — `10a7e12`
- **切片 `T[:]`**：补丁 0011、文档与示例 — `fbab3dd`
- **结构体方法与 interface**：方法符号 `<Tag>_<meth>`，胖指针 + vtable — `7838182`
