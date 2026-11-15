# c6c —— 用 c6 写的 c6 编译器

自举、脱离 tcc、多后端（native x86-64 / C99 / LLVM IR）的 c6 编译器。

本目录是**独立项目边界**：自己的 Makefile、自己的许可证、不 `#include` `tcc/*.h`、
不链接 `libtcc`、不复制 `tcc/` 的源码（`make c6c-lint` 机器可查）。仓库里的 `tcc/` 只承担
两件事：**引导第一步**（用它编出 c6c stage1）和**差分测试的 oracle**。

| 文档 | 内容 |
|---|---|
| [docs/plan.md](docs/plan.md) | 目标、验收口径、实测基线、阶段计划、风险 |
| [docs/design.md](docs/design.md) | 架构、流式 IR、三后端、错误 ABI、内存预算 |
| [docs/todo.md](docs/todo.md) | 可提交项清单与当前状态（一项一个提交） |

现状：**P0**（立项文档 + 基准脚本），还没有可运行代码。目标与门槛见 plan.md。

```sh
make c6c-bench        # 跑基线（tcc 与 c6c 都测，输出表格 + JSON）
make c6c-check-all    # 最终验收（P8 之前不会绿）
```

许可证：`LICENSE` 为 MIT，与 `tcc/`（LGPL-2.1）严格分离；若项目所有者另有安排，替换本文件即可。
