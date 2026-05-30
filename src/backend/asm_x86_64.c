#include "asm_x86_64.h"
#include "x86_mir.h"
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

    if (line[0] != ' ' || line[1] != ' ' || line[2] != ' ' || line[3] != ' ') {
        flush_peep(out, peep_count);
        fputs(line, out);
        return ret;
    }

    if (peep_count >= 4) flush_peep(out, 1);
    
    // 消除 MSVC C6385 警告：强制让静态分析器知道索引在安全范围内
    if (peep_count >= 4) return ret;

    strcpy(peep_window[peep_count++], line);

    char reg[32] = {0};
    if (sscanf(peep_window[peep_count-1], "    movq $0, %%%s\n", reg) == 1 ||
        sscanf(peep_window[peep_count-1], "    movl $0, %%%s\n", reg) == 1) {
        sprintf(peep_window[peep_count-1], "    xorq %%%s, %%%s\n", reg, reg);
    }

    if (peep_count >= 2) {
        char r1[32] = {0}, r2[32] = {0}, r3[32] = {0}, r4[32] = {0};
        if (sscanf(peep_window[peep_count-2], "    movq %%%[^, \n], %%%s\n", r1, r2) == 2 &&
            sscanf(peep_window[peep_count-1], "    movq %%%[^, \n], %%%s\n", r3, r4) == 2) {
            if (strcmp(r1, r4) == 0 && strcmp(r2, r3) == 0) {
                peep_count--;
            }
            else if (strcmp(r2, r3) == 0 && strcmp(r1, r4) != 0) {
                sprintf(peep_window[peep_count-1], "    movq %%%s, %%%s\n", r1, r4);
            }
        }
    }

    return ret;
}
#define fprintf my_fprintf

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

static const char* get_reg_name(X86Reg reg, int size) {
    if (reg == X86_REG_RAX) return size == 1 ? "%al" : size == 2 ? "%ax" : size == 4 ? "%eax" : "%rax";
    if (reg == X86_REG_RCX) return size == 1 ? "%cl" : size == 2 ? "%cx" : size == 4 ? "%ecx" : "%rcx";
    if (reg == X86_REG_RDX) return size == 1 ? "%dl" : size == 2 ? "%dx" : size == 4 ? "%edx" : "%rdx";
    if (reg == X86_REG_RSP) return "%rsp";
    if (reg == X86_REG_RBP) return "%rbp";
    if (reg >= X86_REG_XMM0 && reg <= X86_REG_XMM7) {
        static char buf[8];
        sprintf(buf, "%%xmm%d", reg - X86_REG_XMM0);
        return buf;
    }
    
    int color = -1;
    if (reg == X86_REG_RBX) color = 0;
    else if (reg == X86_REG_RSI) color = 1;
    else if (reg == X86_REG_RDI) color = 2;
    else if (reg == X86_REG_R12) color = 3;
    else if (reg == X86_REG_R13) color = 4;
    else if (reg == X86_REG_R14) color = 5;
    else if (reg == X86_REG_R15) color = 6;
    else if (reg == X86_REG_R8) color = 7;
    else if (reg == X86_REG_R9) color = 8;
    else if (reg == X86_REG_R10) color = 9;
    else if (reg == X86_REG_R11) color = 10;
    
    if (color != -1) {
        if (size == 1) return phys_regs8[color];
        if (size == 2) return phys_regs16[color];
        if (size == 4) return phys_regs32[color];
        return phys_regs64[color];
    }
    return "???";
}

static void print_operand(char* buf, X86Operand* op) {
    switch (op->kind) {
        case X86_OP_REG:
            strcpy(buf, get_reg_name(op->as.reg, op->size));
            break;
        case X86_OP_IMM:
            sprintf(buf, "$%lld", (long long)op->as.imm);
            break;
        case X86_OP_MEM_BASE_DISP:
            if (op->as.mem_bd.disp == 0) sprintf(buf, "(%s)", get_reg_name(op->as.mem_bd.base, 8));
            else sprintf(buf, "%d(%s)", op->as.mem_bd.disp, get_reg_name(op->as.mem_bd.base, 8));
            break;
        case X86_OP_MEM_SIB:
            if (op->as.mem_sib.disp == 0) 
                sprintf(buf, "(%s, %s, %d)", get_reg_name(op->as.mem_sib.base, 8), get_reg_name(op->as.mem_sib.index, 8), op->as.mem_sib.scale);
            else 
                sprintf(buf, "%d(%s, %s, %d)", op->as.mem_sib.disp, get_reg_name(op->as.mem_sib.base, 8), get_reg_name(op->as.mem_sib.index, 8), op->as.mem_sib.scale);
            break;
        case X86_OP_MEM_RIP:
            sprintf(buf, "%s(%%rip)", op->as.label);
            break;
        case X86_OP_LABEL:
            sprintf(buf, "%s", op->as.label);
            break;
        case X86_OP_BLOCK:
            sprintf(buf, ".Lblock_%u", op->as.block_id);
            break;
        case X86_OP_STRING:
            sprintf(buf, ".Lstr%d(%%rip)", get_string_id(op->as.string.str, op->as.string.len));
            break;
        default:
            strcpy(buf, "");
    }
}

