#include "builtins.h"

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
    fprintf(out, "__print_str:\n");
    fprintf(out, "    testq %%rdx, %%rdx\n");
    fprintf(out, "    jnz .Lprint_str_len_ok\n");
    fprintf(out, "    movq %%rcx, %%rax\n");
    fprintf(out, ".Lprint_str_len_loop:\n");
    fprintf(out, "    cmpb $0, (%%rax)\n");
    fprintf(out, "    jz .Lprint_str_len_done\n");
    fprintf(out, "    incq %%rax\n");
    fprintf(out, "    jmp .Lprint_str_len_loop\n");
    fprintf(out, ".Lprint_str_len_done:\n");
    fprintf(out, "    subq %%rcx, %%rax\n");
    fprintf(out, "    movq %%rax, %%rdx\n");
    fprintf(out, ".Lprint_str_len_ok:\n");
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
    fprintf(out, "    ret\n\n");

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

    fprintf(out, "    .section .rdata,\"a\"\n");
    fprintf(out, ".Lstr_minus:\n    .byte 45, 0\n");
    fprintf(out, ".Lstr_dot:\n    .byte 46, 0\n");
    fprintf(out, "    .align 8\n");
    fprintf(out, ".Lfloat_10:\n    .quad 4621819117588971520\n"); // 10.0 in IEEE 754 double
    fprintf(out, "    .text\n\n");

    fprintf(out, "    .globl main\n");
    fprintf(out, "main:\n");
    fprintf(out, "    pushq %%rbp\n");
    fprintf(out, "    movq %%rsp, %%rbp\n");
    fprintf(out, "    subq $32, %%rsp\n");
    fprintf(out, "    call __scoria_init\n");
    fprintf(out, "    call princeps\n");
    fprintf(out, "    addq $32, %%rsp\n");
    fprintf(out, "    popq %%rbp\n");
    fprintf(out, "    ret\n\n");
}

