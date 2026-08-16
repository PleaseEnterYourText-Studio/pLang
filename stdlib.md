# PLang 标准库

标准库是真正的源码库，位于 `std/` 目录。`import` 后编译器对库包做**独立编译**：
函数注入 extern 声明（定义留在库包 `.o`）、结构体合并进用户程序、链接时把库包 `.o` 一起链接。
`pub` 函数可跨包调用（如 `io.print(...)`、`thread.join(...)`）。

## 总览

| 库 | 导入方式 | 功能 |
|----|---------|------|
| `std.thread` | `import std.thread;` | 多线程：线程创建/等待、互斥锁、休眠、让出 CPU |
| `std.io` | `import std.io;` | 标准输入输出：打印、读输入、数字格式化 |
| `std.mem` | `import std.mem;` | 堆内存：malloc/free、memcpy/memset/memcmp |
| `std.atomic` | `import std.atomic;` | 原子操作：load/store/add/exchange/cas + 内存序 |
| `std.option` | `import std.option;` | null 安全：`Option<T>` 泛型结构体 |
| `std.result` | `import std.result;` | 错误处理：`Result<T, E>` 泛型结构体 |

---

# std.thread 多线程

## 线程

| 函数 | 说明 |
|------|------|
| `thread.spawn(fn)` | 启动一个新线程运行**无参函数** `fn`，返回线程句柄（`var -> var: ptr`） |
| `thread.spawn(fn, p)` | 启动线程运行**带一个指针参数**的函数，`p` 为共享数据指针（多线程共享同一内存） |
| `thread.join(t)` | 阻塞等待线程 `t` 结束 |

- 线程入口必须是无参函数，或（共享内存模式）恰好一个指针参数；返回值被忽略。
- 共享内存配合 `std.atomic` 使用，避免数据竞争：

```plang
func worker(var -> var: int counter) : int {
    var: int i = 0;
    while (i < 1000) {
        atomic.add(counter, 1);
        i = i + 1;
    }
    return 0;
}

func main() : int {
    var: int counter = 0;
    var -> var: int p = &counter;
    var -> var: ptr t1 = thread.spawn(worker, p);
    var -> var: ptr t2 = thread.spawn(worker, p);
    thread.join(t1);
    thread.join(t2);
    io.printInt(counter);   // 2000
    io.println("");
    return 0;
}
```
- `join` 前线程与主线程并行执行；程序退出前应 `join` 所有已 spawn 的线程。

## 互斥锁

| 函数 | 说明 |
|------|------|
| `thread.mutex.create()` | 从全局锁池分配并初始化一把锁，返回其地址（`var -> var: ptr`） |
| `thread.lock(m)` | 加锁（阻塞） |
| `thread.unlock(m)` | 解锁 |
| `thread.destroy(m)` | 销毁 |

- 锁池上限 **64 把**，超出返回空指针。
- `lock` / `unlock` / `destroy` 是库源码实现，`spawn` / `sleep` / `mutex.create` 因需蹦床与全局锁池为编译器内置。

## 其他

| 函数 | 说明 |
|------|------|
| `thread.sleep(ms)` | 当前线程休眠指定毫秒数 |
| `thread.yield()` | 当前线程主动让出 CPU |

## 示例

```plang
package demo;
import std.thread;

func worker() : int {
    var: i64 i = 0;
    while (i < 100000000) { i = i + 1; }
    return 0;
}

func main() : int {
    // 两线程并行
    var -> var: ptr t1 = thread.spawn(worker);
    var -> var: ptr t2 = thread.spawn(worker);
    thread.join(t1);
    thread.join(t2);

    // 互斥锁：创建 → 加锁 → 解锁 → 销毁
    var -> var: ptr m = thread.mutex.create();
    thread.lock(m);
    thread.unlock(m);
    thread.destroy(m);

    thread.sleep(10);
    thread.yield();
    return 0;
}
```

---

# std.io 标准输入输出

## 输出

| 函数 | 说明 |
|------|------|
| `io.print(s)` | 输出字符串，不换行 |
| `io.println(s)` | 输出字符串并换行 |
| `io.printChar(c)` | 输出单个字符 |
| `io.printInt(n)` | 输出整数（十进制） |
| `io.printFloat(f)` | 输出浮点数，固定 6 位小数（四舍五入） |
| `io.error(s)` | 输出到标准错误 `stderr` |
| `io.flush()` | 冲刷 `stdout` 缓冲 |

- `print` / `println` / `error` 的参数为通用指针：字符串字面量、字符串变量、字符缓冲指针均可。
- 数字格式化在源码内实现（不依赖 libc 舍入），跨平台输出位级一致。

## 输入

| 函数 | 说明 |
|------|------|
| `io.readChar()` | 读一个字符，返回其 ASCII 码（`int`）；EOF 返回 -1 |
| `io.readInt()` | 读一个整数（跳过空白）；输入非数字时返回 0 |
| `io.readLine(buf, size)` | 读一行到缓冲区（含换行）；需调用方提供缓冲 |

- `readLine` 需要自备缓冲：`var: char[64] line; io.readLine(&line, 64);`
- 读入内容含末尾换行符，暂无 EOF 处理（失败时缓冲区不变）。

## 示例

```plang
package demo;
import std.io;

func main() : int {
    io.println("请输入你的名字:");
    var: char[64] name;
    io.readLine(&name, 64);
    io.print("你好，");
    io.print(&name);   // name 已含换行，无需 println

    // 读两个整数并求和
    var: int a = io.readInt();
    var: int b = io.readInt();
    io.print("a+b = ");
    io.printInt(a + b);
    io.println("");

    // 字符与浮点
    io.printChar('!');
    io.println("");
    io.printFloat(2.5);   // 2.500000
    io.println("");

    io.flush();
    return 0;
}
```