static const char* get_opcode_name(X86Opcode opc, int size) {
    switch (opc) {
        case X86_INST_MOV: return size == 1 ? "movb" : size == 2 ? "movw" : size == 4 ? "movl" : "movq";
        case X86_INST_MOVSX: return size == 4 ? "movslq" : size == 2 ? "movswq" : "movsbq";
        case X86_INST_MOVZX: return size == 2 ? "movzwq" : "movzbq";
        case X86_INST_LEA: return "leaq";
        case X86_INST_ADD: return size == 1 ? "addb" : size == 2 ? "addw" : size == 4 ? "addl" : "addq";
        case X86_INST_SUB: return size == 1 ? "subb" : size == 2 ? "subw" : size == 4 ? "subl" : "subq";
        case X86_INST_IMUL: return size == 4 ? "imull" : "imulq";
        case X86_INST_IDIV: return size == 4 ? "idivl" : "idivq";
        case X86_INST_DIV: return size == 4 ? "divl" : "divq";
        case X86_INST_AND: return size == 1 ? "andb" : size == 2 ? "andw" : size == 4 ? "andl" : "andq";
        case X86_INST_OR: return size == 1 ? "orb" : size == 2 ? "orw" : size == 4 ? "orl" : "orq";
        case X86_INST_XOR: return size == 1 ? "xorb" : size == 2 ? "xorw" : size == 4 ? "xorl" : "xorq";
        case X86_INST_SHL: return size == 1 ? "shlb" : size == 2 ? "shlw" : size == 4 ? "shll" : "shlq";
        case X86_INST_SHR: return size == 1 ? "shrb" : size == 2 ? "shrw" : size == 4 ? "shrl" : "shrq";
        case X86_INST_SAR: return size == 1 ? "sarb" : size == 2 ? "sarw" : size == 4 ? "sarl" : "sarq";
        case X86_INST_NEG: return size == 1 ? "negb" : size == 2 ? "negw" : size == 4 ? "negl" : "negq";
        case X86_INST_NOT: return size == 1 ? "notb" : size == 2 ? "notw" : size == 4 ? "notl" : "notq";
        case X86_INST_INC: return size == 1 ? "incb" : size == 2 ? "incw" : size == 4 ? "incl" : "incq";
        case X86_INST_DEC: return size == 1 ? "decb" : size == 2 ? "decw" : size == 4 ? "decl" : "decq";
        case X86_INST_CMP: return size == 1 ? "cmpb" : size == 2 ? "cmpw" : size == 4 ? "cmpl" : "cmpq";
        case X86_INST_TEST: return size == 1 ? "testb" : size == 2 ? "testw" : size == 4 ? "testl" : "testq";
        case X86_INST_JMP: return "jmp";
        case X86_INST_CALL: return "call";
        case X86_INST_RET: return "ret";
        case X86_INST_PUSH: return "pushq";
        case X86_INST_POP: return "popq";
        case X86_INST_CQO: return "cqo";
        case X86_INST_CDQ: return "cdq";
        case X86_INST_XCHG: return size == 4 ? "xchgl" : "xchgq";
        case X86_INST_MOVD: return "movd";
        case X86_INST_MOVQ: return "movq";
        case X86_INST_MOVSS: return "movss";
        case X86_INST_MOVSD: return "movsd";
        case X86_INST_CVTSI2SS: return "cvtsi2ss";
        case X86_INST_CVTSI2SD: return "cvtsi2sd";
        case X86_INST_CVTTSS2SI: return "cvttss2si";
        case X86_INST_CVTTSD2SI: return "cvttsd2si";
        case X86_INST_CVTSS2SD: return "cvtss2sd";
        case X86_INST_CVTSD2SS: return "cvtsd2ss";
        case X86_INST_ADDSS: return "addss";
        case X86_INST_ADDSD: return "addsd";
        case X86_INST_SUBSS: return "subss";
        case X86_INST_SUBSD: return "subsd";
        case X86_INST_MULSS: return "mulss";
        case X86_INST_MULSD: return "mulsd";
        case X86_INST_DIVSS: return "divss";
        case X86_INST_DIVSD: return "divsd";
        case X86_INST_UCOMISS: return "ucomiss";
        case X86_INST_UCOMISD: return "ucomisd";
        case X86_INST_REP_MOVSB: return "rep movsb";
        case X86_INST_CLD: return "cld";
        case X86_INST_UD2: return "ud2";
        default: return "???";
    }
}

