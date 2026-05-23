#ifndef SCORIA_BUILTINS_H
#define SCORIA_BUILTINS_H

#include <stdio.h>
#include <stdint.h>
#include "pe_linker.h"

// 汇编后端内置例程
void asm_builtins_generate(FILE* out);

// PE 链接器后端内置例程
void pe_builtins_generate(PeLinker* linker, uint32_t princeps_offset, uint32_t init_offset);

// 共享的偏移量与重定位记录
extern uint32_t g_print_str_offset;
extern uint32_t g_print_int_offset;
extern uint32_t g_print_float_offset;
extern uint32_t g_print_hex_offset;
extern uint32_t g_print_bool_offset;
extern uint32_t g_crea_offset;
extern uint32_t g_neca_offset;

extern uint32_t g_call_getstdhandle_reloc;
extern uint32_t g_call_writeconsolea_reloc;
extern uint32_t g_call_getstdhandle_reloc2;
extern uint32_t g_call_writeconsolea_reloc2;
extern uint32_t g_call_exitprocess_reloc;

extern uint32_t g_call_getprocessheap_reloc1;
extern uint32_t g_call_getprocessheap_reloc2;
extern uint32_t g_call_heapalloc_reloc;
extern uint32_t g_call_heapfree_reloc;

#endif // SCORIA_BUILTINS_H
