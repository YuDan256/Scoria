#include "asm_x86_64.h"
#include "reg_alloc.h"
#include "builtins.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// =========================================================
// 窥孔优化器 (Peephole Optimizer)
// =========================================================
static char peep_window[4][128];
static int peep_count = 0;

static void flush_peep(FILE* out, int count) {
    for (int i = 0; i < count && i < peep_count; i++) {
        fputs(peep_window[i], out);
    }
    int remaining = peep_count - count;
    for (int i = 0; i < remaining; i++) {
        strcpy(peep_window[i], peep_window[i + count]);
    }
    peep_count = remaining;
}

static int my_fprintf(FILE* out, const char* fmt, ...) {
    char line[256];
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    // 如果不是缩进的汇编指令（如标签、伪指令），直接清空窗口并输出
    if (line[0] != ' ' || line[1] != ' ' || line[2] != ' ' || line[3] != ' ') {
        flush_peep(out, peep_count);
        fputs(line, out);
        return ret;
    }

    if (peep_count == 4) flush_peep(out, 1);
    strcpy(peep_window[peep_count++], line);

    // 模式 1: mov $0, reg -> xor reg, reg
    char reg[32];
    if (sscanf(peep_window[peep_count-1], "    movq $0, %%%s\n", reg) == 1 ||
        sscanf(peep_window[peep_count-1], "    movl $0, %%%s\n", reg) == 1) {
        sprintf(peep_window[peep_count-1], "    xorq %%%s, %%%s\n", reg, reg);
    }

    if (peep_count >= 2) {
        char r1[32], r2[32], r3[32], r4[32];
        if (sscanf(peep_window[peep_count-2], "    movq %%%[^, \n], %%%s\n", r1, r2) == 2 &&
            sscanf(peep_window[peep_count-1], "    movq %%%[^, \n], %%%s\n", r3, r4) == 2) {
            
            // 模式 2: mov r1, r2 ; mov r2, r1 -> mov r1, r2
            if (strcmp(r1, r4) == 0 && strcmp(r2, r3) == 0) {
                peep_count--;
            }
            // 模式 3: mov r1, r2 ; mov r2, r3 -> mov r1, r2 ; mov r1, r3 (打破依赖链)
            else if (strcmp(r2, r3) == 0 && strcmp(r1, r4) != 0) {
                sprintf(peep_window[peep_count-1], "    movq %%%s, %%%s\n", r1, r4);
            }
        }
    }

    return ret;
}
#define fprintf my_fprintf

// 物理寄存器映射表 (对应 reg_alloc.h 中的 NUM_PHYS_REGS = 11)
static const char* phys_regs64[] = {"%rbx", "%rsi", "%rdi", "%r12", "%r13", "%r14", "%r15", "%r8", "%r9", "%r10", "%r11"};
static const char* phys_regs32[] = {"%ebx", "%esi", "%edi", "%r12d", "%r13d", "%r14d", "%r15d", "%r8d", "%r9d", "%r10d", "%r11d"};
static const char* phys_regs16[] = {"%bx", "%si", "%di", "%r12w", "%r13w", "%r14w", "%r15w", "%r8w", "%r9w", "%r10w", "%r11w"};
static const char* phys_regs8[]  = {"%bl", "%sil", "%dil", "%r12b", "%r13b", "%r14b", "%r15b", "%r8b", "%r9b", "%r10b", "%r11b"};

typedef struct {
    const char* str;
    uint32_t len;
    int id;
} StringConst;

static StringConst g_strings[65536];
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
static void get_operand_str(char* buf, SirValue* val, RegAllocator* alloc, int size, int frame_size) {
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
                sprintf(buf, "%d(%%rsp)", frame_size + offset);
            }
            break;
        }
        default:
            strcpy(buf, "???");
            break;
    }
}

static void emit_load_operand(FILE* out, const char* dest_reg64, const char* dest_reg32, SirValue* val, RegAllocator* alloc, int frame_size) {
    char arg_str[64];
    int arg_size = (val->type) ? type_get_size(val->type) : 8;
    if (arg_size < 4) arg_size = 4; // Registers are at least 32-bit
    get_operand_str(arg_str, val, alloc, arg_size, frame_size);
    
    if (val->kind == SIR_VAL_CONST_FLOAT && (!val->type || val->type->kind == TY_F64)) {
        fprintf(out, "    movabsq %s, %s\n", arg_str, dest_reg64);
        return;
    }
    
    if (arg_str[0] != '$' && arg_str[0] != '%') {
        bool is_signed = type_is_signed(val->type);
        int actual_size = (val->type) ? type_get_size(val->type) : 8;
        if (actual_size == 1) {
            fprintf(out, "    %s %s, %s\n", is_signed ? "movsbq" : "movzbq", arg_str, dest_reg64);
        } else if (actual_size == 2) {
            fprintf(out, "    %s %s, %s\n", is_signed ? "movswq" : "movzwq", arg_str, dest_reg64);
        } else if (actual_size == 4) {
            if (is_signed) fprintf(out, "    movslq %s, %s\n", arg_str, dest_reg64);
            else fprintf(out, "    movl %s, %s\n", arg_str, dest_reg32);
        } else {
            fprintf(out, "    movq %s, %s\n", arg_str, dest_reg64);
        }
    } else {
        if (val->kind == SIR_VAL_CONST_INT) {
            if (val->as.int_val == 0) {
                fprintf(out, "    xorq %s, %s\n", dest_reg64, dest_reg64);
                return;
            }
            if (val->as.int_val > 0 && val->as.int_val <= 0xFFFFFFFFLL) {
                fprintf(out, "    movl %s, %s\n", arg_str, dest_reg32);
                return;
            }
            fprintf(out, "    movq %s, %s\n", arg_str, dest_reg64);
            return;
        }
        if (arg_size <= 4) {
            if (strcmp(arg_str, dest_reg32) != 0) fprintf(out, "    movl %s, %s\n", arg_str, dest_reg32);
            if (type_is_signed(val->type)) {
                if (strcmp(dest_reg64, "%rax") == 0) fprintf(out, "    cltq\n");
                else fprintf(out, "    movslq %s, %s\n", dest_reg32, dest_reg64);
            }
        } else {
            if (strcmp(arg_str, dest_reg64) != 0) fprintf(out, "    movq %s, %s\n", arg_str, dest_reg64);
        }
    }
}

