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
| `std.vector` | `import std.vector;` | 动态数组：`Vec<T>` 泛型容器，自动扩容 |
| `std.string` | `import std.string;` | 字符串操作：len/cat/dup/eq/cmp + 子串/查找/大小写/替换/解析 |
| `std.fs` | `import std.fs;` | 文件系统：读/写/定位/删除/重命名/读整个文件 |
| `std.buffer` | `import std.buffer;` | 可变长字符串构建器（StringBuilder） |
| `std.map` | `import std.map;` | 字符串键哈希表：`Map<T>` 泛型容器 |
| `std.sqlite` | `import std.sqlite;` | 数据库：SQLite 绑定，自动链接 -lsqlite3 |

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

---

# std.vector 动态数组

`Vec<T>` 是泛型结构体（堆内存），可变长数组，自动扩容。需要 `import std.vector;`。

| 函数 | 说明 |
|------|------|
| `vector.new<T>(cap)` | 创建空 Vec（预留 `cap` 个元素容量），返回 `Vec<T>` |
| `vector.push<T>(&v, item)` | 末尾追加（容量不足自动翻倍扩容） |
| `vector.get<T>(&v, i)` | 取第 `i` 个元素（不做越界检查） |
| `vector.len<T>(&v)` | 元素个数 |
| `vector.pop<T>(&v)` | 弹出并返回末尾元素 |
| `vector.destroy<T>(&v)` | 释放堆内存 |

```plang
import std.io;
import std.vector;

func main() : int {
    var: Vec<int> v = vector.new<int>(2);
    vector.push<int>(&v, 10);
    vector.push<int>(&v, 20);
    vector.push<int>(&v, 30);        // 触发扩容
    io.printInt(vector.len<int>(&v));  // 3
    io.println("");
    io.printInt(vector.get<int>(&v, 0));  // 10
    io.println("");
    vector.destroy<int>(&v);
    return 0;
}
```

> 泛型结构体方法暂不克隆，故以自由泛型函数 `vector.xxx<T>(&v, ...)` 形式提供；
> `sizeof(T)` 用于按元素大小分配内存。

---

# std.sqlite 数据库

SQLite 数据库绑定（extern FFI 直通 libc sqlite3，**自动链接 `-lsqlite3`**，无需手动配置）。

| 函数 | 说明 |
|------|------|
| `sqlite.open(path)` | 打开/创建数据库文件，成功返回句柄，失败返回 `null` |
| `sqlite.close(db)` | 关闭数据库 |
| `sqlite.exec(db, sql)` | 执行无返回的 SQL（CREATE/INSERT/UPDATE/DELETE），0=成功 |
| `sqlite.query(db, sql)` | 查询并逐行打印结果（列以 `\|` 分隔）；失败打印错误信息 |
| `sqlite.prepare(db, sql)` | 准备一条 SQL，返回语句句柄（失败返回 `null`） |
| `sqlite.step(stmt)` | 推进一行；`true`=有数据行，`false`=结束/出错 |
| `sqlite.columnCount(stmt)` | 当前行列数 |
| `sqlite.columnInt(stmt, col)` | 取当前行第 `col` 列为整数 |
| `sqlite.columnText(stmt, col)` | 取当前行第 `col` 列为字符串（指向 sqlite 内部缓冲） |
| `sqlite.finalize(stmt)` | 释放语句 |
| `sqlite.errmsg(db)` | 最近一次错误的描述信息 |

```plang
import std.io;
import std.sqlite;

func main() : int {
    var -> var: ptr db = sqlite.open("test.db");
    if (db == null) { io.println("open failed"); return 1; }

    sqlite.exec(db, "DROP TABLE IF EXISTS user");
    sqlite.exec(db, "CREATE TABLE user (id INT, name TEXT, score INT)");
    sqlite.exec(db, "INSERT INTO user VALUES (1, 'Alice', 90)");
    sqlite.exec(db, "INSERT INTO user VALUES (2, 'Bob', 75)");

    // 按列取值：数据读进变量做计算
    var -> var: ptr stmt = sqlite.prepare(db, "SELECT id, name, score FROM user");
    var: int total = 0;
    var: int n = 0;
    while (sqlite.step(stmt)) {
        io.printInt(sqlite.columnInt(stmt, 0));
        io.print(": ");
        io.println(sqlite.columnText(stmt, 1));
        total = total + sqlite.columnInt(stmt, 2);
        n = n + 1;
    }
    io.printInt(total / n);   // 平均分 82
    io.println("");
    sqlite.finalize(stmt);
    sqlite.close(db);
    return 0;
}
```

