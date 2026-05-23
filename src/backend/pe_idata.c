#include "pe_idata.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* func_name;
    uint32_t iat_offset; // 在 rdata_section 中的偏移
} ImportFunc;

typedef struct {
    char* dll_name;
    ImportFunc* funcs;
    int func_count;
    int func_capacity;
} ImportDll;

struct PeImportTable {
    ImportDll* dlls;
    int dll_count;
    int dll_capacity;
};

PeImportTable* pe_idata_create(void) {
    PeImportTable* it = (PeImportTable*)calloc(1, sizeof(PeImportTable));
    it->dll_capacity = 4;
    it->dlls = (ImportDll*)calloc(it->dll_capacity, sizeof(ImportDll));
    return it;
}

void pe_idata_free(PeImportTable* it) {
    if (!it) return;
    for (int i = 0; i < it->dll_count; i++) {
        for (int j = 0; j < it->dlls[i].func_count; j++) {
            free(it->dlls[i].funcs[j].func_name);
        }
        free(it->dlls[i].funcs);
        free(it->dlls[i].dll_name);
    }
    free(it->dlls);
    free(it);
}

void pe_idata_add_import(PeImportTable* it, const char* dll_name, const char* func_name) {
    ImportDll* target_dll = NULL;
    for (int i = 0; i < it->dll_count; i++) {
        if (_stricmp(it->dlls[i].dll_name, dll_name) == 0) {
            target_dll = &it->dlls[i];
            break;
        }
    }

    if (!target_dll) {
        if (it->dll_count >= it->dll_capacity) {
            it->dll_capacity *= 2;
            it->dlls = (ImportDll*)realloc(it->dlls, it->dll_capacity * sizeof(ImportDll));
        }
        target_dll = &it->dlls[it->dll_count++];
        target_dll->dll_name = _strdup(dll_name);
        target_dll->func_capacity = 8;
        target_dll->func_count = 0;
        target_dll->funcs = (ImportFunc*)calloc(target_dll->func_capacity, sizeof(ImportFunc));
    }

    for (int i = 0; i < target_dll->func_count; i++) {
        if (strcmp(target_dll->funcs[i].func_name, func_name) == 0) {
            return; // 已存在
        }
    }

    if (target_dll->func_count >= target_dll->func_capacity) {
        target_dll->func_capacity *= 2;
        target_dll->funcs = (ImportFunc*)realloc(target_dll->funcs, target_dll->func_capacity * sizeof(ImportFunc));
    }

    target_dll->funcs[target_dll->func_count].func_name = _strdup(func_name);
    target_dll->funcs[target_dll->func_count].iat_offset = 0;
    target_dll->func_count++;
}

static void append_zeros(PeCodeBuffer* cb, int count) {
    for (int i = 0; i < count; i++) emit8(cb, 0);
}

static void align_buffer(PeCodeBuffer* cb, int alignment) {
    while (cb->size % alignment != 0) emit8(cb, 0);
}

