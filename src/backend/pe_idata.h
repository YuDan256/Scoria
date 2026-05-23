#ifndef SCORIA_PE_IDATA_H
#define SCORIA_PE_IDATA_H

#include <stdint.h>
#include "pe_linker.h"

typedef struct PeImportTable PeImportTable;

PeImportTable* pe_idata_create(void);
void pe_idata_free(PeImportTable* it);

// 添加一个导入函数。如果 DLL 或函数已存在，则不会重复添加。
void pe_idata_add_import(PeImportTable* it, const char* dll_name, const char* func_name);

// 构建导入表并追加到 rdata_section 中。
// rdata_rva 是 rdata_section 在内存中的起始 RVA。
// 返回 Import Directory Table 的偏移量和大小，以及 IAT 的 RVA 和大小。
void pe_idata_build(PeImportTable* it, PeCodeBuffer* rdata_section, uint32_t rdata_rva, 
                    uint32_t* out_import_dir_offset, uint32_t* out_import_dir_size,
                    uint32_t* out_iat_rva, uint32_t* out_iat_size);

// 获取指定函数在 rdata_section 中的 IAT 条目偏移量 (用于 call [rip + rel32] 重定位)
uint32_t pe_idata_get_iat_offset(PeImportTable* it, const char* dll_name, const char* func_name);

#endif // SCORIA_PE_IDATA_H
