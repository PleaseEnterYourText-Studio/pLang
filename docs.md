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
using t = struct;
impl t {
};
```
实现.

### 结构体方法中的this
在结构体/接口的成员函数中, `this`表示当前实例的引用:
```plang
using Circle = struct : pub Shape {
    pub func area() -> f64 {
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

## 继承
支持多继承, 一个结构体可同时继承多个父结构体.
继承默认`pri`(私有继承), 仅当显式书写`pub`时为公开继承:
```plang
using A = struct { pub func run() -> int; };
using B = struct { pub func stop() -> int; };
using S = struct : pub A, B { }; // A 公开继承, B 私有继承
```
禁止菱形继承, 一个类只能作为直接基类出现一次, 若A和B均继承自C, 则S同时继承A和B是非法的.
重名成员必须限定, 若多个父类存在同名成员/方法, 必须使用`s.A.foo()`/`s.B.foo()`显式指定, 否则编译错误.
允许向上转型, 可将子类对象/指针转换为公开继承的父类类型, 偏移由编译器在编译期计算.

### 动态分派
不提供虚函数, 无vtable, 所有调用在编译期静态解析.
提供接口(`abstract`), 父类只声明函数而无实现, 子类必须实现, 并允许"子类当作父类使用", 运行时通过函数指针表分派:
```plang
using Shape = abstract {
    pub func area() -> f64; // 仅声明, 无实现
};
using Circle = struct : pub Shape {
    pub func area() -> f64 {
        return 3.14 * this.r * this.r;
    }
};
```

### 与内存模型的关系
析构按构造逆序执行, 子类自身先析构, 再按声明逆序析构各父类.
拷贝/移动/构造/析构链同样遵循该顺序.
覆盖父类成员时, `prt`(子类可写)语义在继承下依旧成立.

## 标准库类型
- `std.vector.vec`: 可变长数组, 有运行环境下可使用堆内存, 否则abort.
- `std.string.str`: 可变ASCII字符串, 有运行环境下可使用堆内存, 否则强制使用栈.
- `std.string.wstr`: 可变UTF-32字符串，有运行环境下可时候堆内存, 否则强制使用栈.

