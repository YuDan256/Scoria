#include "builtins.h"
#include <string.h>

bool g_use_print_str = false;
bool g_use_print_int = false;
bool g_use_print_uint = false;
bool g_use_print_char = false;
bool g_use_print_float = false;
bool g_use_print_bool = false;
bool g_use_print_hex = false;
bool g_use_crea = false;
bool g_use_neca = false;
bool g_use_lege = false;

PrintType builtins_get_print_type(SirValue* arg) {
    ScoriaType* arg_type = arg->type;
    bool is_via = (arg_type && arg_type->kind == TY_VIA);
    ScoriaType* inner_type = is_via ? arg_type->as.inner : arg_type;
    
    bool is_str = (inner_type && inner_type->kind == TY_COHORS && inner_type->as.inner->kind == TY_LITTERA);
    bool is_bool = (inner_type && inner_type->kind == TY_LOGICA) || (arg->kind == SIR_VAL_CONST_BOOL);
    bool is_char = (inner_type && inner_type->kind == TY_LITTERA);
    bool is_float = (inner_type && (inner_type->kind == TY_F32 || inner_type->kind == TY_F64)) || (arg->kind == SIR_VAL_CONST_FLOAT);
    bool is_uint = (inner_type && (inner_type->kind == TY_P8 || inner_type->kind == TY_P16 || inner_type->kind == TY_P32 || inner_type->kind == TY_P64));
    bool is_ptr = is_via && !is_str;

    if (is_str) return PRINT_STR;
    if (is_bool) return PRINT_BOOL;
    if (is_char) return PRINT_CHAR;
    if (is_ptr) return PRINT_HEX;
    if (is_float) return PRINT_FLOAT;
    if (is_uint) return PRINT_UINT;
    return PRINT_INT;
}

void builtins_analyze_usage(SirModule* module) {
    g_use_print_str = false;
    g_use_print_int = false;
    g_use_print_uint = false;
    g_use_print_char = false;
    g_use_print_float = false;
    g_use_print_bool = false;
    g_use_print_hex = false;
    g_use_crea = false;
    g_use_neca = false;

    for (SirFunction* func = module->first_func; func; func = func->next) {
        for (SirBlock* block = func->first_block; block; block = block->next) {
            for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                if (inst->opcode == SIR_CALL && inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                    const char* callee = inst->operands[0]->as.global_name;
                    if (strcmp(callee, "scribe") == 0) {
                        PrintType pt = builtins_get_print_type(inst->operands[1]);
                        if (pt == PRINT_STR) g_use_print_str = true;
                        else if (pt == PRINT_BOOL) g_use_print_bool = true;
                        else if (pt == PRINT_CHAR) g_use_print_char = true;
                        else if (pt == PRINT_HEX) g_use_print_hex = true;
                        else if (pt == PRINT_FLOAT) g_use_print_float = true;
                        else if (pt == PRINT_UINT) g_use_print_uint = true;
                        else g_use_print_int = true;
                    } else if (strcmp(callee, "crea") == 0) {
                        g_use_crea = true;
                    } else if (strcmp(callee, "neca") == 0) {
                        g_use_neca = true;
                    } else if (strcmp(callee, "lege") == 0) {
                        g_use_lege = true;
                    }
                }
            }
        }
    }

    // 解决内部依赖
    if (g_use_print_float) { g_use_print_int = true; g_use_print_str = true; }
    if (g_use_print_bool) { g_use_print_str = true; }
    if (g_use_print_hex) { g_use_print_str = true; }
    if (g_use_print_char) { g_use_print_str = true; }
}

uint32_t g_print_str_offset = 0;
uint32_t g_print_int_offset = 0;
uint32_t g_print_uint_offset = 0;
uint32_t g_print_float_offset = 0;
uint32_t g_print_hex_offset = 0;
uint32_t g_print_bool_offset = 0;
uint32_t g_crea_offset = 0;
uint32_t g_neca_offset = 0;

uint32_t g_call_getstdhandle_reloc = 0;
uint32_t g_call_writeconsolea_reloc = 0;
uint32_t g_call_getstdhandle_reloc2 = 0;
uint32_t g_call_writeconsolea_reloc2 = 0;
uint32_t g_call_getstdhandle_reloc3 = 0;
uint32_t g_call_writeconsolea_reloc3 = 0;
uint32_t g_call_exitprocess_reloc = 0;

uint32_t g_call_getprocessheap_reloc1 = 0;
uint32_t g_call_getprocessheap_reloc2 = 0;
uint32_t g_call_heapalloc_reloc = 0;
uint32_t g_call_heapfree_reloc = 0;

uint32_t g_read_char_offset = 0;
uint32_t g_lege_int_offset = 0;
uint32_t g_lege_float_offset = 0;
uint32_t g_lege_char_offset = 0;
uint32_t g_lege_bool_offset = 0;

uint32_t g_call_getstdhandle_reloc_read = 0;
uint32_t g_call_readfile_reloc = 0;