> `query` 是"打印结果"的便捷版；`prepare/step/columnXxx` 是把数据读进变量的底层 API。
> `columnText` 返回的指针指向 sqlite 内部缓冲，下次 `step` 前有效；需要长期保存请 `string.dup`。


---

# std.string 字符串操作

`char*` 字符串（`\0` 结尾）操作库。需要 `import std.string;`（拼接/复制返回堆内存，用完 `mem.free`）。

| 函数 | 说明 |
|------|------|
| `string.len(s)` | 字符串长度（不含末尾 `\0`） |
| `string.cat(a, b)` | 拼接 `a + b`，返回**新分配**的堆字符串（调用方 free） |
| `string.dup(s)` | 复制字符串，返回新分配的堆字符串（调用方 free） |
| `string.eq(a, b)` | 内容相等比较（非指针比较），返回 bool |
| `string.cmp(a, b)` | 字典序比较，返回 `<0 / 0 / >0` |

**扩展（自举用，返回堆内存的调用方 `mem.free`）：**

| 函数 | 说明 |
|------|------|
| `string.sub(s, start, n)` | 截取子串 `s[start..start+n)`，越界自动裁剪 |
| `string.find(hay, needle)` | 子串首次出现位置，未找到返回 -1 |
| `string.rfind(hay, needle)` | 子串最后一次出现位置，未找到返回 -1 |
| `string.contains(hay, needle)` | 是否包含子串 |
| `string.startsWith(s, prefix)` | 前缀匹配 |
| `string.endsWith(s, suffix)` | 后缀匹配 |
| `string.trim(s)` | 去除首尾空白（空格/制表/换行/回车） |
| `string.toUpper(s)` / `string.toLower(s)` | 大小写副本 |
| `string.replace(s, from, to)` | 替换所有出现 |
| `string.parseInt(s, base)` | 按 base（2/8/10/16）解析，失败返回 0 |
| `string.intToStr(n)` | 整数 → 十进制堆字符串 |
| `string.isDigit/isAlpha/isSpace/isAlnum/isUpper/isLower(c)` | 字符分类 |
| `string.toUpperChar/toLowerChar(c)` | 字符大小写转换 |

> 字符串字面量是全局常量（不可 free）；`cat`/`dup`/`sub`/`trim`/`toUpper`/`toLower`/`replace`/`intToStr` 返回堆内存需释放。

```plang
import std.io;
import std.mem;
import std.string;

func main() : int {
    var -> var: ptr s = string.trim("  hello  ");
    io.println(s);                     // hello
    var: int i = string.find("a-b-c", "-");
    io.printInt(i); io.println("");    // 1
    var: i64 n = string.parseInt("ff", 16);
    io.printInt(int as n); io.println("");  // 255
    mem.free(s);
    return 0;
}
```

---

# std.fs 文件系统

`extern` 直通 libc stdio。`FILE*` 句柄用 `ptr` 表示，打开失败返回 `null`。需要 `import std.fs;`。

| 函数 | 说明 |
|------|------|
| `fs.openFile(path, mode)` | 打开文件（`"r" "w" "a" "rb" "wb" "r+"...`），失败返回 `null` |
| `fs.closeFile(fp)` | 关闭，返回 0=成功 |
| `fs.readBytes(buf, size, count, fp)` | 读 `count` 个 `size` 字节块，返回实际块数 |
| `fs.writeBytes(buf, size, count, fp)` | 写入，返回实际块数 |
| `fs.readByte(fp)` | 读一个字节，EOF 返回 -1 |
| `fs.writeByte(c, fp)` | 写一个字节 |
| `fs.writeStr(fp, s)` | 写字符串（不含结束符） |
| `fs.writeLine(fp, s)` | 写字符串并换行 |
| `fs.seek(fp, offset, origin)` | 定位（0=头 1=当前 2=尾） |
| `fs.tell(fp)` | 当前位置 |
| `fs.eof(fp)` | 是否到文件尾 |
| `fs.size(fp)` | 文件总字节数（不改变位置） |
| `fs.removeFile(path)` | 删除文件，返回 0=成功 |
| `fs.renameFile(old, new)` | 重命名/移动，返回 0=成功 |
| `fs.exists(path)` | 文件是否存在 |
| `fs.readAll(path)` | 读整个文件到堆缓冲（`\0` 结尾），失败返回 `null`（调用方 free） |
| `fs.writeAll(path, content)` | 把字符串写入文件（覆盖），返回 bool |
| `fs.writeAllBytes(path, buf, n)` | 把前 `n` 字节写入文件（覆盖），返回 bool |

