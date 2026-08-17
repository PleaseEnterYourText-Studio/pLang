# PLang 语言文档

> **实现状态**：本文为语言设计文档。已实现：指针/数组、结构体/联合/位域/对齐、泛型结构体与泛型函数、sizeof、RAII（construction/destroy）、
> 继承与 abstract 基础、move、extern FFI、变参、goto/switch、原子操作、多线程、堆内存、标准库（io/thread/mem/atomic/option/result/vector/string/sqlite）。
> 设计中（尚未实现）：`@` 引用扩展、`a...b` 范围循环、模板函数自动生成、`?` 错误传播。
> 编译器：LLVM 优化（-O0~-O3）、DWARF 调试信息、错误恢复、独立编译单元、LSP（悬停/补全/跳转/重命名）。
> 标准库通过 `import std.xxx` 独立编译为 `.o` 并与用户程序链接，见 `stdlib.md`。

# 包
## 包的声明
一个目录为一个包, 目录内所有 `.plang` 源文件同属一个包.
每个源文件需在顶部声明其所属包:
```plang
package foo;
```
`impl` 仅在同一包内生效, 跨包无法扩展结构体的实现.

## 包的引用
使用 `import` 引入其他包的符号:
```plang
import std.vector;
using vec = vector.vec<i32>;
```
`import` 用于引入, `using` 用于类型别名, 二者职责不同.
顶层符号(函数/结构体/类型)默认仅包内可见, 显式 `pub` 修饰后对外公开.
禁止循环依赖: A 包 import B 包且 B 包 import A 包时, 编译报错.

# 类型系统
## 变量修饰
- `var`: 表示变量, 其数据可变.
- `val`: 表示常量, 其数据不可变且必须当地构造.
- `moved`: 表示`将亡值`, 例如`函数返回`/`显示移动`.

在声明变量时, **不可省略**其类型修饰.
例如, 声明一个类型为长整形的变量:
```plang
var a = 1ll;
```
或者:
```plang
var: i64 a;
```
类型系统可根据后面的初始值进行类型推导, 整数默认为`int`, 字符默认为`char`, 字符串默认为`string`.
**将亡值**不可作为类型修饰, 只出现在非平凡类型的`移动构造/赋值函数`中, 被移动的变量将不会调用`析构函数`.
对于一个`val`修饰的变量, 可称为`常量`, 需在定义处构造, 不可修改, 否则调用abort.

### 移动语义
采用Rust风格的显式移动, 使用`move`关键字转移所有权:
```plang
var a = 1;
var b = move a;   // a 的所有权移交给 b, 之后 a 不可再使用
```
- 移动后原变量(`a`)失效, 再访问触发编译错误.
- 移动应用于堆内存/非平凡类型时, 被移动的变量不会调用析构函数.

### 变量命名规范
变量/常量名由字母/数字/下划线组成, 不能以数字开头, 不可与保留字重名:
```plang
var: int myValue = 1;
val: string userName = "plang";
```

#### 保留字
以下单词被语言保留, 不可用作标识符(变量名/函数名/类型名等):
- `package` `import`: 包声明与引用.
- `var` `val` `moved`: 变量修饰.
- `func` `impl` `return`: 函数定义/实现/返回.
- `using` `struct` `abstract`: 类型定义.
- `pub` `prt` `pri`: 访问权限.
- `extern`: 外部函数声明 (FFI).
- `null`: 空指针字面量.
- `this` `thisType`: 当前实例与自身类型.
- `type`: 模板类型参数.
- `as`: 强制类型转换.
- `if` `else` `while` `for`: 流程控制.
- `int` `char` `string` 等内置类型名: 见`类型系统`.


### 类型转换
类型转换分为自动转换与强制转换.
自动转换发生在声明/赋值时显式声明了目标类型, 编译器按目标类型自动转换:
```plang
var: int a = b;
```
强制转换使用`as`关键字, 显式转换:
```plang
var: int a = int as b;
```
无类型标注且类型不符时编译报错, 不会自动推导错位类型.
取变量地址以转换为指针, 使用`&`操作符:
```plang
var: T a;
var -> var: T p = &a;
```
数组可转换为指向其首元素的指针:
```plang
var: T[1] a;
var -> var: T p = a;
```

