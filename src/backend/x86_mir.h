#ifndef SCORIA_X86_MIR_H
#define SCORIA_X86_MIR_H

#include "../ir/sir.h"
#include <stdint.h>
#include <stdbool.h>

// x86_64 物理寄存器枚举
typedef enum {
    X86_REG_RAX = 0, X86_REG_RCX, X86_REG_RDX, X86_REG_RBX,
    X86_REG_RSP, X86_REG_RBP, X86_REG_RSI, X86_REG_RDI,
    X86_REG_R8, X86_REG_R9, X86_REG_R10, X86_REG_R11,
    X86_REG_R12, X86_REG_R13, X86_REG_R14, X86_REG_R15,
    X86_REG_XMM0, X86_REG_XMM1, X86_REG_XMM2, X86_REG_XMM3,
    X86_REG_XMM4, X86_REG_XMM5, X86_REG_XMM6, X86_REG_XMM7,
    X86_REG_NONE = -1
} X86Reg;

// 操作数类型
typedef enum {
    X86_OP_NONE,
    X86_OP_REG,
    X86_OP_IMM,
    X86_OP_MEM_BASE_DISP,
    X86_OP_MEM_SIB,
    X86_OP_MEM_RIP,
    X86_OP_LABEL,
    X86_OP_BLOCK,
    X86_OP_STRING
} X86OperandKind;

typedef struct {
    X86OperandKind kind;
    int size; // 1, 2, 4, 8 字节
    union {
        X86Reg reg;
        int64_t imm;
        struct { X86Reg base; int32_t disp; } mem_bd;
        struct { X86Reg base; X86Reg index; int scale; int32_t disp; } mem_sib;
        const char* label;
        uint32_t block_id;
        struct { const char* str; uint32_t len; } string;
    } as;
} X86Operand;

// x86_64 机器指令操作码 (精简版)
typedef enum {
    X86_INST_MOV, X86_INST_MOVSX, X86_INST_MOVZX, X86_INST_LEA,
    X86_INST_ADD, X86_INST_SUB, X86_INST_IMUL, X86_INST_IDIV, X86_INST_DIV,
    X86_INST_AND, X86_INST_OR, X86_INST_XOR,
    X86_INST_SHL, X86_INST_SHR, X86_INST_SAR,
    X86_INST_NEG, X86_INST_NOT, X86_INST_INC, X86_INST_DEC,
    X86_INST_CMP, X86_INST_TEST,
    X86_INST_JMP, X86_INST_JCC,
    X86_INST_CALL, X86_INST_RET,
    X86_INST_PUSH, X86_INST_POP,
    X86_INST_CQO, X86_INST_CDQ, X86_INST_XCHG,
    X86_INST_SETCC, X86_INST_CMOVCC,
    X86_INST_MOVD, X86_INST_MOVQ,
    X86_INST_MOVSS, X86_INST_MOVSD,
    X86_INST_CVTSI2SS, X86_INST_CVTSI2SD,
    X86_INST_CVTTSS2SI, X86_INST_CVTTSD2SI,
    X86_INST_CVTSS2SD, X86_INST_CVTSD2SS,
    X86_INST_ADDSS, X86_INST_ADDSD,
    X86_INST_SUBSS, X86_INST_SUBSD,
    X86_INST_MULSS, X86_INST_MULSD,
    X86_INST_DIVSS, X86_INST_DIVSD,
    X86_INST_UCOMISS, X86_INST_UCOMISD,
    X86_INST_REP_MOVSB, X86_INST_CLD, X86_INST_UD2
} X86Opcode;

typedef enum {
    X86_COND_E, X86_COND_NE, X86_COND_L, X86_COND_LE, X86_COND_G, X86_COND_GE,
    X86_COND_B, X86_COND_BE, X86_COND_A, X86_COND_AE, X86_COND_NONE
} X86Condition;

typedef struct X86Inst {
    X86Opcode opcode;
    X86Condition cond;
    int num_ops;
    X86Operand ops[3];
    struct X86Inst* next;
} X86Inst;

typedef struct X86Block {
    uint32_t id;
    const char* name;
    X86Inst* first_inst;
    X86Inst* last_inst;
    struct X86Block* next;
} X86Block;

typedef struct X86Function {
    const char* name;
    int frame_size;
    bool has_fast_path;
    bool used_callee_saved[16];
    X86Block* first_block;
    X86Block* last_block;
    struct X86Function* next;
} X86Function;

typedef struct {
    X86Function* first_func;
    X86Function* last_func;
    SirModule* sir_module; // 关联的原始 SIR 模块（用于全局变量等信息）
} X86Module;

// 核心 API：将 SIR 模块降级为 X86 MIR 模块
X86Module* x86_mir_build(SirModule* module, int opt_level);
void x86_mir_free(X86Module* module);

#endif // SCORIA_X86_MIR_H
