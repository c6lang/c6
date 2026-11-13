# c6 文档索引

> **状态**：现行索引 · 入口：[README](../README.md) · **变更史**：[CHANGELOG](../CHANGELOG.md)
>
> 文档按「你现在是什么角色」分三条路。全部文档都只有一个事实来源，互相之间用相对链接引用。

## 三条读者路径

### 1. 我是使用者：写 c6 代码

1. [README 的快速开始](../README.md#快速开始) —— 构建、跑第一个 `.c6`。
2. [manual/](manual/) —— 按特性查用法；每篇都是「一句话定位 → 例子 → 速查 → 陷阱」。
3. [stdlib.md](stdlib.md) —— 标准库 `std/` 的 API 与写法约定。
   - 覆盖面： [stdlib-coverage.md](stdlib-coverage.md) —— 逐族盘点「还缺什么」与「什么时候到头」。
   - 异步： [async-framework.md](async-framework.md) —— 语言级 `async`/`await`/`co` 框架整体与本机实测开销；
     [async-comparison.md](async-comparison.md) —— 六种语言（c6/C/C++/Go/Rust/Uya）的异步写法与实测对比。
   - 静态分析： [ubcheck.md](ubcheck.md) —— 用 c6 写的 c6 分析工具链（lex/parse/ub）与 `stdub` 命令行：13 类 UB 检查、一定/可能口径、每条附具体修法与整行代码。
4. [reference/grammar.md](reference/grammar.md) —— 需要精确语法时查 EBNF。
5. 报错看不懂 → [reference/diagnostics.md](reference/diagnostics.md) 按消息文本查编号。

### 2. 我是编译器贡献者：改前端/后端/运行时

1. [design/plan.md](design/plan.md) —— 每个特性为什么这么设计（分阶段方案与评审结论）。
2. [design/implementation-notes.md](design/implementation-notes.md) —— 运行时状态、ABI、每个语法对应哪个实现函数。
3. [reference/grammar.md](reference/grammar.md) —— 语法产生式与重写规则；改语法先改这里。
4. [reference/diagnostics.md](reference/diagnostics.md) —— 约束编号与诊断文本；加检查先领编号。
5. [manual/testing.md](manual/testing.md) —— test 块、`// EXPECT` 诊断语料、完整回归怎么跑。
6. [reference/c99-backend.md](reference/c99-backend.md) / [reference/llvm-backend.md](reference/llvm-backend.md) —— 两个后端。
7. [manual/freestanding.md](manual/freestanding.md) —— 无 libc 与裸机约束。

### 3. 我是评审 / 接手者：看做到哪一步、为什么

1. [history/status.md](history/status.md) —— 分阶段实施状态与验收口径。
2. [CHANGELOG](../CHANGELOG.md) —— 版本与特性变更史。
3. [design/gc-design.md](design/gc-design.md)、[design/closure-design.md](design/closure-design.md)、
   [design/generics-design.md](design/generics-design.md) —— 三个大特性的完整设计与明确不做的部分。
4. [history/gc-todo.md](history/gc-todo.md)、[history/closure-todo.md](history/closure-todo.md) —— 施工日志（已全部完成，保留作决策依据）。

## 文档清单

| 路径 | 角色 | 状态 | 什么时候读 |
|---|---|---|---|
| [../README.md](../README.md) | 落地页 | 现行 | 第一次来、找入口 |
| [manual/](manual/) | 语言手册（一特性一篇） | 现行 | 写代码 |
| [stdlib.md](stdlib.md) | 标准库设计与 API | 现行 | 用 `std/` |
| [reference/grammar.md](reference/grammar.md) | 语法规范（EBNF + 重写规则） | 现行 | 查精确语法 |
| [reference/diagnostics.md](reference/diagnostics.md) | 诊断码 C1–C134 | 现行 | 查报错、加检查 |
| [reference/cli.md](reference/cli.md) | 编译器选项 + CMake 构建目标 | 现行 | 构建、跑测试 |
| [reference/c99-backend.md](reference/c99-backend.md) | C99 输出后端 | 现行 | 跨平台、自举 |
| [reference/llvm-backend.md](reference/llvm-backend.md) | LLVM IR 后端 | 现行 | 交叉编译 |
| [design/plan.md](design/plan.md) | 语言设计总方案 | 现行 | 改语义前 |
| [design/implementation-notes.md](design/implementation-notes.md) | 实现说明与实现位置 | 现行 | 改代码 |
| [design/gc-design.md](design/gc-design.md) | 引用计数 GC 设计 | 已落地 | 动托管引用 |
| [design/closure-design.md](design/closure-design.md) | 函数字面量与闭包设计 | V1/V2 已落地 | 动闭包 |
| [design/generics-design.md](design/generics-design.md) | 泛型设计 | 已落地 | 动模板 |
| [history/status.md](history/status.md) | 实施状态与验收 | 历史记录 | 评审、接手 |
| [history/gc-todo.md](history/gc-todo.md) | GC 施工日志 | 历史记录 | 考古 |
| [history/closure-todo.md](history/closure-todo.md) | 闭包施工日志 | 历史记录 | 考古 |

## 维护约定

改文档时守这几条，链接和数字才不会漂：

1. **一篇只讲一件事**：用法进 `manual/`，规范进 `reference/`，动机进 `design/`，进度进 `history/`。
2. **相对链接**：所有跨文档链接写相对路径。移动/重命名文档后要全仓库更新
   `docs/...` 引用（C 源码注释里也有），再用链接检查确认零断链。
3. **数字不写死**：模块数、用例数、自检条数不抄进文档，改成「见对应 CMake 目标的输出」，
   或只在一处（`history/status.md` 的验收口径）出现。
4. **单一事实源**：错误码号段以项目根 `errbase.def` 为准；语法以 `grammar.md` 为准；
   诊断文本以 `diagnostics.md` 为准；其余文档只引用、不复制。
5. **状态行**：每篇顶部写 `状态 / 索引 / 关联文档`，历史文档标「历史记录」而不是删掉。
6. **新增特性**：manual 一篇 + grammar 一节 + diagnostics 编号 + 用例 + README 特性表一行 +
   CHANGELOG 一条，六件事一起提交。
