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

void pe_linker_init(PeLinker* linker);
void pe_linker_free(PeLinker* linker);

// 将 SIR 模块直接编译并链接为 Windows PE 可执行文件 (.exe)
bool pe_linker_generate_executable(PeLinker* linker, SirModule* module, const char* output_filename);

#endif // SCORIA_PE_LINKER_H