编译运行（输入用管道喂给程序）：

```bash
./cmake-build-debug/PLang main.plang -o main
printf "小明\n12 30\n" | ./main
```

输出：

```
请输入你的名字:
你好，小明
a+b = 42
!
2.500000
```

---

# 注意事项

- 线程：程序退出前应 `join` 所有已 spawn 的线程；锁池上限 64 把。
- `readLine`：内容含换行、无 EOF 处理；缓冲要够大。
- `readInt`：非数字输入返回 0。
- `printFloat`：固定 6 位小数，四舍五入。
- 字符串支持转义：`\n` `\t` `\r` `\\` `\"` `\0`。
- LSP 已支持 import 解析：编辑器中 `thread.join` 等库函数可正常悬停/补全/跳转。

---

# std.mem 堆内存

extern 直通 libc 分配器，配合指针算术使用。

| 函数 | 说明 |
|------|------|
| `mem.malloc(n)` | 分配 `n` 字节，返回指针（失败返回 `null`） |
| `mem.free(p)` | 释放内存 |
| `mem.memcpy(dst, src, n)` | 复制 `n` 字节 |
| `mem.memset(dst, value, n)` | 将 `n` 字节设为 `value` |
| `mem.memcmp(a, b, n)` | 比较 `n` 字节，返回 0/负数/正数 |

示例：

```plang
import std.io;
import std.mem;

func main() : int {
    var -> var: int p = mem.malloc(16);   // 4 个 int
    if (p == null) return 1;
    p[0] = 10;
    p[1] = 20;
    io.printInt(p[0] + p[1]);   // 30
    io.println("");
    mem.free(p);
    return 0;
}
```

> 注意：`n` 为字节数；按元素访问需自行乘元素大小（`int` 为 4 字节）。

---

# 变参 FFI 与 io.printf

extern 声明支持变参 `...`（仅限最后一个参数），用于 `printf` 这类 C 变参函数：

```plang
pub extern func printf(var -> var: ptr fmt, ...) : int;
```

`io.printf(fmt, ...)` 万能格式化：

```plang
io.printf("%s %d\n", "value=", 42);   // value= 42
io.printf("pi=%.2f\n", 3.14159);      // pi=3.14
io.printf("hex=%x\n", 255);           // hex=ff
```

> 格式符与实参类型必须匹配（`%d`→int、`%f`→f64、`%s`→字符串/指针、`%x`→int）。

---

# std.atomic 原子操作

编译器内置（LLVM atomicrmw/cmpxchg），用于多线程共享计数器/标志。需要 `import std.atomic;`。

| 函数 | 说明 |
|------|------|
| `atomic.load(p)` | 原子读取 |
| `atomic.store(p, v)` | 原子写入 |
| `atomic.add(p, v)` / `atomic.sub(p, v)` | 原子加/减，**返回旧值** |
| `atomic.exchange(p, v)` | 原子交换，返回旧值 |
| `atomic.cas(p, expect, desired)` | 比较交换，返回是否成功（bool） |

内存序参数（可选，最后一个）：`0`=relaxed、`1`=acquire、`2`=release、`3`=acq_rel、`4`=seq_cst（默认）。

`p` 必须是指向整数类型的指针（如 `var -> var: int`）。

```plang
var: int counter = 0;
var -> var: int p = &counter;
var: int old = atomic.add(p, 1);   // 原子自增，返回旧值
var: bool ok = atomic.cas(p, 1, 2);
```

## volatile 变量

`volatile var: int flag = 0;` —— 对该变量的读写不走缓存优化（LLVM volatile load/store），用于与外部/中断交互。

```plang
volatile var: int flag = 0;
flag = 1;          // volatile store
var: int v = flag; // volatile load
```

---

# std.option 可选值（null 安全基础）

`Option<T>` 是泛型结构体，表示"可能有值，也可能没有"。用于替代裸指针/`null`，避免空引用错误。
需要 `import std.option;`。

| 字段 | 类型 | 说明 |
|------|------|------|
| `isSome` | `bool` | 是否包含值 |
| `value` | `T` | 值（`isSome=false` 时无效） |

构造：`{true, v}` 有值，`{false, 0}` 无值。

```plang
import std.io;
import std.option;

func main() : int {
    var: Option<int> some = {true, 42};
    var: Option<int> none = {false, 0};

    if (some.isSome) { io.printInt(some.value); io.println(""); }   // 42
    if (!none.isSome) { io.println("none 无值"); }
    return 0;
}
```

> 库级实现：编译期强制使用（访问 `value` 前必须检查 `isSome`）需要类型系统 D1 完整支持，当前为约定。

---

# std.result 错误处理

`Result<T, E>` 是泛型结构体，表示"操作成功携带值，或失败携带错误码"。需要 `import std.result;`。

| 字段 | 类型 | 说明 |
|------|------|------|
| `isOk` | `bool` | 是否成功 |
| `value` | `T` | 成功值（`isOk=false` 时无效） |
| `error` | `E` | 错误码（`isOk=true` 时无效） |

```plang
import std.io;
import std.result;

func divide(a: int, b: int) -> Result<int, int> {
    if (b == 0) { return {false, 0, 1}; }   // Err(1)
    return {true, a / b, 0};                // Ok(a/b)
}

func main() : int {
    var: Result<int, int> r = divide(10, 2);
    if (r.isOk) { io.printInt(r.value); io.println(""); }   // 5
    var: Result<int, int> e = divide(1, 0);
    if (!e.isOk) { io.print("错误码 "); io.printInt(e.error); io.println(""); }
    return 0;
}
```

> `?` 传播运算符尚未实现（D6 后续）；错误处理目前为显式分支检查。