static void generate_function(FILE* out, SirFunction* func, SirModule* module, int opt_level) {
    fprintf(out, "    .p2align 4\n"); // 优化: 函数入口 16 字节对齐
    fprintf(out, "    .globl %s\n", func->name);
    fprintf(out, "    .type %s, @function\n", func->name);
    fprintf(out, "%s:\n", func->name);

    // 优化: 序言前置快路径剥离 (Shrink-Wrapping / Fast Path Peephole)
    if (func->has_fast_path && opt_level > 0) {
        const char* cx = func->fp_w ? "%rcx" : "%ecx";
        const char* cmp_op = func->fp_w ? "cmpq" : "cmpl";
        
        fprintf(out, "    %s $%d, %s\n", cmp_op, func->fp_imm, cx);
        fprintf(out, "    %s .Lfast_%s\n", func->fp_jcc_asm, func->name);
    }

    // 1. 扫描函数，找到最大的虚拟寄存器 ID 和 ALLOCA 空间
    uint32_t max_vreg = 0;
    int local_stack_size = 0;

    for (SirBlock* block = func->first_block; block; block = block->next) {
        for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
            if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                if (inst->dest->as.vreg > max_vreg) max_vreg = inst->dest->as.vreg;
            }
            for (int i=0; i<inst->num_operands; i++) {
                if (inst->operands[i] && inst->operands[i]->kind == SIR_VAL_VREG) {
                    if (inst->operands[i]->as.vreg > max_vreg) max_vreg = inst->operands[i]->as.vreg;
                }
            }
        }
    }

    int* alloca_offsets = calloc(max_vreg + 1, sizeof(int));
    int max_call_area = 0;
    bool requires_align = false;

    for (SirBlock* block = func->first_block; block; block = block->next) {
        for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
            if (inst->opcode == SIR_CALL) {
                if (inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                    const char* name = inst->operands[0]->as.global_name;
                    if (strcmp(name, "scribe") == 0 || strcmp(name, "crea") == 0 || strcmp(name, "neca") == 0) {
                        requires_align = true;
                        continue;
                    }
                }
                int args = inst->num_operands - 1;
                bool is_ext = false;
                if (inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                    for (SirExternFunc* ext = module->first_extern; ext; ext = ext->next) {
                        if (strcmp(ext->name, inst->operands[0]->as.global_name) == 0) {
                            is_ext = true; break;
                        }
                    }
                }
                if (is_ext) requires_align = true;
                int area = ((is_ext || opt_level < 2) ? 32 : 0) + args * 8;
                if (area > max_call_area) max_call_area = area;
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
    reg_alloc_build_and_color(&allocator, func, opt_level);

    // 预留栈空间：只为被溢出 (Spilled) 的虚拟寄存器分配栈内存
    for (uint32_t i = 1; i <= max_vreg; i++) {
        if (reg_alloc_get_color(&allocator, i) == -1) {
            reg_alloc_get_offset(&allocator, i, 8); // 强制分配 8 字节偏移量
        }
    }
    
    int local_and_args = allocator.current_offset + max_call_area;

    int num_callee_pushes = 0;
    for (int i = 0; i < 7; i++) {
        if (allocator.used_callee_saved[i]) num_callee_pushes++;
    }

    int stack_sub_size = local_and_args;
    if (opt_level < 2 || requires_align || stack_sub_size > 0) {
        if ((stack_sub_size + num_callee_pushes * 8 + 8) % 16 != 0) {
            stack_sub_size += 8;
        }
    }
    int total_frame_size = stack_sub_size;

    // 4. 遍历基本块和指令 (指令选择 Instruction Selection)
    char op0[64], op1[64], op2[64], dest[64];
    bool prologue_emitted = false;
    
    for (SirBlock* block = func->first_block; block; block = block->next) {
        if (block != func->first_block) {
            SirBlock* prev = func->first_block;
            while (prev->next != block) prev = prev->next;
            if (prev->last_inst) {
                if (prev->last_inst->opcode == SIR_RET) {
                    fprintf(out, "    .p2align 4\n");
                } else if (prev->last_inst->opcode == SIR_JMP) {
                    if (prev->last_inst->operands[0]->as.block != block) {
                        fprintf(out, "    .p2align 4\n");
                    }
                }
            }
        }
        fprintf(out, ".L%s_%u:\n", block->name, block->id);
        
        if (!block->is_frameless && !prologue_emitted) {
            if (allocator.used_callee_saved[0]) fprintf(out, "    pushq %%rbx\n");
            if (allocator.used_callee_saved[1]) fprintf(out, "    pushq %%rsi\n");
            if (allocator.used_callee_saved[2]) fprintf(out, "    pushq %%rdi\n");
            if (allocator.used_callee_saved[3]) fprintf(out, "    pushq %%r12\n");
            if (allocator.used_callee_saved[4]) fprintf(out, "    pushq %%r13\n");
            if (allocator.used_callee_saved[5]) fprintf(out, "    pushq %%r14\n");
            if (allocator.used_callee_saved[6]) fprintf(out, "    pushq %%r15\n");
            if (stack_sub_size > 0) {
                fprintf(out, "    subq $%d, %%rsp\n", stack_sub_size);
            }
            prologue_emitted = true;
        }

        for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
            // 解析操作数
            int s0 = (inst->num_operands > 0 && inst->operands[0]->type) ? type_get_size(inst->operands[0]->type) : 8;
            int s1 = (inst->num_operands > 1 && inst->operands[1]->type) ? type_get_size(inst->operands[1]->type) : 8;
            int s2 = (inst->num_operands > 2 && inst->operands[2]->type) ? type_get_size(inst->operands[2]->type) : 8;
            if (s0 < 4) s0 = 4; if (s1 < 4) s1 = 4; if (s2 < 4) s2 = 4;
            
            if (inst->num_operands > 0) get_operand_str(op0, inst->operands[0], &allocator, s0, total_frame_size);
            if (inst->num_operands > 1) get_operand_str(op1, inst->operands[1], &allocator, s1, total_frame_size);
            if (inst->num_operands > 2) get_operand_str(op2, inst->operands[2], &allocator, s2, total_frame_size);
            
            if (inst->dest) {
                int dest_size = (inst->dest->type) ? type_get_size(inst->dest->type) : 8;
                if (dest_size < 4) dest_size = 4;
                get_operand_str(dest, inst->dest, &allocator, dest_size, total_frame_size);
                if (opt_level > 0 && inst->dest->kind == SIR_VAL_VREG && inst->next && allocator.use_count[inst->dest->as.vreg] == 2) {
                    if (inst->opcode == SIR_ADD || inst->opcode == SIR_SUB || inst->opcode == SIR_MUL || inst->opcode == SIR_AND || inst->opcode == SIR_OR || inst->opcode == SIR_XOR) {
                        if (inst->next->opcode == SIR_RET && inst->next->num_operands > 0 && inst->next->operands[0] == inst->dest) {
                            strcpy(dest, dest_size <= 4 ? "%eax" : "%rax");
                        } else if (inst->next->opcode == SIR_CALL && inst->next->num_operands == 2 && inst->next->operands[1] == inst->dest) {
                            strcpy(dest, dest_size <= 4 ? "%ecx" : "%rcx");
                        }
                    }
                }
            }

            switch (inst->opcode) {
                case SIR_ALLOCA: {
                    int offset = alloca_offsets[inst->dest->as.vreg];
                    if (dest[0] == '%') {
                        fprintf(out, "    leaq %d(%%rsp), %s\n", total_frame_size + offset, dest);
                    } else {
                        fprintf(out, "    leaq %d(%%rsp), %%rax\n", total_frame_size + offset);
                        fprintf(out, "    movq %%rax, %s\n", dest);
                    }
                    break;
                }

                case SIR_STORE: {
                    int size = 8;
                    if (inst->operands[1]->type && inst->operands[1]->type->kind == TY_VIA) {
                        size = type_get_size(inst->operands[1]->type->as.inner);
                    } else if (inst->operands[0]->type) {
                        size = type_get_size(inst->operands[0]->type);
                    }
                    
                    const char* ptr_reg = op1;
                    if (op1[0] != '%') {
                        fprintf(out, "    movq %s, %%rcx\n", op1);
                        ptr_reg = "%rcx";
                    }
                    
                    const char* val_reg = op0;
                    if (op0[0] != '%') {
                        bool is_small_const = false;
                        if (inst->operands[0]->kind == SIR_VAL_CONST_INT) {
                            int64_t val = inst->operands[0]->as.int_val;
                            if (val >= -2147483648LL && val <= 2147483647LL) is_small_const = true;
                        }
                        
                        if (is_small_const) {
                            val_reg = op0;
                        } else {
                            const char* ax = (size <= 4) ? "%eax" : "%rax";
                            emit_load_operand(out, "%rax", "%eax", inst->operands[0], &allocator, total_frame_size);
                            val_reg = ax;
                        }
                    } else {
                        if (size == 4 && strcmp(op0, "%rax") == 0) val_reg = "%eax";
                        else if (size == 4 && strcmp(op0, "%rcx") == 0) val_reg = "%ecx";
                        else if (size == 4 && strcmp(op0, "%rdx") == 0) val_reg = "%edx";
                        else if (size == 4 && strcmp(op0, "%rbx") == 0) val_reg = "%ebx";
                        else if (size == 4 && strcmp(op0, "%rsi") == 0) val_reg = "%esi";
                        else if (size == 4 && strcmp(op0, "%rdi") == 0) val_reg = "%edi";
                    }
                    
                    if (size == 1) {
                        if (val_reg[0] == '%') {
                            if (strcmp(val_reg, "%rax") == 0 || strcmp(val_reg, "%eax") == 0) val_reg = "%al";
                            else if (strcmp(val_reg, "%rcx") == 0 || strcmp(val_reg, "%ecx") == 0) val_reg = "%cl";
                            else if (strcmp(val_reg, "%rdx") == 0 || strcmp(val_reg, "%edx") == 0) val_reg = "%dl";
                            else if (strcmp(val_reg, "%rbx") == 0 || strcmp(val_reg, "%ebx") == 0) val_reg = "%bl";
                            else if (strcmp(val_reg, "%rsi") == 0 || strcmp(val_reg, "%esi") == 0) val_reg = "%sil";
                            else if (strcmp(val_reg, "%rdi") == 0 || strcmp(val_reg, "%edi") == 0) val_reg = "%dil";
                        }
                        fprintf(out, "    movb %s, (%s)\n", val_reg, ptr_reg);
                    } else if (size == 2) {
                        if (val_reg[0] == '%') {
                            if (strcmp(val_reg, "%rax") == 0 || strcmp(val_reg, "%eax") == 0) val_reg = "%ax";
                            else if (strcmp(val_reg, "%rcx") == 0 || strcmp(val_reg, "%ecx") == 0) val_reg = "%cx";
                            else if (strcmp(val_reg, "%rdx") == 0 || strcmp(val_reg, "%edx") == 0) val_reg = "%dx";
                            else if (strcmp(val_reg, "%rbx") == 0 || strcmp(val_reg, "%ebx") == 0) val_reg = "%bx";
                            else if (strcmp(val_reg, "%rsi") == 0 || strcmp(val_reg, "%esi") == 0) val_reg = "%si";
                            else if (strcmp(val_reg, "%rdi") == 0 || strcmp(val_reg, "%edi") == 0) val_reg = "%di";
                        }
                        fprintf(out, "    movw %s, (%s)\n", val_reg, ptr_reg);
                    } else if (size == 4) {
                        fprintf(out, "    movl %s, (%s)\n", val_reg, ptr_reg);
                    } else {
                        fprintf(out, "    movq %s, (%s)\n", val_reg, ptr_reg);
                    }
                    break;
                }

                case SIR_LOAD: {
                    int size = type_get_size(inst->dest->type);
                    bool is_signed = type_is_signed(inst->dest->type);
                    
                    const char* ptr_reg = op0;
                    if (op0[0] != '%') {
                        fprintf(out, "    movq %s, %%rcx\n", op0);
                        ptr_reg = "%rcx";
                    }
                    
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    const char* acc32 = dest_is_mem ? "%eax" : dest;
                    
                    char acc32_buf[16];
                    if (!dest_is_mem) {
                        if (strcmp(dest, "%rax") == 0) strcpy(acc32_buf, "%eax");
                        else if (strcmp(dest, "%rcx") == 0) strcpy(acc32_buf, "%ecx");
                        else if (strcmp(dest, "%rdx") == 0) strcpy(acc32_buf, "%edx");
                        else if (strcmp(dest, "%rbx") == 0) strcpy(acc32_buf, "%ebx");
                        else if (strcmp(dest, "%rsi") == 0) strcpy(acc32_buf, "%esi");
                        else if (strcmp(dest, "%rdi") == 0) strcpy(acc32_buf, "%edi");
                        else if (strcmp(dest, "%r8") == 0) strcpy(acc32_buf, "%r8d");
                        else if (strcmp(dest, "%r9") == 0) strcpy(acc32_buf, "%r9d");
                        else if (strcmp(dest, "%r10") == 0) strcpy(acc32_buf, "%r10d");
                        else if (strcmp(dest, "%r11") == 0) strcpy(acc32_buf, "%r11d");
                        else if (strcmp(dest, "%r12") == 0) strcpy(acc32_buf, "%r12d");
                        else if (strcmp(dest, "%r13") == 0) strcpy(acc32_buf, "%r13d");
                        else if (strcmp(dest, "%r14") == 0) strcpy(acc32_buf, "%r14d");
                        else if (strcmp(dest, "%r15") == 0) strcpy(acc32_buf, "%r15d");
                        acc32 = acc32_buf;
                    }
                    
                    if (size == 1) {
                        if (is_signed) fprintf(out, "    movsbq (%s), %s\n", ptr_reg, acc);
                        else fprintf(out, "    movzbq (%s), %s\n", ptr_reg, acc);
                    } else if (size == 2) {
                        if (is_signed) fprintf(out, "    movswq (%s), %s\n", ptr_reg, acc);
                        else fprintf(out, "    movzwq (%s), %s\n", ptr_reg, acc);
                    } else if (size == 4) {
                        if (is_signed) fprintf(out, "    movslq (%s), %s\n", ptr_reg, acc);
                        else fprintf(out, "    movl (%s), %s\n", ptr_reg, acc32);
                    } else {
                        fprintf(out, "    movq (%s), %s\n", ptr_reg, acc);
                    }
                    
                    if (dest_is_mem) {
                        const char* mov_op = (size <= 4) ? "movl" : "movq";
                        fprintf(out, "    %s %s, %s\n", mov_op, (size <= 4) ? "%eax" : "%rax", dest);
                    }
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
                        emit_load_operand(out, "%rax", "%eax", inst->operands[0], &allocator, total_frame_size);
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
                        emit_load_operand(out, "%rax", "%eax", inst->operands[0], &allocator, total_frame_size);
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
                    int dest_size = (inst->dest && inst->dest->type) ? type_get_size(inst->dest->type) : 8;
                    const char* ax = (dest_size <= 4) ? "%eax" : "%rax";
                    const char* acc = dest_is_mem ? ax : dest;
                    const char* suf = (dest_size <= 4) ? "l" : "q";
                    
                    const char* left_op = op0;
                    if (opt_level > 0 && inst->prev && inst->prev->opcode == SIR_CALL && inst->prev->dest == inst->operands[0]) {
                        if (inst->operands[0]->kind == SIR_VAL_VREG && allocator.use_count[inst->operands[0]->as.vreg] == 2) {
                            left_op = ax;
                        }
                    }
                    const char* right_op = op1;
                    if (opt_level > 0 && inst->num_operands > 1 && inst->prev && inst->prev->opcode == SIR_CALL && inst->prev->dest == inst->operands[1]) {
                        if (inst->operands[1]->kind == SIR_VAL_VREG && allocator.use_count[inst->operands[1]->as.vreg] == 2) {
                            right_op = ax;
                        }
                    }
                    
                    if (inst->operands[1]->kind == SIR_VAL_CONST_INT && strcmp(left_op, acc) != 0 && left_op[0] == '%') {
                        int64_t imm = inst->operands[1]->as.int_val;
                        fprintf(out, "    lea%s %lld(%s), %s\n", suf, (long long)imm, left_op, acc);
                    } else {
                        if (strcmp(right_op, acc) == 0 && strcmp(left_op, acc) != 0) {
                            fprintf(out, "    add%s %s, %s\n", suf, left_op, acc);
                        } else if (strcmp(left_op, acc) != 0 && strcmp(right_op, acc) != 0 && left_op[0] == '%' && right_op[0] == '%') {
                            fprintf(out, "    lea%s (%s, %s), %s\n", suf, left_op, right_op, acc);
                        } else {
                            if (strcmp(left_op, acc) != 0) fprintf(out, "    mov%s %s, %s\n", suf, left_op, acc);
                            if (strcmp(right_op, "$1") == 0) {
                                fprintf(out, "    inc%s %s\n", suf, acc);
                            } else if (strcmp(right_op, "$0") != 0) {
                                fprintf(out, "    add%s %s, %s\n", suf, right_op, acc);
                            }
                        }
                    }
                    if (dest_is_mem) fprintf(out, "    mov%s %s, %s\n", suf, acc, dest);
                    break;
                }

                case SIR_SUB: {
                    bool dest_is_mem = (dest[0] != '%');
                    int dest_size = (inst->dest && inst->dest->type) ? type_get_size(inst->dest->type) : 8;
                    const char* ax = (dest_size <= 4) ? "%eax" : "%rax";
                    const char* acc = dest_is_mem ? ax : dest;
                    const char* suf = (dest_size <= 4) ? "l" : "q";
                    
                    const char* left_op = op0;
                    if (opt_level > 0 && inst->prev && inst->prev->opcode == SIR_CALL && inst->prev->dest == inst->operands[0]) {
                        if (inst->operands[0]->kind == SIR_VAL_VREG && allocator.use_count[inst->operands[0]->as.vreg] == 2) {
                            left_op = ax;
                        }
                    }
                    const char* right_op = op1;
                    if (opt_level > 0 && inst->num_operands > 1 && inst->prev && inst->prev->opcode == SIR_CALL && inst->prev->dest == inst->operands[1]) {
                        if (inst->operands[1]->kind == SIR_VAL_VREG && allocator.use_count[inst->operands[1]->as.vreg] == 2) {
                            right_op = ax;
                        }
                    }
                    
                    if (inst->operands[1]->kind == SIR_VAL_CONST_INT && strcmp(left_op, acc) != 0 && left_op[0] == '%') {
                        int64_t imm = inst->operands[1]->as.int_val;
                        fprintf(out, "    lea%s %lld(%s), %s\n", suf, (long long)-imm, left_op, acc);
                    } else {
                        if (strcmp(right_op, acc) == 0 && strcmp(left_op, acc) != 0) {
                            fprintf(out, "    neg%s %s\n", suf, acc);
                            fprintf(out, "    add%s %s, %s\n", suf, left_op, acc);
                        } else {
                            if (strcmp(left_op, acc) != 0) fprintf(out, "    mov%s %s, %s\n", suf, left_op, acc);
                            if (strcmp(right_op, "$1") == 0) {
                                fprintf(out, "    dec%s %s\n", suf, acc);
                            } else if (strcmp(right_op, "$0") != 0) {
                                fprintf(out, "    sub%s %s, %s\n", suf, right_op, acc);
                            }
                        }
                    }
                    if (dest_is_mem) fprintf(out, "    mov%s %s, %s\n", suf, acc, dest);
                    break;
                }

                case SIR_MUL: {
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    
                    if (inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                        if (strcmp(op0, acc) != 0) {
                            emit_load_operand(out, acc, (strcmp(acc, "%rcx") == 0) ? "%ecx" : "%eax", inst->operands[0], &allocator, total_frame_size);
                        }
                        int64_t imm = inst->operands[1]->as.int_val;
                        if (imm == 0) {
                            fprintf(out, "    xorq %s, %s\n", acc, acc);
                        } else if (imm == 1) {
                            // no-op
                        } else if (imm == 2) {
                            fprintf(out, "    addq %s, %s\n", acc, acc);
                        } else if (imm == 3 || imm == 5 || imm == 9) {
                            int scale = (imm == 3) ? 2 : (imm == 5) ? 4 : 8;
                            fprintf(out, "    leaq (%s, %s, %d), %s\n", acc, acc, scale, acc);
                        } else if (imm > 0 && (imm & (imm - 1)) == 0) {
                            int shift = 0;
                            while ((imm >> shift) > 1) shift++;
                            fprintf(out, "    shlq $%d, %s\n", shift, acc);
                        } else {
                            fprintf(out, "    imulq %s, %s\n", op1, acc);
                        }
                    } else {
                        if (strcmp(op1, acc) == 0 && strcmp(op0, acc) != 0) {
                            emit_load_operand(out, "%rcx", "%ecx", inst->operands[0], &allocator, total_frame_size);
                            fprintf(out, "    imulq %%rcx, %s\n", acc);
                        } else {
                            if (strcmp(op0, acc) != 0) {
                                emit_load_operand(out, acc, (strcmp(acc, "%rcx") == 0) ? "%ecx" : "%eax", inst->operands[0], &allocator, total_frame_size);
                            }
                            emit_load_operand(out, "%rcx", "%ecx", inst->operands[1], &allocator, total_frame_size);
                            fprintf(out, "    imulq %%rcx, %s\n", acc);
                        }
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
                    
                    if (strcmp(op1, acc) == 0 && strcmp(op0, acc) != 0) {
                        emit_load_operand(out, "%rax", "%eax", inst->operands[0], &allocator, total_frame_size);
                        fprintf(out, "    movq %%rax, %%xmm0\n");
                        fprintf(out, "    movq %s, %%xmm1\n", op1);
                    } else {
                        if (strcmp(op0, acc) != 0) {
                            emit_load_operand(out, acc, (strcmp(acc, "%rcx") == 0) ? "%ecx" : "%eax", inst->operands[0], &allocator, total_frame_size);
                        }
                        emit_load_operand(out, "%rcx", "%ecx", inst->operands[1], &allocator, total_frame_size);
                        fprintf(out, "    movq %s, %%xmm0\n", acc);
                        fprintf(out, "    movq %%rcx, %%xmm1\n");
                    }
                    
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
                    if (block->next && inst->operands[0]->as.block == block->next) {
                        fprintf(out, "    # fall-through to .L%s_%u\n", inst->operands[0]->as.block->name, inst->operands[0]->as.block->id);
                    } else {
                        fprintf(out, "    jmp .L%s_%u\n", inst->operands[0]->as.block->name, inst->operands[0]->as.block->id);
                    }
                    break;

                case SIR_BR:
                    // br cond(op0), true_block(op1), false_block(op2)
                    emit_load_operand(out, "%rax", "%eax", inst->operands[0], &allocator, total_frame_size);
                    fprintf(out, "    testq %%rax, %%rax\n");
                    if (block->next && inst->operands[2]->as.block == block->next) {
                        fprintf(out, "    jne .L%s_%u\n", inst->operands[1]->as.block->name, inst->operands[1]->as.block->id);
                    } else if (block->next && inst->operands[1]->as.block == block->next) {
                        fprintf(out, "    je .L%s_%u\n", inst->operands[2]->as.block->name, inst->operands[2]->as.block->id);
                    } else {
                        fprintf(out, "    jne .L%s_%u\n", inst->operands[1]->as.block->name, inst->operands[1]->as.block->id);
                        fprintf(out, "    jmp .L%s_%u\n", inst->operands[2]->as.block->name, inst->operands[2]->as.block->id);
                    }
                    break;

                case SIR_SWITCH: {
                    emit_load_operand(out, "%rax", "%eax", inst->operands[0], &allocator, total_frame_size);

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
                            get_operand_str(val_str, inst->operands[2 + i * 2], &allocator, 8, total_frame_size);
                            fprintf(out, "    cmpq %s, %%rax\n", val_str);
                            SirBlock* target = inst->operands[2 + i * 2 + 1]->as.block;
                            fprintf(out, "    je .L%s_%u\n", target->name, target->id);
                        }
                        fprintf(out, "    jmp .L%s_%u\n", def_block->name, def_block->id);
                    }
                    break;
                }

                case SIR_SELECT: {
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    const char* t_scratch = (strcmp(acc, "%rcx") == 0) ? "%rdx" : "%rcx";
                    const char* cond_scratch = (strcmp(t_scratch, "%rcx") == 0) ? "%r8" : "%rcx";
                    if (strcmp(cond_scratch, acc) == 0) cond_scratch = "%r9";
                    
                    emit_load_operand(out, cond_scratch, (strcmp(cond_scratch, "%rcx") == 0) ? "%ecx" : ((strcmp(cond_scratch, "%r8") == 0) ? "%r8d" : "%r9d"), inst->operands[0], &allocator, total_frame_size);
                    
                    if (strcmp(op1, acc) == 0) {
                        fprintf(out, "    movq %s, %s\n", op1, t_scratch);
                        if (strcmp(op2, acc) != 0) fprintf(out, "    movq %s, %s\n", op2, acc);
                    } else {
                        if (strcmp(op2, acc) != 0) fprintf(out, "    movq %s, %s\n", op2, acc);
                        if (strcmp(op1, t_scratch) != 0) fprintf(out, "    movq %s, %s\n", op1, t_scratch);
                    }
                    
                    fprintf(out, "    testq %s, %s\n", cond_scratch, cond_scratch);
                    
                    fprintf(out, "    cmovneq %s, %s\n", t_scratch, acc);
                    
                    if (dest_is_mem) fprintf(out, "    movq %s, %s\n", acc, dest);
                    break;
                }

                case SIR_GET_PARAM: {
                    int param_idx = (int)inst->operands[0]->as.int_val;
                    int max_reg_args = (opt_level >= 2) ? 6 : 4;
                    if (param_idx < max_reg_args) {
                        const char* regs[] = {"%rcx", "%rdx", "%r8", "%r9", "%r10", "%r11"};
                        const char* regs32[] = {"%ecx", "%edx", "%r8d", "%r9d", "%r10d", "%r11d"};
                        bool is_float = (inst->dest->type && (inst->dest->type->kind == TY_F32 || inst->dest->type->kind == TY_F64));
                        if (is_float) {
                            bool is_f32 = (inst->dest->type->kind == TY_F32);
                            if (is_f32) fprintf(out, "    movd %%xmm%d, %%eax\n", param_idx);
                            else fprintf(out, "    movq %%xmm%d, %%rax\n", param_idx);
                            if (strcmp(dest, "%rax") != 0 && strcmp(dest, "%eax") != 0) {
                                fprintf(out, "    %s %%rax, %s\n", is_f32 ? "movl" : "movq", dest);
                            }
                        } else {
                            int dest_size = (inst->dest->type) ? type_get_size(inst->dest->type) : 8;
                            const char* src_reg = (dest_size <= 4) ? regs32[param_idx] : regs[param_idx];
                            const char* mov_op = (dest_size <= 4) ? "movl" : "movq";
                            if (strcmp(src_reg, dest) != 0 && strcmp(regs[param_idx], dest) != 0) {
                                fprintf(out, "    %s %s, %s\n", mov_op, src_reg, dest);
                            }
                        }
                    } else {
                        int offset = 8 + total_frame_size + num_callee_pushes * 8 + (param_idx - max_reg_args) * 8;
                        int size = type_get_size(inst->dest->type);
                        bool is_signed = type_is_signed(inst->dest->type);
                        
                        if (size == 1) {
                            if (is_signed) fprintf(out, "    movsbq %d(%%rsp), %%rax\n", offset);
                            else fprintf(out, "    movzbq %d(%%rsp), %%rax\n", offset);
                        } else if (size == 2) {
                            if (is_signed) fprintf(out, "    movswq %d(%%rsp), %%rax\n", offset);
                            else fprintf(out, "    movzwq %d(%%rsp), %%rax\n", offset);
                        } else if (size == 4) {
                            if (is_signed) fprintf(out, "    movslq %d(%%rsp), %%rax\n", offset);
                            else fprintf(out, "    movl %d(%%rsp), %%eax\n", offset);
                        } else {
                            fprintf(out, "    movq %d(%%rsp), %%rax\n", offset);
                        }
                        fprintf(out, "    movq %%rax, %s\n", dest);
                    }
                    break;
                }

                case SIR_MEMCPY: {
                    int size = (int)inst->operands[2]->as.int_val;
                    if (strcmp(op0, "%rdx") == 0 && strcmp(op1, "%rax") == 0) {
                        fprintf(out, "    xchgq %%rax, %%rdx\n");
                    } else if (strcmp(op1, "%rax") == 0) {
                        fprintf(out, "    movq %s, %%rdx\n", op1);
                        if (strcmp(op0, "%rax") != 0) fprintf(out, "    movq %s, %%rax\n", op0);
                    } else {
                        if (strcmp(op0, "%rax") != 0) fprintf(out, "    movq %s, %%rax\n", op0);
                        if (strcmp(op1, "%rdx") != 0) fprintf(out, "    movq %s, %%rdx\n", op1);
                    }
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
                    
                    if (inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                        int64_t offset = inst->operands[1]->as.int_val * element_size;
                        if (offset == 0) {
                            if (strcmp(op0, acc) != 0) fprintf(out, "    movq %s, %s\n", op0, acc);
                        } else {
                            if (op0[0] == '%') {
                                fprintf(out, "    leaq %lld(%s), %s\n", (long long)offset, op0, acc);
                            } else {
                                fprintf(out, "    movq %s, %s\n", op0, acc);
                                fprintf(out, "    addq $%lld, %s\n", (long long)offset, acc);
                            }
                        }
                    } else {
                        const char* idx_reg = op1;
                        if (strcmp(op1, acc) == 0) {
                            const char* idx_scratch = (strcmp(acc, "%rcx") == 0) ? "%rdx" : "%rcx";
                            emit_load_operand(out, idx_scratch, (strcmp(idx_scratch, "%rcx") == 0) ? "%ecx" : "%edx", inst->operands[1], &allocator, total_frame_size);
                            idx_reg = idx_scratch;
                        } else {
                            int idx_size = (inst->operands[1]->type) ? type_get_size(inst->operands[1]->type) : 8;
                            if (idx_size <= 4 && op1[0] == '%') {
                                if (strcmp(op1, "%eax") == 0) idx_reg = "%rax";
                                else if (strcmp(op1, "%ecx") == 0) idx_reg = "%rcx";
                                else if (strcmp(op1, "%edx") == 0) idx_reg = "%rdx";
                                else if (strcmp(op1, "%ebx") == 0) idx_reg = "%rbx";
                                else if (strcmp(op1, "%esi") == 0) idx_reg = "%rsi";
                                else if (strcmp(op1, "%edi") == 0) idx_reg = "%rdi";
                                else if (strcmp(op1, "%r8d") == 0) idx_reg = "%r8";
                                else if (strcmp(op1, "%r9d") == 0) idx_reg = "%r9";
                                else if (strcmp(op1, "%r10d") == 0) idx_reg = "%r10";
                                else if (strcmp(op1, "%r11d") == 0) idx_reg = "%r11";
                                else if (strcmp(op1, "%r12d") == 0) idx_reg = "%r12";
                                else if (strcmp(op1, "%r13d") == 0) idx_reg = "%r13";
                                else if (strcmp(op1, "%r14d") == 0) idx_reg = "%r14";
                                else if (strcmp(op1, "%r15d") == 0) idx_reg = "%r15";
                            } else if (op1[0] != '$' && op1[0] != '%') {
                                const char* idx_scratch = (strcmp(acc, "%rcx") == 0) ? "%rdx" : "%rcx";
                                emit_load_operand(out, idx_scratch, (strcmp(idx_scratch, "%rcx") == 0) ? "%ecx" : "%edx", inst->operands[1], &allocator, total_frame_size);
                                idx_reg = idx_scratch;
                            }
                        }
                        if (strcmp(op0, acc) != 0) fprintf(out, "    movq %s, %s\n", op0, acc);
                        
                        if (element_size == 1) {
                            fprintf(out, "    addq %s, %s\n", idx_reg, acc);
                        } else if ((element_size == 2 || element_size == 4 || element_size == 8) && idx_reg[0] == '%') {
                            fprintf(out, "    leaq (%s, %s, %d), %s\n", acc, idx_reg, element_size, acc);
                        } else {
                            const char* mul_scratch = (strcmp(acc, "%rcx") == 0) ? "%rdx" : "%rcx";
                            if (strcmp(idx_reg, mul_scratch) != 0) fprintf(out, "    movq %s, %s\n", idx_reg, mul_scratch);
                            fprintf(out, "    imulq $%d, %s\n", element_size, mul_scratch);
                            fprintf(out, "    addq %s, %s\n", mul_scratch, acc);
                        }
                    }
                    if (dest_is_mem) fprintf(out, "    movq %s, %s\n", acc, dest);
                    break;
                }

                case SIR_DIV:
                case SIR_MOD: {
                    bool is_unsigned = type_is_unsigned(inst->operands[0]->type);
                    
                    // 优化：除以 2 的幂转换为移位
                    if (opt_level > 0 && inst->opcode == SIR_DIV && inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                        int64_t imm = inst->operands[1]->as.int_val;
                        if (imm > 0 && (imm & (imm - 1)) == 0) {
                            emit_load_operand(out, "%rax", "%eax", inst->operands[0], &allocator, total_frame_size);
                            int shift = 0;
                            while ((imm >> shift) > 1) shift++;
                            if (shift > 0) {
                                if (is_unsigned) {
                                    fprintf(out, "    shrq $%d, %%rax\n", shift);
                                } else {
                                    fprintf(out, "    cqo\n");
                                    fprintf(out, "    andq $%lld, %%rdx\n", (long long)((1ULL << shift) - 1));
                                    fprintf(out, "    addq %%rdx, %%rax\n");
                                    fprintf(out, "    sarq $%d, %%rax\n", shift);
                                }
                            }
                            fprintf(out, "    movq %%rax, %s\n", dest);
                            break;
                        }
                    }
                    
                    if (strcmp(op0, "%rcx") == 0 && strcmp(op1, "%rax") == 0) {
                        fprintf(out, "    xchgq %%rax, %%rcx\n");
                    } else if (strcmp(op1, "%rax") == 0) {
                        emit_load_operand(out, "%rcx", "%ecx", inst->operands[1], &allocator, total_frame_size);
                        emit_load_operand(out, "%rax", "%eax", inst->operands[0], &allocator, total_frame_size);
                    } else {
                        emit_load_operand(out, "%rax", "%eax", inst->operands[0], &allocator, total_frame_size);
                        emit_load_operand(out, "%rcx", "%ecx", inst->operands[1], &allocator, total_frame_size);
                    }
                    
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
                    if (strcmp(op1, acc) == 0 && strcmp(op0, acc) != 0) {
                        emit_load_operand(out, "%rcx", "%ecx", inst->operands[0], &allocator, total_frame_size);
                        fprintf(out, "    andq %%rcx, %s\n", acc);
                    } else {
                        if (strcmp(op0, acc) != 0) {
                            emit_load_operand(out, acc, (strcmp(acc, "%rcx") == 0) ? "%ecx" : "%eax", inst->operands[0], &allocator, total_frame_size);
                        }
                        emit_load_operand(out, "%rcx", "%ecx", inst->operands[1], &allocator, total_frame_size);
                        fprintf(out, "    andq %%rcx, %s\n", acc);
                    }
                    if (dest_is_mem) fprintf(out, "    movq %s, %s\n", acc, dest);
                    break;
                }

                case SIR_OR: {
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    if (strcmp(op1, acc) == 0 && strcmp(op0, acc) != 0) {
                        emit_load_operand(out, "%rcx", "%ecx", inst->operands[0], &allocator, total_frame_size);
                        fprintf(out, "    orq %%rcx, %s\n", acc);
                    } else {
                        if (strcmp(op0, acc) != 0) {
                            emit_load_operand(out, acc, (strcmp(acc, "%rcx") == 0) ? "%ecx" : "%eax", inst->operands[0], &allocator, total_frame_size);
                        }
                        emit_load_operand(out, "%rcx", "%ecx", inst->operands[1], &allocator, total_frame_size);
                        fprintf(out, "    orq %%rcx, %s\n", acc);
                    }
                    if (dest_is_mem) fprintf(out, "    movq %s, %s\n", acc, dest);
                    break;
                }

                case SIR_XOR: {
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    if (strcmp(op1, acc) == 0 && strcmp(op0, acc) != 0) {
                        emit_load_operand(out, "%rcx", "%ecx", inst->operands[0], &allocator, total_frame_size);
                        fprintf(out, "    xorq %%rcx, %s\n", acc);
                    } else {
                        if (strcmp(op0, acc) != 0) {
                            emit_load_operand(out, acc, (strcmp(acc, "%rcx") == 0) ? "%ecx" : "%eax", inst->operands[0], &allocator, total_frame_size);
                        }
                        emit_load_operand(out, "%rcx", "%ecx", inst->operands[1], &allocator, total_frame_size);
                        fprintf(out, "    xorq %%rcx, %s\n", acc);
                    }
                    if (dest_is_mem) fprintf(out, "    movq %s, %s\n", acc, dest);
                    break;
                }

                case SIR_SHL:
                case SIR_SHR: {
                    bool is_unsigned = type_is_unsigned(inst->operands[0]->type);
                    bool dest_is_mem = (dest[0] != '%');
                    const char* acc = dest_is_mem ? "%rax" : dest;
                    
                    if (strcmp(op1, acc) == 0 && strcmp(op0, "%rcx") == 0) {
                        fprintf(out, "    xchgq %s, %%rcx\n", acc);
                    } else if (strcmp(op1, acc) == 0 && strcmp(op0, acc) != 0) {
                        emit_load_operand(out, "%rcx", "%ecx", inst->operands[1], &allocator, total_frame_size);
                        emit_load_operand(out, acc, (strcmp(acc, "%rcx") == 0) ? "%ecx" : "%eax", inst->operands[0], &allocator, total_frame_size);
                    } else {
                        if (strcmp(op0, acc) != 0) {
                            emit_load_operand(out, acc, (strcmp(acc, "%rcx") == 0) ? "%ecx" : "%eax", inst->operands[0], &allocator, total_frame_size);
                        }
                        emit_load_operand(out, "%rcx", "%ecx", inst->operands[1], &allocator, total_frame_size);
                    }
                    
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
                    int cmp_size = (inst->operands[0]->type) ? type_get_size(inst->operands[0]->type) : 8;
                    const char* suf = (cmp_size <= 4) ? "l" : "q";
                    
                    const char* cc = "e";
                    if (inst->opcode == SIR_ICMP_NE) cc = "ne";
                    else if (inst->opcode == SIR_ICMP_LT) cc = is_unsigned ? "b" : "l";
                    else if (inst->opcode == SIR_ICMP_LE) cc = is_unsigned ? "be" : "le";
                    else if (inst->opcode == SIR_ICMP_GT) cc = is_unsigned ? "a" : "g";
                    else if (inst->opcode == SIR_ICMP_GE) cc = is_unsigned ? "ae" : "ge";
                    
                    const char* left_op = op0;
                    if (op0[0] != '%') {
                        if (cmp_size <= 4) {
                            fprintf(out, "    movl %s, %%eax\n", op0);
                            left_op = "%eax";
                        } else {
                            fprintf(out, "    movq %s, %%rax\n", op0);
                            left_op = "%rax";
                        }
                    }
                    if (strcmp(op1, "$0") == 0) {
                        fprintf(out, "    test%s %s, %s\n", suf, left_op, left_op);
                    } else {
                        fprintf(out, "    cmp%s %s, %s\n", suf, op1, left_op);
                    }
                    bool can_fuse = false;
                    SirInst* next_inst = inst->next;
                    if (opt_level > 0 && next_inst && next_inst->opcode == SIR_BR && next_inst->operands[0]->kind == SIR_VAL_VREG && inst->dest->kind == SIR_VAL_VREG && next_inst->operands[0]->as.vreg == inst->dest->as.vreg) {
                        bool used_elsewhere = false;
                        for (SirInst* scan = next_inst->next; scan; scan = scan->next) {
                            for (int i=0; i<scan->num_operands; i++) {
                                if (scan->operands[i] && scan->operands[i]->kind == SIR_VAL_VREG && scan->operands[i]->as.vreg == inst->dest->as.vreg) {
                                    used_elsewhere = true; break;
                                }
                            }
                            if (used_elsewhere) break;
                        }
                        if (!used_elsewhere) can_fuse = true;
                    }

                    if (can_fuse) {
                        const char* jcc = "je";
                        const char* inv_jcc = "jne";
                        if (inst->opcode == SIR_ICMP_NE) { jcc = "jne"; inv_jcc = "je"; }
                        else if (inst->opcode == SIR_ICMP_LT) { jcc = is_unsigned ? "jb" : "jl"; inv_jcc = is_unsigned ? "jae" : "jge"; }
                        else if (inst->opcode == SIR_ICMP_LE) { jcc = is_unsigned ? "jbe" : "jle"; inv_jcc = is_unsigned ? "ja" : "jg"; }
                        else if (inst->opcode == SIR_ICMP_GT) { jcc = is_unsigned ? "ja" : "jg"; inv_jcc = is_unsigned ? "jbe" : "jle"; }
                        else if (inst->opcode == SIR_ICMP_GE) { jcc = is_unsigned ? "jae" : "jge"; inv_jcc = is_unsigned ? "jb" : "jl"; }
                        
                        if (block->next && next_inst->operands[2]->as.block == block->next) {
                            fprintf(out, "    %s .L%s_%u\n", jcc, next_inst->operands[1]->as.block->name, next_inst->operands[1]->as.block->id);
                        } else if (block->next && next_inst->operands[1]->as.block == block->next) {
                            fprintf(out, "    %s .L%s_%u\n", inv_jcc, next_inst->operands[2]->as.block->name, next_inst->operands[2]->as.block->id);
                        } else {
                            fprintf(out, "    %s .L%s_%u\n", jcc, next_inst->operands[1]->as.block->name, next_inst->operands[1]->as.block->id);
                            fprintf(out, "    jmp .L%s_%u\n", next_inst->operands[2]->as.block->name, next_inst->operands[2]->as.block->id);
                        }
                        
                        inst = next_inst; // 跳过下一个 BR 指令
                    } else {
                        fprintf(out, "    set%s %%al\n", cc);
                        fprintf(out, "    movzbq %%al, %%rax\n");
                        fprintf(out, "    movq %%rax, %s\n", dest);
                    }
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
                    
                    if (strcmp(op0, "%rcx") == 0 && strcmp(op1, "%rax") == 0) {
                        fprintf(out, "    xchgq %%rax, %%rcx\n");
                    } else if (strcmp(op1, "%rax") == 0) {
                        fprintf(out, "    movq %s, %%rcx\n", op1);
                        if (strcmp(op0, "%rax") != 0) fprintf(out, "    movq %s, %%rax\n", op0);
                    } else {
                        if (strcmp(op0, "%rax") != 0) fprintf(out, "    movq %s, %%rax\n", op0);
                        if (strcmp(op1, "%rcx") != 0) fprintf(out, "    movq %s, %%rcx\n", op1);
                    }
                    if (is_f32) {
                        fprintf(out, "    movd %%eax, %%xmm0\n");
                        fprintf(out, "    movd %%ecx, %%xmm1\n");
                        fprintf(out, "    ucomiss %%xmm1, %%xmm0\n");
                    } else {
                        fprintf(out, "    movq %%rax, %%xmm0\n");
                        fprintf(out, "    movq %%rcx, %%xmm1\n");
                        fprintf(out, "    ucomisd %%xmm1, %%xmm0\n");
                    }
                    bool can_fuse = false;
                    SirInst* next_inst = inst->next;
                    if (opt_level > 0 && next_inst && next_inst->opcode == SIR_BR && next_inst->operands[0]->kind == SIR_VAL_VREG && inst->dest->kind == SIR_VAL_VREG && next_inst->operands[0]->as.vreg == inst->dest->as.vreg) {
                        bool used_elsewhere = false;
                        for (SirInst* scan = next_inst->next; scan; scan = scan->next) {
                            for (int i=0; i<scan->num_operands; i++) {
                                if (scan->operands[i] && scan->operands[i]->kind == SIR_VAL_VREG && scan->operands[i]->as.vreg == inst->dest->as.vreg) {
                                    used_elsewhere = true; break;
                                }
                            }
                            if (used_elsewhere) break;
                        }
                        if (!used_elsewhere) can_fuse = true;
                    }

                    if (can_fuse) {
                        const char* jcc = "je";
                        const char* inv_jcc = "jne";
                        if (inst->opcode == SIR_FCMP_NE) { jcc = "jne"; inv_jcc = "je"; }
                        else if (inst->opcode == SIR_FCMP_LT) { jcc = "jb"; inv_jcc = "jae"; }
                        else if (inst->opcode == SIR_FCMP_LE) { jcc = "jbe"; inv_jcc = "ja"; }
                        else if (inst->opcode == SIR_FCMP_GT) { jcc = "ja"; inv_jcc = "jbe"; }
                        else if (inst->opcode == SIR_FCMP_GE) { jcc = "jae"; inv_jcc = "jb"; }
                        
                        if (block->next && next_inst->operands[2]->as.block == block->next) {
                            fprintf(out, "    %s .L%s_%u\n", jcc, next_inst->operands[1]->as.block->name, next_inst->operands[1]->as.block->id);
                        } else if (block->next && next_inst->operands[1]->as.block == block->next) {
                            fprintf(out, "    %s .L%s_%u\n", inv_jcc, next_inst->operands[2]->as.block->name, next_inst->operands[2]->as.block->id);
                        } else {
                            fprintf(out, "    %s .L%s_%u\n", jcc, next_inst->operands[1]->as.block->name, next_inst->operands[1]->as.block->id);
                            fprintf(out, "    jmp .L%s_%u\n", next_inst->operands[2]->as.block->name, next_inst->operands[2]->as.block->id);
                        }
                        
                        inst = next_inst; // 跳过下一个 BR 指令
                    } else {
                        fprintf(out, "    set%s %%al\n", cc);
                        fprintf(out, "    movzbq %%al, %%rax\n");
                        fprintf(out, "    movq %%rax, %s\n", dest);
                    }
                    break;
                }

                case SIR_CALL:
                    if (inst->operands[0]->kind == SIR_VAL_GLOBAL && strcmp(inst->operands[0]->as.global_name, "scribe") == 0) {
                        emit_load_operand(out, "%rax", "%eax", inst->operands[1], &allocator, total_frame_size);
                        fprintf(out, "    movq %%rax, %%rcx\n");
                                
                        PrintType pt = builtins_get_print_type(inst->operands[1]);
                                
                        if (pt == PRINT_STR) {
                            fprintf(out, "    movq 8(%%rcx), %%rdx\n");
                            fprintf(out, "    movq (%%rcx), %%rcx\n");
                            fprintf(out, "    call __print_str\n");
                        } else if (pt == PRINT_BOOL) {
                            fprintf(out, "    call __print_bool\n");
                        } else if (pt == PRINT_CHAR) {
                            fprintf(out, "    subq $24, %%rsp\n");
                            fprintf(out, "    movb %%cl, 15(%%rsp)\n");
                            fprintf(out, "    leaq 15(%%rsp), %%rcx\n");
                            fprintf(out, "    movq $1, %%rdx\n");
                            fprintf(out, "    call __print_str\n");
                            fprintf(out, "    addq $24, %%rsp\n");
                        } else if (pt == PRINT_HEX) {
                            fprintf(out, "    call __print_hex\n");
                        } else if (pt == PRINT_FLOAT) {
                            if (inst->operands[1]->type && inst->operands[1]->type->kind == TY_F32) {
                                fprintf(out, "    movd %%ecx, %%xmm0\n");
                                fprintf(out, "    cvtss2sd %%xmm0, %%xmm0\n");
                                fprintf(out, "    movq %%xmm0, %%rcx\n");
                            }
                            fprintf(out, "    call __print_float\n");
                        } else if (pt == PRINT_UINT) {
                            fprintf(out, "    call __print_uint\n");
                        } else {
                            fprintf(out, "    call __print_int\n");
                        }
                        break;
                    }
                    
                    bool is_extern = false;
                    if (inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                        for (SirExternFunc* ext = module->first_extern; ext; ext = ext->next) {
                            if (strcmp(ext->name, inst->operands[0]->as.global_name) == 0) {
                                is_extern = true; break;
                            }
                        }
                    }

                    int num_args = inst->num_operands - 1;
                    int max_reg_args = (is_extern || opt_level < 2) ? 4 : 6;
                    int reg_args = num_args > max_reg_args ? max_reg_args : num_args;
                    const char* arg_regs_ext[] = {"%rcx", "%rdx", "%r8", "%r9"};
                    const char* arg_regs32_ext[] = {"%ecx", "%edx", "%r8d", "%r9d"};
                    const char* arg_regs_int[] = {"%rcx", "%rdx", "%r8", "%r9", "%r10", "%r11"};
                    const char* arg_regs32_int[] = {"%ecx", "%edx", "%r8d", "%r9d", "%r10d", "%r11d"};
                    const char** arg_regs = is_extern ? arg_regs_ext : arg_regs_int;
                    const char** arg_regs32 = is_extern ? arg_regs32_ext : arg_regs32_int;
                    
                    bool is_tail_call = false;
                    if (opt_level > 0 && inst->next && inst->next->opcode == SIR_RET && inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                        if (inst->next->num_operands == 0 || (inst->dest && inst->next->operands[0]->kind == SIR_VAL_VREG && inst->next->operands[0]->as.vreg == inst->dest->as.vreg)) {
                            if (num_args <= max_reg_args) is_tail_call = true;
                        }
                    }

                    int stack_args = num_args > max_reg_args ? num_args - max_reg_args : 0;
                    int scratch_base = ((is_extern || opt_level < 2) ? 32 : 0) + stack_args * 8;

                    for (int i = num_args - 1; i >= max_reg_args; i--) {
                        emit_load_operand(out, "%rax", "%eax", inst->operands[i+1], &allocator, total_frame_size);
                        int offset = ((is_extern || opt_level < 2) ? 32 : 0) + (i - max_reg_args) * 8;
                        fprintf(out, "    movq %%rax, %d(%%rsp)\n", offset);
                    }
                            
                    if (reg_args == 1) {
                        bool already_in_rcx = false;
                        if (opt_level > 0 && inst->prev && (inst->prev->opcode == SIR_ADD || inst->prev->opcode == SIR_SUB || inst->prev->opcode == SIR_MUL || inst->prev->opcode == SIR_AND || inst->prev->opcode == SIR_OR || inst->prev->opcode == SIR_XOR) && inst->prev->dest == inst->operands[1]) {
                            if (inst->operands[1]->kind == SIR_VAL_VREG && allocator.use_count[inst->operands[1]->as.vreg] == 2) {
                                already_in_rcx = true;
                            }
                        }
                        if (!already_in_rcx) {
                            emit_load_operand(out, "%rcx", "%ecx", inst->operands[1], &allocator, total_frame_size);
                        }
                        bool is_float = (inst->operands[1]->type && (inst->operands[1]->type->kind == TY_F32 || inst->operands[1]->type->kind == TY_F64));
                        if (is_float) {
                            if (inst->operands[1]->type->kind == TY_F32) fprintf(out, "    movd %%ecx, %%xmm0\n");
                            else fprintf(out, "    movq %%rcx, %%xmm0\n");
                        }
                    } else if (reg_args == 2) {
                        char arg_str0[64], arg_str1[64];
                        int arg_s0 = (inst->operands[1]->type) ? type_get_size(inst->operands[1]->type) : 8;
                        int arg_s1 = (inst->operands[2]->type) ? type_get_size(inst->operands[2]->type) : 8;
                        if (arg_s0 < 4) arg_s0 = 4; if (arg_s1 < 4) arg_s1 = 4;
                        get_operand_str(arg_str0, inst->operands[1], &allocator, arg_s0, total_frame_size);
                        get_operand_str(arg_str1, inst->operands[2], &allocator, arg_s1, total_frame_size);
                        
                        if ((strcmp(arg_str0, "%rdx") == 0 || strcmp(arg_str0, "%edx") == 0) && 
                            (strcmp(arg_str1, "%rcx") == 0 || strcmp(arg_str1, "%ecx") == 0)) {
                            fprintf(out, "    xchgq %%rcx, %%rdx\n");
                        } else if (strcmp(arg_str1, "%rcx") == 0 || strcmp(arg_str1, "%ecx") == 0) {
                            emit_load_operand(out, "%rdx", "%edx", inst->operands[2], &allocator, total_frame_size);
                            emit_load_operand(out, "%rcx", "%ecx", inst->operands[1], &allocator, total_frame_size);
                        } else {
                            emit_load_operand(out, "%rcx", "%ecx", inst->operands[1], &allocator, total_frame_size);
                            emit_load_operand(out, "%rdx", "%edx", inst->operands[2], &allocator, total_frame_size);
                        }
                        
                        for (int i = 0; i < 2; i++) {
                            bool is_float = (inst->operands[i+1]->type && (inst->operands[i+1]->type->kind == TY_F32 || inst->operands[i+1]->type->kind == TY_F64));
                            if (is_float) {
                                if (inst->operands[i+1]->type->kind == TY_F32) fprintf(out, "    movd %s, %%xmm%d\n", i==0?"%ecx":"%edx", i);
                                else fprintf(out, "    movq %s, %%xmm%d\n", arg_regs[i], i);
                            }
                        }
                    } else {
                        for (int i = 0; i < reg_args; i++) {
                            emit_load_operand(out, "%rax", "%eax", inst->operands[i+1], &allocator, total_frame_size);
                            fprintf(out, "    movq %%rax, %d(%%rsp)\n", scratch_base + i * 8);
                        }
                        for (int i = 0; i < reg_args; i++) {
                            fprintf(out, "    movq %d(%%rsp), %s\n", scratch_base + i * 8, arg_regs[i]);
                            bool is_float = (inst->operands[i+1]->type && (inst->operands[i+1]->type->kind == TY_F32 || inst->operands[i+1]->type->kind == TY_F64));
                            if (is_float) {
                                if (inst->operands[i+1]->type->kind == TY_F32) fprintf(out, "    movd %s, %%xmm%d\n", arg_regs32[i], i);
                                else fprintf(out, "    movq %s, %%xmm%d\n", arg_regs[i], i);
                            }
                        }
                    }
                    
                    if (is_extern) {
                        // 针对可变参数函数 (如 printf/scribe)，清空 %al
                        fprintf(out, "    xorq %%rax, %%rax\n");
                    }
                    
                    if (is_tail_call) {
                        fprintf(out, "    # Tail Call Optimization\n");
                        if (!block->is_frameless) {
                            if (stack_sub_size > 0) fprintf(out, "    addq $%d, %%rsp\n", stack_sub_size);
                            if (allocator.used_callee_saved[6]) fprintf(out, "    popq %%r15\n");
                            if (allocator.used_callee_saved[5]) fprintf(out, "    popq %%r14\n");
                            if (allocator.used_callee_saved[4]) fprintf(out, "    popq %%r13\n");
                            if (allocator.used_callee_saved[3]) fprintf(out, "    popq %%r12\n");
                            if (allocator.used_callee_saved[2]) fprintf(out, "    popq %%rdi\n");
                            if (allocator.used_callee_saved[1]) fprintf(out, "    popq %%rsi\n");
                            if (allocator.used_callee_saved[0]) fprintf(out, "    popq %%rbx\n");
                        }
                        
                        fprintf(out, "    jmp %s\n", inst->operands[0]->as.global_name);
                        inst = inst->next; // 跳过 RET
                        break;
                    }

                    if (inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                        fprintf(out, "    call %s\n", inst->operands[0]->as.global_name);
                    } else {
                        char callee_str[64];
                        get_operand_str(callee_str, inst->operands[0], &allocator, 8, total_frame_size);
                        fprintf(out, "    call *%s\n", callee_str);
                    }
                    
                    if (inst->dest) {
                        bool ret_is_float = (inst->dest->type && (inst->dest->type->kind == TY_F32 || inst->dest->type->kind == TY_F64));
                        if (ret_is_float) {
                            fprintf(out, "    movq %%xmm0, %%rax\n");
                        }
                        bool skip_store = false;
                        if (opt_level > 0 && !ret_is_float && inst->next && (inst->next->opcode == SIR_ADD || inst->next->opcode == SIR_SUB || inst->next->opcode == SIR_MUL || inst->next->opcode == SIR_AND || inst->next->opcode == SIR_OR || inst->next->opcode == SIR_XOR)) {
                            if (inst->next->operands[0] == inst->dest || inst->next->operands[1] == inst->dest) {
                                if (allocator.use_count[inst->dest->as.vreg] == 2) {
                                    skip_store = true;
                                }
                            }
                        }
                        if (!skip_store && strcmp(dest, "%rax") != 0 && strcmp(dest, "%eax") != 0) {
                            int w = (inst->dest->type && type_get_size(inst->dest->type) <= 4) ? 0 : 1;
                            if (w) fprintf(out, "    movq %%rax, %s\n", dest);
                            else fprintf(out, "    movl %%eax, %s\n", dest);
                        }
                    }
                    break;

                case SIR_RET:
                    if (inst->num_operands > 0) {
                        bool already_in_rax = false;
                        if (opt_level > 0 && inst->prev && (inst->prev->opcode == SIR_ADD || inst->prev->opcode == SIR_SUB || inst->prev->opcode == SIR_MUL || inst->prev->opcode == SIR_AND || inst->prev->opcode == SIR_OR || inst->prev->opcode == SIR_XOR) && inst->prev->dest == inst->operands[0]) {
                            if (inst->operands[0]->kind == SIR_VAL_VREG && allocator.use_count[inst->operands[0]->as.vreg] == 2) {
                                already_in_rax = true;
                            }
                        }
                        if (!already_in_rax) {
                            emit_load_operand(out, "%rax", "%eax", inst->operands[0], &allocator, total_frame_size);
                        }
                        bool ret_is_float = (inst->operands[0]->type && (inst->operands[0]->type->kind == TY_F32 || inst->operands[0]->type->kind == TY_F64));
                        if (ret_is_float) {
                            fprintf(out, "    movq %%rax, %%xmm0\n");
                        }
                    }
                    // 5. 函数跋 (Epilogue)
                    if (!block->is_frameless) {
                        if (stack_sub_size > 0) {
                            fprintf(out, "    addq $%d, %%rsp\n", stack_sub_size);
                        }
                        
                        if (allocator.used_callee_saved[6]) fprintf(out, "    popq %%r15\n");
                        if (allocator.used_callee_saved[5]) fprintf(out, "    popq %%r14\n");
                        if (allocator.used_callee_saved[4]) fprintf(out, "    popq %%r13\n");
                        if (allocator.used_callee_saved[3]) fprintf(out, "    popq %%r12\n");
                        if (allocator.used_callee_saved[2]) fprintf(out, "    popq %%rdi\n");
                        if (allocator.used_callee_saved[1]) fprintf(out, "    popq %%rsi\n");
                        if (allocator.used_callee_saved[0]) fprintf(out, "    popq %%rbx\n");
                    }
                    
                    fprintf(out, "    ret\n");
                    break;

                default:
                    fprintf(out, "    # TODO: codex operationis ignotus %d\n", inst->opcode);
                    break;
            }
        }
    }

    if (func->has_fast_path && opt_level > 0) {
        const char* cx = func->fp_w ? "%rcx" : "%ecx";
        const char* ax = func->fp_w ? "%rax" : "%eax";
        const char* mov_op = func->fp_w ? "movq" : "movl";
        fprintf(out, ".Lfast_%s:\n", func->name);
        fprintf(out, "    %s %s, %s\n", mov_op, cx, ax);
        fprintf(out, "    ret\n");
    }

    free(alloca_offsets);
    reg_alloc_free(&allocator);
    fprintf(out, "\n");
}

void asm_x86_64_generate(FILE* out, SirModule* module, int opt_level) {
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
        generate_function(out, func, module, opt_level);
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
    
    flush_peep(out, peep_count);
}
#undef fprintf
