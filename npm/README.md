# pLang 编译器 (plc)

## 安装

```bash
npm install -g @peyt/plang
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
## 许可证

AGPL-3.0
