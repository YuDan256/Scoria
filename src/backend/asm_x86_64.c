#include "asm_x86_64.h"
#include "reg_alloc.h"
#include "builtins.h"
#include <stdlib.h>
#include <string.h>

// 物理寄存器映射表 (对应 reg_alloc.h 中的 NUM_PHYS_REGS = 9)
static const char* phys_regs64[] = {"%rbx", "%rsi", "%rdi", "%r12", "%r13", "%r14", "%r15", "%r10", "%r11"};
static const char* phys_regs32[] = {"%ebx", "%esi", "%edi", "%r12d", "%r13d", "%r14d", "%r15d", "%r10d", "%r11d"};
static const char* phys_regs16[] = {"%bx", "%si", "%di", "%r12w", "%r13w", "%r14w", "%r15w", "%r10w", "%r11w"};
static const char* phys_regs8[]  = {"%bl", "%sil", "%dil", "%r12b", "%r13b", "%r14b", "%r15b", "%r10b", "%r11b"};

typedef struct {
    const char* str;
    uint32_t len;
    int id;
} StringConst;

static StringConst g_strings[1024];
static int g_string_count = 0;

static int get_string_id(const char* str, uint32_t len) {
    for (int i = 0; i < g_string_count; i++) {
        if (g_strings[i].len == len && memcmp(g_strings[i].str, str, len) == 0) return g_strings[i].id;
    }
    g_strings[g_string_count].str = str;
    g_strings[g_string_count].len = len;
    g_strings[g_string_count].id = g_string_count;
    return g_string_count++;
}

// 辅助函数：将 SirValue 转换为 x86_64 汇编操作数格式
static void get_operand_str(char* buf, SirValue* val, RegAllocator* alloc, int size) {
    if (!val) {
        strcpy(buf, "");
        return;
    }
    switch (val->kind) {
        case SIR_VAL_CONST_INT:
            sprintf(buf, "$%lld", (long long)val->as.int_val);
            break;
        case SIR_VAL_CONST_FLOAT: {
            if (val->type && val->type->kind == TY_F32) {
                float f = (float)val->as.float_val;
                uint32_t bits;
                memcpy(&bits, &f, 4);
                sprintf(buf, "$%u", bits);
            } else {
                uint64_t bits;
                double d = val->as.float_val;
                memcpy(&bits, &d, 8);
                sprintf(buf, "$%llu", (unsigned long long)bits);
            }
            break;
        }
        case SIR_VAL_CONST_BOOL:
            sprintf(buf, "$%d", val->as.bool_val ? 1 : 0);
            break;
        case SIR_VAL_GLOBAL:
            sprintf(buf, "$%s", val->as.global_name);
            break;
        case SIR_VAL_CONST_STRING:
            sprintf(buf, "$.Lstr%d", get_string_id(val->as.string_val.str, val->as.string_val.len));
            break;
        case SIR_VAL_VREG: {
            int color = reg_alloc_get_color(alloc, val->as.vreg);
            if (color != -1) {
                // 分配到了物理寄存器
                if (size == 1) strcpy(buf, phys_regs8[color]);
                else if (size == 2) strcpy(buf, phys_regs16[color]);
                else if (size == 4) strcpy(buf, phys_regs32[color]);
                else strcpy(buf, phys_regs64[color]);
            } else {
                // 溢出到了栈内存
                int offset = reg_alloc_get_offset(alloc, val->as.vreg, 8);
                sprintf(buf, "%d(%%rbp)", offset);
            }
            break;
        }
        default:
            strcpy(buf, "???");
            break;
    }
}

