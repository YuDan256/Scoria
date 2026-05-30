# Scoria 语言语法设计说明 (alpha)

基于当前前端（Lexer & Parser）的实现，Scoria 语言的语法设计呈现出强烈的古典罗马风格与现代系统级编程语言的严谨性。

## 0. 核心哲学 (Philosophia Nuclei)

*   **绝不依赖 C 标准库 (No C Standard Library Dependency)**：Scoria 是一门纯粹的底层系统级语言。为了保证其能够在没有任何 C 运行时 (CRT) 的裸机 (Bare Metal) 或自研操作系统上运行，Scoria 的标准库和核心实现**绝不**调用 `libc` (如 `msvcrt.dll`, `glibc` 等)。所有与外部环境的交互必须直接通过 `barbara` (FFI) 绑定操作系统的最底层内核 API (如 Windows 的 `kernel32.dll` 或 Linux 的 Syscalls)。

以下是当前支持的语法全景：

## 1. 模块与可见性 (Systema Modulorum)

Scoria 摒弃了传统的 `#include`，采用纯净的命名空间管理：

*   **声明当前模块**：`liber <模块名>;` (必须在文件顶部)
*   **引入整个模块**：`consule liber <模块名>;`
*   **跨模块访问**：通过 `.` 运算符访问模块内的公开符号，如 `<模块名>.<函数名>(...)` 或 `<模块名>.<类型名>`。
*   **精确摘录符号**：`de <模块名> excerpe <符号1>, <符号2>;`
*   **可见性修饰符**：`edita` (公开，对外可见)。默认情况下，所有声明都是私有的。
*   **外部/FFI 修饰符**：`barbara("dll_nomen")` (蛮族接口，用于声明外部动态链接库中的 C ABI 函数)。

## 2. 数据类型系统 (Typi Datorum)

支持现代工程缩写、古典罗马词缀以及辅音骨架的三轨制，在 AST 层面完全等价：

*   **有符号整数**：`i8` (minimus), `i16` (brevis), `i32` (medius), `i64` (integer)
*   **无符号整数**：`p8` (minimus purus), `p16`, `p32`, `p64`
*   **浮点数**：`f32` (fractus medius), `f64` (fractus)
*   **布尔值**：`logica` (字面量为 `verum` 和 `falsum`)
*   **字符与文本**：`littera` (单字符), `textus` (不可变字符串)
*   **空类型**：`nihil` / `nhl` (相当于 C 的 `void`)
*   **空指针值**：`nullus` / `nls` (相当于 C 的 `NULL`)

**内存多级抽象 (防线)**：
*   `via <类型>`：裸游标 (Raw Pointer)，无边界检查。
*   `cohors <类型>`：胖指针切片 (Slice)，底层为包含 `caput` (首地址, `via T`) 和 `longitudo` (长度, `i64`) 的结构体。
*   `acies[<长度>] <类型>`：静态定长数组。

## 3. 变量与常量 (Imperia et Fluxus)

*   **变量声明**：`sit [edita] <变量名>[: <类型>] [= <表达式>];` (类型与表达式至少存在其一，支持编译期推断)
*   **常量声明**：`lex [edita] <常量名>[: <类型>] = <表达式>;` (编译期不可变，支持编译期推断)

## 4. 函数与控制流 (Actio et Fluxus)

*   **函数定义**：
    ```scoria
    // 内部函数 (支持原生同构变长参数，使用 etc 声明，底层自动降级为 cohors 切片)
    actio [edita] <函数名>(<参数名>: <类型>, etc <变长参数名>: <类型>) -> <返回类型> {
        // 函数体
        redde <返回值>;
    }

    // 外部函数 (FFI，支持 C ABI 无类型变长参数)
    actio barbara("dll_nomen") <函数名>(<参数名>: <类型>, etc) -> <返回类型>;
    ```
*   **条件分支**：
    ```scoria
    si (条件) {
        // ...
    } aliter si (条件) {
        // ...
    } aliter {
        // ...
    }
    ```