static const char* get_cond_name(X86Condition cond) {
    switch (cond) {
        case X86_COND_E: return "e";
        case X86_COND_NE: return "ne";
        case X86_COND_L: return "l";
        case X86_COND_LE: return "le";
        case X86_COND_G: return "g";
        case X86_COND_GE: return "ge";
        case X86_COND_B: return "b";
        case X86_COND_BE: return "be";
        case X86_COND_A: return "a";
        case X86_COND_AE: return "ae";
        default: return "";
    }
}

static void generate_x86_function(FILE* out, X86Function* func) {
    fprintf(out, "    .p2align 4\n");
    fprintf(out, "    .globl %s\n", func->name);
    fprintf(out, "    .type %s, @function\n", func->name);
    fprintf(out, "%s:\n", func->name);

    for (X86Block* block = func->first_block; block; block = block->next) {
        if (block != func->first_block) {
            fprintf(out, "    .p2align 4\n");
        }
        fprintf(out, ".Lblock_%u:\n", block->id);

        for (X86Inst* inst = block->first_inst; inst; inst = inst->next) {
            char op0[64] = {0}, op1[64] = {0}, op2[64] = {0};
            if (inst->num_ops > 0) print_operand(op0, &inst->ops[0]);
            if (inst->num_ops > 1) print_operand(op1, &inst->ops[1]);
            if (inst->num_ops > 2) print_operand(op2, &inst->ops[2]);

            if (inst->opcode == X86_INST_JCC) {
                fprintf(out, "    j%s %s\n", get_cond_name(inst->cond), op0);
            } else if (inst->opcode == X86_INST_SETCC) {
                fprintf(out, "    set%s %s\n", get_cond_name(inst->cond), op0);
            } else if (inst->opcode == X86_INST_CMOVCC) {
                int size = inst->ops[0].size;
                fprintf(out, "    cmov%s%s %s, %s\n", get_cond_name(inst->cond), size == 4 ? "l" : "q", op1, op0);
            } else {
                const char* opc_name = get_opcode_name(inst->opcode, inst->num_ops > 0 ? inst->ops[0].size : 8);
                if (inst->num_ops == 0) {
                    fprintf(out, "    %s\n", opc_name);
                } else if (inst->num_ops == 1) {
                    if (inst->opcode == X86_INST_CALL && (inst->ops[0].kind == X86_OP_REG || inst->ops[0].kind == X86_OP_MEM_RIP)) {
                        fprintf(out, "    call *%s\n", op0);
                    } else {
                        fprintf(out, "    %s %s\n", opc_name, op0);
                    }
                } else if (inst->num_ops == 2) {
                    fprintf(out, "    %s %s, %s\n", opc_name, op1, op0);
                }
            }
        }
    }
    fprintf(out, "\n");
}

void asm_x86_64_generate(FILE* out, SirModule* module, int opt_level) {
    if (!module) return;

    g_string_count = 0;

    if (module->first_global) {
        fprintf(out, "    .data\n");
        for (SirGlobalVar* g = module->first_global; g; g = g->next) {
            fprintf(out, "    .globl %s\n", g->name);
            fprintf(out, "    .align 8\n");
            fprintf(out, "%s:\n", g->name);
            if (g->init_data) {
                for (int i = 0; i < g->size; i++) {
                    if (i % 16 == 0) {
                        if (i > 0) fprintf(out, "\n");
                        fprintf(out, "    .byte ");
                    } else {
                        fprintf(out, ", ");
                    }
                    fprintf(out, "%d", g->init_data[i]);
                }
                fprintf(out, "\n");
            } else {
                fprintf(out, "    .zero %d\n", g->size);
            }
        }
        fprintf(out, "\n");
    }

    // 预扫描字符串
    for (SirFunction* func = module->first_func; func; func = func->next) {
        for (SirBlock* block = func->first_block; block; block = block->next) {
            for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                for (int i = 0; i < inst->num_operands; i++) {
                    if (inst->operands[i] && inst->operands[i]->kind == SIR_VAL_CONST_STRING) {
                        get_string_id(inst->operands[i]->as.string_val.str, inst->operands[i]->as.string_val.len);
                    }
                }
            }
        }
    }

    fprintf(out, "    .text\n\n");

    X86Module* mir = x86_mir_build(module, opt_level);
    for (X86Function* func = mir->first_func; func; func = func->next) {
        generate_x86_function(out, func);
    }
    x86_mir_free(mir);

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