static void generate_function(FILE* out, SirFunction* func) {
    fprintf(out, "    .globl %s\n", func->name);
    fprintf(out, "    .type %s, @function\n", func->name);
    fprintf(out, "%s:\n", func->name);

    // 1. 函数序言 (Prologue)
    fprintf(out, "    pushq %%rbp\n");
    fprintf(out, "    movq %%rsp, %%rbp\n");
    
    // 保存 callee-saved 寄存器
    fprintf(out, "    pushq %%rbx\n");
    fprintf(out, "    pushq %%rsi\n");
    fprintf(out, "    pushq %%rdi\n");
    fprintf(out, "    pushq %%r12\n");
    fprintf(out, "    pushq %%r13\n");
    fprintf(out, "    pushq %%r14\n");
    fprintf(out, "    pushq %%r15\n");
    
    // 将前 4 个参数寄存器保存到 Shadow Space (支持浮点数与隐藏返回指针)
    bool hidden_ret = type_get_size(func->type->as.func_type.return_type) > 8;
    int explicit_param_count = func->type->as.func_type.param_count;
    int total_phys_params = hidden_ret ? explicit_param_count + 1 : explicit_param_count;
    
    for (int i = 0; i < total_phys_params && i < 4; i++) {
        bool is_float = false;
        bool is_f32 = false;
        if (!hidden_ret || i > 0) {
            int explicit_idx = hidden_ret ? i - 1 : i;
            ScoriaType* ptype = func->type->as.func_type.param_types[explicit_idx];
            is_float = (ptype && (ptype->kind == TY_F32 || ptype->kind == TY_F64));
            is_f32 = (ptype && ptype->kind == TY_F32);
        }
        
        int offset = 16 + i * 8;
        if (is_float) {
            if (is_f32) fprintf(out, "    movss %%xmm%d, %d(%%rbp)\n", i, offset);
            else fprintf(out, "    movsd %%xmm%d, %d(%%rbp)\n", i, offset);
        } else {
            const char* regs[] = {"%rcx", "%rdx", "%r8", "%r9"};
            fprintf(out, "    movq %s, %d(%%rbp)\n", regs[i], offset);
        }
    }

    // 2. 扫描函数，找到最大的虚拟寄存器 ID 和 ALLOCA 空间
    uint32_t max_vreg = 0;
    int local_stack_size = 72; // 56字节给7个callee-saved + 16字节给caller-saved(r10,r11)
    int max_call_args = 0;
    int* alloca_offsets = calloc(10000, sizeof(int));

    for (SirBlock* block = func->first_block; block; block = block->next) {
        for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
            if (inst->opcode == SIR_CALL) {
                int args = inst->num_operands - 1;
                if (args > max_call_args) max_call_args = args;
            }
            if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                if (inst->dest->as.vreg > max_vreg) max_vreg = inst->dest->as.vreg;
            }
            for (int i=0; i<inst->num_operands; i++) {
                if (inst->operands[i] && inst->operands[i]->kind == SIR_VAL_VREG) {
                    if (inst->operands[i]->as.vreg > max_vreg) max_vreg = inst->operands[i]->as.vreg;
                }
            }
            if (inst->opcode == SIR_ALLOCA) {
                int alloc_size = (int)inst->operands[0]->as.int_val;
                alloc_size = (alloc_size + 7) & ~7; // 保持 8 字节对齐
                local_stack_size += alloc_size;
                alloca_offsets[inst->dest->as.vreg] = -local_stack_size;
            }
        }
    }

    // 3. 初始化并运行图着色寄存器分配器
    RegAllocator allocator;
    reg_alloc_init(&allocator, max_vreg);
    allocator.current_offset = local_stack_size;
    reg_alloc_build_and_color(&allocator, func);

    // 预留栈空间：只为被溢出 (Spilled) 的虚拟寄存器分配栈内存
    for (uint32_t i = 1; i <= max_vreg; i++) {
        if (reg_alloc_get_color(&allocator, i) == -1) {
            reg_alloc_get_offset(&allocator, i, 8); // 强制分配 8 字节偏移量
        }
    }
    
    int call_stack_space = max_call_args > 4 ? (max_call_args - 4) * 8 : 0;
    int stack_size = allocator.current_offset + 32 + call_stack_space; // 预留 Shadow Space 和溢出参数空间
    // 保持 16 字节对齐，并补偿 9 个 push (1 个 ret addr + 8 个 rbp/rbx 等) 造成的 8 字节偏移
    stack_size = (stack_size + 15) & ~15;
    stack_size += 8;
    if (stack_size > 0) {
        fprintf(out, "    subq $%d, %%rsp\n", stack_size);
    }

    // 4. 遍历基本块和指令 (指令选择 Instruction Selection)
    char op0[64], op1[64], dest[64];
    
    for (SirBlock* block = func->first_block; block; block = block->next) {
        fprintf(out, ".L%s_%u:\n", block->name, block->id);
        
        for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
            // 解析操作数
            if (inst->num_operands > 0) get_operand_str(op0, inst->operands[0], &allocator, 8);
            if (inst->num_operands > 1) get_operand_str(op1, inst->operands[1], &allocator, 8);
            if (inst->dest) get_operand_str(dest, inst->dest, &allocator, 8);

            switch (inst->opcode) {
                case SIR_ALLOCA: {
                    int offset = alloca_offsets[inst->dest->as.vreg];
                    fprintf(out, "    leaq %d(%%rbp), %%rax\n", offset);
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;
                }

                case SIR_STORE: {
                    int size = 8;
                    // 优先使用指针解引用后的目标类型大小，防止右值字面量丢失类型导致越界写入
                    if (inst->operands[1]->type && inst->operands[1]->type->kind == TY_VIA) {
                        size = type_get_size(inst->operands[1]->type->as.inner);
                    } else if (inst->operands[0]->type) {
                        size = type_get_size(inst->operands[0]->type);
                    }
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    movq %s, %%rcx\n", op1);
                    if (size == 1) fprintf(out, "    movb %%al, (%%rcx)\n");
                    else if (size == 2) fprintf(out, "    movw %%ax, (%%rcx)\n");
                    else if (size == 4) fprintf(out, "    movl %%eax, (%%rcx)\n");
                    else fprintf(out, "    movq %%rax, (%%rcx)\n");
                    break;
                }

                case SIR_LOAD: {
                    int size = type_get_size(inst->dest->type);
                    bool is_signed = type_is_signed(inst->dest->type);
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    if (size == 1) {
                        if (is_signed) fprintf(out, "    movsbq (%%rax), %%rcx\n");
                        else fprintf(out, "    movzbq (%%rax), %%rcx\n");
                    } else if (size == 2) {
                        if (is_signed) fprintf(out, "    movswq (%%rax), %%rcx\n");
                        else fprintf(out, "    movzwq (%%rax), %%rcx\n");
                    } else if (size == 4) {
                        if (is_signed) fprintf(out, "    movslq (%%rax), %%rcx\n");
                        else fprintf(out, "    movl (%%rax), %%ecx\n");
                    } else {
                        fprintf(out, "    movq (%%rax), %%rcx\n");
                    }
                    fprintf(out, "    movq %%rcx, %s\n", dest);
                    break;
                }

                case SIR_CAST: {
                    ScoriaType* src_type = inst->operands[0]->type;
                    ScoriaType* dst_type = inst->dest->type;
                    bool src_is_float = (src_type && (src_type->kind == TY_F32 || src_type->kind == TY_F64));
                    bool dst_is_float = (dst_type && (dst_type->kind == TY_F32 || dst_type->kind == TY_F64));
                    
                    if (src_is_float && !dst_is_float) {
                        if (src_type->kind == TY_F32) {
                            fprintf(out, "    movd %s, %%xmm0\n", op0);
                            fprintf(out, "    cvttss2si %%xmm0, %%rax\n");
                        } else {
                            fprintf(out, "    movq %s, %%xmm0\n", op0);
                            fprintf(out, "    cvttsd2si %%xmm0, %%rax\n");
                        }
                        fprintf(out, "    movq %%rax, %s\n", dest);
                    } else if (!src_is_float && dst_is_float) {
                        fprintf(out, "    movq %s, %%rax\n", op0);
                        if (dst_type->kind == TY_F32) {
                            fprintf(out, "    cvtsi2ss %%rax, %%xmm0\n");
                            fprintf(out, "    movd %%xmm0, %%eax\n");
                        } else {
                            fprintf(out, "    cvtsi2sd %%rax, %%xmm0\n");
                            fprintf(out, "    movq %%xmm0, %%rax\n");
                        }
                        fprintf(out, "    movq %%rax, %s\n", dest);
                    } else if (src_is_float && dst_is_float && src_type->kind != dst_type->kind) {
                        if (src_type->kind == TY_F32) {
                            fprintf(out, "    movd %s, %%xmm0\n", op0);
                            fprintf(out, "    cvtss2sd %%xmm0, %%xmm0\n");
                            fprintf(out, "    movq %%xmm0, %%rax\n");
                        } else {
                            fprintf(out, "    movq %s, %%xmm0\n", op0);
                            fprintf(out, "    cvtsd2ss %%xmm0, %%xmm0\n");
                            fprintf(out, "    movd %%xmm0, %%eax\n");
                        }
                        fprintf(out, "    movq %%rax, %s\n", dest);
                    } else {
                        fprintf(out, "    movq %s, %%rax\n", op0);
                        int dst_size = type_get_size(dst_type);
                        if (dst_size < 8) {
                            if (type_is_signed(dst_type)) {
                                if (dst_size == 1) fprintf(out, "    movsbq %%al, %%rax\n");
                                else if (dst_size == 2) fprintf(out, "    movswq %%ax, %%rax\n");
                                else if (dst_size == 4) fprintf(out, "    movslq %%eax, %%rax\n");
                            } else {
                                if (dst_size == 1) fprintf(out, "    movzbq %%al, %%rax\n");
                                else if (dst_size == 2) fprintf(out, "    movzwq %%ax, %%rax\n");
                                else if (dst_size == 4) fprintf(out, "    movl %%eax, %%eax\n");
                            }
                        }
                        fprintf(out, "    movq %%rax, %s\n", dest);
                    }
                    break;
                }

                case SIR_ADD: {
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    if (strcmp(op0, acc) != 0) fprintf(out, "    movq %s, %s\n", op0, acc);
                    if (strcmp(op1, "$1") == 0) {
                        fprintf(out, "    incq %s\n", acc);
                    } else if (strcmp(op1, "$0") != 0) {
                        fprintf(out, "    addq %s, %s\n", op1, acc);
                    }
                    if (dest_is_mem) fprintf(out, "    movq %s, %s\n", acc, dest);
                    break;
                }

                case SIR_SUB: {
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    if (strcmp(op0, acc) != 0) fprintf(out, "    movq %s, %s\n", op0, acc);
                    if (strcmp(op1, "$1") == 0) {
                        fprintf(out, "    decq %s\n", acc);
                    } else if (strcmp(op1, "$0") != 0) {
                        fprintf(out, "    subq %s, %s\n", op1, acc);
                    }
                    if (dest_is_mem) fprintf(out, "    movq %s, %s\n", acc, dest);
                    break;
                }

                case SIR_MUL: {
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    if (strcmp(op0, acc) != 0) fprintf(out, "    movq %s, %s\n", op0, acc);
                    
                    if (inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                        int64_t imm = inst->operands[1]->as.int_val;
                        if (imm == 0) {
                            fprintf(out, "    xorq %s, %s\n", acc, acc);
                        } else if (imm == 1) {
                            // no-op
                        } else if (imm == 2) {
                            fprintf(out, "    addq %s, %s\n", acc, acc);
                        } else if (imm > 0 && (imm & (imm - 1)) == 0) {
                            int shift = 0;
                            while ((imm >> shift) > 1) shift++;
                            fprintf(out, "    shlq $%d, %s\n", shift, acc);
                        } else {
                            fprintf(out, "    imulq %s, %s\n", op1, acc);
                        }
                    } else {
                        fprintf(out, "    imulq %s, %s\n", op1, acc);
                    }
                    
                    if (dest_is_mem) fprintf(out, "    movq %s, %s\n", acc, dest);
                    break;
                }

                case SIR_FADD:
                case SIR_FSUB:
                case SIR_FMUL:
                case SIR_FDIV: {
                    bool is_f32 = (inst->operands[0]->type && inst->operands[0]->type->kind == TY_F32);
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    if (strcmp(op0, acc) != 0) fprintf(out, "    movq %s, %s\n", op0, acc);
                    fprintf(out, "    movq %s, %%rcx\n", op1);
                    
                    fprintf(out, "    movq %s, %%xmm0\n", acc);
                    fprintf(out, "    movq %%rcx, %%xmm1\n");
                    if (is_f32) {
                        if (inst->opcode == SIR_FADD) fprintf(out, "    addss %%xmm1, %%xmm0\n");
                        else if (inst->opcode == SIR_FSUB) fprintf(out, "    subss %%xmm1, %%xmm0\n");
                        else if (inst->opcode == SIR_FMUL) fprintf(out, "    mulss %%xmm1, %%xmm0\n");
                        else if (inst->opcode == SIR_FDIV) fprintf(out, "    divss %%xmm1, %%xmm0\n");
                    } else {
                        if (inst->opcode == SIR_FADD) fprintf(out, "    addsd %%xmm1, %%xmm0\n");
                        else if (inst->opcode == SIR_FSUB) fprintf(out, "    subsd %%xmm1, %%xmm0\n");
                        else if (inst->opcode == SIR_FMUL) fprintf(out, "    mulsd %%xmm1, %%xmm0\n");
                        else if (inst->opcode == SIR_FDIV) fprintf(out, "    divsd %%xmm1, %%xmm0\n");
                    }
                    fprintf(out, "    movq %%xmm0, %s\n", acc);
                    if (dest_is_mem) fprintf(out, "    movq %s, %s\n", acc, dest);
                    break;
                }

                case SIR_JMP:
                    fprintf(out, "    jmp .L%s_%u\n", inst->operands[0]->as.block->name, inst->operands[0]->as.block->id);
                    break;

                case SIR_BR:
                    // br cond(op0), true_block(op1), false_block(op2)
                    if (op0[0] == '%') {
                        fprintf(out, "    testq %s, %s\n", op0, op0);
                    } else {
                        fprintf(out, "    cmpq $0, %s\n", op0);
                    }
                    fprintf(out, "    jne .L%s_%u\n", inst->operands[1]->as.block->name, inst->operands[1]->as.block->id);
                    fprintf(out, "    jmp .L%s_%u\n", inst->operands[2]->as.block->name, inst->operands[2]->as.block->id);
                    break;

                case SIR_SWITCH: {
                    char cond_str[64];
                    get_operand_str(cond_str, inst->operands[0], &allocator, 8);
                    fprintf(out, "    movq %s, %%rax\n", cond_str);

                    int case_count = (inst->num_operands - 2) / 2;
                    SirBlock* def_block = inst->operands[1]->as.block;

                    bool can_jump_table = true;
                    int64_t min_val = INT64_MAX;
                    int64_t max_val = INT64_MIN;

                    for (int i = 0; i < case_count; i++) {
                        SirValue* cval = inst->operands[2 + i * 2];
                        if (cval->kind != SIR_VAL_CONST_INT) {
                            can_jump_table = false;
                            break;
                        }
                        if (cval->as.int_val < min_val) min_val = cval->as.int_val;
                        if (cval->as.int_val > max_val) max_val = cval->as.int_val;
                    }

                    if (case_count == 0) can_jump_table = false;
                    if (can_jump_table && (max_val - min_val > 256)) can_jump_table = false;

                    if (can_jump_table) {
                        if (min_val != 0) fprintf(out, "    subq $%lld, %%rax\n", (long long)min_val);
                        fprintf(out, "    cmpq $%lld, %%rax\n", (long long)(max_val - min_val));
                        fprintf(out, "    ja .L%s_%u\n", def_block->name, def_block->id);
                        fprintf(out, "    leaq .Ljt_%p(%%rip), %%rcx\n", (void*)inst);
                        fprintf(out, "    movslq (%%rcx, %%rax, 4), %%rdx\n");
                        fprintf(out, "    addq %%rcx, %%rdx\n");
                        fprintf(out, "    jmp *%%rdx\n");

                        fprintf(out, "    .section .rdata,\"a\"\n");
                        fprintf(out, "    .align 4\n");
                        fprintf(out, ".Ljt_%p:\n", (void*)inst);
                        for (int64_t v = min_val; v <= max_val; v++) {
                            SirBlock* target = def_block;
                            for (int i = 0; i < case_count; i++) {
                                if (inst->operands[2 + i * 2]->as.int_val == v) {
                                    target = inst->operands[2 + i * 2 + 1]->as.block;
                                    break;
                                }
                            }
                            fprintf(out, "    .long .L%s_%u - .Ljt_%p\n", target->name, target->id, (void*)inst);
                        }
                        fprintf(out, "    .text\n");
                    } else {
                        // 退化为 If-Else 链
                        for (int i = 0; i < case_count; i++) {
                            char val_str[64];
                            get_operand_str(val_str, inst->operands[2 + i * 2], &allocator, 8);
                            fprintf(out, "    cmpq %s, %%rax\n", val_str);
                            SirBlock* target = inst->operands[2 + i * 2 + 1]->as.block;
                            fprintf(out, "    je .L%s_%u\n", target->name, target->id);
                        }
                        fprintf(out, "    jmp .L%s_%u\n", def_block->name, def_block->id);
                    }
                    break;
                }

                case SIR_GET_PARAM: {
                    int param_idx = (int)inst->operands[0]->as.int_val;
                    int offset = 16 + param_idx * 8;
                    int size = type_get_size(inst->dest->type);
                    bool is_signed = type_is_signed(inst->dest->type);
                    
                    if (size == 1) {
                        if (is_signed) fprintf(out, "    movsbq %d(%%rbp), %%rax\n", offset);
                        else fprintf(out, "    movzbq %d(%%rbp), %%rax\n", offset);
                    } else if (size == 2) {
                        if (is_signed) fprintf(out, "    movswq %d(%%rbp), %%rax\n", offset);
                        else fprintf(out, "    movzwq %d(%%rbp), %%rax\n", offset);
                    } else if (size == 4) {
                        if (is_signed) fprintf(out, "    movslq %d(%%rbp), %%rax\n", offset);
                        else fprintf(out, "    movl %d(%%rbp), %%eax\n", offset);
                    } else {
                        fprintf(out, "    movq %d(%%rbp), %%rax\n", offset);
                    }
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;
                }

                case SIR_MEMCPY: {
                    int size = (int)inst->operands[2]->as.int_val;
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    movq %s, %%rdx\n", op1);
                    fprintf(out, "    pushq %%rsi\n");
                    fprintf(out, "    pushq %%rdi\n");
                    fprintf(out, "    pushq %%rcx\n");
                    fprintf(out, "    movq %%rax, %%rdi\n");
                    fprintf(out, "    movq %%rdx, %%rsi\n");
                    fprintf(out, "    movq $%d, %%rcx\n", size);
                    fprintf(out, "    cld\n");
                    fprintf(out, "    rep movsb\n");
                    fprintf(out, "    popq %%rcx\n");
                    fprintf(out, "    popq %%rdi\n");
                    fprintf(out, "    popq %%rsi\n");
                    break;
                }

                case SIR_GEP: {
                    int element_size = (int)inst->operands[2]->as.int_val;
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    
                    if (strcmp(op0, acc) != 0) fprintf(out, "    movq %s, %s\n", op0, acc);
                    
                    if (inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                        int64_t offset = inst->operands[1]->as.int_val * element_size;
                        if (offset != 0) {
                            fprintf(out, "    addq $%lld, %s\n", (long long)offset, acc);
                        }
                    } else {
                        if (element_size == 1) {
                            fprintf(out, "    addq %s, %s\n", op1, acc);
                        } else if ((element_size == 2 || element_size == 4 || element_size == 8) && op1[0] == '%') {
                            fprintf(out, "    leaq (%s, %s, %d), %s\n", acc, op1, element_size, acc);
                        } else {
                            fprintf(out, "    movq %s, %%rcx\n", op1);
                            fprintf(out, "    imulq $%d, %%rcx\n", element_size);
                            fprintf(out, "    addq %%rcx, %s\n", acc);
                        }
                    }
                    if (dest_is_mem) fprintf(out, "    movq %s, %s\n", acc, dest);
                    break;
                }

                case SIR_DIV:
                case SIR_MOD: {
                    bool is_unsigned = type_is_unsigned(inst->operands[0]->type);
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    
                    // 优化：无符号除以 2 的幂转换为逻辑右移 (shr)
                    if (is_unsigned && inst->opcode == SIR_DIV && inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                        int64_t imm = inst->operands[1]->as.int_val;
                        if (imm > 0 && (imm & (imm - 1)) == 0) {
                            int shift = 0;
                            while ((imm >> shift) > 1) shift++;
                            if (shift > 0) fprintf(out, "    shrq $%d, %%rax\n", shift);
                            fprintf(out, "    movq %%rax, %s\n", dest);
                            break;
                        }
                    }
                    
                    fprintf(out, "    movq %s, %%rcx\n", op1);
                    if (is_unsigned) {
                        fprintf(out, "    xorq %%rdx, %%rdx\n");
                        fprintf(out, "    divq %%rcx\n");
                    } else {
                        fprintf(out, "    cqo\n");
                        fprintf(out, "    idivq %%rcx\n");
                    }
                    if (inst->opcode == SIR_DIV) {
                        fprintf(out, "    movq %%rax, %s\n", dest);
                    } else {
                        fprintf(out, "    movq %%rdx, %s\n", dest);
                    }
                    break;
                }

                case SIR_AND: {
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    if (strcmp(op0, acc) != 0) fprintf(out, "    movq %s, %s\n", op0, acc);
                    fprintf(out, "    andq %s, %s\n", op1, acc);
                    if (dest_is_mem) fprintf(out, "    movq %s, %s\n", acc, dest);
                    break;
                }

                case SIR_OR: {
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    if (strcmp(op0, acc) != 0) fprintf(out, "    movq %s, %s\n", op0, acc);
                    fprintf(out, "    orq %s, %s\n", op1, acc);
                    if (dest_is_mem) fprintf(out, "    movq %s, %s\n", acc, dest);
                    break;
                }

                case SIR_XOR: {
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    if (strcmp(op0, acc) != 0) fprintf(out, "    movq %s, %s\n", op0, acc);
                    fprintf(out, "    xorq %s, %s\n", op1, acc);
                    if (dest_is_mem) fprintf(out, "    movq %s, %s\n", acc, dest);
                    break;
                }

                case SIR_SHL:
                case SIR_SHR: {
                    bool is_unsigned = type_is_unsigned(inst->operands[0]->type);
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    if (strcmp(op0, acc) != 0) fprintf(out, "    movq %s, %s\n", op0, acc);
                    fprintf(out, "    movq %s, %%rcx\n", op1);
                    if (inst->opcode == SIR_SHL) {
                        fprintf(out, "    shlq %%cl, %s\n", acc);
                    } else {
                        if (is_unsigned) fprintf(out, "    shrq %%cl, %s\n", acc);
                        else fprintf(out, "    sarq %%cl, %s\n", acc);
                    }
                    if (dest_is_mem) fprintf(out, "    movq %s, %s\n", acc, dest);
                    break;
                }

                case SIR_ICMP_EQ:
                case SIR_ICMP_NE:
                case SIR_ICMP_LT:
                case SIR_ICMP_LE:
                case SIR_ICMP_GT:
                case SIR_ICMP_GE: {
                    bool is_unsigned = type_is_unsigned(inst->operands[0]->type);
                    const char* cc = "e";
                    if (inst->opcode == SIR_ICMP_NE) cc = "ne";
                    else if (inst->opcode == SIR_ICMP_LT) cc = is_unsigned ? "b" : "l";
                    else if (inst->opcode == SIR_ICMP_LE) cc = is_unsigned ? "be" : "le";
                    else if (inst->opcode == SIR_ICMP_GT) cc = is_unsigned ? "a" : "g";
                    else if (inst->opcode == SIR_ICMP_GE) cc = is_unsigned ? "ae" : "ge";
                    
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    cmpq %s, %%rax\n", op1);
                    fprintf(out, "    set%s %%al\n", cc);
                    fprintf(out, "    movzbq %%al, %%rax\n");
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;
                }

                case SIR_FCMP_EQ:
                case SIR_FCMP_NE:
                case SIR_FCMP_LT:
                case SIR_FCMP_LE:
                case SIR_FCMP_GT:
                case SIR_FCMP_GE: {
                    bool is_f32 = (inst->operands[0]->type && inst->operands[0]->type->kind == TY_F32);
                    const char* cc = "e";
                    if (inst->opcode == SIR_FCMP_NE) cc = "ne";
                    else if (inst->opcode == SIR_FCMP_LT) cc = "b";
                    else if (inst->opcode == SIR_FCMP_LE) cc = "be";
                    else if (inst->opcode == SIR_FCMP_GT) cc = "a";
                    else if (inst->opcode == SIR_FCMP_GE) cc = "ae";
                    
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    movq %s, %%rcx\n", op1);
                    if (is_f32) {
                        fprintf(out, "    movd %%eax, %%xmm0\n");
                        fprintf(out, "    movd %%ecx, %%xmm1\n");
                        fprintf(out, "    ucomiss %%xmm1, %%xmm0\n");
                    } else {
                        fprintf(out, "    movq %%rax, %%xmm0\n");
                        fprintf(out, "    movq %%rcx, %%xmm1\n");
                        fprintf(out, "    ucomisd %%xmm1, %%xmm0\n");
                    }
                    fprintf(out, "    set%s %%al\n", cc);
                    fprintf(out, "    movzbq %%al, %%rax\n");
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;
                }

                case SIR_CALL:
                    // 保护 Caller-Saved 寄存器
                    fprintf(out, "    movq %%r10, -64(%%rbp)\n");
                    fprintf(out, "    movq %%r11, -72(%%rbp)\n");

                    if (inst->operands[0]->kind == SIR_VAL_GLOBAL && strcmp(inst->operands[0]->as.global_name, "scribe") == 0) {
                        char arg_str[64];
                        get_operand_str(arg_str, inst->operands[1], &allocator, 8);
                        fprintf(out, "    movq %s, %%rcx\n", arg_str);
                        
                        ScoriaType* arg_type = inst->operands[1]->type;
                        if (arg_type && arg_type->kind == TY_VIA) arg_type = arg_type->as.inner;
                        bool is_str = (arg_type && arg_type->kind == TY_COHORS && arg_type->as.inner->kind == TY_LITTERA);
                        bool is_bool = (arg_type && arg_type->kind == TY_LOGICA) || (inst->operands[1]->kind == SIR_VAL_CONST_BOOL);
                        bool is_ptr = !is_str && (arg_type && (arg_type->kind == TY_VIA || arg_type->kind == TY_COHORS || arg_type->kind == TY_ACIES));
                        bool is_float = (arg_type && (arg_type->kind == TY_F32 || arg_type->kind == TY_F64)) || (inst->operands[1]->kind == SIR_VAL_CONST_FLOAT);
                        
                        if (is_str) {
                            fprintf(out, "    movq %s, %%rax\n", arg_str);
                            fprintf(out, "    movq 8(%%rax), %%rdx\n");
                            fprintf(out, "    movq (%%rax), %%rcx\n");
                            fprintf(out, "    call __print_str\n");
                        } else if (is_bool) {
                            fprintf(out, "    call __print_bool\n");
                        } else if (is_ptr) {
                            fprintf(out, "    call __print_hex\n");
                        } else if (is_float) {
                            if (inst->operands[1]->type && inst->operands[1]->type->kind == TY_F32) {
                                fprintf(out, "    movd %%ecx, %%xmm0\n");
                                fprintf(out, "    cvtss2sd %%xmm0, %%xmm0\n");
                                fprintf(out, "    movq %%xmm0, %%rcx\n");
                            }
                            fprintf(out, "    call __print_float\n");
                        } else {
                            fprintf(out, "    call __print_int\n");
                        }
                        
                        // 恢复 Caller-Saved 寄存器
                        fprintf(out, "    movq -64(%%rbp), %%r10\n");
                        fprintf(out, "    movq -72(%%rbp), %%r11\n");
                        break;
                    }
                    
                    // Windows x64 ABI: rcx, rdx, r8, r9, 然后是栈
                    int num_args = inst->num_operands - 1;
                    for (int i = num_args - 1; i >= 4; i--) {
                        char arg_str[64];
                        get_operand_str(arg_str, inst->operands[i+1], &allocator, 8);
                        if (inst->operands[i+1]->kind == SIR_VAL_CONST_FLOAT && (!inst->operands[i+1]->type || inst->operands[i+1]->type->kind == TY_F64)) {
                            fprintf(out, "    movabsq %s, %%rax\n", arg_str);
                        } else {
                            fprintf(out, "    movq %s, %%rax\n", arg_str);
                        }
                        fprintf(out, "    movq %%rax, %d(%%rsp)\n", 32 + (i - 4) * 8);
                    }
                    
                    int reg_args = num_args > 4 ? 4 : num_args;
                    for (int i = 0; i < reg_args; i++) {
                        char arg_str[64];
                        get_operand_str(arg_str, inst->operands[i+1], &allocator, 8);
                        if (inst->operands[i+1]->kind == SIR_VAL_CONST_FLOAT && (!inst->operands[i+1]->type || inst->operands[i+1]->type->kind == TY_F64)) {
                            fprintf(out, "    movabsq %s, %%rax\n", arg_str);
                        } else {
                            fprintf(out, "    movq %s, %%rax\n", arg_str);
                        }
                        fprintf(out, "    movq %%rax, %d(%%rsp)\n", i * 8);
                    }
                    
                    const char* arg_regs[] = {"%rcx", "%rdx", "%r8", "%r9"};
                    for (int i = 0; i < reg_args; i++) {
                        fprintf(out, "    movq %d(%%rsp), %s\n", i * 8, arg_regs[i]);
                        bool is_float = (inst->operands[i+1]->type && (inst->operands[i+1]->type->kind == TY_F32 || inst->operands[i+1]->type->kind == TY_F64));
                        if (is_float) {
                            fprintf(out, "    movq %s, %%xmm%d\n", arg_regs[i], i);
                        }
                    }
                    // 针对可变参数函数 (如 printf/scribe)，清空 %al
                    fprintf(out, "    xorq %%rax, %%rax\n");
                    if (inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                        fprintf(out, "    call %s\n", inst->operands[0]->as.global_name);
                    } else {
                        char callee_str[64];
                        get_operand_str(callee_str, inst->operands[0], &allocator, 8);
                        fprintf(out, "    call *%s\n", callee_str);
                    }
                    
                    // 恢复 Caller-Saved 寄存器
                    fprintf(out, "    movq -64(%%rbp), %%r10\n");
                    fprintf(out, "    movq -72(%%rbp), %%r11\n");
                    
                    if (inst->dest) {
                        bool ret_is_float = (inst->dest->type && (inst->dest->type->kind == TY_F32 || inst->dest->type->kind == TY_F64));
                        if (ret_is_float) {
                            fprintf(out, "    movq %%xmm0, %%rax\n");
                        }
                        fprintf(out, "    movq %%rax, %s\n", dest);
                    }
                    break;

                case SIR_RET:
                    if (inst->num_operands > 0) {
                        fprintf(out, "    movq %s, %%rax\n", op0);
                        bool ret_is_float = (inst->operands[0]->type && (inst->operands[0]->type->kind == TY_F32 || inst->operands[0]->type->kind == TY_F64));
                        if (ret_is_float) {
                            fprintf(out, "    movq %%rax, %%xmm0\n");
                        }
                    }
                    // 5. 函数跋 (Epilogue)
                    fprintf(out, "    leaq -56(%%rbp), %%rsp\n");
                    fprintf(out, "    popq %%r15\n");
                    fprintf(out, "    popq %%r14\n");
                    fprintf(out, "    popq %%r13\n");
                    fprintf(out, "    popq %%r12\n");
                    fprintf(out, "    popq %%rdi\n");
                    fprintf(out, "    popq %%rsi\n");
                    fprintf(out, "    popq %%rbx\n");
                    fprintf(out, "    popq %%rbp\n");
                    fprintf(out, "    ret\n");
                    break;

                default:
                    fprintf(out, "    # TODO: unhandled opcode %d\n", inst->opcode);
                    break;
            }
        }
    }

    free(alloca_offsets);
    reg_alloc_free(&allocator);
    fprintf(out, "\n");
}

