# c6c 工程管理

> **状态**：现行参考 · **索引**：[c6c README](../README.md) ·
> **总索引**：[docs/README.md](../../docs/README.md) ·
> **编译器选项速查**：[docs/reference/cli.md](../../docs/reference/cli.md)
>
> c6c 工程管理命令与清单格式的**单一事实源**。命令分派在 `c6c/src/pm.c6`，
> 实现在 `c6c/src/pm.c6`（用 c6 自身写，随 c6c 单 TU 自举）。

## 命令

第一个参数是下面这些名字时进入工程管理，其余情况仍是普通编译器：

```sh
c6c scan              # 只扫描仓库：推断 name/version/kind、源文件、import、test 块
c6c init [--force] [--from=cmake|cargo|make|auto]
                      # 扫描仓库写 c6c.yaml，可从既有 CMake/Cargo 工程迁移元数据
c6c build [--force] [--locked] [--release]
                      # 按清单编译（mtime + 指纹增量 / 锁文件校验 / profile）
c6c test [--release]  # 编译全部源文件并运行 `test "…" { }` 块
c6c run [参数...]     # 先 build，再执行产物并把参数透传
c6c package           # 按 pkg_files 装 staging 树，出 TGZ / DEB
c6c install [--destdir=pkg] [--prefix=/usr]  # 按 pkg_files 安装（保留执行位）
c6c publish [--tag]   # package + SHA256SUMS；--tag 再打 v<version>
c6c check [--release] # -fsyntax-only，只检查不落盘
c6c clean [--dry-run] # 只删本清单产物（不删整个 build/）
c6c env / list [--json]  # 解析后的配置 / 源文件列表
c6c lock                # 生成 c6c.lock（含本地 #include 依赖）
c6c doctor              # 检查源文件 / import 是否齐全
c6c workspace <cmd>     # 多包：init / list / build / test / check / clean / lock / doctor …
c6c flash|monitor|debug [--dry-run]  # 按 *_cmd 模板操作固件
c6c help                # 子命令帮助
```

未知选项、选项用错子命令（如 `test --locked`）都会报错并返回 2；`run` 的
`--` 之后的内容一律原样透传。

## 扫描与推断

`init` 的扫描规则：递归遍历当前目录（跳过隐藏目录与 `build/`、`out/`、
`dist/`、`target/`、`node_modules/`），收集 `.c6` / `.c` 源文件；有 `main()`
或只有 `test` 块 → `kind: app` / `lib`；只有 `_start`（无 main）→
`kind: firmware`，并在 `flags` 里自动加 `-nostdlib`。注释与字符串里的
`main` / `_start` / `test` 字样不算定义。`import a.b;` 按目录分段核对
`a/b.c6` / `a/b.c`，找不到时打到 stderr 作为提醒。版本优先取仓库根
`VERSION`（从当前目录向上找），否则 `0.1.0`。

## 清单 `c6c.yaml`

`c6c.yaml`（或 `c6c.yml`）是受控子集，不依赖 `std/data/yaml.c6`，保持
c6c 单 TU 可自举：

```yaml
name: hello
version: 0.1.0
kind: app            # app / lib / firmware
out: build/hello
sources:
  - src/main.c6
include:
  - .
flags:
  - -b
```

**多 `.c6` 源合并（`merge_c6`）**：c6c 默认按文件切换 TU。当某个 `.c6`
调用另一个 `.c6` 里的文件级 `static` 符号时（c6 单遍编译要求定义先于调用），
在清单里设 `merge_c6: true`，`build` / `test` / `check` 会先把 `sources`
中的 `.c6` 原样文本按清单顺序合并成 `<out>.merged.<pid>.c6` 再编译，`.c`
宿主源保持独立；临时文件在编译后自动删除，`clean` 也能清残留。约束：

- 合并顺序 = `sources` 顺序，被调函数所在文件要排在调用者之前；
- 仅对 `app` / `lib` 生效，`firmware` 逐文件 `-c` 编译，该字段被忽略；
- 重复 `#include` 靠头文件自身的 include guard 去重；
- 切换该值会触发重编（fingerprint 已含此字段），但 `c6c.lock` 不记录它。

