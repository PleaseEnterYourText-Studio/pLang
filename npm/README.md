# pLang Compiler (plc)

**pLang** — a systems programming language that is low-level like C and high-level like Rust.
This package installs the `plc` compiler and its standard library.

Developed by the **PeYT** organization.
Contributors: **ieshishinjin**, **Carry-Rao**, **MaherJon**.

## Install

```bash
npm install -g plang-compiler
```

This requires **LLVM** on your machine:

- macOS: `brew install llvm`
- Linux (Debian/Ubuntu): `sudo apt install llvm`
- Windows: install LLVM from https://llvm.org and set `LLVM_HOME`

The installer will locate LLVM automatically and configure `plc` to use it.
If LLVM is not at a standard location, set the `LLVM_HOME` environment variable first.

## Usage

```bash
plc main.plang            # compile -> ./main (same name as source)
plc main.plang -o myapp   # compile -> ./myapp
./main                    # run
```

Options:

| Option | Meaning |
|--------|---------|
| `-o <file>` | output file name (default: source name without .plang) |
| `-O0..-O3` | optimization level (default O2) |
| `-c` | compile to object file only |
| `-static` | build static library (.a) |
| `--save-temps` | keep intermediate files (.ll, .o) |

## Standard Library

`import std.io;` / `std.mem` / `std.thread` / `std.atomic` / `std.option` / `std.result` / `std.vector` / `std.string` / `std.sqlite`
are compiled and linked automatically. `import std.sqlite` links `-lsqlite3` automatically.

## Language Server

The LSP server (`lsp-server`) is built separately from the pLang project and is used by the VS Code extension / Neovim / Helix.

## License

ISC