void asm_builtins_generate(FILE* out) {
    if (g_use_print_str) {
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
    }

    if (g_use_print_uint) {
    fprintf(out, "__print_uint:\n");
    fprintf(out, "    subq $104, %%rsp\n");
    fprintf(out, "    movq %%rcx, %%rax\n");
    fprintf(out, "    leaq 80(%%rsp), %%r8\n");
    fprintf(out, "    decq %%r8\n");
    fprintf(out, "    movq $10, %%r9\n");
    fprintf(out, ".Luloop:\n");
    fprintf(out, "    xorq %%rdx, %%rdx\n");
    fprintf(out, "    divq %%r9\n");
    fprintf(out, "    addb $'0', %%dl\n");
    fprintf(out, "    movb %%dl, (%%r8)\n");
    fprintf(out, "    decq %%r8\n");
    fprintf(out, "    testq %%rax, %%rax\n");
    fprintf(out, "    jnz .Luloop\n");
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
    fprintf(out, "    ret\n\n");
    }

    if (g_use_print_int) {
    fprintf(out, "__print_int:\n");
    fprintf(out, "    subq $104, %%rsp\n");
    fprintf(out, "    movq %%rcx, %%r10\n");
    fprintf(out, "    xorq %%r11, %%r11\n");
    fprintf(out, "    testq %%rcx, %%rcx\n");
    fprintf(out, "    jns .Lpos\n");
    fprintf(out, "    negq %%r10\n");
    fprintf(out, "    movq $1, %%r11\n");
    fprintf(out, ".Lpos:\n");
    fprintf(out, "    leaq 80(%%rsp), %%r8\n");
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
    fprintf(out, "    ret\n\n");
    }

    if (g_use_print_float) {
    fprintf(out, "__print_float:\n");
    fprintf(out, "    pushq %%rbp\n");
    fprintf(out, "    movq %%rsp, %%rbp\n");
    fprintf(out, "    subq $64, %%rsp\n");
    fprintf(out, "    movq %%rcx, -8(%%rbp)\n");
    fprintf(out, "    movq %%rcx, %%rax\n");
    fprintf(out, "    testq %%rax, %%rax\n");
    fprintf(out, "    jns .Lpf_pos\n");
    fprintf(out, "    leaq .Lstr_minus(%%rip), %%rcx\n");
    fprintf(out, "    movq $1, %%rdx\n");
    fprintf(out, "    call __print_str\n");
    fprintf(out, "    movq -8(%%rbp), %%rax\n");
    fprintf(out, "    btrq $63, %%rax\n");
    fprintf(out, "    movq %%rax, -8(%%rbp)\n");
    fprintf(out, ".Lpf_pos:\n");
    fprintf(out, "    movq -8(%%rbp), %%xmm0\n");
    fprintf(out, "    cvttsd2si %%xmm0, %%rcx\n");
    fprintf(out, "    movq %%rcx, -16(%%rbp)\n");
    fprintf(out, "    call __print_int\n");
    fprintf(out, "    leaq .Lstr_dot(%%rip), %%rcx\n");
    fprintf(out, "    movq $1, %%rdx\n");
    fprintf(out, "    call __print_str\n");
    fprintf(out, "    movq -8(%%rbp), %%xmm0\n");
    fprintf(out, "    cvtsi2sdq -16(%%rbp), %%xmm1\n");
    fprintf(out, "    subsd %%xmm1, %%xmm0\n");
    fprintf(out, "    movl $6, -20(%%rbp)\n");
    fprintf(out, ".Lpf_loop:\n");
    fprintf(out, "    leaq .Lfloat_10(%%rip), %%rax\n");
    fprintf(out, "    movsd (%%rax), %%xmm2\n");
    fprintf(out, "    mulsd %%xmm2, %%xmm0\n");
    fprintf(out, "    cvttsd2si %%xmm0, %%rcx\n");
    fprintf(out, "    movsd %%xmm0, -32(%%rbp)\n");
    fprintf(out, "    call __print_int\n");
    fprintf(out, "    movsd -32(%%rbp), %%xmm0\n");
    fprintf(out, "    cvttsd2si %%xmm0, %%rcx\n");
    fprintf(out, "    cvtsi2sd %%rcx, %%xmm1\n");
    fprintf(out, "    subsd %%xmm1, %%xmm0\n");
    fprintf(out, "    decl -20(%%rbp)\n");
    fprintf(out, "    jnz .Lpf_loop\n");
    fprintf(out, "    movq %%rbp, %%rsp\n");
    fprintf(out, "    popq %%rbp\n");
    fprintf(out, "    ret\n\n");
    }

    if (g_use_print_bool) {
    fprintf(out, "__print_bool:\n");
    fprintf(out, "    testq %%rcx, %%rcx\n");
    fprintf(out, "    jz .Lbool_false\n");
    fprintf(out, "    leaq .Lstr_verum(%%rip), %%rcx\n");
    fprintf(out, "    movq $5, %%rdx\n");
    fprintf(out, "    jmp __print_str\n");
    fprintf(out, ".Lbool_false:\n");
    fprintf(out, "    leaq .Lstr_falsum(%%rip), %%rcx\n");
    fprintf(out, "    movq $6, %%rdx\n");
    fprintf(out, "    jmp __print_str\n\n");
    }

    if (g_use_print_hex) {
    fprintf(out, "__print_hex:\n");
    fprintf(out, "    subq $56, %%rsp\n");
    fprintf(out, "    movq %%rcx, %%r10\n");
    fprintf(out, "    leaq 31(%%rsp), %%r8\n");
    fprintf(out, "    movb $'0', (%%r8)\n");
    fprintf(out, "    incq %%r8\n");
    fprintf(out, "    movb $'x', (%%r8)\n");
    fprintf(out, "    leaq 48(%%rsp), %%r8\n");
    fprintf(out, "    movq $16, %%r9\n");
    fprintf(out, ".Lhex_loop:\n");
    fprintf(out, "    movq %%r10, %%rax\n");
    fprintf(out, "    andq $15, %%rax\n");
    fprintf(out, "    cmpb $9, %%al\n");
    fprintf(out, "    jbe .Lhex_digit\n");
    fprintf(out, "    addb $87, %%al\n");
    fprintf(out, "    jmp .Lhex_store\n");
    fprintf(out, ".Lhex_digit:\n");
    fprintf(out, "    addb $'0', %%al\n");
    fprintf(out, ".Lhex_store:\n");
    fprintf(out, "    movb %%al, (%%r8)\n");
    fprintf(out, "    decq %%r8\n");
    fprintf(out, "    shrq $4, %%r10\n");
    fprintf(out, "    decq %%r9\n");
    fprintf(out, "    jnz .Lhex_loop\n");
    fprintf(out, "    leaq 31(%%rsp), %%rcx\n");
    fprintf(out, "    movq $18, %%rdx\n");
    fprintf(out, "    call __print_str\n");
    fprintf(out, "    addq $56, %%rsp\n");
    fprintf(out, "    ret\n\n");
    }

    if (g_use_crea) {
    fprintf(out, "    .globl crea\n");
    fprintf(out, "crea:\n");
    fprintf(out, "    pushq %%rbx\n");
    fprintf(out, "    subq $32, %%rsp\n");
    fprintf(out, "    movq %%rcx, %%rbx\n");
    fprintf(out, "    call GetProcessHeap\n");
    fprintf(out, "    movq %%rbx, %%r8\n");
    fprintf(out, "    movq $8, %%rdx\n");
    fprintf(out, "    movq %%rax, %%rcx\n");
    fprintf(out, "    call HeapAlloc\n");
    fprintf(out, "    addq $32, %%rsp\n");
    fprintf(out, "    popq %%rbx\n");
    fprintf(out, "    ret\n\n");
    }

    if (g_use_neca) {
    fprintf(out, "    .globl neca\n");
    fprintf(out, "neca:\n");
    fprintf(out, "    pushq %%rbx\n");
    fprintf(out, "    subq $32, %%rsp\n");
    fprintf(out, "    movq %%rcx, %%rbx\n");
    fprintf(out, "    call GetProcessHeap\n");
    fprintf(out, "    movq %%rbx, %%r8\n");
    fprintf(out, "    movq $0, %%rdx\n");
    fprintf(out, "    movq %%rax, %%rcx\n");
    fprintf(out, "    call HeapFree\n");
    fprintf(out, "    addq $32, %%rsp\n");
    fprintf(out, "    popq %%rbx\n");
    fprintf(out, "    ret\n\n");
    }

    if (g_use_lege) {
    fprintf(out, "__read_char:\n");
    fprintf(out, "    subq $56, %%rsp\n");
    fprintf(out, "    movl $-10, %%ecx\n");
    fprintf(out, "    call GetStdHandle\n");
    fprintf(out, "    movq %%rax, %%rcx\n");
    fprintf(out, "    leaq 48(%%rsp), %%rdx\n");
    fprintf(out, "    movq $1, %%r8\n");
    fprintf(out, "    leaq 40(%%rsp), %%r9\n");
    fprintf(out, "    movq $0, 32(%%rsp)\n");
    fprintf(out, "    call ReadFile\n");
    fprintf(out, "    testl %%eax, %%eax\n");
    fprintf(out, "    jz .Lrc_eof\n");
    fprintf(out, "    movl 40(%%rsp), %%eax\n");
    fprintf(out, "    testl %%eax, %%eax\n");
    fprintf(out, "    jz .Lrc_eof\n");
    fprintf(out, "    movzbq 48(%%rsp), %%rax\n");
    fprintf(out, "    addq $56, %%rsp\n");
    fprintf(out, "    ret\n");
    fprintf(out, ".Lrc_eof:\n");
    fprintf(out, "    xorq %%rax, %%rax\n");
    fprintf(out, "    addq $56, %%rsp\n");
    fprintf(out, "    ret\n\n");

    fprintf(out, "__lege_int:\n");
    fprintf(out, "    pushq %%rbx\n");
    fprintf(out, "    pushq %%r12\n");
    fprintf(out, "    pushq %%r13\n");
    fprintf(out, "    pushq %%r14\n");
    fprintf(out, "    subq $40, %%rsp\n");
    fprintf(out, "    movq %%rcx, %%rbx\n");
    fprintf(out, "    movq %%rdx, %%r12\n");
    fprintf(out, "    xorq %%r13, %%r13\n");
    fprintf(out, "    xorq %%r14, %%r14\n");
    fprintf(out, ".Lli_ws:\n");
    fprintf(out, "    call __read_char\n");
    fprintf(out, "    testq %%rax, %%rax\n");
    fprintf(out, "    jz .Lli_fail\n");
    fprintf(out, "    cmpb $' ', %%al\n");
    fprintf(out, "    je .Lli_ws\n");
    fprintf(out, "    cmpb $'\\n', %%al\n");
    fprintf(out, "    je .Lli_ws\n");
    fprintf(out, "    cmpb $'\\r', %%al\n");
    fprintf(out, "    je .Lli_ws\n");
    fprintf(out, "    cmpb $'\\t', %%al\n");
    fprintf(out, "    je .Lli_ws\n");
    fprintf(out, "    cmpb $'-', %%al\n");
    fprintf(out, "    jne .Lli_chk_plus\n");
    fprintf(out, "    movq $1, %%r13\n");
    fprintf(out, "    jmp .Lli_next\n");
    fprintf(out, ".Lli_chk_plus:\n");
    fprintf(out, "    cmpb $'+', %%al\n");
    fprintf(out, "    jne .Lli_digit\n");
    fprintf(out, ".Lli_next:\n");
    fprintf(out, "    call __read_char\n");
    fprintf(out, "    testq %%rax, %%rax\n");
    fprintf(out, "    jz .Lli_done\n");
    fprintf(out, ".Lli_digit:\n");
    fprintf(out, "    cmpb $'0', %%al\n");
    fprintf(out, "    jb .Lli_done\n");
    fprintf(out, "    cmpb $'9', %%al\n");
    fprintf(out, "    ja .Lli_done\n");
    fprintf(out, "    subb $'0', %%al\n");
    fprintf(out, "    movzbq %%al, %%rax\n");
    fprintf(out, "    imulq $10, %%r14\n");
    fprintf(out, "    addq %%rax, %%r14\n");
    fprintf(out, "    jmp .Lli_next\n");
    fprintf(out, ".Lli_done:\n");
    fprintf(out, "    testq %%r13, %%r13\n");
    fprintf(out, "    jz .Lli_store\n");
    fprintf(out, "    negq %%r14\n");
    fprintf(out, ".Lli_store:\n");
    fprintf(out, "    cmpq $1, %%r12\n");
    fprintf(out, "    je .Lli_s1\n");
    fprintf(out, "    cmpq $2, %%r12\n");
    fprintf(out, "    je .Lli_s2\n");
    fprintf(out, "    cmpq $4, %%r12\n");
    fprintf(out, "    je .Lli_s4\n");
    fprintf(out, "    movq %%r14, (%%rbx)\n");
    fprintf(out, "    jmp .Lli_ok\n");
    fprintf(out, ".Lli_s1:\n");
    fprintf(out, "    movb %%r14b, (%%rbx)\n");
    fprintf(out, "    jmp .Lli_ok\n");
    fprintf(out, ".Lli_s2:\n");
    fprintf(out, "    movw %%r14w, (%%rbx)\n");
    fprintf(out, "    jmp .Lli_ok\n");
    fprintf(out, ".Lli_s4:\n");
    fprintf(out, "    movl %%r14d, (%%rbx)\n");
    fprintf(out, ".Lli_ok:\n");
    fprintf(out, "    movl $1, %%eax\n");
    fprintf(out, "    addq $40, %%rsp\n");
    fprintf(out, "    popq %%r14\n");
    fprintf(out, "    popq %%r13\n");
    fprintf(out, "    popq %%r12\n");
    fprintf(out, "    popq %%rbx\n");
    fprintf(out, "    ret\n");
    fprintf(out, ".Lli_fail:\n");
    fprintf(out, "    xorl %%eax, %%eax\n");
    fprintf(out, "    addq $40, %%rsp\n");
    fprintf(out, "    popq %%r14\n");
    fprintf(out, "    popq %%r13\n");
    fprintf(out, "    popq %%r12\n");
    fprintf(out, "    popq %%rbx\n");
    fprintf(out, "    ret\n\n");

    fprintf(out, "__lege_float:\n");
    fprintf(out, "    pushq %%rbx\n");
    fprintf(out, "    pushq %%r12\n");
    fprintf(out, "    pushq %%r13\n");
    fprintf(out, "    pushq %%r14\n");
    fprintf(out, "    subq $40, %%rsp\n");
    fprintf(out, "    movq %%rcx, %%rbx\n");
    fprintf(out, "    movq %%rdx, %%r12\n");
    fprintf(out, "    xorq %%r13, %%r13\n");
    fprintf(out, "    xorq %%r14, %%r14\n");
    fprintf(out, "    movq $0, 32(%%rsp)\n");
    fprintf(out, "    movq $1, 24(%%rsp)\n");
    fprintf(out, ".Llf_ws:\n");
    fprintf(out, "    call __read_char\n");
    fprintf(out, "    testq %%rax, %%rax\n");
    fprintf(out, "    jz .Llf_fail\n");
    fprintf(out, "    cmpb $' ', %%al\n");
    fprintf(out, "    je .Llf_ws\n");
    fprintf(out, "    cmpb $'\\n', %%al\n");
    fprintf(out, "    je .Llf_ws\n");
    fprintf(out, "    cmpb $'\\r', %%al\n");
    fprintf(out, "    je .Llf_ws\n");
    fprintf(out, "    cmpb $'\\t', %%al\n");
    fprintf(out, "    je .Llf_ws\n");
    fprintf(out, "    cmpb $'-', %%al\n");
    fprintf(out, "    jne .Llf_chk_plus\n");
    fprintf(out, "    movq $1, %%r13\n");
    fprintf(out, "    jmp .Llf_next\n");
    fprintf(out, ".Llf_chk_plus:\n");
    fprintf(out, "    cmpb $'+', %%al\n");
    fprintf(out, "    jne .Llf_digit\n");
    fprintf(out, ".Llf_next:\n");
    fprintf(out, "    call __read_char\n");
    fprintf(out, "    testq %%rax, %%rax\n");
    fprintf(out, "    jz .Llf_done\n");
    fprintf(out, ".Llf_digit:\n");
    fprintf(out, "    cmpb $'.', %%al\n");
    fprintf(out, "    je .Llf_frac_next\n");
    fprintf(out, "    cmpb $'0', %%al\n");
    fprintf(out, "    jb .Llf_done\n");
    fprintf(out, "    cmpb $'9', %%al\n");
    fprintf(out, "    ja .Llf_done\n");
    fprintf(out, "    subb $'0', %%al\n");
    fprintf(out, "    movzbq %%al, %%rax\n");
    fprintf(out, "    imulq $10, %%r14\n");
    fprintf(out, "    addq %%rax, %%r14\n");
    fprintf(out, "    jmp .Llf_next\n");
    fprintf(out, ".Llf_frac_next:\n");
    fprintf(out, "    call __read_char\n");
    fprintf(out, "    testq %%rax, %%rax\n");
    fprintf(out, "    jz .Llf_done\n");
    fprintf(out, "    cmpb $'0', %%al\n");
    fprintf(out, "    jb .Llf_done\n");
    fprintf(out, "    cmpb $'9', %%al\n");
    fprintf(out, "    ja .Llf_done\n");
    fprintf(out, "    subb $'0', %%al\n");
    fprintf(out, "    movzbq %%al, %%rax\n");
    fprintf(out, "    movq 32(%%rsp), %%rcx\n");
    fprintf(out, "    imulq $10, %%rcx\n");
    fprintf(out, "    addq %%rax, %%rcx\n");
    fprintf(out, "    movq %%rcx, 32(%%rsp)\n");
    fprintf(out, "    movq 24(%%rsp), %%rcx\n");
    fprintf(out, "    imulq $10, %%rcx\n");
    fprintf(out, "    movq %%rcx, 24(%%rsp)\n");
    fprintf(out, "    jmp .Llf_frac_next\n");
    fprintf(out, ".Llf_done:\n");
    fprintf(out, "    cvtsi2sd %%r14, %%xmm0\n");
    fprintf(out, "    movq 32(%%rsp), %%rax\n");
    fprintf(out, "    cvtsi2sd %%rax, %%xmm1\n");
    fprintf(out, "    movq 24(%%rsp), %%rax\n");
    fprintf(out, "    cvtsi2sd %%rax, %%xmm2\n");
    fprintf(out, "    divsd %%xmm2, %%xmm1\n");
    fprintf(out, "    addsd %%xmm1, %%xmm0\n");
    fprintf(out, "    testq %%r13, %%r13\n");
    fprintf(out, "    jz .Llf_store\n");
    fprintf(out, "    xorpd %%xmm1, %%xmm1\n");
    fprintf(out, "    subsd %%xmm0, %%xmm1\n");
    fprintf(out, "    movapd %%xmm1, %%xmm0\n");
    fprintf(out, ".Llf_store:\n");
    fprintf(out, "    cmpq $4, %%r12\n");
    fprintf(out, "    je .Llf_s4\n");
    fprintf(out, "    movsd %%xmm0, (%%rbx)\n");
    fprintf(out, "    jmp .Llf_ok\n");
    fprintf(out, ".Llf_s4:\n");
    fprintf(out, "    cvtsd2ss %%xmm0, %%xmm0\n");
    fprintf(out, "    movss %%xmm0, (%%rbx)\n");
    fprintf(out, ".Llf_ok:\n");
    fprintf(out, "    movl $1, %%eax\n");
    fprintf(out, "    addq $40, %%rsp\n");
    fprintf(out, "    popq %%r14\n");
    fprintf(out, "    popq %%r13\n");
    fprintf(out, "    popq %%r12\n");
    fprintf(out, "    popq %%rbx\n");
    fprintf(out, "    ret\n");
    fprintf(out, ".Llf_fail:\n");
    fprintf(out, "    xorl %%eax, %%eax\n");
    fprintf(out, "    addq $40, %%rsp\n");
    fprintf(out, "    popq %%r14\n");
    fprintf(out, "    popq %%r13\n");
    fprintf(out, "    popq %%r12\n");
    fprintf(out, "    popq %%rbx\n");
    fprintf(out, "    ret\n\n");

    fprintf(out, "__lege_char:\n");
    fprintf(out, "    pushq %%rbx\n");
    fprintf(out, "    subq $32, %%rsp\n");
    fprintf(out, "    movq %%rcx, %%rbx\n");
    fprintf(out, ".Llc_ws:\n");
    fprintf(out, "    call __read_char\n");
    fprintf(out, "    testq %%rax, %%rax\n");
    fprintf(out, "    jz .Llc_fail\n");
    fprintf(out, "    cmpb $' ', %%al\n");
    fprintf(out, "    je .Llc_ws\n");
    fprintf(out, "    cmpb $'\\n', %%al\n");
    fprintf(out, "    je .Llc_ws\n");
    fprintf(out, "    cmpb $'\\r', %%al\n");
    fprintf(out, "    je .Llc_ws\n");
    fprintf(out, "    cmpb $'\\t', %%al\n");
    fprintf(out, "    je .Llc_ws\n");
    fprintf(out, "    movb %%al, (%%rbx)\n");
    fprintf(out, "    movl $1, %%eax\n");
    fprintf(out, "    addq $32, %%rsp\n");
    fprintf(out, "    popq %%rbx\n");
    fprintf(out, "    ret\n");
    fprintf(out, ".Llc_fail:\n");
    fprintf(out, "    xorl %%eax, %%eax\n");
    fprintf(out, "    addq $32, %%rsp\n");
    fprintf(out, "    popq %%rbx\n");
    fprintf(out, "    ret\n\n");

    fprintf(out, "__lege_bool:\n");
    fprintf(out, "    pushq %%rbx\n");
    fprintf(out, "    subq $32, %%rsp\n");
    fprintf(out, "    movq %%rcx, %%rbx\n");
    fprintf(out, ".Llb_ws:\n");
    fprintf(out, "    call __read_char\n");
    fprintf(out, "    testq %%rax, %%rax\n");
    fprintf(out, "    jz .Llb_fail\n");
    fprintf(out, "    cmpb $' ', %%al\n");
    fprintf(out, "    je .Llb_ws\n");
    fprintf(out, "    cmpb $'\\n', %%al\n");
    fprintf(out, "    je .Llb_ws\n");
    fprintf(out, "    cmpb $'\\r', %%al\n");
    fprintf(out, "    je .Llb_ws\n");
    fprintf(out, "    cmpb $'\\t', %%al\n");
    fprintf(out, "    je .Llb_ws\n");
    fprintf(out, "    cmpb $'1', %%al\n");
    fprintf(out, "    je .Llb_true\n");
    fprintf(out, "    cmpb $'v', %%al\n");
    fprintf(out, "    je .Llb_true\n");
    fprintf(out, "    cmpb $'V', %%al\n");
    fprintf(out, "    je .Llb_true\n");
    fprintf(out, "    movb $0, (%%rbx)\n");
    fprintf(out, "    jmp .Llb_ok\n");
    fprintf(out, ".Llb_true:\n");
    fprintf(out, "    movb $1, (%%rbx)\n");
    fprintf(out, ".Llb_ok:\n");
    fprintf(out, "    movl $1, %%eax\n");
    fprintf(out, "    addq $32, %%rsp\n");
    fprintf(out, "    popq %%rbx\n");
    fprintf(out, "    ret\n");
    fprintf(out, ".Llb_fail:\n");
    fprintf(out, "    xorl %%eax, %%eax\n");
    fprintf(out, "    addq $32, %%rsp\n");
    fprintf(out, "    popq %%rbx\n");
    fprintf(out, "    ret\n\n");
    }

    if (g_use_print_float || g_use_print_bool || g_use_lege) {
        fprintf(out, "    .section .rdata,\"a\"\n");
        if (g_use_print_float) {
            fprintf(out, ".Lstr_minus:\n    .byte 45\n");
            fprintf(out, ".Lstr_dot:\n    .byte 46\n");
            fprintf(out, "    .align 8\n");
            fprintf(out, ".Lfloat_10:\n    .quad 4621819117588971520\n");
        }
        if (g_use_print_bool) {
            fprintf(out, ".Lstr_verum:\n    .byte 118, 101, 114, 117, 109\n");
            fprintf(out, ".Lstr_falsum:\n    .byte 102, 97, 108, 115, 117, 109\n");
        }
        fprintf(out, "    .text\n\n");
    }

    fprintf(out, "    .globl main\n");
    fprintf(out, "main:\n");
    fprintf(out, "    pushq %%rbp\n");
    fprintf(out, "    movq %%rsp, %%rbp\n");
    fprintf(out, "    subq $48, %%rsp\n");
    fprintf(out, "    movq %%rcx, -8(%%rbp)\n");
    fprintf(out, "    movq %%rdx, -16(%%rbp)\n");
    fprintf(out, "    call __scoria_init\n");
    fprintf(out, "    movq -8(%%rbp), %%rcx\n");
    fprintf(out, "    movq -16(%%rbp), %%rdx\n");
    fprintf(out, "    call princeps\n");
    fprintf(out, "    addq $48, %%rsp\n");
    fprintf(out, "    popq %%rbp\n");
    fprintf(out, "    ret\n\n");
}