### 变量初始化
变量通过`=`赋值初始化, 或通过`{...}`调用构造函数初始化:
```plang
var: int a = 1;          // 赋值初始化
var: int a{1};           // 构造函数初始化
val: string s = "hi";    // 字符串字面量
```
数组与结构体使用`{...}`初始化:
```plang
var: int[3] a = {1, 2, 3};
using t = struct { pub val: int x; pub val: int y; };
var: t point = {1, 2};   // 按成员顺序初始化
```
`val`常量必须在定义处初始化, `var`可不初始化(默认值为0).

## 平凡类型
包含以下整数类型: 
- `i32`: 32位有符号整数.
- `int`: 32位有符号整数, `i32`别名.
- `i16`: 16位有符号整数.
- `i64`: 64位有符号整数.
- `i8`: 8位有符号整数.
+ `u32`: 32位无符号整数.
+ `uint`: 32位无符号整数，`u32`别名.
+ `u16`: 16位无符号整数.
+ `u64`: 64位无符号整数.
+ `u8`: 8位无符号整数, `char`别名.
以及以下字符(串)类型:
- `char`: 8位ASCII码字符.
- `wchar`: UTF-32编码字符.
+ `string`: ASCII只读字符串.
+ `wstring`: UTF-32只读字符串.
以及以下浮点类型:
- `f32`: 32位单精度浮点数.
- `f64`: 64位双精度浮点数.
整数默认字面量类型为`int`, 浮点字面量默认类型为`f64`.

平凡类型即编译器自动生成`析构`与`拷贝/自动的构造/赋值`函数的类型.
一般情况下, 对于以上类型, 其构造函数位赋值为0, 析构为空.
当然也可以通过调用其构造函数的方式构造其变量, 见下文`变量与常量`.

## 数组, 指针与引用
### 数组
数组是一组数据:
```plang
var: T[1] a;
```
长度不可变, 可通过类型转换为指针, 见`类型转换`.

### 指针
指针是指向一个变量的类型:
```plang
var: T a;
var -> var: T p1;
var -> var p2; // 可省略其真实类型
```
显示书写`var`/`val`可避免修改误底层数据. 
如果书写指向`var`却实际指向`val`触发编译错误, 除非强制类型转换.
```
val: T a{0};
var -> val p;
var -> var p; // 编译报错
```
对于指针的解引用, 可使用`*p`:
```plang
var: T a;
var -> var p;
*p = 1;
```
或者使用`.member`访问/调用其成员.
### 引用
引用与指针类似, 可延长生命周期:
```plang
var: T a;
var b@a;
b = 1; // a = 1;
var: T c;
b@c;
b = 2; // c = 2;
```
`var`引用不可引用一个`val`变量.

## 结构体与模板
包含成员, 作为一个整体使用, 需使用:
- `pub`: 公共可读写.
- `prt`: 可读, 仅内部可写.
- `pri`: 仅内部读写.
来确定其权限, 且需显示定义类型.
使用`using`将名字设为一个匿名`struct`的别名:
```plang
using t = struct {
    pub val: T a;
    pub func .converter();
    pub func .construction() : int;
    pub func getData() : T;
};
```
其内部函数可在直接在内部实现, 也可在同包内用`impl`实现:
```plang
impl t.converter {
    a = 114514;
}
```
结构体同理, 可用:
```plang
using t = struct;
impl t {
};
```
实现.

### 结构体方法中的this
在结构体/接口的成员函数中, `this`表示当前实例的引用:
```plang
using Circle = struct : pub Shape {
    pub func area() : f64 {
        return 3.14 * this.r * this.r;
    }
};
```
`this`是一个`@`引用, 类型为当前结构体, 可省略前缀直接访问成员.

### 模板
模板是一种编译期能力, 模板参数必须为编译期可得值的数据:
```plang
func foo<a: int>() {
    return a * 2;
}
```
或者是将`type`作为模板类型:
```
func foo<T: type>(val: T a, val: T b) {
    return a + b;
}
```
如果函数未声明将自动生成模板函数:
```
func foo1<T: type>(val: T a) : T {
    return a;
}

func foo2(val a) : typeof(a) {
    return a;
}
```
`typeof`见下文.
模板可同样应用于结构体:
```
using t = struct<T: type> {
    val: T a;
};
```

