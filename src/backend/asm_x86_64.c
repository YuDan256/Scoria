#include "asm_x86_64.h"
#include "reg_alloc.h"
#include <stdlib.h>
#include <string.h>

// 物理寄存器映射表 (对应 reg_alloc.h 中的 NUM_PHYS_REGS = 6)
static const char* phys_regs64[] = {"%rbx", "%r10", "%r11", "%r12", "%r13", "%r14"};
static const char* phys_regs32[] = {"%ebx", "%r10d", "%r11d", "%r12d", "%r13d", "%r14d"};
static const char* phys_regs8[]  = {"%bl", "%r10b", "%r11b", "%r12b", "%r13b", "%r14b"};

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
        case SIR_VAL_CONST_BOOL:
            sprintf(buf, "$%d", val->as.bool_val ? 1 : 0);
            break;
        case SIR_VAL_GLOBAL:
            sprintf(buf, "%s(%%rip)", val->as.global_name);
            break;
        case SIR_VAL_CONST_STRING:
            sprintf(buf, "$.Lstr"); // 仅用于调试输出
            break;
        case SIR_VAL_VREG: {
            int color = reg_alloc_get_color(alloc, val->as.vreg);
            if (color != -1) {
                // 分配到了物理寄存器
                if (size == 1) strcpy(buf, phys_regs8[color]);
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

    // 2. 扫描函数，找到最大的虚拟寄存器 ID 和 ALLOCA 空间
    uint32_t max_vreg = 0;
    int local_stack_size = 0;
    int* alloca_offsets = calloc(10000, sizeof(int));

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
            if (inst->opcode == SIR_ALLOCA) {
                local_stack_size += 8;
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
    
    int stack_size = allocator.current_offset + 32; // 预留 32 字节 Shadow Space (Windows ABI)
    // 保持 16 字节对齐
    if (stack_size % 16 != 0) stack_size += 16 - (stack_size % 16);
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

                case SIR_STORE:
                    // store val(op0), ptr(op1)
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    movq %s, %%rcx\n", op1);
                    fprintf(out, "    movq %%rax, (%%rcx)\n");
                    break;

                case SIR_LOAD:
                    // dest = load ptr(op0)
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    movq (%%rax), %%rcx\n");
                    fprintf(out, "    movq %%rcx, %s\n", dest);
                    break;

                case SIR_CAST:
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;

                case SIR_ADD:
                    // dest = op0 + op1
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    addq %s, %%rax\n", op1);
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;

                case SIR_SUB:
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    subq %s, %%rax\n", op1);
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;

                case SIR_MUL:
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    imulq %s, %%rax\n", op1);
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;

                case SIR_JMP:
                    fprintf(out, "    jmp .L%s_%u\n", inst->operands[0]->as.block->name, inst->operands[0]->as.block->id);
                    break;

                case SIR_BR:
                    // br cond(op0), true_block(op1), false_block(op2)
                    fprintf(out, "    cmpq $0, %s\n", op0);
                    fprintf(out, "    jne .L%s_%u\n", inst->operands[1]->as.block->name, inst->operands[1]->as.block->id);
                    fprintf(out, "    jmp .L%s_%u\n", inst->operands[2]->as.block->name, inst->operands[2]->as.block->id);
                    break;

                case SIR_GET_PARAM:
                    fprintf(out, "    # get_param %d\n", (int)inst->operands[0]->as.int_val);
                    break;

                case SIR_GEP:
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    if (inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                        if (inst->operands[1]->as.int_val != 0) {
                            fprintf(out, "    addq $%lld, %%rax\n", (long long)(inst->operands[1]->as.int_val * 8));
                        }
                    } else {
                        fprintf(out, "    movq %s, %%rcx\n", op1);
                        fprintf(out, "    shlq $3, %%rcx\n");
                        fprintf(out, "    addq %%rcx, %%rax\n");
                    }
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;

                case SIR_DIV:
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    cqo\n");
                    fprintf(out, "    idivq %s\n", op1);
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;

                case SIR_MOD:
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    cqo\n");
                    fprintf(out, "    idivq %s\n", op1);
                    fprintf(out, "    movq %%rdx, %s\n", dest);
                    break;

                case SIR_AND:
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    andq %s, %%rax\n", op1);
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;

                case SIR_OR:
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    orq %s, %%rax\n", op1);
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;

                case SIR_XOR:
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    xorq %s, %%rax\n", op1);
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;

                case SIR_SHL:
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    movq %s, %%rcx\n", op1);
                    fprintf(out, "    shlq %%cl, %%rax\n");
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;

                case SIR_SHR:
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    movq %s, %%rcx\n", op1);
                    fprintf(out, "    shrq %%cl, %%rax\n");
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;

                case SIR_ICMP_EQ:
                case SIR_ICMP_NE:
                case SIR_ICMP_LT:
                case SIR_ICMP_LE:
                case SIR_ICMP_GT:
                case SIR_ICMP_GE: {
                    const char* cc = "e";
                    if (inst->opcode == SIR_ICMP_NE) cc = "ne";
                    else if (inst->opcode == SIR_ICMP_LT) cc = "l";
                    else if (inst->opcode == SIR_ICMP_LE) cc = "le";
                    else if (inst->opcode == SIR_ICMP_GT) cc = "g";
                    else if (inst->opcode == SIR_ICMP_GE) cc = "ge";
                    
                    fprintf(out, "    movq %s, %%rax\n", op0);
                    fprintf(out, "    cmpq %s, %%rax\n", op1);
                    fprintf(out, "    set%s %%al\n", cc);
                    fprintf(out, "    movzbq %%al, %%rax\n");
                    fprintf(out, "    movq %%rax, %s\n", dest);
                    break;
                }

                case SIR_CALL:
                    if (inst->operands[0]->kind == SIR_VAL_GLOBAL && strcmp(inst->operands[0]->as.global_name, "scribe") == 0) {
                        char arg_str[64];
                        get_operand_str(arg_str, inst->operands[1], &allocator, 8);
                        fprintf(out, "    movq %s, %%rcx\n", arg_str);
                        bool is_str = (inst->operands[1]->type && inst->operands[1]->type->kind == TY_TEXTUS) || (inst->operands[1]->kind == SIR_VAL_CONST_STRING);
                        if (is_str) {
                            int str_len = 0;
                            if (inst->operands[1]->kind == SIR_VAL_CONST_STRING) {
                                str_len = (int)strlen(inst->operands[1]->as.string_val);
                            }
                            fprintf(out, "    movq $%d, %%rdx\n", str_len);
                            fprintf(out, "    call __print_str\n");
                        } else {
                            fprintf(out, "    call __print_int\n");
                        }
                        break;
                    }
                    
                    // Windows x64 ABI: rcx, rdx, r8, r9, 然后是栈
                    for (int i = 0; i < inst->num_operands - 1; i++) {
                        char arg_str[64];
                        get_operand_str(arg_str, inst->operands[i+1], &allocator, 8);
                        if (i < 4) {
                            const char* arg_regs[] = {"%rcx", "%rdx", "%r8", "%r9"};
                            fprintf(out, "    movq %s, %s\n", arg_str, arg_regs[i]);
                        } else {
                            fprintf(out, "    movq %s, %%rax\n", arg_str);
                            fprintf(out, "    movq %%rax, %d(%%rsp)\n", 32 + (i - 4) * 8);
                        }
                    }
                    // 针对可变参数函数 (如 printf/scribe)，清空 %al
                    fprintf(out, "    xorq %%rax, %%rax\n");
                    fprintf(out, "    call %s\n", inst->operands[0]->as.global_name);
                    if (inst->dest) {
                        fprintf(out, "    movq %%rax, %s\n", dest);
                    }
                    break;

                case SIR_RET:
                    if (inst->num_operands > 0) {
                        fprintf(out, "    movq %s, %%rax\n", op0);
                    }
                    // 5. 函数跋 (Epilogue)
                    fprintf(out, "    movq %%rbp, %%rsp\n");
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

    // 汇编文件头部
    fprintf(out, "    .text\n\n");

    // 遍历生成所有函数
    for (SirFunction* func = module->first_func; func; func = func->next) {
        generate_function(out, func);
    }

    // 追加内置汇编例程
    fprintf(out, "__print_str:\n");
    fprintf(out, "    subq $72, %%rsp\n");
    fprintf(out, "    movq %%rcx, 48(%%rsp)\n");
    fprintf(out, "    movq %%rdx, 56(%%rsp)\n");
    fprintf(out, "    movl $-11, %%ecx\n");
    fprintf(out, "    call GetStdHandle\n");
    fprintf(out, "    movq %%rax, %%rcx\n");
    fprintf(out, "    movq 48(%%rsp), %%rdx\n");
    fprintf(out, "    movq 56(%%rsp), %%r8\n");
    fprintf(out, "    leaq 40(%%rsp), %%r9\n");
    fprintf(out, "    movq $0, 32(%%rsp)\n");
    fprintf(out, "    call WriteFile\n");
    fprintf(out, "    addq $72, %%rsp\n");
    fprintf(out, "    ret\n\n");

    fprintf(out, "__print_int:\n");
    fprintf(out, "    subq $104, %%rsp\n");
    fprintf(out, "    movq %%rcx, %%r10\n");
    fprintf(out, "    xorq %%r11, %%r11\n");
    fprintf(out, "    testq %%rcx, %%rcx\n");
    fprintf(out, "    jns .Lpos\n");
    fprintf(out, "    negq %%r10\n");
    fprintf(out, "    movq $1, %%r11\n");
    fprintf(out, ".Lpos:\n");
    fprintf(out, "    leaq 79(%%rsp), %%r8\n");
    fprintf(out, "    movb $10, (%%r8)\n");
    fprintf(out, "    decq %%r8\n");
    fprintf(out, "    movq %%r10, %%rax\n");
    fprintf(out, "    movq $10, %%r9\n");
    fprintf(out, ".Lloop:\n");
    fprintf(out, "    xorq %%rdx, %%rdx\n");
    fprintf(out, "    divq %%r9\n");
    fprintf(out, "    addb $'0', %%dl\n");
    fprintf(out, "    movb %%dl, (%%r8)\n");
    fprintf(out, "    decq %%r8\n");
    fprintf(out, "    testq %%rax, %%rax\n");
    fprintf(out, "    jnz .Lloop\n");
    fprintf(out, "    testq %%r11, %%r11\n");
    fprintf(out, "    jz .Ldone\n");
    fprintf(out, "    movb $'-', (%%r8)\n");
    fprintf(out, "    decq %%r8\n");
    fprintf(out, ".Ldone:\n");
    fprintf(out, "    incq %%r8\n");
    fprintf(out, "    leaq 80(%%rsp), %%r10\n");
    fprintf(out, "    subq %%r8, %%r10\n");
    fprintf(out, "    movq %%r8, 88(%%rsp)\n");
    fprintf(out, "    movq %%r10, 96(%%rsp)\n");
    fprintf(out, "    movl $-11, %%ecx\n");
    fprintf(out, "    call GetStdHandle\n");
    fprintf(out, "    movq %%rax, %%rcx\n");
    fprintf(out, "    movq 88(%%rsp), %%rdx\n");
    fprintf(out, "    movq 96(%%rsp), %%r8\n");
    fprintf(out, "    leaq 40(%%rsp), %%r9\n");
    fprintf(out, "    movq $0, 32(%%rsp)\n");
    fprintf(out, "    call WriteFile\n");
    fprintf(out, "    addq $104, %%rsp\n");
    fprintf(out, "    ret\n");
}