*   **多路分支 (Switch)**：
    ```scoria
    elige (条件) {
        casus 1:
            // 匹配 1 时执行。默认不贯穿 (No Fallthrough)，执行完自动跳出
        casus 2, 3:
            // 支持多值匹配
        aliter:
            // 兜底分支 (default)
    }
    ```
    *注：当 `casus` 值为密集整数时，编译器底层会自动优化为 O(1) 的跳转表 (Jump Table)。*
*   **无条件跳转 (Goto)**：
    ```scoria
    sali <标签名>;
    <标签名>:
    ```
*   **循环**：
    ```scoria
    dum (条件) {
        // ...
    }
    
    per (sit i: i32 = 0; i < 10; i += 1) {
        // ...
    }
    ```
*   **循环控制**：`rumpe` / `rmp` (跳出循环, break), `perge` / `prg` (继续下一次循环, continue)
*   **异常终止**：`morere` / `mor` (触发硬件陷阱 Trap，如 x86 的 `ud2`，表示不可达或致命错误)

## 5. 内存与指针操作 (Memoria)

Scoria 提供了极其显式的内存操作指令，**绝对拒绝指针类型的隐式转换**：

*   **空值与不透明指针**：
    *   `nullus`：纯粹的空指针字面量（Bottom Type），**唯一**允许隐式转换为任何指针类型（`via T`, `cohors T`, `actio`）的值。
    *   `via nihil`：不透明指针（相当于 C 的 `void*`），表示“未知类型的内存”。`via T` 与 `via nihil` 之间**没有任何隐式转换**，必须使用 `muta` 强制跨越边界。
*   **取址与解引用**：`locus <变量>` (取地址), `tene <指针>` (解引用)。**严禁直接解引用 `via nihil`**，因为编译器无法得知目标内存的大小与布局。必须先使用 `muta` 将其转换为具体类型的指针（如 `via i8`）后方可解引用。
*   **指针偏移**：`vade(指针, 偏移量)` (指针前进), `recede(指针, 偏移量)` (指针后退)。同理，**严禁对 `via nihil` 进行偏移操作**，必须先转换为具体类型指针。
*   **动态内存分配**：
    *   `crea(<类型>)` 或 `crea(<类型>, <数量>)` (相当于 malloc/new)
    *   `neca(<指针>)` (相当于 free/delete)
*   **内存大小计算**：`magnitudo(<类型>)` 或 `magnitudo(<表达式>)` (编译期求值为 `i64` 常量，相当于 C 的 `sizeof`)
*   **强制类型转换**：`muta(<目标类型>, <表达式>)` (唯一打破类型安全边界的系统特权)

## 6. 结构体、联合体、枚举与类型别名 (Structurae, Uniones, Ordines et Imagines)

*   **枚举 (Ordo)**：
    使用 `ordo` 关键字声明一组强类型的常量集合。底层严格等价于 `i32`，零运行时开销。
    ```scoria
    ordo [edita] <枚举名> {
        <变体名1> [= <常量表达式>],
        <变体名2>, // 自动递增
        ...
    }
    // 示例: ordo TokenKind { TK_EOF, TK_IDENTIFIER = 10 }
    // 访问: TokenKind.TK_EOF
    ```
*   **类型别名 (Imago)**：
    使用 `imago` 关键字为现有类型创建透明的等价别名。在底层编译时，别名会被完全展开，零运行时开销。
    ```scoria
    imago [edita] <别名> = <目标类型>;
    // 示例: imago AstNodePtr = via AstNode;
    ```
*   **标准结构体**：遵循 C ABI 内存对齐。
    ```scoria
    forma [edita] <结构体名> {
        sit <字段名>: <类型>;
    }
    ```
*   **实密结构体**：使用 `densa` 关键字，剥离对齐填充 (Packed)。形容词后置。
    ```scoria
    forma [densa] [edita] <结构体名> { ... }
    ```
*   **联合体**：所有字段共享同一块内存，大小为最大字段的大小（对齐到最大对齐要求）。
    ```scoria
    unio [edita] <联合体名> {
        sit <字段名>: <类型>;
    }
    ```

## 7. 表达式与字面量 (Lexicon)

*   **内置宏**：
    *   `scribe(arg1, arg2, ...)` (安全变长输出)
    *   `lege(arg1, arg2, ...)` (安全变长输入，自动推导类型，参数必须为可赋值的左值)