## 非平凡类型
与`平凡类型`相反, 可使用一下方法定义几个函数:
```plang
using t = struct {
    pub func .construction() {} // 构造函数
    pub func .destroy() {} // 析构函数
    pub func .construction(val: thisType d) {} // 拷贝构造函数
    pub func .copy(val: thisType d) {} // 拷贝赋值函数
    pub func .construction(moved: thisType d) {} // 移动构造函数
    pub func .copy(moved: thisType d) {} // 移动赋值函数
};
```
其中`thisType`为`pri`, 用于表示这个类型, 也可使用上面的t.
为了区分显示调用和自动调用的函数, 这些函数应该在开头加上`.`.

## 继承
支持多继承, 一个结构体可同时继承多个父结构体.
继承默认`pri`(私有继承), 仅当显式书写`pub`时为公开继承:
```plang
using A = struct { pub func run() : int; };
using B = struct { pub func stop() : int; };
using S = struct : pub A, B { }; // A 公开继承, B 私有继承
```
禁止菱形继承, 一个类只能作为直接基类出现一次, 若A和B均继承自C, 则S同时继承A和B是非法的.
重名成员必须限定, 若多个父类存在同名成员/方法, 必须使用`s.A.foo()`/`s.B.foo()`显式指定, 否则编译错误.
允许向上转型, 可将子类对象/指针转换为公开继承的父类类型, 偏移由编译器在编译期计算.

### 多态与分派
分派分为静态分派与动态分派.
普通方法调用走静态分派, 编译期直接确定目标, 无vtable, 性能与直接调用一致.
接口(`abstract`)走动态分派, 父类只声明函数而无实现, 子类必须实现, 运行时通过函数指针表分派:
```plang
using Shape = abstract {
    pub func area() : f64; // 仅声明, 无实现
};
using Circle = struct : pub Shape {
    pub func area() : f64 {
        return 3.14 * this.r * this.r;
    }
};
```

### 与内存模型的关系
析构按构造逆序执行, 子类自身先析构, 再按声明逆序析构各父类.
拷贝/移动/构造/析构链同样遵循该顺序.
覆盖父类成员时, `prt`(子类可写)语义在继承下依旧成立.

## 已实现的附加语言特性

以下特性已实现并有测试覆盖，补充说明（详情见 `stdlib.md`）：

### 泛型结构体实例化
`struct<T: type>` 定义模板，`Name<Arg1, Arg2>` 使用（支持多参数、嵌套、泛型方法）：
```plang
using Box = struct<T: type> {
    pub val: T value;
};
var: Box<int> b = {42};          // 实例化 Box<int>
var: Box<Box<int>> nb = {{7}};   // 嵌套实例化
```

### 泛型函数
`func foo<T: type>(...)` 定义，调用时 `foo<int>(args)` 实例化（支持多参数、与泛型结构体组合、跨包调用）：
```plang
func maxOf<T: type>(val: T a, val: T b) : T {
    if (a > b) { return a; }
    return b;
}
var: int m = maxOf<int>(3, 7);       // 7
var: f64 mf = maxOf<f64>(1.5, 2.5);  // 2.5
```
泛型结构体方法暂不克隆，标准库以自由泛型函数形式提供（如 `vector.push<int>(&v, x)`）。

### sizeof
`sizeof(T)` 返回类型字节数（编译期常量）：
```plang
sizeof(i8)       // 1
sizeof(int)      // 4
sizeof(i64)      // 8
sizeof(Box<int>) // 4
```

### RAII：construction / destroy
变量声明后自动调用 `.construction`，作用域退出时逆序调用 `.destroy`：
```plang
using File = struct {
    pub func .construction() : int { io.println("open"); return 0; }
    pub func .destroy() : int { io.println("close"); return 0; }
};
func main() : int {
    var: File f;   // 自动 construction
    return 0;      // 块结束自动 destroy
}
```
已知限制：`return`/`goto` 提前退出时不执行析构。