void pe_builtins_generate(PeLinker* linker, uint32_t princeps_offset, uint32_t init_offset) {
    if (g_use_print_str) {
    // 追加内置汇编例程: __print_str
    g_print_str_offset = (uint32_t)linker->text_section.size;
    
    // sub rsp, 72
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x83); emit8(&linker->text_section, 0xEC); emit8(&linker->text_section, 0x48);
    // mov [rsp+48], rcx (save string address)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0x4C); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x30);
    // mov [rsp+56], rdx (save string length)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0x54); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x38);
    // mov ecx, -11 (STD_OUTPUT_HANDLE)
    emit8(&linker->text_section, 0xB9); emit32(&linker->text_section, (uint32_t)-11);
    // call [rip + IAT_GetStdHandle]
    emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x15);
    g_call_getstdhandle_reloc = (uint32_t)linker->text_section.size;
    emit32(&linker->text_section, 0);
    // mov rcx, rax (hFile)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xC1);
    // mov rdx, [rsp+48] (lpBuffer)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x8B); emit8(&linker->text_section, 0x54); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x30);
    // mov r8, [rsp+56] (nNumberOfBytesToWrite)
    emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x8B); emit8(&linker->text_section, 0x44); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x38);
    // lea r9, [rsp+40] (lpNumberOfBytesWritten)
    emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x8D); emit8(&linker->text_section, 0x4C); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x28);
    // mov qword ptr [rsp+32], 0 (lpOverlapped)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0xC7); emit8(&linker->text_section, 0x44); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x20); emit32(&linker->text_section, 0);
    // call [rip + IAT_WriteFile]
    emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x15);
    g_call_writeconsolea_reloc = (uint32_t)linker->text_section.size;
    emit32(&linker->text_section, 0);
    // add rsp, 72
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x83); emit8(&linker->text_section, 0xC4); emit8(&linker->text_section, 0x48);
    // ret
    emit8(&linker->text_section, 0xC3);
    }

    if (g_use_print_uint) {
    // 追加内置汇编例程: __print_uint
    g_print_uint_offset = (uint32_t)linker->text_section.size;
    uint8_t print_uint_code[] = {
        0x48, 0x83, 0xEC, 0x68, 0x48, 0x89, 0xC8, 0x4C, 0x8D, 0x44, 0x24, 0x50, 0x49, 0xFF, 0xC8, 0x49,
        0xC7, 0xC1, 0x0A, 0x00, 0x00, 0x00, 0x48, 0x31, 0xD2, 0x49, 0xF7, 0xF1, 0x80, 0xC2, 0x30, 0x41,
        0x88, 0x10, 0x49, 0xFF, 0xC8, 0x48, 0x85, 0xC0, 0x75, 0xEC, 0x49, 0xFF, 0xC0, 0x4C, 0x8D, 0x54,
        0x24, 0x50, 0x4D, 0x29, 0xC2, 0x4C, 0x89, 0x44, 0x24, 0x58, 0x4C, 0x89, 0x54, 0x24, 0x60, 0xB9,
        0xF5, 0xFF, 0xFF, 0xFF, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0xC1, 0x48, 0x8B, 0x54,
        0x24, 0x58, 0x4C, 0x8B, 0x44, 0x24, 0x60, 0x4C, 0x8D, 0x4C, 0x24, 0x28, 0x48, 0xC7, 0x44, 0x24,
        0x20, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x68, 0xC3
    };
    for (size_t i = 0; i < sizeof(print_uint_code); i++) emit8(&linker->text_section, print_uint_code[i]);
    g_call_getstdhandle_reloc3 = g_print_uint_offset + 70;
    g_call_writeconsolea_reloc3 = g_print_uint_offset + 103;
    }

    if (g_use_print_int) {
    // 追加内置汇编例程: __print_int
    g_print_int_offset = (uint32_t)linker->text_section.size;
    uint8_t print_int_code[] = {
        0x48, 0x83, 0xEC, 0x68, 0x49, 0x89, 0xCA, 0x4D, 0x31, 0xDB, 0x48, 0x85, 0xC9, 0x79, 0x0A, 0x49,
        0xF7, 0xDA, 0x49, 0xC7, 0xC3, 0x01, 0x00, 0x00, 0x00, 0x4C, 0x8D, 0x44, 0x24, 0x50, 0x49, 0xFF,
        0xC8, 0x4C, 0x89, 0xD0, 0x49, 0xC7, 0xC1, 0x0A, 0x00, 0x00, 0x00, 0x48, 0x31, 0xD2, 0x49, 0xF7,
        0xF1, 0x80, 0xC2, 0x30, 0x41, 0x88, 0x10, 0x49, 0xFF, 0xC8, 0x48, 0x85, 0xC0, 0x75, 0xEC, 0x4D,
        0x85, 0xDB, 0x74, 0x07, 0x41, 0xC6, 0x00, 0x2D, 0x49, 0xFF, 0xC8, 0x49, 0xFF, 0xC0, 0x4C, 0x8D,
        0x54, 0x24, 0x50, 0x4D, 0x29, 0xC2, 0x4C, 0x89, 0x44, 0x24, 0x58, 0x4C, 0x89, 0x54, 0x24, 0x60,
        0xB9, 0xF5, 0xFF, 0xFF, 0xFF, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0xC1, 0x48, 0x8B,
        0x54, 0x24, 0x58, 0x4C, 0x8B, 0x44, 0x24, 0x60, 0x4C, 0x8D, 0x4C, 0x24, 0x28, 0x48, 0xC7, 0x44,
        0x24, 0x20, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x68,
        0xC3
    };
    for (size_t i = 0; i < sizeof(print_int_code); i++) emit8(&linker->text_section, print_int_code[i]);
    g_call_getstdhandle_reloc2 = g_print_int_offset + 103;
    g_call_writeconsolea_reloc2 = g_print_int_offset + 136;
    }

    if (g_use_print_float) {
    // 追加内置汇编例程: __print_float
    g_print_float_offset = (uint32_t)linker->text_section.size;
    uint8_t print_float_code[] = {
        0x55, 0x48, 0x89, 0xE5, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x89, 0x4D, 0xF8, 0x48, 0x89, 0xC8, 0x48,
        0x85, 0xC0, 0x79, 0x20, 0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48, 0xC7, 0xC2, 0x01, 0x00,
        0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x45, 0xF8, 0x48, 0x0F, 0xBA, 0xF8, 0x3F,
        0x48, 0x89, 0x45, 0xF8, 0xF3, 0x0F, 0x7E, 0x45, 0xF8, 0xF2, 0x48, 0x0F, 0x2C, 0xC8, 0x48, 0x89,
        0x4D, 0xF0, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48, 0xC7,
        0xC2, 0x01, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x7E, 0x45, 0xF8, 0xF2,
        0x48, 0x0F, 0x2A, 0x4D, 0xF0, 0xF2, 0x0F, 0x5C, 0xC1, 0xC7, 0x45, 0xEC, 0x06, 0x00, 0x00, 0x00,
        0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00, 0xF2, 0x0F, 0x10, 0x10, 0xF2, 0x0F, 0x59, 0xC2, 0xF2,
        0x48, 0x0F, 0x2C, 0xC8, 0xF2, 0x0F, 0x11, 0x45, 0xE0, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xF2, 0x0F,
        0x10, 0x45, 0xE0, 0xF2, 0x48, 0x0F, 0x2C, 0xC8, 0xF2, 0x48, 0x0F, 0x2A, 0xC9, 0xF2, 0x0F, 0x5C,
        0xC1, 0xFF, 0x4D, 0xEC, 0x75, 0xCA, 0x48, 0x89, 0xEC, 0x5D, 0xC3
    };
    for (size_t i = 0; i < sizeof(print_float_code); i++) emit8(&linker->text_section, print_float_code[i]);
    }

    if (g_use_print_hex) {
    // 追加内置汇编例程: __print_hex
    g_print_hex_offset = (uint32_t)linker->text_section.size;
    uint8_t print_hex_code[] = {
        0x48, 0x83, 0xEC, 0x38, 0x49, 0x89, 0xCA, 0x4C, 0x8D, 0x44, 0x24, 0x1F, 0x41, 0xC6, 0x00, 0x30,
        0x49, 0xFF, 0xC0, 0x41, 0xC6, 0x00, 0x78, 0x4C, 0x8D, 0x44, 0x24, 0x30, 0x49, 0xC7, 0xC1, 0x10,
        0x00, 0x00, 0x00, 0x4C, 0x89, 0xD0, 0x48, 0x83, 0xE0, 0x0F, 0x3C, 0x09, 0x76, 0x04, 0x04, 0x57,
        0xEB, 0x02, 0x04, 0x30, 0x41, 0x88, 0x00, 0x49, 0xFF, 0xC8, 0x49, 0xC1, 0xEA, 0x04, 0x49, 0xFF,
        0xC9, 0x75, 0xE0, 0x48, 0x8D, 0x4C, 0x24, 0x1F, 0xBA, 0x12, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00,
        0x00, 0x00, 0x48, 0x83, 0xC4, 0x38, 0xC3
    };
    for (size_t i = 0; i < sizeof(print_hex_code); i++) emit8(&linker->text_section, print_hex_code[i]);
    }
    
    if (g_use_print_bool) {
    // 追加内置汇编例程: __print_bool
    g_print_bool_offset = (uint32_t)linker->text_section.size;
    uint8_t print_bool_code[] = {
        0x48, 0x85, 0xC9, 0x74, 0x0E, 0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xBA, 0x05, 0x00, 0x00,
        0x00, 0xEB, 0x0C, 0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xBA, 0x06, 0x00, 0x00, 0x00, 0xE8,
        0x00, 0x00, 0x00, 0x00, 0xC3
    };
    for (size_t i = 0; i < sizeof(print_bool_code); i++) emit8(&linker->text_section, print_bool_code[i]);
    }

    if (g_use_crea) {
    // 追加内置汇编例程: __crea
    g_crea_offset = (uint32_t)linker->text_section.size;
    emit8(&linker->text_section, 0x53); // push rbx
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x83); emit8(&linker->text_section, 0xEC); emit8(&linker->text_section, 0x20); // sub rsp, 32
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xCB); // mov rbx, rcx
    emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x15); // call GetProcessHeap
    g_call_getprocessheap_reloc1 = (uint32_t)linker->text_section.size;
    emit32(&linker->text_section, 0);
    emit_rex(&linker->text_section, 1, 0, 0, 1); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xD8); // mov r8, rbx
    emit_mov_reg_imm32(&linker->text_section, REG_RDX, 8); // mov rdx, 8 (HEAP_ZERO_MEMORY)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xC1); // mov rcx, rax
    emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x15); // call HeapAlloc
    g_call_heapalloc_reloc = (uint32_t)linker->text_section.size;
    emit32(&linker->text_section, 0);
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x83); emit8(&linker->text_section, 0xC4); emit8(&linker->text_section, 0x20); // add rsp, 32
    emit8(&linker->text_section, 0x5B); // pop rbx
    emit8(&linker->text_section, 0xC3); // ret
    }

    if (g_use_neca) {
    // 追加内置汇编例程: __neca
    g_neca_offset = (uint32_t)linker->text_section.size;
    emit8(&linker->text_section, 0x53); // push rbx
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x83); emit8(&linker->text_section, 0xEC); emit8(&linker->text_section, 0x20); // sub rsp, 32
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xCB); // mov rbx, rcx
    emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x15); // call GetProcessHeap
    g_call_getprocessheap_reloc2 = (uint32_t)linker->text_section.size;
    emit32(&linker->text_section, 0);
    emit_rex(&linker->text_section, 1, 0, 0, 1); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xD8); // mov r8, rbx
    emit_mov_reg_imm32(&linker->text_section, REG_RDX, 0); // mov rdx, 0
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xC1); // mov rcx, rax
    emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x15); // call HeapFree
    g_call_heapfree_reloc = (uint32_t)linker->text_section.size;
    emit32(&linker->text_section, 0);
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x83); emit8(&linker->text_section, 0xC4); emit8(&linker->text_section, 0x20); // add rsp, 32
    emit8(&linker->text_section, 0x5B); // pop rbx
    emit8(&linker->text_section, 0xC3); // ret
    }

    if (g_use_lege) {
        PeCodeBuffer* cb = &linker->text_section;
        
        // =========================================================
        // __read_char
        // =========================================================
        g_read_char_offset = (uint32_t)cb->size;
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xEC); emit8(cb, 0x38); // sub rsp, 56
        emit8(cb, 0xB9); emit32(cb, (uint32_t)-10); // mov ecx, -10
        emit8(cb, 0xFF); emit8(cb, 0x15); g_call_getstdhandle_reloc_read = (uint32_t)cb->size; emit32(cb, 0); // call GetStdHandle
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x89); emit8(cb, 0xC1); // mov rcx, rax
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x8D); emit8(cb, 0x54); emit8(cb, 0x24); emit8(cb, 0x30); // lea rdx, [rsp+48]
        emit_rex(cb, 1, 0, 0, 1); emit8(cb, 0xC7); emit8(cb, 0xC0); emit32(cb, 1); // mov r8, 1
        emit_rex(cb, 1, 1, 0, 0); emit8(cb, 0x8D); emit8(cb, 0x4C); emit8(cb, 0x24); emit8(cb, 0x28); // lea r9, [rsp+40]
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0xC7); emit8(cb, 0x44); emit8(cb, 0x24); emit8(cb, 0x20); emit32(cb, 0); // mov qword ptr [rsp+32], 0
        emit8(cb, 0xFF); emit8(cb, 0x15); g_call_readfile_reloc = (uint32_t)cb->size; emit32(cb, 0); // call ReadFile
        
        emit8(cb, 0x85); emit8(cb, 0xC0); // test eax, eax
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t rc_fail_patch1 = (uint32_t)cb->size; emit32(cb, 0); // jz .Lrc_eof
        
        emit8(cb, 0x8B); emit8(cb, 0x44); emit8(cb, 0x24); emit8(cb, 0x28); // mov eax, dword ptr [rsp+40]
        emit8(cb, 0x85); emit8(cb, 0xC0); // test eax, eax
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t rc_fail_patch2 = (uint32_t)cb->size; emit32(cb, 0); // jz .Lrc_eof
        
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x0F); emit8(cb, 0xB6); emit8(cb, 0x44); emit8(cb, 0x24); emit8(cb, 0x30); // movzx rax, byte ptr [rsp+48]
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xC4); emit8(cb, 0x38); // add rsp, 56
        emit8(cb, 0xC3); // ret
        
        // .Lrc_eof:
        uint32_t rc_eof = (uint32_t)cb->size;
        memcpy(cb->buffer + rc_fail_patch1, &(int32_t){rc_eof - (rc_fail_patch1 + 4)}, 4);
        memcpy(cb->buffer + rc_fail_patch2, &(int32_t){rc_eof - (rc_fail_patch2 + 4)}, 4);
        
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x31); emit8(cb, 0xC0); // xor rax, rax
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xC4); emit8(cb, 0x38); // add rsp, 56
        emit8(cb, 0xC3); // ret

        // =========================================================
        // __lege_int
        // =========================================================
        g_lege_int_offset = (uint32_t)cb->size;
        emit8(cb, 0x53); // push rbx
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x54); // push r12
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x55); // push r13
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x56); // push r14
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xEC); emit8(cb, 0x28); // sub rsp, 40
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x89); emit8(cb, 0xCB); // mov rbx, rcx
        emit_rex(cb, 1, 0, 0, 1); emit8(cb, 0x89); emit8(cb, 0xD4); // mov r12, rdx
        emit_rex(cb, 1, 1, 0, 1); emit8(cb, 0x31); emit8(cb, 0xED); // xor r13, r13
        emit_rex(cb, 1, 1, 0, 1); emit8(cb, 0x31); emit8(cb, 0xF6); // xor r14, r14
        uint32_t li_ws = (uint32_t)cb->size;
        emit8(cb, 0xE8); emit32(cb, g_read_char_offset - ((uint32_t)cb->size + 4)); // call __read_char
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x85); emit8(cb, 0xC0); // test rax, rax
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t li_fail_patch = (uint32_t)cb->size; emit32(cb, 0); // jz .Lli_fail
        emit8(cb, 0x3C); emit8(cb, 0x20); // cmp al, ' '
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, li_ws - ((uint32_t)cb->size + 4)); // je .Lli_ws
        emit8(cb, 0x3C); emit8(cb, 0x0A); // cmp al, '\n'
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, li_ws - ((uint32_t)cb->size + 4)); // je .Lli_ws
        emit8(cb, 0x3C); emit8(cb, 0x0D); // cmp al, '\r'
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, li_ws - ((uint32_t)cb->size + 4)); // je .Lli_ws
        emit8(cb, 0x3C); emit8(cb, 0x09); // cmp al, '\t'
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, li_ws - ((uint32_t)cb->size + 4)); // je .Lli_ws
        emit8(cb, 0x3C); emit8(cb, 0x2D); // cmp al, '-'
        emit8(cb, 0x0F); emit8(cb, 0x85); uint32_t li_chk_plus_patch = (uint32_t)cb->size; emit32(cb, 0); // jne .Lli_chk_plus
        emit_rex(cb, 1, 0, 0, 1); emit8(cb, 0xC7); emit8(cb, 0xC5); emit32(cb, 1); // mov r13, 1
        emit8(cb, 0xE9); uint32_t li_next_patch1 = (uint32_t)cb->size; emit32(cb, 0); // jmp .Lli_next
        uint32_t li_chk_plus = (uint32_t)cb->size;
        memcpy(cb->buffer + li_chk_plus_patch, &(int32_t){li_chk_plus - (li_chk_plus_patch + 4)}, 4);
        emit8(cb, 0x3C); emit8(cb, 0x2B); // cmp al, '+'
        emit8(cb, 0x0F); emit8(cb, 0x85); uint32_t li_digit_patch1 = (uint32_t)cb->size; emit32(cb, 0); // jne .Lli_digit
        uint32_t li_next = (uint32_t)cb->size;
        memcpy(cb->buffer + li_next_patch1, &(int32_t){li_next - (li_next_patch1 + 4)}, 4);
        emit8(cb, 0xE8); emit32(cb, g_read_char_offset - ((uint32_t)cb->size + 4)); // call __read_char
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x85); emit8(cb, 0xC0); // test rax, rax
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t li_done_patch1 = (uint32_t)cb->size; emit32(cb, 0); // jz .Lli_done
        uint32_t li_digit = (uint32_t)cb->size;
        memcpy(cb->buffer + li_digit_patch1, &(int32_t){li_digit - (li_digit_patch1 + 4)}, 4);
        emit8(cb, 0x3C); emit8(cb, 0x30); // cmp al, '0'
        emit8(cb, 0x0F); emit8(cb, 0x82); uint32_t li_done_patch2 = (uint32_t)cb->size; emit32(cb, 0); // jb .Lli_done
        emit8(cb, 0x3C); emit8(cb, 0x39); // cmp al, '9'
        emit8(cb, 0x0F); emit8(cb, 0x87); uint32_t li_done_patch3 = (uint32_t)cb->size; emit32(cb, 0); // ja .Lli_done
        emit8(cb, 0x2C); emit8(cb, 0x30); // sub al, '0'
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x0F); emit8(cb, 0xB6); emit8(cb, 0xC0); // movzx rax, al
        emit_rex(cb, 1, 1, 0, 1); emit8(cb, 0x6B); emit8(cb, 0xF6); emit8(cb, 0x0A); // imul r14, r14, 10
        emit_rex(cb, 1, 0, 0, 1); emit8(cb, 0x01); emit8(cb, 0xC6); // add r14, rax
        emit8(cb, 0xE9); emit32(cb, li_next - ((uint32_t)cb->size + 4)); // jmp .Lli_next
        uint32_t li_done = (uint32_t)cb->size;
        memcpy(cb->buffer + li_done_patch1, &(int32_t){li_done - (li_done_patch1 + 4)}, 4);
        memcpy(cb->buffer + li_done_patch2, &(int32_t){li_done - (li_done_patch2 + 4)}, 4);
        memcpy(cb->buffer + li_done_patch3, &(int32_t){li_done - (li_done_patch3 + 4)}, 4);
        emit_rex(cb, 1, 1, 0, 1); emit8(cb, 0x85); emit8(cb, 0xED); // test r13, r13
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t li_store_patch = (uint32_t)cb->size; emit32(cb, 0); // jz .Lli_store
        emit_rex(cb, 1, 0, 0, 1); emit8(cb, 0xF7); emit8(cb, 0xDE); // neg r14
        uint32_t li_store = (uint32_t)cb->size;
        memcpy(cb->buffer + li_store_patch, &(int32_t){li_store - (li_store_patch + 4)}, 4);
        emit_rex(cb, 1, 0, 0, 1); emit8(cb, 0x83); emit8(cb, 0xFC); emit8(cb, 0x01); // cmp r12, 1
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t li_s1_patch = (uint32_t)cb->size; emit32(cb, 0); // je .Lli_s1
        emit_rex(cb, 1, 0, 0, 1); emit8(cb, 0x83); emit8(cb, 0xFC); emit8(cb, 0x02); // cmp r12, 2
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t li_s2_patch = (uint32_t)cb->size; emit32(cb, 0); // je .Lli_s2
        emit_rex(cb, 1, 0, 0, 1); emit8(cb, 0x83); emit8(cb, 0xFC); emit8(cb, 0x04); // cmp r12, 4
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t li_s4_patch = (uint32_t)cb->size; emit32(cb, 0); // je .Lli_s4
        emit_rex(cb, 1, 1, 0, 0); emit8(cb, 0x89); emit8(cb, 0x33); // mov [rbx], r14
        emit8(cb, 0xE9); uint32_t li_ok_patch1 = (uint32_t)cb->size; emit32(cb, 0); // jmp .Lli_ok
        uint32_t li_s1 = (uint32_t)cb->size;
        memcpy(cb->buffer + li_s1_patch, &(int32_t){li_s1 - (li_s1_patch + 4)}, 4);
        emit_rex(cb, 0, 1, 0, 0); emit8(cb, 0x88); emit8(cb, 0x33); // mov [rbx], r14b
        emit8(cb, 0xE9); uint32_t li_ok_patch2 = (uint32_t)cb->size; emit32(cb, 0); // jmp .Lli_ok
        uint32_t li_s2 = (uint32_t)cb->size;
        memcpy(cb->buffer + li_s2_patch, &(int32_t){li_s2 - (li_s2_patch + 4)}, 4);
        emit8(cb, 0x66); emit_rex(cb, 0, 1, 0, 0); emit8(cb, 0x89); emit8(cb, 0x33); // mov [rbx], r14w
        emit8(cb, 0xE9); uint32_t li_ok_patch3 = (uint32_t)cb->size; emit32(cb, 0); // jmp .Lli_ok
        uint32_t li_s4 = (uint32_t)cb->size;
        memcpy(cb->buffer + li_s4_patch, &(int32_t){li_s4 - (li_s4_patch + 4)}, 4);
        emit_rex(cb, 0, 1, 0, 0); emit8(cb, 0x89); emit8(cb, 0x33); // mov [rbx], r14d
        uint32_t li_ok = (uint32_t)cb->size;
        memcpy(cb->buffer + li_ok_patch1, &(int32_t){li_ok - (li_ok_patch1 + 4)}, 4);
        memcpy(cb->buffer + li_ok_patch2, &(int32_t){li_ok - (li_ok_patch2 + 4)}, 4);
        memcpy(cb->buffer + li_ok_patch3, &(int32_t){li_ok - (li_ok_patch3 + 4)}, 4);
        emit8(cb, 0xB8); emit32(cb, 1); // mov eax, 1
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xC4); emit8(cb, 0x28); // add rsp, 40
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x5E); // pop r14
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x5D); // pop r13
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x5C); // pop r12
        emit8(cb, 0x5B); // pop rbx
        emit8(cb, 0xC3); // ret
        uint32_t li_fail = (uint32_t)cb->size;
        memcpy(cb->buffer + li_fail_patch, &(int32_t){li_fail - (li_fail_patch + 4)}, 4);
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x31); emit8(cb, 0xC0); // xor rax, rax
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xC4); emit8(cb, 0x28); // add rsp, 40
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x5E); // pop r14
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x5D); // pop r13
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x5C); // pop r12
        emit8(cb, 0x5B); // pop rbx
        emit8(cb, 0xC3); // ret

        // =========================================================
        // __lege_float
        // =========================================================
        g_lege_float_offset = (uint32_t)cb->size;
        emit8(cb, 0x53); // push rbx
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x54); // push r12
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x55); // push r13
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x56); // push r14
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xEC); emit8(cb, 0x28); // sub rsp, 40
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x89); emit8(cb, 0xCB); // mov rbx, rcx
        emit_rex(cb, 1, 0, 0, 1); emit8(cb, 0x89); emit8(cb, 0xD4); // mov r12, rdx
        emit_rex(cb, 1, 1, 0, 1); emit8(cb, 0x31); emit8(cb, 0xED); // xor r13, r13
        emit_rex(cb, 1, 1, 0, 1); emit8(cb, 0x31); emit8(cb, 0xF6); // xor r14, r14
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0xC7); emit8(cb, 0x44); emit8(cb, 0x24); emit8(cb, 0x20); emit32(cb, 0); // mov qword ptr [rsp+32], 0
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0xC7); emit8(cb, 0x44); emit8(cb, 0x24); emit8(cb, 0x18); emit32(cb, 1); // mov qword ptr [rsp+24], 1
        uint32_t lf_ws = (uint32_t)cb->size;
        emit8(cb, 0xE8); emit32(cb, g_read_char_offset - ((uint32_t)cb->size + 4)); // call __read_char
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x85); emit8(cb, 0xC0); // test rax, rax
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t lf_fail_patch1 = (uint32_t)cb->size; emit32(cb, 0); // jz .Llf_fail
        emit8(cb, 0x3C); emit8(cb, 0x20); // cmp al, ' '
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, lf_ws - ((uint32_t)cb->size + 4)); // je .Llf_ws
        emit8(cb, 0x3C); emit8(cb, 0x0A); // cmp al, '\n'
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, lf_ws - ((uint32_t)cb->size + 4)); // je .Llf_ws
        emit8(cb, 0x3C); emit8(cb, 0x0D); // cmp al, '\r'
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, lf_ws - ((uint32_t)cb->size + 4)); // je .Llf_ws
        emit8(cb, 0x3C); emit8(cb, 0x09); // cmp al, '\t'
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, lf_ws - ((uint32_t)cb->size + 4)); // je .Llf_ws
        emit8(cb, 0x3C); emit8(cb, 0x2D); // cmp al, '-'
        emit8(cb, 0x0F); emit8(cb, 0x85); uint32_t lf_chk_plus_patch = (uint32_t)cb->size; emit32(cb, 0); // jne .Llf_chk_plus
        emit_rex(cb, 1, 0, 0, 1); emit8(cb, 0xC7); emit8(cb, 0xC5); emit32(cb, 1); // mov r13, 1
        emit8(cb, 0xE9); uint32_t lf_next_patch1 = (uint32_t)cb->size; emit32(cb, 0); // jmp .Llf_next
        uint32_t lf_chk_plus = (uint32_t)cb->size;
        memcpy(cb->buffer + lf_chk_plus_patch, &(int32_t){lf_chk_plus - (lf_chk_plus_patch + 4)}, 4);
        emit8(cb, 0x3C); emit8(cb, 0x2B); // cmp al, '+'
        emit8(cb, 0x0F); emit8(cb, 0x85); uint32_t lf_digit_patch1 = (uint32_t)cb->size; emit32(cb, 0); // jne .Llf_digit
        uint32_t lf_next = (uint32_t)cb->size;
        memcpy(cb->buffer + lf_next_patch1, &(int32_t){lf_next - (lf_next_patch1 + 4)}, 4);
        emit8(cb, 0xE8); emit32(cb, g_read_char_offset - ((uint32_t)cb->size + 4)); // call __read_char
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x85); emit8(cb, 0xC0); // test rax, rax
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t lf_done_patch1 = (uint32_t)cb->size; emit32(cb, 0); // jz .Llf_done
        uint32_t lf_digit = (uint32_t)cb->size;
        memcpy(cb->buffer + lf_digit_patch1, &(int32_t){lf_digit - (lf_digit_patch1 + 4)}, 4);
        emit8(cb, 0x3C); emit8(cb, 0x2E); // cmp al, '.'
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t lf_frac_next_patch1 = (uint32_t)cb->size; emit32(cb, 0); // je .Llf_frac_next
        emit8(cb, 0x3C); emit8(cb, 0x30); // cmp al, '0'
        emit8(cb, 0x0F); emit8(cb, 0x82); uint32_t lf_done_patch2 = (uint32_t)cb->size; emit32(cb, 0); // jb .Llf_done
        emit8(cb, 0x3C); emit8(cb, 0x39); // cmp al, '9'
        emit8(cb, 0x0F); emit8(cb, 0x87); uint32_t lf_done_patch3 = (uint32_t)cb->size; emit32(cb, 0); // ja .Llf_done
        emit8(cb, 0x2C); emit8(cb, 0x30); // sub al, '0'
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x0F); emit8(cb, 0xB6); emit8(cb, 0xC0); // movzx rax, al
        emit_rex(cb, 1, 1, 0, 1); emit8(cb, 0x6B); emit8(cb, 0xF6); emit8(cb, 0x0A); // imul r14, r14, 10
        emit_rex(cb, 1, 0, 0, 1); emit8(cb, 0x01); emit8(cb, 0xC6); // add r14, rax
        emit8(cb, 0xE9); emit32(cb, lf_next - ((uint32_t)cb->size + 4)); // jmp .Llf_next
        uint32_t lf_frac_next = (uint32_t)cb->size;
        memcpy(cb->buffer + lf_frac_next_patch1, &(int32_t){lf_frac_next - (lf_frac_next_patch1 + 4)}, 4);
        emit8(cb, 0xE8); emit32(cb, g_read_char_offset - ((uint32_t)cb->size + 4)); // call __read_char
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x85); emit8(cb, 0xC0); // test rax, rax
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t lf_done_patch4 = (uint32_t)cb->size; emit32(cb, 0); // jz .Llf_done
        emit8(cb, 0x3C); emit8(cb, 0x30); // cmp al, '0'
        emit8(cb, 0x0F); emit8(cb, 0x82); uint32_t lf_done_patch5 = (uint32_t)cb->size; emit32(cb, 0); // jb .Llf_done
        emit8(cb, 0x3C); emit8(cb, 0x39); // cmp al, '9'
        emit8(cb, 0x0F); emit8(cb, 0x87); uint32_t lf_done_patch6 = (uint32_t)cb->size; emit32(cb, 0); // ja .Llf_done
        emit8(cb, 0x2C); emit8(cb, 0x30); // sub al, '0'
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x0F); emit8(cb, 0xB6); emit8(cb, 0xC0); // movzx rax, al
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x8B); emit8(cb, 0x4C); emit8(cb, 0x24); emit8(cb, 0x20); // mov rcx, [rsp+32]
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x6B); emit8(cb, 0xC9); emit8(cb, 0x0A); // imul rcx, rcx, 10
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x01); emit8(cb, 0xC1); // add rcx, rax
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x89); emit8(cb, 0x4C); emit8(cb, 0x24); emit8(cb, 0x20); // mov [rsp+32], rcx
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x8B); emit8(cb, 0x4C); emit8(cb, 0x24); emit8(cb, 0x18); // mov rcx, [rsp+24]
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x6B); emit8(cb, 0xC9); emit8(cb, 0x0A); // imul rcx, rcx, 10
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x89); emit8(cb, 0x4C); emit8(cb, 0x24); emit8(cb, 0x18); // mov [rsp+24], rcx
        emit8(cb, 0xE9); emit32(cb, lf_frac_next - ((uint32_t)cb->size + 4)); // jmp .Llf_frac_next
        uint32_t lf_done = (uint32_t)cb->size;
        memcpy(cb->buffer + lf_done_patch1, &(int32_t){lf_done - (lf_done_patch1 + 4)}, 4);
        memcpy(cb->buffer + lf_done_patch2, &(int32_t){lf_done - (lf_done_patch2 + 4)}, 4);
        memcpy(cb->buffer + lf_done_patch3, &(int32_t){lf_done - (lf_done_patch3 + 4)}, 4);
        memcpy(cb->buffer + lf_done_patch4, &(int32_t){lf_done - (lf_done_patch4 + 4)}, 4);
        memcpy(cb->buffer + lf_done_patch5, &(int32_t){lf_done - (lf_done_patch5 + 4)}, 4);
        memcpy(cb->buffer + lf_done_patch6, &(int32_t){lf_done - (lf_done_patch6 + 4)}, 4);
        emit8(cb, 0xF2); emit_rex(cb, 1, 0, 0, 1); emit8(cb, 0x0F); emit8(cb, 0x2A); emit8(cb, 0xC6); // cvtsi2sd xmm0, r14
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x8B); emit8(cb, 0x44); emit8(cb, 0x24); emit8(cb, 0x20); // mov rax, [rsp+32]
        emit8(cb, 0xF2); emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x0F); emit8(cb, 0x2A); emit8(cb, 0xC8); // cvtsi2sd xmm1, rax
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x8B); emit8(cb, 0x44); emit8(cb, 0x24); emit8(cb, 0x18); // mov rax, [rsp+24]
        emit8(cb, 0xF2); emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x0F); emit8(cb, 0x2A); emit8(cb, 0xD0); // cvtsi2sd xmm2, rax
        emit8(cb, 0xF2); emit8(cb, 0x0F); emit8(cb, 0x5E); emit8(cb, 0xCA); // divsd xmm1, xmm2
        emit8(cb, 0xF2); emit8(cb, 0x0F); emit8(cb, 0x58); emit8(cb, 0xC1); // addsd xmm0, xmm1
        emit_rex(cb, 1, 1, 0, 1); emit8(cb, 0x85); emit8(cb, 0xED); // test r13, r13
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t lf_store_patch = (uint32_t)cb->size; emit32(cb, 0); // jz .Llf_store
        emit8(cb, 0x66); emit8(cb, 0x0F); emit8(cb, 0x57); emit8(cb, 0xC9); // xorpd xmm1, xmm1
        emit8(cb, 0xF2); emit8(cb, 0x0F); emit8(cb, 0x5C); emit8(cb, 0xC8); // subsd xmm1, xmm0
        emit8(cb, 0x66); emit8(cb, 0x0F); emit8(cb, 0x28); emit8(cb, 0xC1); // movapd xmm0, xmm1
        uint32_t lf_store = (uint32_t)cb->size;
        memcpy(cb->buffer + lf_store_patch, &(int32_t){lf_store - (lf_store_patch + 4)}, 4);
        emit_rex(cb, 1, 0, 0, 1); emit8(cb, 0x83); emit8(cb, 0xFC); emit8(cb, 0x04); // cmp r12, 4
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t lf_s4_patch = (uint32_t)cb->size; emit32(cb, 0); // je .Llf_s4
        emit8(cb, 0xF2); emit8(cb, 0x0F); emit8(cb, 0x11); emit8(cb, 0x03); // movsd [rbx], xmm0
        emit8(cb, 0xE9); uint32_t lf_ok_patch1 = (uint32_t)cb->size; emit32(cb, 0); // jmp .Llf_ok
        uint32_t lf_s4 = (uint32_t)cb->size;
        memcpy(cb->buffer + lf_s4_patch, &(int32_t){lf_s4 - (lf_s4_patch + 4)}, 4);
        emit8(cb, 0xF2); emit8(cb, 0x0F); emit8(cb, 0x5A); emit8(cb, 0xC0); // cvtsd2ss xmm0, xmm0
        emit8(cb, 0xF3); emit8(cb, 0x0F); emit8(cb, 0x11); emit8(cb, 0x03); // movss [rbx], xmm0
        uint32_t lf_ok = (uint32_t)cb->size;
        memcpy(cb->buffer + lf_ok_patch1, &(int32_t){lf_ok - (lf_ok_patch1 + 4)}, 4);
        emit8(cb, 0xB8); emit32(cb, 1); // mov eax, 1
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xC4); emit8(cb, 0x28); // add rsp, 40
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x5E); // pop r14
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x5D); // pop r13
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x5C); // pop r12
        emit8(cb, 0x5B); // pop rbx
        emit8(cb, 0xC3); // ret
        uint32_t lf_fail = (uint32_t)cb->size;
        memcpy(cb->buffer + lf_fail_patch1, &(int32_t){lf_fail - (lf_fail_patch1 + 4)}, 4);
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x31); emit8(cb, 0xC0); // xor rax, rax
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xC4); emit8(cb, 0x28); // add rsp, 40
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x5E); // pop r14
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x5D); // pop r13
        emit_rex(cb, 0, 0, 0, 1); emit8(cb, 0x5C); // pop r12
        emit8(cb, 0x5B); // pop rbx
        emit8(cb, 0xC3); // ret

        // =========================================================
        // __lege_char
        // =========================================================
        g_lege_char_offset = (uint32_t)cb->size;
        emit8(cb, 0x53); // push rbx
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xEC); emit8(cb, 0x20); // sub rsp, 32
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x89); emit8(cb, 0xCB); // mov rbx, rcx
        uint32_t lc_ws = (uint32_t)cb->size;
        emit8(cb, 0xE8); emit32(cb, g_read_char_offset - ((uint32_t)cb->size + 4)); // call __read_char
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x85); emit8(cb, 0xC0); // test rax, rax
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t lc_fail_patch = (uint32_t)cb->size; emit32(cb, 0); // jz .Llc_fail
        emit8(cb, 0x3C); emit8(cb, 0x20); // cmp al, ' '
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, lc_ws - ((uint32_t)cb->size + 4)); // je .Llc_ws
        emit8(cb, 0x3C); emit8(cb, 0x0A); // cmp al, '\n'
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, lc_ws - ((uint32_t)cb->size + 4)); // je .Llc_ws
        emit8(cb, 0x3C); emit8(cb, 0x0D); // cmp al, '\r'
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, lc_ws - ((uint32_t)cb->size + 4)); // je .Llc_ws
        emit8(cb, 0x3C); emit8(cb, 0x09); // cmp al, '\t'
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, lc_ws - ((uint32_t)cb->size + 4)); // je .Llc_ws
        emit8(cb, 0x88); emit8(cb, 0x03); // mov [rbx], al
        emit8(cb, 0xB8); emit32(cb, 1); // mov eax, 1
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xC4); emit8(cb, 0x20); // add rsp, 32
        emit8(cb, 0x5B); // pop rbx
        emit8(cb, 0xC3); // ret
        uint32_t lc_fail = (uint32_t)cb->size;
        memcpy(cb->buffer + lc_fail_patch, &(int32_t){lc_fail - (lc_fail_patch + 4)}, 4);
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x31); emit8(cb, 0xC0); // xor rax, rax
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xC4); emit8(cb, 0x20); // add rsp, 32
        emit8(cb, 0x5B); // pop rbx
        emit8(cb, 0xC3); // ret

        // =========================================================
        // __lege_bool
        // =========================================================
        g_lege_bool_offset = (uint32_t)cb->size;
        emit8(cb, 0x53); // push rbx
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xEC); emit8(cb, 0x20); // sub rsp, 32
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x89); emit8(cb, 0xCB); // mov rbx, rcx
        uint32_t lb_ws = (uint32_t)cb->size;
        emit8(cb, 0xE8); emit32(cb, g_read_char_offset - ((uint32_t)cb->size + 4)); // call __read_char
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x85); emit8(cb, 0xC0); // test rax, rax
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t lb_fail_patch = (uint32_t)cb->size; emit32(cb, 0); // jz .Llb_fail
        emit8(cb, 0x3C); emit8(cb, 0x20); // cmp al, ' '
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, lb_ws - ((uint32_t)cb->size + 4)); // je .Llb_ws
        emit8(cb, 0x3C); emit8(cb, 0x0A); // cmp al, '\n'
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, lb_ws - ((uint32_t)cb->size + 4)); // je .Llb_ws
        emit8(cb, 0x3C); emit8(cb, 0x0D); // cmp al, '\r'
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, lb_ws - ((uint32_t)cb->size + 4)); // je .Llb_ws
        emit8(cb, 0x3C); emit8(cb, 0x09); // cmp al, '\t'
        emit8(cb, 0x0F); emit8(cb, 0x84); emit32(cb, lb_ws - ((uint32_t)cb->size + 4)); // je .Llb_ws
        emit8(cb, 0x3C); emit8(cb, 0x31); // cmp al, '1'
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t lb_true_patch1 = (uint32_t)cb->size; emit32(cb, 0); // je .Llb_true
        emit8(cb, 0x3C); emit8(cb, 0x76); // cmp al, 'v'
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t lb_true_patch2 = (uint32_t)cb->size; emit32(cb, 0); // je .Llb_true
        emit8(cb, 0x3C); emit8(cb, 0x56); // cmp al, 'V'
        emit8(cb, 0x0F); emit8(cb, 0x84); uint32_t lb_true_patch3 = (uint32_t)cb->size; emit32(cb, 0); // je .Llb_true
        emit8(cb, 0xC6); emit8(cb, 0x03); emit8(cb, 0x00); // mov byte ptr [rbx], 0
        emit8(cb, 0xE9); uint32_t lb_ok_patch = (uint32_t)cb->size; emit32(cb, 0); // jmp .Llb_ok
        uint32_t lb_true = (uint32_t)cb->size;
        memcpy(cb->buffer + lb_true_patch1, &(int32_t){lb_true - (lb_true_patch1 + 4)}, 4);
        memcpy(cb->buffer + lb_true_patch2, &(int32_t){lb_true - (lb_true_patch2 + 4)}, 4);
        memcpy(cb->buffer + lb_true_patch3, &(int32_t){lb_true - (lb_true_patch3 + 4)}, 4);
        emit8(cb, 0xC6); emit8(cb, 0x03); emit8(cb, 0x01); // mov byte ptr [rbx], 1
        uint32_t lb_ok = (uint32_t)cb->size;
        memcpy(cb->buffer + lb_ok_patch, &(int32_t){lb_ok - (lb_ok_patch + 4)}, 4);
        emit8(cb, 0xB8); emit32(cb, 1); // mov eax, 1
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xC4); emit8(cb, 0x20); // add rsp, 32
        emit8(cb, 0x5B); // pop rbx
        emit8(cb, 0xC3); // ret
        uint32_t lb_fail = (uint32_t)cb->size;
        memcpy(cb->buffer + lb_fail_patch, &(int32_t){lb_fail - (lb_fail_patch + 4)}, 4);
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x31); emit8(cb, 0xC0); // xor rax, rax
        emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x83); emit8(cb, 0xC4); emit8(cb, 0x20); // add rsp, 32
        emit8(cb, 0x5B); // pop rbx
        emit8(cb, 0xC3); // ret
    }

    // 追加内置汇编例程: _start (真正的入口点)
    linker->entry_point_offset = (uint32_t)linker->text_section.size;
    // sub rsp, 40 (32 bytes shadow space + 8 bytes alignment)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x83); emit8(&linker->text_section, 0xEC); emit8(&linker->text_section, 0x28);
    // mov [rsp+48], rcx (保存 argc 到 Caller 的 Shadow Space)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0x4C); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x30);
    // mov [rsp+56], rdx (保存 argv 到 Caller 的 Shadow Space)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0x54); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x38);
    
    // call __scoria_init
    emit8(&linker->text_section, 0xE8);
    int32_t rel_init = (int32_t)(init_offset - (linker->text_section.size + 4));
    emit32(&linker->text_section, (uint32_t)rel_init);
    
    // mov rcx, [rsp+48] (恢复 argc)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x8B); emit8(&linker->text_section, 0x4C); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x30);
    // mov rdx, [rsp+56] (恢复 argv)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x8B); emit8(&linker->text_section, 0x54); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x38);
    
    // call princeps
    emit8(&linker->text_section, 0xE8);
    int32_t rel_princeps = (int32_t)(princeps_offset - (linker->text_section.size + 4));
    emit32(&linker->text_section, (uint32_t)rel_princeps);
    
    // mov rcx, rax (exit code)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xC1);
    // call [rip + IAT_ExitProcess]
    emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x15);
    g_call_exitprocess_reloc = (uint32_t)linker->text_section.size;
    emit32(&linker->text_section, 0);
}