void asm_x86_64_generate(FILE* out, SirModule* module) {
    if (!module) return;

    builtins_analyze_usage(module);
    g_string_count = 0;

    if (module->first_global) {
        fprintf(out, "    .data\n");
        for (SirGlobalVar* g = module->first_global; g; g = g->next) {
            fprintf(out, "    .globl %s\n", g->name);
            fprintf(out, "    .align 8\n");
            fprintf(out, "%s:\n", g->name);
            fprintf(out, "    .zero %d\n", g->size);
        }
        fprintf(out, "\n");
    }

    // 汇编文件头部
    fprintf(out, "    .text\n\n");

    // 遍历生成所有函数
    for (SirFunction* func = module->first_func; func; func = func->next) {
        generate_function(out, func);
    }

    // 追加内置汇编例程
    asm_builtins_generate(out);

    if (g_string_count > 0) {
        fprintf(out, "    .section .rdata,\"a\"\n");
        for (int i = 0; i < g_string_count; i++) {
            fprintf(out, ".Lstr%d:\n", g_strings[i].id);
            const char* s = g_strings[i].str;
            uint32_t len = g_strings[i].len;
            if (len > 0) {
                fprintf(out, "    .byte ");
                for (size_t j = 0; j < len; j++) {
                    fprintf(out, "%d%s", (unsigned char)s[j], j == len - 1 ? "" : ", ");
                }
                fprintf(out, "\n");
            }
        }
    }
}
