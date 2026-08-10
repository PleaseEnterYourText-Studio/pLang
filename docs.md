# 函数
## 函数的定义
以`func`关键字定义
```plang
func foo() -> T {
}
```
其中`T`为返回类型. 无返回省略.
当然可以使用无实现, 在同一包内用`impl`实现:
```
func foo() -> int;
impl foo {
    return 1;
};
```

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

平凡类型即编译器自动生成`析构`与`拷贝/自动的构造/赋值`函数的类型.
一般情况下, 对于以上类型, 其构造函数位赋值为0, 析构为空.
当然也可以通过调用其构造函数的方式构造其变量, 见下文`变量与常量`.

## 数组, 指针与引用
### 数组
数组是一组数据:
```plang
var: T[1] a;
```
长度不可变, 可通过类型转换为指针.

### 指针
指针是指向一个变量的类型:
```plang
var: T a;
var -> var: T p1;
var -> var: p2; // 可省略其真实类型
```
显示书写`var`/`val`可避免修改误底层数据. 
如果书写指向`var`却实际指向`val`触发编译错误, 除非强制类型转换.
```
val: T a{0};
var -> val p;
var -> var p; // 编译报错
```
对于指针的接引用, 可使用`*p`:
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
    pub func .construction() -> int;
    pub func getData() -> T;
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
using t = struct{};
impl t {
};
```
实现.

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
func foo1<T: type>(val: T a) -> T {
    return a;
}

func foo2(val a) -> typeof(a) {
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
    pub func .copy(move: thisType d) {} // 移动赋值函数
};
```
其中`thisType`为`pri`, 用于表示这个类型, 也可使用上面的t.
为了区分显示调用和自动调用的函数, 这些函数应该在开头加上`.`.

## 标准库类型
- `std.vector`: 可变长数组, 有运行环境下可使用堆内存, 否则abort.
- `std.string`: 可变ASCII字符串, 有运行环境下可使用堆内存, 否则强制使用栈.
- `std.wstring`: 可变UTF-32字符串，有运行环境下可时候堆内存, 否则强制使用栈.