### goto / label / switch
```plang
label start;             // 定义标签
goto start;              // 跳转（支持前向跳转）
switch (n) {             // 整数 switch
    case 1: io.println("one");
    case 2: io.println("two");
    default: io.println("other");
}
```

### 借用检查（悬垂返回检测）
返回局部变量地址在编译期报错（D5 基础）：
```plang
func bad() -> var: ptr {
    var: i64 x = 42;
    return &x;   // 编译错误：cannot return reference to local variable 'x'
}
```
参数指针、堆内存指针（`mem.malloc`）可正常返回。完整借用规则（可变/不可变冲突）尚未实现。

### extern 全局数据
`extern var` 声明外部全局变量（FFI）：
```plang
extern var stdin : ptr;
```

## 标准库类型
- `std.string.str`: 可变ASCII字符串（**设计中，尚未实现**）.
- `std.string.wstr`: 可变UTF-32字符串（**设计中，尚未实现**）.

已实现的标准库（位于 `std/`，通过 `import std.xxx` 独立编译链接）：

| 库 | 功能 | 详述 |
|----|------|------|
| `std.io` | 标准输入输出 | 见下文 `# 标准输入输出` |
| `std.thread` | 多线程 | 见下文 `# 多线程` |
| `std.mem` | 堆内存 | `mem.malloc/free/memcpy/memset/memcmp`，extern 直通 libc |
| `std.atomic` | 原子操作 | `atomic.load/store/add/sub/exchange/cas` + 内存序参数 |
| `std.option` | null 安全 | `Option<T>` 泛型结构体：`{true, v}` 有值 / `{false, 0}` 无值 |
| `std.result` | 错误处理 | `Result<T, E>` 泛型结构体：`{true, v, 0}` 成功 / `{false, 0, e}` 失败 |
| `std.vector` | 动态数组 | `Vec<T>` 泛型容器：`vector.new/push/get/len/pop/destroy`，自动扩容 |
| `std.string` | 字符串操作 | `string.len/cat/dup/eq/cmp`（char\* 字符串） |
| `std.sqlite` | 数据库 | SQLite 绑定：`sqlite.open/close/exec/query` + 按列取值，自动链接 -lsqlite3 |

# 多线程 (std.thread)
`std.thread` 是**真正的源码库**，位于 `std/thread/thread.plang`：`import std.thread;` 后编译器将该包独立编译为 `.o` 并与用户程序链接。
其中 `join` / `yield` / `lock` / `unlock` / `destroy` 是库内用源码实现的函数（内部通过 `extern` 调用 pthread），
`spawn` / `sleep` / `mutex.create` 因需要蹦床与全局锁池而保留为编译器内置。

## 线程
使用 `thread.spawn` 启动一个新线程运行一个**无参函数**, 返回线程句柄（`var -> var: ptr`）;
`thread.join` 阻塞等待该线程结束:
```plang
import std.thread;

func worker() : int {
    // 线程体
    return 0;
}

func main() : int {
    var -> var: ptr t1 = thread.spawn(worker);
    var -> var: ptr t2 = thread.spawn(worker);
    thread.join(t1);
    thread.join(t2);
    return 0;
}
```
- 线程入口必须是无参函数, 返回值被忽略.
- `join` 前线程与主线程并行执行; 程序退出前应 `join` 所有已 spawn 的线程.

## 互斥锁
互斥锁由编译器维护一个全局锁池 (上限 64 把), `thread.mutex.create()` 分配并初始化一把锁,
返回其地址（`var -> var: ptr`）; `lock` / `unlock` / `destroy` 加锁 / 解锁 / 销毁:
```plang
var -> var: ptr m = thread.mutex.create();
thread.lock(m);
// 临界区
thread.unlock(m);
thread.destroy(m);
```

## 其他
- `thread.sleep(ms)`: 当前线程休眠指定毫秒数（编译器内置）.
- `thread.yield()`: 当前线程主动让出 CPU（源码库实现）.