```plang
import std.fs;
import std.mem;
import std.io;

func main() : int {
    fs.writeAll("test.txt", "hello\n");
    var -> var: ptr data = fs.readAll("test.txt");
    if (data != null) {
        io.print(data);   // hello
        mem.free(data);
    }
    fs.removeFile("test.txt");
    return 0;
}
```

---

# std.buffer 字符串构建器

可变长字符缓冲（StringBuilder），用于逐步拼装字符串，避免反复 `cat` 分配。需要 `import std.buffer;`。

| 函数 | 说明 |
|------|------|
| `buffer.new()` | 创建空缓冲，返回 `Buf` |
| `buffer.append(&b, s)` | 追加字符串 |
| `buffer.appendChar(&b, c)` | 追加字符 |
| `buffer.appendInt(&b, n)` | 追加整数（十进制） |
| `buffer.appendFloat(&b, f)` | 追加浮点数（固定 6 位小数） |
| `buffer.clear(&b)` | 清空（不释放内存） |
| `buffer.cstr(&b)` | 返回堆上 `\0` 结尾副本（调用方 free） |
| `buffer.data(&b)` | 内部缓冲指针（借用，不释放） |
| `buffer.destroy(&b)` | 释放堆内存 |

```plang
import std.buffer;
import std.io;
import std.mem;

func main() : int {
    var: Buf b = buffer.new();
    buffer.append(&b, "x = ");
    buffer.appendInt(&b, 42);
    buffer.appendChar(&b, '\n');
    var -> var: ptr s = buffer.cstr(&b);
    io.print(s);            // x = 42
    mem.free(s);
    buffer.destroy(&b);
    return 0;
}
```

---

# std.map 哈希表

字符串键 → 泛型值的开放寻址哈希表，桶数为 2 的幂，负载因子超过 3/4 自动翻倍扩容。需要 `import std.map;`。

| 函数 | 说明 |
|------|------|
| `map.new<T>(cap)` | 创建空表，返回 `Map<T>` |
| `map.put<T>(&m, key, v)` | 插入/覆盖键值对（键自动复制） |
| `map.get<T>(&m, key)` | 取值，返回 `Option<T>`（`isSome=false` 表示未找到） |
| `map.contains<T>(&m, key)` | 是否包含键 |
| `map.remove<T>(&m, key)` | 删除键，返回是否删除成功 |
| `map.len<T>(&m)` | 有效条目数 |
| `map.cap<T>(&m)` | 桶数量（2 的幂） |
| `map.keyAt<T>(&m, n)` | 第 n 个有效键（借用指针，不释放） |
| `map.clear<T>(&m)` | 清空（释放键副本） |
| `map.destroy<T>(&m)` | 释放整个表 |

- 键由 `put` 复制（堆副本），`destroy`/`clear`/`remove` 时释放；**值按位拷贝**，要求值类型为 POD 或由调用方管理生命周期。
- 遍历键：`for` 循环从 `keyAt(&m, 0)` 到 `keyAt(&m, len-1)`。

```plang
import std.io;
import std.map;
import std.option;

func main() : int {
    var: Map<int> m = map.new<int>(4);
    map.put<int>(&m, "apple", 3);
    map.put<int>(&m, "pear", 5);
    var: Option<int> r = map.get<int>(&m, "apple");
    if (r.isSome) {
        io.printInt(r.value); io.println("");   // 3
    }
    var: bool has = map.contains<int>(&m, "pear");
    map.destroy<int>(&m);
    return 0;
}
```

---

# 注意事项

- **`&&` / `||` 不做短路求值**（编译器按位求值再合并）。**禁止**写 `p != null && *p > 0` 或依赖短路保护内存访问的代码；请用嵌套 `if` 显式判断。
- 非泛型 `pub` 函数的链接符号是裸函数名，跨包不能重名（如 `io.readChar` 与 `fs.readChar` 会冲突）；泛型函数实例化名带包前缀（`map.get<int>`）故安全。