void pe_idata_build(PeImportTable* it, PeCodeBuffer* rdata, uint32_t rdata_rva, 
                    uint32_t* out_import_dir_offset, uint32_t* out_import_dir_size,
                    uint32_t* out_iat_rva, uint32_t* out_iat_size) {
    if (it->dll_count == 0) {
        *out_import_dir_offset = 0;
        *out_import_dir_size = 0;
        *out_iat_rva = 0;
        *out_iat_size = 0;
        return;
    }

    align_buffer(rdata, 8);

    // 1. 分配 IAT (Import Address Table)
    uint32_t iat_start_offset = (uint32_t)rdata->size;
    for (int i = 0; i < it->dll_count; i++) {
        for (int j = 0; j < it->dlls[i].func_count; j++) {
            it->dlls[i].funcs[j].iat_offset = (uint32_t)rdata->size;
            append_zeros(rdata, 8); // 占位
        }
        append_zeros(rdata, 8); // NULL 结尾
    }
    uint32_t iat_end_offset = (uint32_t)rdata->size;

    // 2. 分配 INT (Import Name Table / OriginalFirstThunk)
    uint32_t* int_offsets = (uint32_t*)calloc(it->dll_count, sizeof(uint32_t));
    for (int i = 0; i < it->dll_count; i++) {
        int_offsets[i] = (uint32_t)rdata->size;
        for (int j = 0; j < it->dlls[i].func_count; j++) {
            append_zeros(rdata, 8); // 占位
        }
        append_zeros(rdata, 8); // NULL 结尾
    }

    // 3. 写入 Hint/Name 表
    uint32_t** hn_offsets = (uint32_t**)calloc(it->dll_count, sizeof(uint32_t*));
    for (int i = 0; i < it->dll_count; i++) {
        hn_offsets[i] = (uint32_t*)calloc(it->dlls[i].func_count, sizeof(uint32_t));
        for (int j = 0; j < it->dlls[i].func_count; j++) {
            hn_offsets[i][j] = (uint32_t)rdata->size;
            emit8(rdata, 0); emit8(rdata, 0); // Hint = 0
            const char* name = it->dlls[i].funcs[j].func_name;
            for (size_t k = 0; k <= strlen(name); k++) emit8(rdata, (uint8_t)name[k]);
            if (rdata->size % 2 != 0) emit8(rdata, 0); // 保持偶数对齐
        }
    }

    // 4. 写入 DLL 名称字符串
    uint32_t* dll_name_offsets = (uint32_t*)calloc(it->dll_count, sizeof(uint32_t));
    for (int i = 0; i < it->dll_count; i++) {
        dll_name_offsets[i] = (uint32_t)rdata->size;
        const char* name = it->dlls[i].dll_name;
        for (size_t k = 0; k <= strlen(name); k++) emit8(rdata, (uint8_t)name[k]);
    }

    align_buffer(rdata, 8);

    // 5. 写入 Import Directory Table
    uint32_t import_dir_offset = (uint32_t)rdata->size;
    for (int i = 0; i < it->dll_count; i++) {
        uint32_t original_first_thunk = rdata_rva + int_offsets[i];
        uint32_t time_date_stamp = 0;
        uint32_t forwarder_chain = 0;
        uint32_t name_rva = rdata_rva + dll_name_offsets[i];
        uint32_t first_thunk = rdata_rva + it->dlls[i].funcs[0].iat_offset;

        emit32(rdata, original_first_thunk);
        emit32(rdata, time_date_stamp);
        emit32(rdata, forwarder_chain);
        emit32(rdata, name_rva);
        emit32(rdata, first_thunk);
    }
    // 空的结尾描述符 (20 字节)
    for (int i = 0; i < 5; i++) emit32(rdata, 0);
    uint32_t import_dir_size = (uint32_t)rdata->size - import_dir_offset;

    // 6. 回填 IAT 和 INT
    for (int i = 0; i < it->dll_count; i++) {
        for (int j = 0; j < it->dlls[i].func_count; j++) {
            uint64_t hn_rva = rdata_rva + hn_offsets[i][j];
            
            uint32_t iat_off = it->dlls[i].funcs[j].iat_offset;
            memcpy(rdata->buffer + iat_off, &hn_rva, 8);
            
            uint32_t int_off = int_offsets[i] + j * 8;
            memcpy(rdata->buffer + int_off, &hn_rva, 8);
        }
    }

    free(int_offsets);
    for (int i = 0; i < it->dll_count; i++) free(hn_offsets[i]);
    free(hn_offsets);
    free(dll_name_offsets);

    *out_import_dir_offset = import_dir_offset;
    *out_import_dir_size = import_dir_size;
    *out_iat_rva = rdata_rva + iat_start_offset;
    *out_iat_size = iat_end_offset - iat_start_offset;
}

uint32_t pe_idata_get_iat_offset(PeImportTable* it, const char* dll_name, const char* func_name) {
    for (int i = 0; i < it->dll_count; i++) {
        if (_stricmp(it->dlls[i].dll_name, dll_name) == 0) {
            for (int j = 0; j < it->dlls[i].func_count; j++) {
                if (strcmp(it->dlls[i].funcs[j].func_name, func_name) == 0) {
                    return it->dlls[i].funcs[j].iat_offset;
                }
            }
        }
    }
    return 0;
}