# 外部函数接口 (extern FFI)
使用 `extern func` 声明 C 函数, 编译器映射为 LLVM 外部声明 (declare), 链接期解析符号:
```plang
extern func sched_yield() : int;
extern func pthread_join(var -> var: ptr handle, var -> var: ptr result) : int;
```
- 参数类型使用指针类型 `var -> var: ptr`（通用指针）, 空指针字面量写作 `null`.
- extern 声明属于声明所在包, 遵循包可见性规则（跨包调用需 `pub`）.

# 函数
## 函数的定义
以`func`关键字定义, 返回值书写方式与变量声明统一:
- `func foo() : T` 返回类型`T`（与 `var: T a` 一致）.
- `func foo() -> var T` 返回`T`指针（与 `var -> var: T p` 的箭头写法一致, 也可写 `-> var: T`）.
```plang
func foo() : T {
}
func bar() -> var T {
}
```
其中`T`为返回类型. 无返回省略. 旧写法 `-> T` 仍兼容（值返回）.
函数参数使用`val`/`var`修饰, 对于`移动构造/赋值函数`可用`moved`, 格式与变量声明一致, 多个参数以逗号分隔:
```plang
func foo(val: int a, var: string b) : int {
    return a;
}
```
当然可以使用无实现, 在同一包内用`impl`实现:
```
func foo() : int;
impl foo {
    return 1;
};
```

## 程序入口
程序通常以`main`函数为入口:
```plang
func main() : int {
    return 0;
}
```
`main`返回类型和返回值可省略.

## 表达式
### 函数调用
以`名字(参数列表)`调用函数, 参数逗号分隔:
```plang
foo(1, 2);
```
### 成员访问与方法调用
使用`.`访问成员或调用方法, 支持链式调用:
```plang
s.a.foo();
system.io.output("hello");
```
### 函数返回值
使用`return`返回, 返回值的表达式遵循C语言运算符优先级.

# 基础语法
## 注释
支持行注释与块注释:
- 行注释以`//`开始, 到行尾结束.
- 块注释以`/*`开始, `*/`结束, 可跨行.
```plang
// 行注释
/* 块注释 */
```
## 字符串与字符
字符串/字符字面量支持以下转义序列:
- `\\` 反斜杠.
- `\"` 双引号.
- `\'` 单引号.
- `\n` 换行.
- `\t` 制表符.
- `\r` 回车.
- `\0` 空字符.
`string`与`wstring`会在末尾添加`\0`.

## 数字字面量
- 十进制: `0`, `42`, `1ll`(`ll`后缀表示`i64`).
- 十六进制: 以`0x`开头, 如`0xFF`, `0x1A`.
- 八进制: 以`0o`开头, 如`0o17`.
- 二进制: 以`0b`开头, 如`0b1010`.
- 浮点: `3.14`, `2.5f`(`f`后缀表示`f32`).

## 运算符
运算符参考C语言:
- 算术: `+` `-` `*` `/` `%`.
- 比较: `==` `!=` `<` `>` `<=` `>=`.
- 逻辑: `&&` `||` `!`.
- 位运算: `&` `|` `^` `~` `<<` `>>`.
- 赋值: `=` `+=` `-=` `*=` `/=` `%=` `<<=` `>>=` `&=` `|=` `^=`.
- 自增自减: `++` `--`.
- 数组取下标: `[]`
- 取地址/解引用/取引用: `&` `*` `@`.
- 范围(左闭右开区间, 用于循环): `a...b`
运算符优先级与C基本一致.

优先级从高到低排列如下:
1. 后缀: `()` `.` `++` `--` `[]`
2. 范围: `a...b`
3. 一元: `!` `~` `++`(前缀) `--`(前缀) `*`(解引用) `&`(取地址) `@`
4. 乘除: `*` `/` `%`
5. 加减: `+` `-`
6. 移位: `<<` `>>`
7. 关系: `<` `>` `<=` `>=`
8. 相等: `==` `!=`
9. 按位与: `&`
10. 异或: `^`
11. 按位或: `|`
12. 逻辑与: `&&`
13. 逻辑或: `||`
14. 赋值: `=` `+=` `-=` 等

