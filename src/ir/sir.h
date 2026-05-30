#ifndef SCORIA_SIR_H
#define SCORIA_SIR_H

#include <stdint.h>
#include <stdbool.h>
#include "../middleend/types.h"

// =========================================================
// SIR 操作码 (Opcodes)
// =========================================================
typedef enum {
    // 算术与位运算
    SIR_ADD, SIR_SUB, SIR_MUL, SIR_DIV, SIR_MOD,
    SIR_FADD, SIR_FSUB, SIR_FMUL, SIR_FDIV,
    SIR_AND, SIR_OR, SIR_XOR, SIR_SHL, SIR_SHR,
    
    // 比较运算
    SIR_ICMP_EQ, SIR_ICMP_NE, SIR_ICMP_LT, SIR_ICMP_LE, SIR_ICMP_GT, SIR_ICMP_GE,
    SIR_FCMP_EQ, SIR_FCMP_NE, SIR_FCMP_LT, SIR_FCMP_LE, SIR_FCMP_GT, SIR_FCMP_GE,
    
    // 内存操作
    SIR_ALLOCA,  // 栈分配
    SIR_LOAD,    // 读内存
    SIR_STORE,   // 写内存
    SIR_GEP,     // 计算元素指针 (Get Element Pointer)
    SIR_MEMCPY,  // 内存拷贝 (dest_ptr, src_ptr, size)
    
    // 控制流
    SIR_JMP,     // 无条件跳转
    SIR_BR,      // 条件分支
    SIR_SWITCH,  // 多路分支 (跳转表)
    SIR_CALL,    // 函数调用
    SIR_RET,     // 返回
    SIR_TRAP,    // 硬件陷阱 (mori)
    
    // 系统抽象 (OS Lowering)
    SIR_SYS_ALLOC,
    SIR_SYS_FREE,
    SIR_SYS_WRITE,
    SIR_SYS_READ,
    SIR_SYS_EXIT,
    
    // 其他
    SIR_CAST,    // 类型转换
    SIR_GET_PARAM, // 获取函数参数
    SIR_SELECT   // 条件选择
} SirOpcode;

// =========================================================
// SIR 值 (Values)
// =========================================================
typedef enum {
    SIR_VAL_VREG,         // 虚拟寄存器 (如 %1, %2)
    SIR_VAL_CONST_INT,    // 整型常量
    SIR_VAL_CONST_FLOAT,  // 浮点常量
    SIR_VAL_CONST_BOOL,   // 布尔常量
    SIR_VAL_CONST_STRING, // 字符串常量
    SIR_VAL_GLOBAL,       // 全局符号 (函数名或全局变量名)
    SIR_VAL_BLOCK         // 基本块 (用于跳转目标)
} SirValueKind;

typedef struct SirBlock SirBlock;

typedef struct SirValue {
    SirValueKind kind;
    ScoriaType* type;     // 该值的类型
    union {
        uint32_t vreg;
        int64_t int_val;
        double float_val;
        bool bool_val;
        struct {
            const char* str;
            uint32_t len;
        } string_val;
        const char* global_name;
        SirBlock* block;
    } as;
} SirValue;

// =========================================================
// SIR 指令 (Instructions)
// =========================================================
typedef struct SirInst {
    SirOpcode opcode;
    SirValue* dest;       // 目标操作数 (可能为 NULL，如 STORE, JMP)
    SirValue** operands;  // 源操作数数组
    int num_operands;     // 源操作数数量
    
    struct SirInst* prev; // 双向链表
    struct SirInst* next;
} SirInst;

// =========================================================
// SIR 基本块 (Basic Blocks)
// =========================================================
struct SirBlock {
    uint32_t id;          // 基本块唯一 ID
    const char* name;     // 基本块名称 (如 "entry", "if.then")
    
    SirInst* first_inst;  // 指令链表头
    SirInst* last_inst;   // 指令链表尾
    
    struct SirBlock* next; // 函数内的下一个基本块
    bool is_frameless;    // 是否为无栈帧块 (用于 Prologue Shrink-wrapping)
};

// =========================================================
// SIR 函数 (Functions)
// =========================================================
typedef struct SirFunction {
    const char* name;
    ScoriaType* type;     // 必须是 TY_ACTIO
    
    SirBlock* first_block;
    SirBlock* last_block;
    
    // O3 Const-Eval 纯函数标记
    bool is_pure;
    
    // 序言前置快路径 (Fast Path) 信息
    bool has_fast_path;
    int32_t fp_imm;
    int fp_w;
    const char* fp_jcc_asm;
    uint8_t fp_jcc_pe;
    
    struct SirFunction* next;
} SirFunction;

// =========================================================
// SIR 全局变量 (Global Variables)
// =========================================================
typedef struct SirGlobalVar {
    const char* name;
    ScoriaType* type;
    int size;
    uint8_t* init_data; // 如果非空，则包含编译期确定的初始值
    struct SirGlobalVar* next;
} SirGlobalVar;

// =========================================================
// SIR 外部函数 (Extern Functions)
// =========================================================
typedef struct SirExternFunc {
    const char* name;
    const char* dll_name;
    struct SirExternFunc* next;
} SirExternFunc;

// =========================================================
// SIR 模块 (Modules)
// =========================================================
typedef struct SirModule {
    const char* name;
    
    SirFunction* first_func;
    SirFunction* last_func;
    
    SirGlobalVar* first_global;
    SirGlobalVar* last_global;
    
    SirExternFunc* first_extern;
    SirExternFunc* last_extern;
} SirModule;

#endif // SCORIA_SIR_H