**并发安全**：同一工程内会落盘的命令（`build` / `test` / `check` / `clean` /
`package` / `install` / `publish` / `lock` / `init`）通过清单目录的 `.c6c.lock`
取 `fcntl` 记录锁串行执行，进程退出（含 `kill -9`）时由内核自动释放，无 stale
锁问题；fingerprint / lock 等小文件用「写临时文件 + `rename`」原子提交，合并
TU 文件名带 PID，因此这些命令可安全并发。`run` 只在 build 阶段持锁，执行产物
前放锁。不同工程的清单目录各自持锁，可并行；把 c6c 当编译器用（`make -j`
多个不同 `-o`）也不受工程锁影响。**注意**：`build` 与 `clean` 并发时，谁先拿到
锁决定最终状态——`clean` 后执行会删掉产物，这是其正常语义，不是竞态损坏。

打包与固件的附加键：

```yaml
pkg_name: hello
pkg_desc: hello from c6
pkg_license: MIT
# 发行版打包：DESTDIR="${pkgdir}" c6c install --prefix=/usr
destdir: pkg            # install 默认落点；DESTDIR / --destdir 优先
prefix: /usr            # PREFIX / --prefix 优先
dev_flags:              # 默认 profile 的追加编译参数（build/test/check 都吃）
release_flags:          # --release 的追加编译参数
pkg_formats:            # tgz / deb（deb 需要 dpkg-deb）
  - tgz
pkg_files:              # "源:目标"，package/install 共用
  - build/hello:bin/hello
fw_formats:             # firmware 专用：ELF 由 cc 链接，BIN/HEX 用 objcopy
  - elf
  - bin
  - hex
link_flags:             # 传给链接固件的 cc（如 -T board.ld）
flash_cmd:              # 参数列表；{elf} {bin} {out} {name} {version} 会展开
  - openocd
  - -f
  - openocd.cfg
  - -c
  - "program {elf} verify reset exit"
monitor_cmd:
debug_cmd:
```

`pkg_files` 的目标只接受安全相对路径；绝对路径按 prefix 去重（`/usr/bin/x`
配 `prefix: /usr` 落到 `bin/x`），`..` 会被拒绝。`package` 与 `install`
共用这张表，`DESTDIR` 的用法与 autotools/cmake 一致：

```sh
DESTDIR="${pkgdir}" c6c install --prefix=/usr
# 等价于：c6c install --destdir="${pkgdir}" --prefix=/usr
```

## 锁文件与 workspace

**锁文件**：`c6c lock` 生成 `c6c.lock`（JSON：元数据 + `include` / `flags` /
`link_flags` / `dev_flags` / `release_flags` / `pkg_formats` / `pkg_files` /
`fw_formats` + 每个源文件的大小与 FNV 校验和）。本地 `#include` 会沿源文件
目录和 `include:` 列表展开并一起锁住；`c6c build --locked` 在源文件或构建
配置漂移时直接失败，适合 CI 与可复现构建。

**workspace**：仓库根的 `c6c-workspace.yaml` 用 `members:` 列出子包：

```yaml
members:
  - c6c
```

`c6c workspace init` 自动发现含 `c6c.yaml` / `c6c.yml` 的子目录；
`workspace build|test|check|clean|lock|doctor|list` 逐成员执行（失败即停）。
越界成员（绝对路径或含 `..`）会被拒绝。

## 迁移、固件与 dogfood

`c6c init --from=cmake|cargo|make|auto` 从 `project(NAME VERSION …)`、
`[package]` 的 `name/version/description/license` 提取元数据；`c6c doctor`
报告缺失源文件与未解析的 `import`。

像 go 找 `go.mod` 一样，`c6c` 从当前目录逐级向上找 `c6c.yaml` /
`c6c.yml`；`--manifest-path=...` 可显式指定（绝对路径也可用）。
`build --release` 用 `release_flags`，其余用 `dev_flags`；`env` /
`list --json` 便于脚本读取解析后的配置。

固件构建是两阶段：c6c 只认 `main` 当 ELF 入口，所以 `_start` 工程先把每个
源文件 `-c` 成 ET_REL（多源文件各自一个对象），再由宿主
`cc -nostdlib -Wl,-e,_start` 链接（`link_flags` 可追加 `-T`）；随后按
`fw_formats` 用 `objcopy` 出 `.bin` / `.hex`。`flash` / `monitor` /
`debug` 是模板执行器，非 `--dry-run` 时真起进程。

`c6c/` 目录自带的 `c6c.yaml` 是 dogfood 样本：自编到 `build/c6c`、TGZ
打包、staged install；`pmtest` 会真跑一遍这条链（用编译器副本引导，避免
覆盖正在执行的二进制）。回归入口：
`cmake --build build --target pmtest`（或 `ctest -R '^c6c-pm$'`），当前 83 项
检查（81 项常规 + 2 项 DEB；缺 `dpkg-deb` 的环境跳过 2 项）。