*   **复合字面量 (Compound Literals)**：采用现代语法，底层作为左值分配在栈上，可直接取地址或访问成员。
    *   **数组字面量**：`[<值1>, <值2>, ...]` (支持自适应类型推导)
    *   **结构体/联合体/胖指针字面量**：`<类型名> { <字段名>: <值>, ... }`。当上下文已提供期待类型时（如赋值或显式类型声明），允许省略 `<类型名>`，使用匿名结构体字面量 `{ ... }`。
*   **自适应类型推导 (Top-Down Type Inference)**：
    *   纯数字字面量（如 `10`, `0xFF`）会根据上下文（如 `sit x: i8 = 10;`）自动推导为目标类型，并在编译期进行严格的值域安全校验（精确区分正负数边界）。
*   **文本字面量 (Text Literals)**：
    *   **无隐式零截断 (No Implicit Null-Terminator)**：Scoria 编译器**绝对不会**在字符串字面量末尾隐式追加 `\0`。`textus` 的本质是纯粹的切片 (`cohors littera`)，由指针和精确的长度构成。如果需要直接通过 `barbara` (FFI) 将字符串传递给期望 C 风格字符串的外部函数，开发者**必须**在字面量末尾显式写出 `\0`。
    *   **显式边界哲学 (Explicit Boundary Philosophy)**：Scoria 拒绝任何形式的隐式内存填充。标准库（如 `fasc.sco`）在封装底层 C ABI 时，会在内部显式处理 `\0` 的转换，对用户保持透明（如调用 `lege_fasc("test.sco")` 无需手动补零）。同时，标准库返回的切片（如读取的文件内容）严格遵循原始数据的精确长度，绝不隐式追加多余的 `\0` 字节。
*   **字面量前缀**：
    *   十六进制：`0x...`
    *   二进制：`0b...`
    *   八进制：`0o...`
    *   罗马数字：`0r...` (如 `0rXIV`，编译期折叠)
*   **操作符**：完全映射 CPU 硬件指令，支持 `+`, `-`, `*`, `/`, `%`, `<<`, `>>`, `&`, `|`, `^`, `~` 以及复合赋值 `+=`, `<<=` 等。成员访问支持 `.` (用于 `forma`、`unio`、`cohors` 及 `liber` 模块命名空间) 和 `->`。

## 8. 辅音骨架缩写对照表 (Tabula Isomorphismi)

基于“无元音提取法则”，Scoria 支持使用辅音骨架作为古典长词的等价缩写，在底层 AST 中它们被映射为完全相同的物理标记：

| 古典罗马全称 | 辅音骨架缩写 | 功用说明 |
| :--- | :--- | :--- |
| `edita` | `edt` | 公开暴露(导出) |
| `barbara` | `bbr` | 蛮族接口(外部) |
| `consule` | `csl` | 引入整个模块 |
| `excerpe` | `xcp` | 精确摘录符号 |
| `aliter` | `alt` | 条件分支的否则 |
| `scribe` | `scb` | 安全变长输出 |
| `lege` | `leg` | 安全变长输入 |
| `recede` | `rcd` | 指针后退 |
| `logica` | `lgc` | 逻辑电平(bool) |
| `littera` | `ltr` | 1字节 ASCII |
| `textus` | `txt` | 静态文本字面量 |
| `imago` | `img` | 声明类型别名 |
| `ordo` | `ord` | 声明枚举类型 |
| `forma` | `frm` | 声明标准结构体 |
| `densa` | `dns` | 紧凑内存无缝隙 |
| `cohors` | `crs` | 16字节胖指针 |
| `actio` | `act` | 宣告函数与动作 |
| `redde` | `rdd` | 返回并切断栈帧 |
| `rumpe` | `rmp` | 打破并跳出当前循环 |
| `perge` | `prg` | 推进至下一次循环 |
| `morere` | `mor` | 触发硬件陷阱并终止程序 |
| `nihil` | `nhl` | 无返回类型(void) |
| `nullus` | `nls` | 空指针值(NULL) |
| `magnitudo` | `mgn` | 编译期计算内存大小(sizeof) |