## 流程控制
### if / else
用于判断:
```plang
if (condition) {
} else if (condition) {
} else {
}
```
条件需用`()`包裹, 分支体必须使用`{}`.

### while
用于循环:
```plang
while (condition) {
}
```

### for
用于循环:
```plang
for (init; condition; update) {
}
```
或者是range-base loop:
```plang
for (var i = 1...5) {
}
for (var i = 1...5 step 2) {
}
var: i32[6] a;
for (var i = a[1...5 step 1]) {
}
```
默认步长为1.

# 标准输入输出 (std.io)
`std.io` 是真正的源码库，位于 `std/io/io.plang`：`import std.io;` 后编译器将该包独立编译为 `.o` 并与用户程序链接。
数字→字符串的格式化在源码内实现（不依赖 libc 的 printf 舍入，跨平台位级一致）。

## 输出
- `io.print(s)`: 输出字符串（不换行）, `s` 为字符串或字符缓冲指针.
- `io.println(s)`: 输出字符串并换行.
- `io.printChar(c)`: 输出单个字符.
- `io.printInt(n)`: 输出整数.
- `io.printFloat(f)`: 输出浮点数（保留 6 位小数）.
- `io.error(s)`: 输出到标准错误.
- `io.flush()`: 冲刷输出缓冲.

## 输入
- `io.readChar()`: 读取一个字符（EOF 返回 -1）.
- `io.readInt()`: 读取一个整数.
- `io.readLine(buf, size)`: 读取一行到缓冲区（含换行）.
```plang
import std.io;

func main() : int {
    io.println("请输入一个整数:");
    var: int n = io.readInt();
    io.print("你输入了: ");
    io.printInt(n);
    io.println("");
    return 0;
}
```

# 堆内存 (std.mem)

`std.mem` extern 直通 libc 分配器，配合指针算术使用：

- `mem.malloc(n)`: 分配 `n` 字节，返回指针（失败返回 `null`）.
- `mem.free(p)`: 释放内存.
- `mem.memcpy(dst, src, n)`: 复制 `n` 字节.
- `mem.memset(dst, value, n)`: 将 `n` 字节设为 `value`.
- `mem.memcmp(a, b, n)`: 比较 `n` 字节，返回 0/负数/正数.

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

> `n` 为字节数；按元素访问需自行乘元素大小（`int` 为 4 字节）。

# 原子操作与 volatile

## 原子操作 (std.atomic)

`std.atomic` 是编译器内置（LLVM atomicrmw/cmpxchg），用于多线程共享计数器/标志：

- `atomic.load(p)`: 原子读取.
- `atomic.store(p, v)`: 原子写入.
- `atomic.add(p, v)` / `atomic.sub(p, v)`: 原子加/减，**返回旧值**.
- `atomic.exchange(p, v)`: 原子交换，返回旧值.
- `atomic.cas(p, expect, desired)`: 比较交换，返回是否成功（bool）.

内存序参数（可选，最后一个）：`0`=relaxed、`1`=acquire、`2`=release、`3`=acq_rel、`4`=seq_cst（默认）。
`p` 必须是指向整数类型的指针。