void pe_builtins_generate(PeLinker* linker, uint32_t princeps_offset, uint32_t init_offset) {
    // 追加内置汇编例程: __print_str
    g_print_str_offset = (uint32_t)linker->text_section.size;
    
    // test rdx, rdx
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x85); emit8(&linker->text_section, 0xD2);
    // jnz .Lprint_str_len_ok (偏移量稍后计算)
    emit8(&linker->text_section, 0x75); emit8(&linker->text_section, 0x0E); // 14 bytes forward
    
    // mov rax, rcx
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xC8);
    // .Lprint_str_len_loop:
    // cmp byte ptr [rax], 0
    emit8(&linker->text_section, 0x80); emit8(&linker->text_section, 0x38); emit8(&linker->text_section, 0x00);
    // jz .Lprint_str_len_done
    emit8(&linker->text_section, 0x74); emit8(&linker->text_section, 0x05); // 5 bytes forward
    // inc rax
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0xC0);
    // jmp .Lprint_str_len_loop
    emit8(&linker->text_section, 0xEB); emit8(&linker->text_section, 0xF4); // -12 bytes backward
    // .Lprint_str_len_done:
    // sub rax, rcx
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x2B); emit8(&linker->text_section, 0xC1);
    // mov rdx, rax
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xC2);
    
    // .Lprint_str_len_ok:
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

    // 追加内置汇编例程: __print_int
    g_print_int_offset = (uint32_t)linker->text_section.size;
    uint8_t print_int_code[] = {
        0x48, 0x83, 0xEC, 0x68, 0x49, 0x89, 0xCA, 0x4D, 0x31, 0xDB, 0x48, 0x85, 0xC9, 0x79, 0x0A, 0x49,
        0xF7, 0xDA, 0x49, 0xC7, 0xC3, 0x01, 0x00, 0x00, 0x00, 0x4C, 0x8D, 0x44, 0x24, 0x4F, 0x41, 0xC6,
        0x00, 0x0A, 0x49, 0xFF, 0xC8, 0x4C, 0x89, 0xD0, 0x49, 0xC7, 0xC1, 0x0A, 0x00, 0x00, 0x00, 0x48,
        0x31, 0xD2, 0x49, 0xF7, 0xF1, 0x80, 0xC2, 0x30, 0x41, 0x88, 0x10, 0x49, 0xFF, 0xC8, 0x48, 0x85,
        0xC0, 0x75, 0xEC, 0x4D, 0x85, 0xDB, 0x74, 0x07, 0x41, 0xC6, 0x00, 0x2D, 0x49, 0xFF, 0xC8, 0x49,
        0xFF, 0xC0, 0x4C, 0x8D, 0x54, 0x24, 0x50, 0x4D, 0x29, 0xC2, 0x4C, 0x89, 0x44, 0x24, 0x58, 0x4C,
        0x89, 0x54, 0x24, 0x60, 0xB9, 0xF5, 0xFF, 0xFF, 0xFF, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x48,
        0x89, 0xC1, 0x48, 0x8B, 0x54, 0x24, 0x58, 0x4C, 0x8B, 0x44, 0x24, 0x60, 0x4C, 0x8D, 0x4C, 0x24,
        0x28, 0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x83, 0xC4, 0x68, 0xC3
    };
    for (size_t i = 0; i < sizeof(print_int_code); i++) emit8(&linker->text_section, print_int_code[i]);
    g_call_getstdhandle_reloc2 = g_print_int_offset + 107;
    g_call_writeconsolea_reloc2 = g_print_int_offset + 140;

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
    
    // 追加内置汇编例程: __print_bool
    g_print_bool_offset = (uint32_t)linker->text_section.size;
    uint8_t print_bool_code[] = {
        0x48, 0x85, 0xC9, 0x74, 0x0E, 0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xBA, 0x05, 0x00, 0x00,
        0x00, 0xEB, 0x0C, 0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xBA, 0x06, 0x00, 0x00, 0x00, 0xE8,
        0x00, 0x00, 0x00, 0x00, 0xC3
    };
    for (size_t i = 0; i < sizeof(print_bool_code); i++) emit8(&linker->text_section, print_bool_code[i]);

    // 追加内置汇编例程: __crea
    g_crea_offset = (uint32_t)linker->text_section.size;
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x83); emit8(&linker->text_section, 0xEC); emit8(&linker->text_section, 0x28); // sub rsp, 40
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0x4C); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x30); // mov [rsp+48], rcx
    emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x15); // call GetProcessHeap
    g_call_getprocessheap_reloc1 = (uint32_t)linker->text_section.size;
    emit32(&linker->text_section, 0);
    emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x8B); emit8(&linker->text_section, 0x44); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x30); // mov r8, [rsp+48]
    emit_mov_reg_imm32(&linker->text_section, REG_RDX, 8); // mov rdx, 8 (HEAP_ZERO_MEMORY)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xC1); // mov rcx, rax
    emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x15); // call HeapAlloc
    g_call_heapalloc_reloc = (uint32_t)linker->text_section.size;
    emit32(&linker->text_section, 0);
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x83); emit8(&linker->text_section, 0xC4); emit8(&linker->text_section, 0x28); // add rsp, 40
    emit8(&linker->text_section, 0xC3); // ret

    // 追加内置汇编例程: __neca
    g_neca_offset = (uint32_t)linker->text_section.size;
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x83); emit8(&linker->text_section, 0xEC); emit8(&linker->text_section, 0x28); // sub rsp, 40
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0x4C); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x30); // mov [rsp+48], rcx
    emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x15); // call GetProcessHeap
    g_call_getprocessheap_reloc2 = (uint32_t)linker->text_section.size;
    emit32(&linker->text_section, 0);
    emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x8B); emit8(&linker->text_section, 0x44); emit8(&linker->text_section, 0x24); emit8(&linker->text_section, 0x30); // mov r8, [rsp+48]
    emit_mov_reg_imm32(&linker->text_section, REG_RDX, 0); // mov rdx, 0
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xC1); // mov rcx, rax
    emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x15); // call HeapFree
    g_call_heapfree_reloc = (uint32_t)linker->text_section.size;
    emit32(&linker->text_section, 0);
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x83); emit8(&linker->text_section, 0xC4); emit8(&linker->text_section, 0x28); // add rsp, 40
    emit8(&linker->text_section, 0xC3); // ret

    // 追加内置汇编例程: _start (真正的入口点)
    linker->entry_point_offset = (uint32_t)linker->text_section.size;
    // sub rsp, 40 (32 bytes shadow space + 8 bytes alignment)
    emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x83); emit8(&linker->text_section, 0xEC); emit8(&linker->text_section, 0x28);
    // call __scoria_init
    emit8(&linker->text_section, 0xE8);
    int32_t rel_init = (int32_t)(init_offset - (linker->text_section.size + 4));
    emit32(&linker->text_section, (uint32_t)rel_init);
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
