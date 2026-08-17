# pLang 编译器 (plc)

**pLang** —— 一门像 C 一样靠近底层、又像 Rust 一样高级的系统级语言。
本包安装 `plc` 编译器及其标准库。

由 **PeYT** 组织开发。
贡献者：**ieshishinjin**、**Carry-Rao**、**MaherJon**。

## 安装

```bash
npm install -g plang-compiler
```

安装需要你机器上有 **LLVM**：

- macOS：`brew install llvm`
- Linux（Debian/Ubuntu）：`sudo apt install llvm`
- Windows：从 https://llvm.org 安装并设置 `LLVM_HOME`

安装脚本会自动定位 LLVM 并配置 `plc` 使用它。
如果 LLVM 不在标准位置，请先设置 `LLVM_HOME` 环境变量。

## 使用

```bash
plc main.plang            # 编译 -> ./main（与源文件同名）
plc main.plang -o myapp   # 编译 -> ./myapp
./main                    # 运行
```

选项：

| 选项 | 说明 |
|------|------|
| `-o <file>` | 输出文件名（默认：源文件去掉 .plang） |
| `-O0..-O3` | 优化级别（默认 O2） |
| `-c` | 只编译成目标文件 (.o) |
| `-static` | 打包静态库 (.a) |
| `--save-temps` | 保留中间文件 (.ll, .o) |

## 标准库

`import std.io;` / `std.mem` / `std.thread` / `std.atomic` / `std.option` / `std.result` / `std.vector` / `std.string` / `std.sqlite`
都会自动编译并链接。`import std.sqlite` 会自动链接 `-lsqlite3`。

## 语言服务器

LSP 服务器（`lsp-server`）从 pLang 项目单独构建，供 VS Code 插件 / Neovim / Helix 使用。

## 许可证

ISC
