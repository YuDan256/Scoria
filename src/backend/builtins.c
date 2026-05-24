#include "builtins.h"
#include <string.h>

bool g_use_print_str = false;
bool g_use_print_int = false;
bool g_use_print_float = false;
bool g_use_print_bool = false;
bool g_use_print_hex = false;
bool g_use_crea = false;
bool g_use_neca = false;

void builtins_analyze_usage(SirModule* module) {
    g_use_print_str = false;
    g_use_print_int = false;
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
                        ScoriaType* arg_type = inst->operands[1]->type;
                        if (arg_type && arg_type->kind == TY_VIA) arg_type = arg_type->as.inner;
                        bool is_str = (arg_type && arg_type->kind == TY_COHORS && arg_type->as.inner->kind == TY_LITTERA);
                        bool is_bool = (arg_type && arg_type->kind == TY_LOGICA) || (inst->operands[1]->kind == SIR_VAL_CONST_BOOL);
                        bool is_ptr = !is_str && (arg_type && (arg_type->kind == TY_VIA || arg_type->kind == TY_COHORS || arg_type->kind == TY_ACIES));
                        bool is_float = (arg_type && (arg_type->kind == TY_F32 || arg_type->kind == TY_F64)) || (inst->operands[1]->kind == SIR_VAL_CONST_FLOAT);

                        if (is_str) g_use_print_str = true;
                        else if (is_bool) g_use_print_bool = true;
                        else if (is_ptr) g_use_print_hex = true;
                        else if (is_float) g_use_print_float = true;
                        else g_use_print_int = true;
                    } else if (strcmp(callee, "crea") == 0) {
                        g_use_crea = true;
                    } else if (strcmp(callee, "neca") == 0) {
                        g_use_neca = true;
                    }
                }
            }
        }
    }

    // 解决内部依赖
    if (g_use_print_float) { g_use_print_int = true; g_use_print_str = true; }
    if (g_use_print_bool) { g_use_print_str = true; }
    if (g_use_print_hex) { g_use_print_str = true; }
}

uint32_t g_print_str_offset = 0;
uint32_t g_print_int_offset = 0;
uint32_t g_print_float_offset = 0;
uint32_t g_print_hex_offset = 0;
uint32_t g_print_bool_offset = 0;
uint32_t g_crea_offset = 0;
uint32_t g_neca_offset = 0;

uint32_t g_call_getstdhandle_reloc = 0;
uint32_t g_call_writeconsolea_reloc = 0;
uint32_t g_call_getstdhandle_reloc2 = 0;
uint32_t g_call_writeconsolea_reloc2 = 0;
uint32_t g_call_exitprocess_reloc = 0;

uint32_t g_call_getprocessheap_reloc1 = 0;
uint32_t g_call_getprocessheap_reloc2 = 0;
uint32_t g_call_heapalloc_reloc = 0;
uint32_t g_call_heapfree_reloc = 0;

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
    fprintf(out, "    leaq 47(%%rsp), %%r8\n");
    fprintf(out, "    movb $'x', (%%r8)\n");
    fprintf(out, "    decq %%r8\n");
    fprintf(out, "    movb $'0', (%%r8)\n");
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

    if (g_use_print_float || g_use_print_bool) {
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
        0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x4D, 0xF8, 0x48, 0x0F, 0xBA, 0xF8, 0x3F,
        0x48, 0x89, 0x4D, 0xF8, 0xF3, 0x0F, 0x7E, 0x45, 0xF8, 0xF2, 0x48, 0x0F, 0x2C, 0xC8, 0x48, 0x89,
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
        0x48, 0x83, 0xEC, 0x38, 0x49, 0x89, 0xCA, 0x4C, 0x8D, 0x44, 0x24, 0x2F, 0x41, 0xC6, 0x00, 0x78,
        0x49, 0xFF, 0xC8, 0x41, 0xC6, 0x00, 0x30, 0x4C, 0x8D, 0x44, 0x24, 0x30, 0x49, 0xC7, 0xC1, 0x10,
        0x00, 0x00, 0x00, 0x4C, 0x89, 0xD0, 0x48, 0x83, 0xE0, 0x0F, 0x3C, 0x09, 0x76, 0x04, 0x04, 0x57,
        0xEB, 0x02, 0x04, 0x30, 0x41, 0x88, 0x00, 0x49, 0xFF, 0xC8, 0x49, 0xC1, 0xEA, 0x04, 0x49, 0xFF,
        0xC9, 0x75, 0xE3, 0x48, 0x8D, 0x4C, 0x24, 0x1F, 0xBA, 0x12, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00,
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
    emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xD8); // mov r8, rbx
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
    emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xD8); // mov r8, rbx
    emit_mov_reg_imm32(&linker->text_section, REG_RDX, 0); // mov rdx, 0
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xC1); // mov rcx, rax
    emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x15); // call HeapFree
    g_call_heapfree_reloc = (uint32_t)linker->text_section.size;
    emit32(&linker->text_section, 0);
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x83); emit8(&linker->text_section, 0xC4); emit8(&linker->text_section, 0x20); // add rsp, 32
    emit8(&linker->text_section, 0x5B); // pop rbx
    emit8(&linker->text_section, 0xC3); // ret
    }

    // 追加内置汇编例程: _start (真正的入口点)
    linker->entry_point_offset = (uint32_t)linker->text_section.size;
    // sub rsp, 56 (32 bytes shadow space + 16 bytes local + 8 bytes alignment)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x83); emit8(&linker->text_section, 0xEC); emit8(&linker->text_section, 0x38);
    // mov [rsp+32], rcx (保存 argc 到安全的 local 区域)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0x4C); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x20);
    // mov [rsp+40], rdx (保存 argv 到安全的 local 区域)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0x54); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x28);
    
    // call __scoria_init
    emit8(&linker->text_section, 0xE8);
    int32_t rel_init = (int32_t)(init_offset - (linker->text_section.size + 4));
    emit32(&linker->text_section, (uint32_t)rel_init);
    
    // mov rcx, [rsp+32] (恢复 argc)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x8B); emit8(&linker->text_section, 0x4C); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x20);
    // mov rdx, [rsp+40] (恢复 argv)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x8B); emit8(&linker->text_section, 0x54); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x28);
    
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