```plang
import std.atomic;
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

# 可选值与错误处理 (std.option / std.result)

库级安全基础，用泛型结构体实现（完整编译期检查需类型系统后续支持）。

## std.option

`Option<T>` 表示"可能有值，也可能没有"：

- `isSome`: 是否包含值（bool）.
- `value`: 值（`isSome=false` 时无效）.

```plang
import std.option;
var: Option<int> some = {true, 42};
var: Option<int> none = {false, 0};
if (some.isSome) { io.printInt(some.value); }   // 42
if (!none.isSome) { io.println("none"); }
```

## std.result

`Result<T, E>` 表示"成功携带值，或失败携带错误码"：

- `isOk`: 是否成功（bool）.
- `value`: 成功值（`isOk=false` 时无效）.
- `error`: 错误码（`isOk=true` 时无效）.

```plang
import std.result;
func divide(a: int, b: int) -> Result<int, int> {
    if (b == 0) { return {false, 0, 1}; }   // Err(1)
    return {true, a / b, 0};                // Ok(a/b)
}
var: Result<int, int> r = divide(10, 2);   // r.isOk=true, r.value=5
```

> `?` 传播运算符尚未实现；错误处理目前为显式分支检查.

# 动态数组 (std.vector)

`Vec<T>` 是泛型结构体（堆内存），可变长数组，自动扩容：

- `vector.new<T>(cap)`: 创建空 Vec（预留 `cap` 容量），返回 `Vec<T>`.
- `vector.push<T>(&v, item)`: 末尾追加（容量不足自动翻倍）.
- `vector.get<T>(&v, i)`: 取第 `i` 个元素（不做越界检查）.
- `vector.len<T>(&v)`: 元素个数.
- `vector.pop<T>(&v)`: 弹出并返回末尾元素.
- `vector.destroy<T>(&v)`: 释放堆内存.

```plang
import std.vector;
var: Vec<int> v = vector.new<int>(2);
vector.push<int>(&v, 10);
vector.push<int>(&v, 20);
vector.push<int>(&v, 30);        // 触发扩容
io.printInt(vector.len<int>(&v));  // 3
vector.destroy<int>(&v);
```

> 泛型结构体方法暂不克隆，故以自由泛型函数 `vector.xxx<T>(&v, ...)` 形式提供；
> `sizeof(T)` 用于按元素大小分配内存.

# 数据库 (std.sqlite)

SQLite 数据库绑定（extern FFI 直通 libc sqlite3，**自动链接 `-lsqlite3`**，无需手动配置）：

- `sqlite.open(path)`: 打开/创建数据库文件，成功返回句柄，失败返回 `null`.
- `sqlite.close(db)`: 关闭数据库.
- `sqlite.exec(db, sql)`: 执行无返回的 SQL（CREATE/INSERT/UPDATE/DELETE），0=成功.
- `sqlite.query(db, sql)`: 查询并逐行打印结果（列以 `|` 分隔）；失败打印错误信息.
- `sqlite.prepare(db, sql)`: 准备一条 SQL，返回语句句柄（失败返回 `null`）.
- `sqlite.step(stmt)`: 推进一行；`true`=有数据行，`false`=结束/出错.
- `sqlite.columnCount(stmt)`: 当前行列数.
- `sqlite.columnInt(stmt, col)`: 取当前行第 `col` 列为整数.
- `sqlite.columnText(stmt, col)`: 取当前行第 `col` 列为字符串（指向 sqlite 内部缓冲）.
- `sqlite.finalize(stmt)`: 释放语句.
- `sqlite.errmsg(db)`: 最近一次错误的描述信息.

```plang
import std.io;
import std.sqlite;

func main() : int {
    var -> var: ptr db = sqlite.open("test.db");
    if (db == null) { io.println("open failed"); return 1; }

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
> `columnText` 返回的指针指向 sqlite 内部缓冲，下次 `step` 前有效；需要长期保存请 `string.dup`.

# 字符串操作 (std.string)

`char*` 字符串（`\0` 结尾）操作库。需要 `import std.string;`（拼接/复制返回堆内存，用完 `mem.free`）：

- `string.len(s)`: 字符串长度（不含末尾 `\0`）.
- `string.cat(a, b)`: 拼接 `a + b`，返回**新分配**的堆字符串（调用方 free）.
- `string.dup(s)`: 复制字符串，返回新分配的堆字符串（调用方 free）.
- `string.eq(a, b)`: 内容相等比较（非指针比较），返回 bool.
- `string.cmp(a, b)`: 字典序比较，返回 `<0 / 0 / >0`.

```plang
import std.io;
import std.mem;
import std.string;

func main() : int {
    var: string s = "hello";
    var: string t = string.cat(s, " world!");
    io.println(t);                    // hello world!
    if (string.eq(s, "hello")) {
        io.println("eq");
    }
    var: int c = string.cmp("a", "b");  // 负数
    mem.free(t);                      // cat 的结果要释放
    return 0;
}
```

> 字符串字面量是全局常量（不可 free）；`cat`/`dup` 返回堆内存需释放。
> 编译器支持关键字作标识符（`string.len` 里的 `string`）与 char 转义（`'\n'` `'\0'` 等）。
