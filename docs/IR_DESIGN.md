# Scoria 中间语言 (SIR) 设计方案

为了将前端生成的抽象语法树（AST）平滑地转换为目标机器码或 C 语言代码，我们需要引入一层中间表示：**Scoria Intermediate Representation (SIR)**。

SIR 的设计目标是：**剥离高级语法糖，展平控制流，显式化内存操作，并为后续的优化（如常量折叠、死代码消除）提供标准化的数据结构。**

## 1. 核心架构：线性三地址码 (Linear 3-Address Code)

SIR 采用经典的**三地址码**形式。每一条指令极其简单，最多包含一个操作码（Opcode）、两个源操作数（Source）和一个目标操作数（Destination）。
复杂的表达式（如 `a + b * c`）将被拆解为多条简单的指令：
```text
%1 = MUL b, c
%2 = ADD a, %1
```

## 2. 虚拟寄存器 (Virtual Registers)

SIR 假设底层机器拥有**无限个虚拟寄存器**（通常以 `%1`, `%2`, `%t_x` 命名）。
在 AST 转换阶段，所有的局部变量、临时计算结果都会被分配到一个唯一的虚拟寄存器中。这极大地简化了表达式的求值过程。

## 3. 基本块与控制流图 (Basic Blocks & CFG)

高级语言中的 `si` (if)、`dum` (while)、`per` (for) 等嵌套控制流，在 SIR 中将被彻底**展平 (Flatten)**。
*   **基本块 (Basic Block)**：一段连续的直线执行代码，只有一个入口（第一条指令）和一个出口（最后一条指令必须是跳转或返回）。
*   **入口基本块 (Entry Block)**：每个函数都有且仅有一个入口基本块，它是函数被调用时**最先执行**的代码块。通常，所有的局部变量内存分配（`ALLOCA`）和函数参数的初始存储（`STORE`）都会集中放置在这个块的开头，为后续的逻辑执行做好准备。
*   **跳转指令**：通过无条件跳转 (`JMP`) 和条件分支 (`BR`) 在基本块之间转移控制权，从而构建出控制流图 (CFG)。

## 4. SIR 核心指令集 (Instruction Set)

SIR 的指令集设计贴近底层硬件，但保留了 Scoria 的强类型特征：

### A. 算术与逻辑运算
*   `ADD`, `SUB`, `MUL`, `DIV`, `MOD`
*   `AND`, `OR`, `XOR`, `SHL`, `SHR`
*   `CMP_EQ`, `CMP_NE`, `CMP_LT`, `CMP_LE`, `CMP_GT`, `CMP_GE` (比较指令，返回布尔值)

### B. 内存与指针操作 (核心防线)
*   `ALLOCA <type>`：在当前函数的栈帧上分配内存，返回一个指针 (`via`)。
*   `LOAD <ptr>`：从指针指向的内存地址读取数据到虚拟寄存器（对应 `tene`）。
*   `STORE <val>, <ptr>`：将虚拟寄存器的数据写入指针指向的内存地址（对应赋值 `=`）。
*   `GEP <ptr>, <offset/index>`：(Get Element Pointer) 计算数组元素或结构体字段的物理内存地址，**不进行解引用**。这是处理 `vade`, `recede`, `arr[i]`, `obj.field` 的核心指令。

### C. 控制流
*   `JMP <block>`：无条件跳转到目标基本块。
*   `BR <cond>, <true_block>, <false_block>`：条件分支。如果 `<cond>` 为真，跳向 `true_block`，否则跳向 `false_block`。
*   `CALL <func>, <args...>`：调用函数。
*   `RET <val>`：从当前函数返回。

### D. 其他
*   `CAST <val>, <target_type>`：强制类型转换（对应 `muta`）。

## 5. 数据结构映射 (C 语言实现预告)

在接下来的代码编写中，我们将构建以下核心结构体：
*   `SirValue`：表示一个操作数（虚拟寄存器、常量字面量、全局符号）。
*   `SirInst`：表示一条具体的指令。
*   `SirBlock`：基本块，包含一个指令链表。
*   `SirFunction`：函数，包含一个基本块链表。
*   `SirModule`：整个编译单元，包含全局变量和函数的集合。

---
**下一步行动**：
如果你认可这个 SIR 的设计方案，请回复“继续”，我将开始在 `src/ir/sir.h` 中用 C 语言定义这些核心数据结构。
