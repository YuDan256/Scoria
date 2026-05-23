#ifndef SCORIA_PE_LINKER_H
#define SCORIA_PE_LINKER_H

#include "../ir/sir.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// 机器码缓冲区 (Machine Code Buffer)
typedef struct {
    uint8_t* buffer;
    size_t capacity;
    size_t size;
} PeCodeBuffer;

typedef struct {
    PeCodeBuffer text_section;  // .text 段 (存放机器码)
    PeCodeBuffer rdata_section; // .rdata 段 (存放只读数据，如字符串)
    uint32_t entry_point_offset; // 入口函数 (princeps) 在 .text 段中的偏移
} PeLinker;

#define REG_RAX 0
#define REG_RCX 1
#define REG_RDX 2
#define REG_RBX 3
#define REG_RSP 4
#define REG_RBP 5
#define REG_RSI 6
#define REG_RDI 7
#define REG_R8 8
#define REG_R9 9
#define REG_R10 10
#define REG_R11 11
#define REG_R12 12
#define REG_R13 13
#define REG_R14 14
#define REG_R15 15

void emit8(PeCodeBuffer* cb, uint8_t b);
void emit32(PeCodeBuffer* cb, uint32_t v);
void emit_rex(PeCodeBuffer* cb, int w, int r, int x, int b);
void emit_modrm(PeCodeBuffer* cb, int mod, int reg, int rm);
void emit_mov_reg_imm32(PeCodeBuffer* cb, int reg, int32_t imm);
void emit_mov_reg_imm64(PeCodeBuffer* cb, int reg, uint64_t imm);

void pe_linker_init(PeLinker* linker);
void pe_linker_free(PeLinker* linker);

// 将 SIR 模块直接编译并链接为 Windows PE 可执行文件 (.exe)
bool pe_linker_generate_executable(PeLinker* linker, SirModule* module, const char* output_filename);

#endif // SCORIA_PE_LINKER_H
