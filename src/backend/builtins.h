#ifndef SCORIA_BUILTINS_H
#define SCORIA_BUILTINS_H

#include <stdio.h>
#include <stdint.h>
#include "pe_linker.h"

extern bool g_use_print_str;
extern bool g_use_print_int;
extern bool g_use_print_uint;
extern bool g_use_print_char;
extern bool g_use_print_float;
extern bool g_use_print_bool;
extern bool g_use_print_hex;
extern bool g_use_crea;
extern bool g_use_neca;

typedef enum {
    PRINT_INT,
    PRINT_UINT,
    PRINT_FLOAT,
    PRINT_BOOL,
    PRINT_CHAR,
    PRINT_STR,
    PRINT_HEX
} PrintType;

PrintType builtins_get_print_type(SirValue* arg);

void builtins_analyze_usage(SirModule* module);

// 汇编后端内置例程
void asm_builtins_generate(FILE* out);

// PE 链接器后端内置例程
void pe_builtins_generate(PeLinker* linker, uint32_t princeps_offset, uint32_t init_offset);

// 共享的偏移量与重定位记录
extern uint32_t g_print_str_offset;
extern uint32_t g_print_int_offset;
extern uint32_t g_print_uint_offset;
extern uint32_t g_print_float_offset;
extern uint32_t g_print_hex_offset;
extern uint32_t g_print_bool_offset;
extern uint32_t g_crea_offset;
extern uint32_t g_neca_offset;

extern uint32_t g_call_getstdhandle_reloc;
extern uint32_t g_call_writeconsolea_reloc;
extern uint32_t g_call_getstdhandle_reloc2;
extern uint32_t g_call_writeconsolea_reloc2;
extern uint32_t g_call_getstdhandle_reloc3;
extern uint32_t g_call_writeconsolea_reloc3;
extern uint32_t g_call_exitprocess_reloc;

extern uint32_t g_call_getprocessheap_reloc1;
extern uint32_t g_call_getprocessheap_reloc2;
extern uint32_t g_call_heapalloc_reloc;
extern uint32_t g_call_heapfree_reloc;

#endif // SCORIA_BUILTINS_H
