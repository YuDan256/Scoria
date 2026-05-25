#include "pe_linker.h"
#include "reg_alloc.h"
#include "builtins.h"
#include "pe_idata.h"
#include <stdlib.h>
#include <string.h>

// =========================================================
// Windows PE/COFF 结构定义 (强制 1 字节对齐)
// =========================================================
#pragma pack(push, 1)
typedef struct {
    uint16_t e_magic;      // "MZ"
    uint8_t  e_res[58];    // 填充
    uint32_t e_lfanew;     // 指向 PE 头的偏移
} DosHeader;

typedef struct {
    uint32_t Signature;            // "PE\0\0"
    uint16_t Machine;              // 0x8664 (x86_64)
    uint16_t NumberOfSections;     // 1
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader; // 240
    uint16_t Characteristics;      // 0x0022 (Executable | Large Address Aware)
} CoffHeader;

typedef struct {
    uint16_t Magic;                       // 0x020B (PE32+)
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;         // 入口点 RVA
    uint32_t BaseOfCode;
    uint64_t ImageBase;                   // 0x140000000
    uint32_t SectionAlignment;            // 0x1000
    uint32_t FileAlignment;               // 0x200
    uint16_t MajorOperatingSystemVersion; // 5
    uint16_t MinorOperatingSystemVersion; // 2
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;       // 5
    uint16_t MinorSubsystemVersion;       // 2
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;                   // 3 (Windows CUI 命令行程序)
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;         // 16
    struct {
        uint32_t VirtualAddress;
        uint32_t Size;
    } DataDirectory[16];                  // 数据目录表
} OptionalHeader64;

typedef struct {
    uint8_t  Name[8];              // ".text"
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;      // 0x60000020 (Code | Execute | Read)
} SectionHeader;

typedef struct {
    uint32_t OriginalFirstThunk;
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;
    uint32_t FirstThunk;
} ImageImportDescriptor;
#pragma pack(pop)

// =========================================================
// 缓冲区与链接器实现
// =========================================================
static void buf_init(PeCodeBuffer* cb) {
    cb->capacity = 4096;
    cb->size = 0;
    cb->buffer = (uint8_t*)malloc(cb->capacity);
}

static void buf_free(PeCodeBuffer* cb) {
    free(cb->buffer);
}

static void buf_append(PeCodeBuffer* cb, uint8_t byte) {
    if (cb->size >= cb->capacity) {
        cb->capacity *= 2;
        uint8_t* new_buf = (uint8_t*)realloc(cb->buffer, cb->capacity);
        if (!new_buf) {
            fprintf(stderr, "Fatal error: Out of memory in pe_linker\n");
            exit(1);
        }
        cb->buffer = new_buf;
    }
    cb->buffer[cb->size++] = byte;
}

void pe_linker_init(PeLinker* linker) {
    buf_init(&linker->text_section);
    buf_init(&linker->rdata_section);
    buf_init(&linker->data_section);
    linker->entry_point_offset = 0;
}

void pe_linker_free(PeLinker* linker) {
    buf_free(&linker->text_section);
    buf_free(&linker->rdata_section);
    buf_free(&linker->data_section);
}

static uint32_t align_up(uint32_t val, uint32_t alignment) {
    return (val + alignment - 1) & ~(alignment - 1);
}

// =========================================================
// 工业级 x86_64 机器码发射器 (Machine Code Emitter)
// =========================================================

void emit8(PeCodeBuffer* cb, uint8_t b) { buf_append(cb, b); }
void emit32(PeCodeBuffer* cb, uint32_t v) {
    emit8(cb, (uint8_t)(v & 0xFF)); emit8(cb, (uint8_t)((v >> 8) & 0xFF));
    emit8(cb, (uint8_t)((v >> 16) & 0xFF)); emit8(cb, (uint8_t)((v >> 24) & 0xFF));
}

void emit_rex(PeCodeBuffer* cb, int w, int r, int x, int b) {
    uint8_t rex = 0x40 | (w ? 8 : 0) | (r ? 4 : 0) | (x ? 2 : 0) | (b ? 1 : 0);
    // 恢复 REX 前缀发射：在 x86_64 中，访问 sil/dil/spl/bpl 必须有 REX 前缀，
    // 否则会错误地访问 ah/ch/dh/bh。为了安全起见，只要调用了 emit_rex 就强制发射。
    emit8(cb, rex);
}

void emit_modrm(PeCodeBuffer* cb, int mod, int reg, int rm) {
    emit8(cb, (uint8_t)(((mod & 3) << 6) | ((reg & 7) << 3) | (rm & 7)));
}

static void emit_sib(PeCodeBuffer* cb, int scale, int index, int base) {
    emit8(cb, (uint8_t)(((scale & 3) << 6) | ((index & 7) << 3) | (base & 7)));
}

// 内存寻址编码: [base + offset]
static void emit_mem(PeCodeBuffer* cb, int reg, int base, int32_t offset) {
    int r = (reg & 7);
    int b = (base & 7);
    if (offset == 0 && b != 5) {
        emit_modrm(cb, 0, r, b);
        if (b == 4) emit_sib(cb, 0, 4, 4);
    } else if (offset >= -128 && offset <= 127) {
        emit_modrm(cb, 1, r, b);
        if (b == 4) emit_sib(cb, 0, 4, 4);
        emit8(cb, (uint8_t)offset);
    } else {
        emit_modrm(cb, 2, r, b);
        if (b == 4) emit_sib(cb, 0, 4, 4);
        emit32(cb, (uint32_t)offset);
    }
}

void emit_mov_reg_imm32(PeCodeBuffer* cb, int reg, int32_t imm) {
    if (imm == 0) {
        if (reg > 7) emit_rex(cb, 0, 1, 0, 1);
        emit8(cb, 0x31); // xor r32, r32 (自动零扩展到 64 位)
        emit_modrm(cb, 3, reg & 7, reg & 7);
        return;
    }
    if (imm > 0) {
        // 优化: mov r32, imm32 (自动零扩展到 64 位，省去 REX 前缀，指令更短)
        if (reg > 7) emit_rex(cb, 0, 0, 0, 1);
        emit8(cb, 0xB8 | (reg & 7));
        emit32(cb, (uint32_t)imm);
    } else {
        // mov r64, imm32 (符号扩展)
        emit_rex(cb, 1, 0, 0, reg > 7);
        emit8(cb, 0xC7);
        emit_modrm(cb, 3, 0, reg & 7);
        emit32(cb, (uint32_t)imm);
    }
}

void emit_mov_reg_imm64(PeCodeBuffer* cb, int reg, uint64_t imm) {
    if (imm == 0) {
        if (reg > 7) emit_rex(cb, 0, 1, 0, 1);
        emit8(cb, 0x31); // xor r32, r32
        emit_modrm(cb, 3, reg & 7, reg & 7);
        return;
    }
    emit_rex(cb, 1, 0, 0, reg > 7);
    emit8(cb, 0xB8 | (reg & 7));
    emit32(cb, (uint32_t)(imm & 0xFFFFFFFF));
    emit32(cb, (uint32_t)(imm >> 32));
}

static void emit_mov_reg_reg_w(PeCodeBuffer* cb, int w, int dst, int src) {
    if (dst == src) return;
    if (w || src > 7 || dst > 7) emit_rex(cb, w, src > 7, 0, dst > 7);
    emit8(cb, 0x89);
    emit_modrm(cb, 3, src & 7, dst & 7);
}

static void emit_mov_reg_reg(PeCodeBuffer* cb, int dst, int src) {
    emit_mov_reg_reg_w(cb, 1, dst, src);
}

static void emit_alu_reg_reg(PeCodeBuffer* cb, int w, int opc, int dst, int src) {
    if (w || src > 7 || dst > 7) emit_rex(cb, w, src > 7, 0, dst > 7);
    emit8(cb, (uint8_t)opc);
    emit_modrm(cb, 3, src & 7, dst & 7);
}

// opc_ext: 0=ADD, 5=SUB, 7=CMP
static void emit_alu_reg_imm32(PeCodeBuffer* cb, int w, int opc_ext, int dst, int32_t imm) {
    if (imm == 0 && (opc_ext == 0 || opc_ext == 5)) return; // add/sub 0 是空操作
    
    if (imm == 1 && opc_ext == 0) { // inc
        if (w || dst > 7) emit_rex(cb, w, 0, 0, dst > 7);
        emit8(cb, 0xFF);
        emit_modrm(cb, 3, 0, dst & 7);
        return;
    }
    if (imm == 1 && opc_ext == 5) { // dec
        if (w || dst > 7) emit_rex(cb, w, 0, 0, dst > 7);
        emit8(cb, 0xFF);
        emit_modrm(cb, 3, 1, dst & 7);
        return;
    }

    if (w || dst > 7) emit_rex(cb, w, 0, 0, dst > 7);
    if (imm >= -128 && imm <= 127) {
        emit8(cb, 0x83);
        emit_modrm(cb, 3, opc_ext, dst & 7);
        emit8(cb, (uint8_t)imm);
    } else {
        emit8(cb, 0x81);
        emit_modrm(cb, 3, opc_ext, dst & 7);
        emit32(cb, (uint32_t)imm);
    }
}

static int get_phys_reg(int color) {
    int map[] = {REG_RBX, REG_RSI, REG_RDI, REG_R12, REG_R13, REG_R14, REG_R15, REG_R8, REG_R9, REG_R10, REG_R11};
    if (color >= 0 && color < 11) return map[color];
    return REG_RAX;
}

typedef struct {
    int pass;
    const char** strings;
    uint32_t* string_lens;
    uint32_t* string_offsets;
    int string_count;
    const char** globals;
    uint32_t* global_offsets;
    int global_count;
    const char** funcs;
    uint32_t* func_offsets;
    int func_count;
    SirExternFunc* first_extern;
    int frame_size;
} LinkCtx;

static void prune_dead_blocks(SirFunction* func, SirBlock* new_entry) {
    if (!func->first_block) return;
    
    uint32_t max_block_id = 0;
    for (SirBlock* b = func->first_block; b; b = b->next) {
        if (b->id > max_block_id) max_block_id = b->id;
    }
    
    bool* reachable = (bool*)calloc(max_block_id + 1, sizeof(bool));
    SirBlock** stack = (SirBlock**)malloc(sizeof(SirBlock*) * (max_block_id + 1));
    if (!reachable || !stack) {
        if (reachable) free(reachable);
        if (stack) free(stack);
        return;
    }
    int top = 0;
    
    SirBlock* entry = new_entry ? new_entry : func->first_block;
    reachable[entry->id] = true;
    stack[top++] = entry;
    
    while (top > 0) {
        SirBlock* b = stack[--top];
        if (!b->last_inst) continue;
        
        if (b->last_inst->opcode == SIR_JMP) {
            SirBlock* target = b->last_inst->operands[0]->as.block;
            if (!reachable[target->id]) {
                reachable[target->id] = true;
                stack[top++] = target;
            }
        } else if (b->last_inst->opcode == SIR_BR) {
            SirBlock* t_target = b->last_inst->operands[1]->as.block;
            SirBlock* f_target = b->last_inst->operands[2]->as.block;
            if (!reachable[t_target->id]) {
                reachable[t_target->id] = true;
                stack[top++] = t_target;
            }
            if (!reachable[f_target->id]) {
                reachable[f_target->id] = true;
                stack[top++] = f_target;
            }
        } else if (b->last_inst->opcode == SIR_SWITCH) {
            SirBlock* def_target = b->last_inst->operands[1]->as.block;
            if (!reachable[def_target->id]) {
                reachable[def_target->id] = true;
                stack[top++] = def_target;
            }
            int case_count = (b->last_inst->num_operands - 2) / 2;
            for (int i = 0; i < case_count; i++) {
                SirBlock* c_target = b->last_inst->operands[2 + i * 2 + 1]->as.block;
                if (!reachable[c_target->id]) {
                    reachable[c_target->id] = true;
                    stack[top++] = c_target;
                }
            }
        }
    }
    
    SirBlock* new_first = NULL;
    SirBlock* new_last = NULL;
    for (SirBlock* b = func->first_block; b; b = b->next) {
        if (reachable[b->id]) {
            if (!new_first) new_first = b;
            else new_last->next = b;
            new_last = b;
        }
    }
    if (new_last) new_last->next = NULL;
    func->first_block = new_first;
    func->last_block = new_last;
    
    free(reachable);
    free(stack);
}

typedef struct {
    bool active;
    int32_t imm;
    int w;
    uint8_t jcc_slow;
} FastPathInfo;

// 全局重定位表 (用于跨函数回填)
#define MAX_STR_RELOCS 1024
uint32_t g_str_relocs[MAX_STR_RELOCS];
uint32_t g_str_rdata_offs[MAX_STR_RELOCS];
int g_str_reloc_count = 0;

uint32_t g_data_relocs[1024];
uint32_t g_data_offs[1024];
int g_data_reloc_count = 0;

uint32_t g_func_relocs[1024];
uint32_t g_func_offs[1024];
int g_func_reloc_count = 0;

uint32_t g_extern_relocs[1024];
int g_extern_idxs[1024];
int g_extern_reloc_count = 0;

// 智能操作数加载器：处理常量、物理寄存器、栈溢出和 RIP 寻址
static int load_operand(PeCodeBuffer* cb, RegAllocator* alloc, SirValue* val, int scratch, LinkCtx* ctx) {
    if (!val) return scratch;
    if (val->kind == SIR_VAL_CONST_INT) {
        emit_mov_reg_imm32(cb, scratch, (int32_t)val->as.int_val);
        return scratch;
    } else if (val->kind == SIR_VAL_CONST_FLOAT) {
        if (val->type && val->type->kind == TY_F32) {
            float f = (float)val->as.float_val;
            uint32_t bits;
            memcpy(&bits, &f, 4);
            emit_mov_reg_imm32(cb, scratch, (int32_t)bits);
        } else {
            uint64_t bits;
            double d = val->as.float_val;
            memcpy(&bits, &d, 8);
            emit_mov_reg_imm64(cb, scratch, bits);
        }
        return scratch;
    } else if (val->kind == SIR_VAL_CONST_BOOL) {
        emit_mov_reg_imm32(cb, scratch, val->as.bool_val ? 1 : 0);
        return scratch;
    } else if (val->kind == SIR_VAL_CONST_STRING) {
        uint32_t rdata_off = 0;
        for (int i = 0; i < ctx->string_count; i++) {
            if (ctx->string_lens[i] == val->as.string_val.len &&
                memcmp(ctx->strings[i], val->as.string_val.str, val->as.string_val.len) == 0) {
                rdata_off = ctx->string_offsets[i];
                break;
            }
        }
        emit_rex(cb, 1, scratch > 7, 0, 0);
        emit8(cb, 0x8D); // lea r64, [rip + rel32]
        emit_modrm(cb, 0, scratch & 7, 5);
        if (ctx->pass == 1) {
            g_str_relocs[g_str_reloc_count] = (uint32_t)cb->size;
            g_str_rdata_offs[g_str_reloc_count] = rdata_off;
            g_str_reloc_count++;
        }
        emit32(cb, 0); // 占位符，链接时回填
        return scratch;
    } else if (val->kind == SIR_VAL_GLOBAL) {
        bool is_global = false;
        uint32_t target_off = 0;
        for (int i = 0; i < ctx->global_count; i++) {
            if (strcmp(ctx->globals[i], val->as.global_name) == 0) {
                is_global = true;
                target_off = ctx->global_offsets[i];
                break;
            }
        }
        
        if (is_global) {
            emit_rex(cb, 1, scratch > 7, 0, 0);
            emit8(cb, 0x8D); // lea r64, [rip + rel32]
            emit_modrm(cb, 0, scratch & 7, 5);
            if (ctx->pass == 1) {
                g_data_relocs[g_data_reloc_count] = (uint32_t)cb->size;
                g_data_offs[g_data_reloc_count] = target_off;
                g_data_reloc_count++;
            }
        } else {
            bool is_extern = false;
            int target_idx = 0;
            int current_idx = 0;
            for (SirExternFunc* ext = ctx->first_extern; ext; ext = ext->next) {
                if (strcmp(ext->name, val->as.global_name) == 0) {
                    is_extern = true;
                    target_idx = current_idx;
                    break;
                }
                current_idx++;
            }
            
            if (is_extern) {
                emit_rex(cb, 1, scratch > 7, 0, 0);
                emit8(cb, 0x8B); // mov r64, [rip + rel32] (从 IAT 读取函数指针)
                emit_modrm(cb, 0, scratch & 7, 5);
                if (ctx->pass == 1) {
                    g_extern_relocs[g_extern_reloc_count] = (uint32_t)cb->size;
                    g_extern_idxs[g_extern_reloc_count] = target_idx;
                    g_extern_reloc_count++;
                }
            } else {
                emit_rex(cb, 1, scratch > 7, 0, 0);
                emit8(cb, 0x8D); // lea r64, [rip + rel32]
                emit_modrm(cb, 0, scratch & 7, 5);
                for (int i = 0; i < ctx->func_count; i++) {
                    if (strcmp(ctx->funcs[i], val->as.global_name) == 0) {
                        target_off = ctx->func_offsets[i];
                        break;
                    }
                }
                if (ctx->pass == 1) {
                    g_func_relocs[g_func_reloc_count] = (uint32_t)cb->size;
                    g_func_offs[g_func_reloc_count] = target_off;
                    g_func_reloc_count++;
                }
            }
        }
        emit32(cb, 0);
        return scratch;
    } else if (val->kind == SIR_VAL_VREG) {
        int color = reg_alloc_get_color(alloc, val->as.vreg);
        if (color != -1) return get_phys_reg(color);
        int offset = reg_alloc_get_offset(alloc, val->as.vreg, 8);
        int size = type_get_size(val->type);
        bool is_signed = type_is_signed(val->type);
        
        if (size == 1) {
            emit_rex(cb, 1, scratch > 7, 0, 0);
            emit8(cb, 0x0F); emit8(cb, is_signed ? 0xBE : 0xB6); // movsx/movzx r64, m8
        } else if (size == 2) {
            emit_rex(cb, 1, scratch > 7, 0, 0);
            emit8(cb, 0x0F); emit8(cb, is_signed ? 0xBF : 0xB7); // movsx/movzx r64, m16
        } else if (size == 4) {
            if (is_signed) {
                emit_rex(cb, 1, scratch > 7, 0, 0);
                emit8(cb, 0x63); // movsxd r64, m32
            } else {
                if (scratch > 7) emit_rex(cb, 0, scratch > 7, 0, 0);
                emit8(cb, 0x8B); // mov r32, m32
            }
        } else {
            emit_rex(cb, 1, scratch > 7, 0, 0);
            emit8(cb, 0x8B); // mov r64, m64
        }
        emit_mem(cb, scratch, REG_RSP, ctx->frame_size + offset);
        return scratch;
    }
    return scratch;
}

// 智能结果存储器
static void store_result_impl(PeCodeBuffer* cb, RegAllocator* alloc, SirValue* val, int src, LinkCtx* ctx) {
    if (!val || val->kind != SIR_VAL_VREG) return;
    int w = (val->type && type_get_size(val->type) <= 4) ? 0 : 1;
    int color = reg_alloc_get_color(alloc, val->as.vreg);
    if (color != -1) {
        int dst = get_phys_reg(color);
        if (dst != src) emit_mov_reg_reg_w(cb, w, dst, src);
    } else {
        int offset = reg_alloc_get_offset(alloc, val->as.vreg, 8);
        if (w || src > 7) emit_rex(cb, w, src > 7, 0, 0);
        emit8(cb, 0x89); // mov m64/m32, r64/r32
        emit_mem(cb, src, REG_RSP, ctx->frame_size + offset);
    }
}
#define store_result(cb, alloc, val, src) store_result_impl(cb, alloc, val, src, &ctx)

uint32_t g_print_str_relocs[1024];
int g_print_str_reloc_count = 0;

uint32_t g_print_int_relocs[1024];
int g_print_int_reloc_count = 0;

uint32_t g_print_hex_relocs[1024];
int g_print_hex_reloc_count = 0;

uint32_t g_print_float_relocs[1024];
int g_print_float_reloc_count = 0;

uint32_t g_print_bool_relocs[1024];
int g_print_bool_reloc_count = 0;

uint32_t g_verum_rdata_off = 0;
uint32_t g_falsum_rdata_off = 0;
uint32_t g_dot_rdata_off = 0;
uint32_t g_minus_rdata_off = 0;
uint32_t g_float_10_rdata_off = 0;

uint32_t g_princeps_offset = 0;
uint32_t g_init_offset = 0;

uint32_t g_crea_relocs[1024];
int g_crea_reloc_count = 0;

uint32_t g_neca_relocs[1024];
int g_neca_reloc_count = 0;

static void generate_machine_code(PeLinker* linker, SirModule* module) {
    builtins_analyze_usage(module);
    uint32_t* block_offsets = (uint32_t*)calloc(1024, sizeof(uint32_t)); // 记录基本块的机器码偏移量
    
    const char** func_names = (const char**)malloc(256 * sizeof(const char*));
    uint32_t* func_offsets = (uint32_t*)malloc(256 * sizeof(uint32_t));
    int func_count = 0;

    const char** strings = (const char**)malloc(1024 * sizeof(const char*));
    uint32_t* string_lens = (uint32_t*)malloc(1024 * sizeof(uint32_t));
    uint32_t* string_offsets = (uint32_t*)malloc(1024 * sizeof(uint32_t));
    int string_count = 0;
    g_str_reloc_count = 0;

    const char** global_names = (const char**)malloc(1024 * sizeof(const char*));
    uint32_t* global_offsets = (uint32_t*)malloc(1024 * sizeof(uint32_t));
    int global_count = 0;

    // 预扫描：收集所有全局变量并写入 .data 段
    for (SirGlobalVar* g = module->first_global; g; g = g->next) {
        while (linker->data_section.size % 8 != 0) buf_append(&linker->data_section, 0);
        global_names[global_count] = g->name;
        global_offsets[global_count] = (uint32_t)linker->data_section.size;
        global_count++;
        for (int i = 0; i < g->size; i++) buf_append(&linker->data_section, 0);
    }

    // 预扫描：收集所有函数名 (解决前向引用问题)
    for (SirFunction* func = module->first_func; func; func = func->next) {
        func_names[func_count] = func->name;
        func_count++;
    }

    FastPathInfo* fp_infos = (FastPathInfo*)calloc(func_count, sizeof(FastPathInfo));
    int f_idx = 0;
    for (SirFunction* func = module->first_func; func; func = func->next, f_idx++) {
        if (func->first_block && func->first_block->first_inst) {
            SirInst* i1 = func->first_block->first_inst;
            if (i1->opcode == SIR_GET_PARAM && i1->operands[0]->as.int_val == 0) {
                SirInst* i2 = i1->next;
                if (i2 && i2->opcode >= SIR_ICMP_EQ && i2->opcode <= SIR_ICMP_GE && i2->operands[0] == i1->dest && i2->operands[1]->kind == SIR_VAL_CONST_INT) {
                    SirInst* i3 = i2->next;
                    if (i3 && i3->opcode == SIR_BR && i3->operands[0] == i2->dest) {
                        SirBlock* t_block = i3->operands[1]->as.block;
                        SirBlock* f_block = i3->operands[2]->as.block;
                        SirBlock* ret_block = NULL;
                        SirBlock* slow_block = NULL;
                        bool cond_is_true = false;
                        
                        if (t_block->first_inst && t_block->first_inst == t_block->last_inst && t_block->first_inst->opcode == SIR_RET && t_block->first_inst->operands[0] == i1->dest) {
                            ret_block = t_block; slow_block = f_block; cond_is_true = true;
                        } else if (f_block->first_inst && f_block->first_inst == f_block->last_inst && f_block->first_inst->opcode == SIR_RET && f_block->first_inst->operands[0] == i1->dest) {
                            ret_block = f_block; slow_block = t_block; cond_is_true = false;
                        }
                        
                        if (ret_block) {
                            fp_infos[f_idx].active = true;
                            fp_infos[f_idx].imm = (int32_t)i2->operands[1]->as.int_val;
                            fp_infos[f_idx].w = (i1->dest->type && type_get_size(i1->dest->type) <= 4) ? 0 : 1;
                            
                            bool is_unsigned = type_is_unsigned(i1->dest->type);
                            if (i2->opcode == SIR_ICMP_EQ) fp_infos[f_idx].jcc_slow = cond_is_true ? 0x85 : 0x84;
                            else if (i2->opcode == SIR_ICMP_NE) fp_infos[f_idx].jcc_slow = cond_is_true ? 0x84 : 0x85;
                            else if (i2->opcode == SIR_ICMP_LT) fp_infos[f_idx].jcc_slow = cond_is_true ? (is_unsigned ? 0x83 : 0x8D) : (is_unsigned ? 0x82 : 0x8C);
                            else if (i2->opcode == SIR_ICMP_LE) fp_infos[f_idx].jcc_slow = cond_is_true ? (is_unsigned ? 0x87 : 0x8F) : (is_unsigned ? 0x86 : 0x8E);
                            else if (i2->opcode == SIR_ICMP_GT) fp_infos[f_idx].jcc_slow = cond_is_true ? (is_unsigned ? 0x86 : 0x8E) : (is_unsigned ? 0x87 : 0x8F);
                            else if (i2->opcode == SIR_ICMP_GE) fp_infos[f_idx].jcc_slow = cond_is_true ? (is_unsigned ? 0x82 : 0x8C) : (is_unsigned ? 0x83 : 0x8D);
                        }
                    }
                }
            }
        }
    }

    // 预扫描：收集所有字符串常量并写入 .rdata 段
    for (SirFunction* func = module->first_func; func; func = func->next) {
        for (SirBlock* block = func->first_block; block; block = block->next) {
            for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                for (int i = 0; i < inst->num_operands; i++) {
                    if (inst->operands[i] && inst->operands[i]->kind == SIR_VAL_CONST_STRING) {
                        bool found = false;
                        for (int j = 0; j < string_count; j++) {
                            if (string_lens[j] == inst->operands[i]->as.string_val.len &&
                                memcmp(strings[j], inst->operands[i]->as.string_val.str, inst->operands[i]->as.string_val.len) == 0) {
                                found = true; break;
                            }
                        }
                        if (!found) {
                            strings[string_count] = inst->operands[i]->as.string_val.str;
                            string_lens[string_count] = inst->operands[i]->as.string_val.len;
                            string_offsets[string_count] = (uint32_t)linker->rdata_section.size;
                            
                            const char* str = inst->operands[i]->as.string_val.str;
                            uint32_t len = inst->operands[i]->as.string_val.len;
                            for (size_t k = 0; k < len; k++) buf_append(&linker->rdata_section, (uint8_t)str[k]);
                            string_count++;
                        }
                    }
                }
            }
        }
    }

    if (g_use_print_bool) {
        g_verum_rdata_off = (uint32_t)linker->rdata_section.size;
        const char* str_verum = "verum";
        for (size_t k = 0; k < strlen(str_verum); k++) buf_append(&linker->rdata_section, (uint8_t)str_verum[k]);
        
        g_falsum_rdata_off = (uint32_t)linker->rdata_section.size;
        const char* str_falsum = "falsum";
        for (size_t k = 0; k < strlen(str_falsum); k++) buf_append(&linker->rdata_section, (uint8_t)str_falsum[k]);
    }

    if (g_use_print_float) {
        g_dot_rdata_off = (uint32_t)linker->rdata_section.size;
        buf_append(&linker->rdata_section, '.');

        g_minus_rdata_off = (uint32_t)linker->rdata_section.size;
        buf_append(&linker->rdata_section, '-');

        while (linker->rdata_section.size % 8 != 0) buf_append(&linker->rdata_section, 0);
        g_float_10_rdata_off = (uint32_t)linker->rdata_section.size;
        uint64_t float_10_bits = 4621819117588971520ULL; // 10.0
        for (int i = 0; i < 8; i++) buf_append(&linker->rdata_section, (uint8_t)(float_10_bits >> (i * 8)));
    }

    // 两遍汇编 (Two-Pass Assembly)：第一遍计算跳转偏移，第二遍真正写入
    for (int pass = 0; pass < 2; pass++) {
        linker->text_section.size = 0;
        g_print_str_reloc_count = 0;
        g_print_int_reloc_count = 0;
        g_print_hex_reloc_count = 0;
        g_print_float_reloc_count = 0;
        g_print_bool_reloc_count = 0;
        g_str_reloc_count = 0;
        g_crea_reloc_count = 0;
        g_neca_reloc_count = 0;
        g_data_reloc_count = 0;
        g_func_reloc_count = 0;
        g_extern_reloc_count = 0;

        LinkCtx ctx;
        ctx.pass = pass;
        ctx.strings = strings;
        ctx.string_lens = string_lens;
        ctx.string_offsets = string_offsets;
        ctx.string_count = string_count;
        ctx.globals = global_names;
        ctx.global_offsets = global_offsets;
        ctx.global_count = global_count;
        ctx.funcs = func_names;
        ctx.func_offsets = func_offsets;
        ctx.func_count = func_count;
        ctx.first_extern = module->first_extern;

        int current_func_idx = 0;
        for (SirFunction* func = module->first_func; func; func = func->next) {
            // 优化: 函数入口 16 字节对齐 (提升 I-Cache 命中率)
            while (linker->text_section.size % 16 != 0) {
                emit8(&linker->text_section, 0x90); // nop
            }
            func_offsets[current_func_idx++] = (uint32_t)linker->text_section.size;

            if (strcmp(func->name, "princeps") == 0) {
                g_princeps_offset = (uint32_t)linker->text_section.size;
            } else if (strcmp(func->name, "__scoria_init") == 0) {
                g_init_offset = (uint32_t)linker->text_section.size;
            }

            if (fp_infos[current_func_idx - 1].active) {
                FastPathInfo* fp = &fp_infos[current_func_idx - 1];
                // cmp rcx/ecx, imm
                emit_alu_reg_imm32(&linker->text_section, fp->w, 7, REG_RCX, fp->imm);
                
                emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, fp->jcc_slow);
                uint32_t jmp_slow_off = (uint32_t)linker->text_section.size;
                emit32(&linker->text_section, 0); // 占位符
                
                // mov rax/eax, rcx/ecx
                emit_mov_reg_reg_w(&linker->text_section, fp->w, REG_RAX, REG_RCX);
                // ret
                emit8(&linker->text_section, 0xC3);
                
                // 回填 jcc_slow
                int32_t rel_slow = (int32_t)(linker->text_section.size - (jmp_slow_off + 4));
                memcpy(linker->text_section.buffer + jmp_slow_off, &rel_slow, 4);
                
                // 仅在第一遍汇编时切断树根并大扫除
                if (pass == 0) {
                    SirInst* i1 = func->first_block->first_inst;
                    SirInst* i2 = i1->next;
                    SirInst* i3 = i2->next;
                    bool cond_is_true = false;
                    SirBlock* t_block = i3->operands[1]->as.block;
                    if (t_block->first_inst && t_block->first_inst == t_block->last_inst && t_block->first_inst->opcode == SIR_RET && t_block->first_inst->operands[0] == i1->dest) {
                        cond_is_true = true;
                    }
                    
                    i1->next = i3;
                    i3->prev = i1;
                    i3->opcode = SIR_JMP;
                    i3->num_operands = 1;
                    i3->operands[0] = cond_is_true ? i3->operands[2] : i3->operands[1];
                    
                    prune_dead_blocks(func, func->first_block);
                }
            }

            uint32_t max_vreg = 0;
            int local_stack_size = 0;
            int max_call_args = 0;
            bool has_call = false;
            int* alloca_offsets = calloc(10000, sizeof(int));

            // 预扫描：计算最大寄存器、ALLOCA 空间和最大调用参数数
            for (SirBlock* block = func->first_block; block; block = block->next) {
                for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                    if (inst->opcode == SIR_CALL) {
                        has_call = true;
                        int args = inst->num_operands - 1;
                        if (args > max_call_args) max_call_args = args;
                    }
                    if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                        if (inst->dest->as.vreg > max_vreg) max_vreg = inst->dest->as.vreg;
                    }
                    for (int i=0; i<inst->num_operands; i++) {
                        if (inst->operands[i] && inst->operands[i]->kind == SIR_VAL_VREG) {
                            if (inst->operands[i]->as.vreg > max_vreg) max_vreg = inst->operands[i]->as.vreg;
                        }
                    }
                    if (inst->opcode == SIR_ALLOCA) {
                        int alloc_size = (int)inst->operands[0]->as.int_val;
                        alloc_size = (alloc_size + 7) & ~7; // 保持 8 字节对齐
                        local_stack_size += alloc_size;
                        alloca_offsets[inst->dest->as.vreg] = -local_stack_size;
                    }
                }
            }

            RegAllocator allocator;
            reg_alloc_init(&allocator, max_vreg);
            allocator.current_offset = local_stack_size;
            reg_alloc_build_and_color(&allocator, func);

            bool has_extern_call = false;
            for (SirBlock* block = func->first_block; block; block = block->next) {
                for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                    if (inst->opcode == SIR_CALL) {
                        if (inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                            bool is_ext = false;
                            for (SirExternFunc* ext = module->first_extern; ext; ext = ext->next) {
                                if (strcmp(ext->name, inst->operands[0]->as.global_name) == 0) {
                                    is_ext = true;
                                    break;
                                }
                            }
                            if (is_ext) has_extern_call = true;
                        } else {
                            has_extern_call = true;
                        }
                    }
                }
            }
            int call_stack_space = max_call_args > 4 ? (max_call_args - 4) * 8 : 0;
            int shadow_space = (has_extern_call || max_call_args > 4) ? 32 : 0;
            int local_and_args = allocator.current_offset + shadow_space + call_stack_space;

            // 序言 (Prologue)
            int num_callee_pushes = 0;
            if (allocator.used_callee_saved[0]) { emit8(&linker->text_section, 0x53); num_callee_pushes++; } // push rbx
            if (allocator.used_callee_saved[1]) { emit8(&linker->text_section, 0x56); num_callee_pushes++; } // push rsi
            if (allocator.used_callee_saved[2]) { emit8(&linker->text_section, 0x57); num_callee_pushes++; } // push rdi
            if (allocator.used_callee_saved[3]) { emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x54); num_callee_pushes++; } // push r12
            if (allocator.used_callee_saved[4]) { emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x55); num_callee_pushes++; } // push r13
            if (allocator.used_callee_saved[5]) { emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x56); num_callee_pushes++; } // push r14
            if (allocator.used_callee_saved[6]) { emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x57); num_callee_pushes++; } // push r15

            int stack_sub_size = local_and_args;
            if ((stack_sub_size + num_callee_pushes * 8 + 8) % 16 != 0) {
                stack_sub_size += 8;
            }
            int total_frame_size = stack_sub_size + num_callee_pushes * 8;
            ctx.frame_size = total_frame_size;
            if (stack_sub_size > 0) {
                emit_rex(&linker->text_section, 1, 0, 0, 0);
                if (stack_sub_size <= 127) {
                    emit8(&linker->text_section, 0x83); // 优化: sub rsp, imm8
                    emit_modrm(&linker->text_section, 3, 5, REG_RSP);
                    emit8(&linker->text_section, (uint8_t)stack_sub_size);
                } else {
                    emit8(&linker->text_section, 0x81); // sub rsp, imm32
                    emit_modrm(&linker->text_section, 3, 5, REG_RSP);
                    emit32(&linker->text_section, (uint32_t)stack_sub_size);
                }
            }

            for (SirBlock* block = func->first_block; block; block = block->next) {
                // 优化: 如果前一个基本块以无条件跳转或返回结束，当前块不会被 Fall-through 到达，可以安全地进行 16 字节对齐
                // 这能显著提升分支预测和指令缓存的效率
                if (block != func->first_block) {
                    SirBlock* prev = func->first_block;
                    while (prev->next != block) prev = prev->next;
                    if (prev->last_inst && (prev->last_inst->opcode == SIR_JMP || prev->last_inst->opcode == SIR_RET)) {
                        while (linker->text_section.size % 16 != 0) {
                            emit8(&linker->text_section, 0x90); // nop
                        }
                    }
                }
                
                if (block->id < 1024) block_offsets[block->id] = (uint32_t)linker->text_section.size;

                for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                    switch (inst->opcode) {
                        case SIR_SELECT: {
                            int dest_reg = REG_RAX;
                            if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                                int c = reg_alloc_get_color(&allocator, inst->dest->as.vreg);
                                if (c != -1) dest_reg = get_phys_reg(c);
                            }
                            
                            int t_scratch = (dest_reg == REG_RCX) ? REG_RDX : REG_RCX;
                            int cond_scratch = (t_scratch == REG_RCX) ? REG_R8 : REG_RCX;
                            if (cond_scratch == dest_reg) cond_scratch = REG_R9;
                            
                            int cond = load_operand(&linker->text_section, &allocator, inst->operands[0], cond_scratch, &ctx);
                            if (cond == dest_reg || cond == t_scratch) {
                                emit_mov_reg_reg(&linker->text_section, cond_scratch, cond);
                                cond = cond_scratch;
                            }
                            
                            int t_color = (inst->operands[1] && inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                            int t_phys = (t_color != -1) ? get_phys_reg(t_color) : -1;
                            
                            if (t_phys == dest_reg) {
                                int t_val = load_operand(&linker->text_section, &allocator, inst->operands[1], t_scratch, &ctx);
                                if (t_val != t_scratch) emit_mov_reg_reg(&linker->text_section, t_scratch, t_val);
                                
                                int f_val = load_operand(&linker->text_section, &allocator, inst->operands[2], dest_reg, &ctx);
                                if (f_val != dest_reg) emit_mov_reg_reg(&linker->text_section, dest_reg, f_val);
                            } else {
                                int f_val = load_operand(&linker->text_section, &allocator, inst->operands[2], dest_reg, &ctx);
                                if (f_val != dest_reg) emit_mov_reg_reg(&linker->text_section, dest_reg, f_val);
                                
                                int t_val = load_operand(&linker->text_section, &allocator, inst->operands[1], t_scratch, &ctx);
                                if (t_val != t_scratch) emit_mov_reg_reg(&linker->text_section, t_scratch, t_val);
                            }
                            
                            emit_rex(&linker->text_section, 1, cond > 7, 0, cond > 7);
                            emit8(&linker->text_section, 0x85); // test cond, cond
                            emit_modrm(&linker->text_section, 3, cond & 7, cond & 7);
                            
                            emit_rex(&linker->text_section, 1, dest_reg > 7, 0, t_scratch > 7);
                            emit8(&linker->text_section, 0x0F);
                            emit8(&linker->text_section, 0x45); // cmovne
                            emit_modrm(&linker->text_section, 3, dest_reg & 7, t_scratch & 7);
                            
                            store_result(&linker->text_section, &allocator, inst->dest, dest_reg);
                            break;
                        }
                        case SIR_GET_PARAM: {
                            int param_idx = (int)inst->operands[0]->as.int_val;
                            if (param_idx < 4) {
                                int regs[] = {REG_RCX, REG_RDX, 8, 9};
                                int src_reg = regs[param_idx];
                                bool is_float = (inst->dest->type && (inst->dest->type->kind == TY_F32 || inst->dest->type->kind == TY_F64));
                                if (is_float) {
                                    bool is_f32 = (inst->dest->type->kind == TY_F32);
                                    emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); 
                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x7E); 
                                    emit_modrm(&linker->text_section, 3, param_idx, REG_RAX);
                                    store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                                } else {
                                    store_result(&linker->text_section, &allocator, inst->dest, src_reg);
                                }
                            } else {
                                int offset = 8 + total_frame_size + param_idx * 8;
                                int size = type_get_size(inst->dest->type);
                                bool is_signed = type_is_signed(inst->dest->type);
                                
                                if (size == 1) {
                                    emit_rex(&linker->text_section, 1, 0, 0, 0);
                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, is_signed ? 0xBE : 0xB6);
                                } else if (size == 2) {
                                    emit_rex(&linker->text_section, 1, 0, 0, 0);
                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, is_signed ? 0xBF : 0xB7);
                                } else if (size == 4) {
                                    if (is_signed) {
                                        emit_rex(&linker->text_section, 1, 0, 0, 0);
                                        emit8(&linker->text_section, 0x63);
                                    } else {
                                        emit_rex(&linker->text_section, 0, 0, 0, 0);
                                        emit8(&linker->text_section, 0x8B);
                                    }
                                } else {
                                    emit_rex(&linker->text_section, 1, 0, 0, 0);
                                    emit8(&linker->text_section, 0x8B);
                                }
                                emit_mem(&linker->text_section, REG_RAX, REG_RSP, offset);
                                store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            }
                            break;
                        }
                        case SIR_ALLOCA: {
                            int offset = alloca_offsets[inst->dest->as.vreg];
                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                            emit8(&linker->text_section, 0x8D); // lea rax, [rsp + offset]
                            emit_mem(&linker->text_section, REG_RAX, REG_RSP, total_frame_size + offset);
                            store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            break;
                        }
                        case SIR_STORE: {
                            int size = 8;
                            // 优先使用指针解引用后的目标类型大小，防止右值字面量丢失类型导致越界写入
                            if (inst->operands[1]->type && inst->operands[1]->type->kind == TY_VIA) {
                                size = type_get_size(inst->operands[1]->type->as.inner);
                            } else if (inst->operands[0]->type) {
                                size = type_get_size(inst->operands[0]->type);
                            }
                            int ptr_color = (inst->operands[1] && inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                            int ptr_phys = (ptr_color != -1) ? get_phys_reg(ptr_color) : -1;
                            
                            int val_scratch = (ptr_phys == REG_RAX) ? REG_RCX : REG_RAX;
                            int val_reg = load_operand(&linker->text_section, &allocator, inst->operands[0], val_scratch, &ctx);
                            
                            int ptr_scratch = (val_reg == REG_RCX) ? REG_RDX : REG_RCX;
                            if (ptr_scratch == val_scratch) ptr_scratch = REG_R8;
                            int ptr_reg = load_operand(&linker->text_section, &allocator, inst->operands[1], ptr_scratch, &ctx);
                            
                            if (size == 1) {
                                emit_rex(&linker->text_section, 0, val_reg > 7, 0, ptr_reg > 7);
                                emit8(&linker->text_section, 0x88); // mov [ptr_reg], val_reg (8-bit)
                            } else if (size == 2) {
                                emit8(&linker->text_section, 0x66);
                                emit_rex(&linker->text_section, 0, val_reg > 7, 0, ptr_reg > 7);
                                emit8(&linker->text_section, 0x89); // mov [ptr_reg], val_reg (16-bit)
                            } else if (size == 4) {
                                emit_rex(&linker->text_section, 0, val_reg > 7, 0, ptr_reg > 7);
                                emit8(&linker->text_section, 0x89); // mov [ptr_reg], val_reg (32-bit)
                            } else {
                                emit_rex(&linker->text_section, 1, val_reg > 7, 0, ptr_reg > 7);
                                emit8(&linker->text_section, 0x89); // mov [ptr_reg], val_reg (64-bit)
                            }
                            emit_mem(&linker->text_section, val_reg, ptr_reg, 0);
                            break;
                        }
                        case SIR_LOAD: {
                            int size = type_get_size(inst->dest->type);
                            bool is_signed = type_is_signed(inst->dest->type);
                            int ptr_reg = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RCX, &ctx);
                            
                            if (size == 1) {
                                emit_rex(&linker->text_section, 1, 0, 0, ptr_reg > 7);
                                emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, is_signed ? 0xBE : 0xB6); // movsx/movzx rax, byte ptr
                            } else if (size == 2) {
                                emit_rex(&linker->text_section, 1, 0, 0, ptr_reg > 7);
                                emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, is_signed ? 0xBF : 0xB7); // movsx/movzx rax, word ptr
                            } else if (size == 4) {
                                if (is_signed) {
                                    emit_rex(&linker->text_section, 1, 0, 0, ptr_reg > 7);
                                    emit8(&linker->text_section, 0x63); // movsxd rax, dword ptr
                                } else {
                                    emit_rex(&linker->text_section, 0, 0, 0, ptr_reg > 7);
                                    emit8(&linker->text_section, 0x8B); // mov eax, dword ptr
                                }
                            } else {
                                emit_rex(&linker->text_section, 1, 0, 0, ptr_reg > 7);
                                emit8(&linker->text_section, 0x8B); // mov rax, qword ptr
                            }
                            emit_mem(&linker->text_section, REG_RAX, ptr_reg, 0);
                            store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            break;
                        }
                        case SIR_CAST: {
                            ScoriaType* src_type = inst->operands[0]->type;
                            ScoriaType* dst_type = inst->dest->type;
                            bool src_is_float = (src_type && (src_type->kind == TY_F32 || src_type->kind == TY_F64));
                            bool dst_is_float = (dst_type && (dst_type->kind == TY_F32 || dst_type->kind == TY_F64));

                            int src = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                            if (src != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, src);
                            
                            if (src_is_float && !dst_is_float) {
                                bool is_f32 = (src_type->kind == TY_F32);
                                // movd/movq xmm0, rax
                                emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); emit_modrm(&linker->text_section, 3, 0, REG_RAX);
                                // cvttss2si/cvttsd2si rax, xmm0
                                emit8(&linker->text_section, is_f32 ? 0xF3 : 0xF2); emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x2C); emit_modrm(&linker->text_section, 3, REG_RAX, 0);
                            } else if (!src_is_float && dst_is_float) {
                                bool is_f32 = (dst_type->kind == TY_F32);
                                // cvtsi2ss/cvtsi2sd xmm0, rax
                                emit8(&linker->text_section, is_f32 ? 0xF3 : 0xF2); emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x2A); emit_modrm(&linker->text_section, 3, 0, REG_RAX);
                                // movd/movq rax, xmm0
                                emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x7E); emit_modrm(&linker->text_section, 3, 0, REG_RAX);
                            } else if (src_is_float && dst_is_float && src_type->kind != dst_type->kind) {
                                bool src_is_f32 = (src_type->kind == TY_F32);
                                // movd/movq xmm0, rax
                                emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, src_is_f32 ? 0 : 1, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); emit_modrm(&linker->text_section, 3, 0, REG_RAX);
                                // cvtss2sd/cvtsd2ss xmm0, xmm0
                                emit8(&linker->text_section, src_is_f32 ? 0xF3 : 0xF2); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x5A); emit_modrm(&linker->text_section, 3, 0, 0);
                                // movq/movd rax, xmm0
                                emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, src_is_f32 ? 1 : 0, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x7E); emit_modrm(&linker->text_section, 3, 0, REG_RAX);
                            } else {
                                int dst_size = type_get_size(dst_type);
                                if (dst_size < 8) {
                                    if (type_is_signed(dst_type)) {
                                        if (dst_size == 1) {
                                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                                            emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0xBE); // movsx rax, al
                                        } else if (dst_size == 2) {
                                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                                            emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0xBF); // movsx rax, ax
                                        } else if (dst_size == 4) {
                                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                                            emit8(&linker->text_section, 0x63); // movsxd rax, eax
                                        }
                                        emit_modrm(&linker->text_section, 3, REG_RAX, REG_RAX);
                                    } else {
                                        if (dst_size == 1) {
                                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                                            emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0xB6); // movzx rax, al
                                            emit_modrm(&linker->text_section, 3, REG_RAX, REG_RAX);
                                        } else if (dst_size == 2) {
                                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                                            emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0xB7); // movzx rax, ax
                                            emit_modrm(&linker->text_section, 3, REG_RAX, REG_RAX);
                                        } else if (dst_size == 4) {
                                            emit_rex(&linker->text_section, 0, 0, 0, 0);
                                            emit8(&linker->text_section, 0x8B); // mov eax, eax (zero extends to rax)
                                            emit_modrm(&linker->text_section, 3, REG_RAX, REG_RAX);
                                        }
                                    }
                                }
                            }
                            
                            store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            break;
                        }
                        case SIR_ADD:
                        case SIR_SUB:
                        case SIR_MUL:
                        case SIR_AND:
                        case SIR_OR:
                        case SIR_XOR: {
                            int dest_reg = REG_RAX;
                            int w = 1;
                            bool is_ret_peephole = false;
                            if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                                int c = reg_alloc_get_color(&allocator, inst->dest->as.vreg);
                                if (c != -1) dest_reg = get_phys_reg(c);
                                if (inst->dest->type && type_get_size(inst->dest->type) <= 4) w = 0;
                                
                                if (inst->next && (allocator.use_count[inst->dest->as.vreg] == 2 || inst->next->opcode == SIR_RET)) {
                                    if (inst->next->opcode == SIR_RET && inst->next->num_operands > 0 && inst->next->operands[0] == inst->dest) {
                                        dest_reg = REG_RAX;
                                        is_ret_peephole = true;
                                    } else if (inst->next->opcode == SIR_CALL && inst->next->num_operands == 2 && inst->next->operands[1] == inst->dest) {
                                        dest_reg = REG_RCX;
                                        is_ret_peephole = true;
                                    }
                                }
                            }
                            
                            int right_color = (inst->operands[1] && inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                            int right_phys = (right_color != -1) ? get_phys_reg(right_color) : -1;
                            int left_scratch = (right_phys == dest_reg) ? ((dest_reg == REG_RAX) ? REG_RCX : REG_RAX) : dest_reg;
                            
                            int left = REG_RAX;
                            bool left_already_in_rax = false;
                            if (inst->prev && inst->prev->opcode == SIR_CALL && inst->prev->dest == inst->operands[0]) {
                                if (inst->operands[0]->kind == SIR_VAL_VREG && (allocator.use_count[inst->operands[0]->as.vreg] == 2 || (inst->next && inst->next->opcode == SIR_RET))) {
                                    left_already_in_rax = true;
                                }
                            }
                            if (!left_already_in_rax) {
                                left = load_operand(&linker->text_section, &allocator, inst->operands[0], left_scratch, &ctx);
                            }
                            
                            if (inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                                int32_t imm = (int32_t)inst->operands[1]->as.int_val;
                                if (inst->opcode == SIR_ADD || inst->opcode == SIR_SUB) {
                                    int32_t lea_imm = (inst->opcode == SIR_SUB) ? -imm : imm;
                                    if (left != dest_reg) {
                                        if (w || dest_reg > 7 || left > 7) emit_rex(&linker->text_section, w, dest_reg > 7, 0, left > 7);
                                        emit8(&linker->text_section, 0x8D); // lea
                                        emit_mem(&linker->text_section, dest_reg, left, lea_imm);
                                    } else {
                                        int opc_ext = (inst->opcode == SIR_ADD) ? 0 : 5;
                                        emit_alu_reg_imm32(&linker->text_section, w, opc_ext, dest_reg, imm);
                                    }
                                } else {
                                    if (left != dest_reg) emit_mov_reg_reg(&linker->text_section, dest_reg, left);
                                    if (inst->opcode == SIR_MUL) {
                                        if (imm == 0) {
                                            if (dest_reg > 7) emit_rex(&linker->text_section, 0, 1, 0, 1);
                                            emit8(&linker->text_section, 0x31); // xor r32, r32
                                            emit_modrm(&linker->text_section, 3, dest_reg & 7, dest_reg & 7);
                                        } else if (imm == 1) {
                                            // no-op
                                        } else if (imm == 2) {
                                            emit_alu_reg_reg(&linker->text_section, w, 0x01, dest_reg, dest_reg); // add dest, dest
                                        } else if (imm == 3 || imm == 5 || imm == 9) {
                                            int scale = (imm == 3) ? 1 : (imm == 5) ? 2 : 3; // 1=2, 2=4, 3=8
                                            emit_rex(&linker->text_section, 1, dest_reg > 7, dest_reg > 7, dest_reg > 7);
                                            emit8(&linker->text_section, 0x8D); // lea
                                            int b = dest_reg & 7;
                                            if (b == 5) {
                                                emit_modrm(&linker->text_section, 1, dest_reg & 7, 4);
                                                emit_sib(&linker->text_section, scale, dest_reg & 7, b);
                                                emit8(&linker->text_section, 0);
                                            } else {
                                                emit_modrm(&linker->text_section, 0, dest_reg & 7, 4);
                                                emit_sib(&linker->text_section, scale, dest_reg & 7, b);
                                            }
                                        } else if (imm > 0 && (imm & (imm - 1)) == 0) {
                                            int shift = 0;
                                            while ((imm >> shift) > 1) shift++;
                                            emit_rex(&linker->text_section, w, 0, 0, dest_reg > 7);
                                            emit8(&linker->text_section, 0xC1); // shl r64, imm8
                                            emit_modrm(&linker->text_section, 3, 4, dest_reg & 7);
                                            emit8(&linker->text_section, (uint8_t)shift);
                                        } else {
                                            // imul r64, r64, imm32
                                            emit_rex(&linker->text_section, w, dest_reg > 7, 0, dest_reg > 7);
                                            if (imm >= -128 && imm <= 127) {
                                                emit8(&linker->text_section, 0x6B);
                                                emit_modrm(&linker->text_section, 3, dest_reg & 7, dest_reg & 7);
                                                emit8(&linker->text_section, (uint8_t)imm);
                                            } else {
                                                emit8(&linker->text_section, 0x69);
                                                emit_modrm(&linker->text_section, 3, dest_reg & 7, dest_reg & 7);
                                                emit32(&linker->text_section, (uint32_t)imm);
                                            }
                                        }
                                    } else {
                                        int opc_ext = 0;
                                        if (inst->opcode == SIR_AND) opc_ext = 4;
                                        else if (inst->opcode == SIR_OR) opc_ext = 1;
                                        else if (inst->opcode == SIR_XOR) opc_ext = 6;
                                        emit_alu_reg_imm32(&linker->text_section, w, opc_ext, dest_reg, imm);
                                    }
                                }
                            } else {
                                int right = REG_RAX;
                                bool right_already_in_rax = false;
                                if (inst->prev && inst->prev->opcode == SIR_CALL && inst->prev->dest == inst->operands[1]) {
                                    if (inst->operands[1]->kind == SIR_VAL_VREG && (allocator.use_count[inst->operands[1]->as.vreg] == 2 || (inst->next && inst->next->opcode == SIR_RET))) {
                                        right_already_in_rax = true;
                                    }
                                }
                                if (!right_already_in_rax) {
                                    int right_scratch = (dest_reg == REG_RCX) ? REG_RDX : REG_RCX;
                                    if (right_scratch == left) right_scratch = (right_scratch == REG_RDX) ? REG_R8 : REG_RDX;
                                    right = load_operand(&linker->text_section, &allocator, inst->operands[1], right_scratch, &ctx);
                                }
                                
                                // 交换可交换操作数，使得 left == dest_reg，减少 mov 指令
                                if (right == dest_reg && left != dest_reg && inst->opcode != SIR_SUB) {
                                    int temp = left; left = right; right = temp;
                                }
                                
                                if (right == dest_reg && left != dest_reg) {
                                    if (inst->opcode == SIR_SUB) {
                                        emit_rex(&linker->text_section, w, 0, 0, dest_reg > 7);
                                        emit8(&linker->text_section, 0xF7);
                                        emit_modrm(&linker->text_section, 3, 3, dest_reg & 7); // neg
                                        emit_alu_reg_reg(&linker->text_section, w, 0x01, dest_reg, left); // add
                                    } else {
                                        if (inst->opcode == SIR_ADD) emit_alu_reg_reg(&linker->text_section, w, 0x01, dest_reg, left);
                                        else if (inst->opcode == SIR_AND) emit_alu_reg_reg(&linker->text_section, w, 0x21, dest_reg, left);
                                        else if (inst->opcode == SIR_OR) emit_alu_reg_reg(&linker->text_section, w, 0x09, dest_reg, left);
                                        else if (inst->opcode == SIR_XOR) emit_alu_reg_reg(&linker->text_section, w, 0x31, dest_reg, left);
                                        else {
                                            emit_rex(&linker->text_section, w, dest_reg > 7, 0, left > 7);
                                            emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0xAF); // imul dest_reg, left
                                            emit_modrm(&linker->text_section, 3, dest_reg & 7, left & 7);
                                        }
                                    }
                                } else {
                                    if (inst->opcode == SIR_ADD && left != dest_reg && right != dest_reg && (left & 7) != 4 && (right & 7) != 4) {
                                        // 优化: lea dest, [left + right]
                                        if (w || dest_reg > 7 || right > 7 || left > 7) emit_rex(&linker->text_section, w, dest_reg > 7, right > 7, left > 7);
                                        emit8(&linker->text_section, 0x8D); // lea
                                        int b = left & 7;
                                        if (b == 5) {
                                            emit_modrm(&linker->text_section, 1, dest_reg & 7, 4);
                                            emit_sib(&linker->text_section, 0, right & 7, b);
                                            emit8(&linker->text_section, 0);
                                        } else {
                                            emit_modrm(&linker->text_section, 0, dest_reg & 7, 4);
                                            emit_sib(&linker->text_section, 0, right & 7, b);
                                        }
                                    } else if (inst->opcode == SIR_ADD && left != dest_reg && right != dest_reg && (left & 7) == 4 && (right & 7) != 4) {
                                        // 交换 left 和 right 避免 SIB index 为 4 (rsp/r12) 被截断
                                        if (w || dest_reg > 7 || left > 7 || right > 7) emit_rex(&linker->text_section, w, dest_reg > 7, left > 7, right > 7);
                                        emit8(&linker->text_section, 0x8D); // lea
                                        int b = right & 7;
                                        if (b == 5) {
                                            emit_modrm(&linker->text_section, 1, dest_reg & 7, 4);
                                            emit_sib(&linker->text_section, 0, left & 7, b);
                                            emit8(&linker->text_section, 0);
                                        } else {
                                            emit_modrm(&linker->text_section, 0, dest_reg & 7, 4);
                                            emit_sib(&linker->text_section, 0, left & 7, b);
                                        }
                                    } else {
                                        if (left != dest_reg) emit_mov_reg_reg_w(&linker->text_section, w, dest_reg, left);
                                        if (inst->opcode == SIR_ADD) emit_alu_reg_reg(&linker->text_section, w, 0x01, dest_reg, right);
                                        else if (inst->opcode == SIR_SUB) emit_alu_reg_reg(&linker->text_section, w, 0x29, dest_reg, right);
                                        else if (inst->opcode == SIR_AND) emit_alu_reg_reg(&linker->text_section, w, 0x21, dest_reg, right);
                                        else if (inst->opcode == SIR_OR) emit_alu_reg_reg(&linker->text_section, w, 0x09, dest_reg, right);
                                        else if (inst->opcode == SIR_XOR) emit_alu_reg_reg(&linker->text_section, w, 0x31, dest_reg, right);
                                        else {
                                            emit_rex(&linker->text_section, w, dest_reg > 7, 0, right > 7);
                                            emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0xAF); // imul dest_reg, right
                                            emit_modrm(&linker->text_section, 3, dest_reg & 7, right & 7);
                                        }
                                    }
                                }
                            }
                            if (!is_ret_peephole) {
                                store_result(&linker->text_section, &allocator, inst->dest, dest_reg);
                            }
                            break;
                        }
                        case SIR_FADD:
                        case SIR_FSUB:
                        case SIR_FMUL:
                        case SIR_FDIV: {
                            bool is_f32 = (inst->operands[0]->type && inst->operands[0]->type->kind == TY_F32);
                            int dest_reg = REG_RAX;
                            if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                                int c = reg_alloc_get_color(&allocator, inst->dest->as.vreg);
                                if (c != -1) dest_reg = get_phys_reg(c);
                            }
                            
                            int right_color = (inst->operands[1] && inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                            int right_phys = (right_color != -1) ? get_phys_reg(right_color) : -1;
                            int left_scratch = (right_phys == dest_reg) ? ((dest_reg == REG_RAX) ? REG_RCX : REG_RAX) : dest_reg;
                            
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], left_scratch, &ctx);
                            
                            int right_scratch = (dest_reg == REG_RCX) ? REG_RDX : REG_RCX;
                            if (right_scratch == left) right_scratch = (right_scratch == REG_RDX) ? REG_R8 : REG_RDX;
                            int right = load_operand(&linker->text_section, &allocator, inst->operands[1], right_scratch, &ctx);
                            
                            if (right == dest_reg && left != dest_reg) {
                                emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, left > 7); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); emit_modrm(&linker->text_section, 3, 0, left & 7);
                                emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, right > 7); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); emit_modrm(&linker->text_section, 3, 1, right & 7);
                            } else {
                                if (left != dest_reg) emit_mov_reg_reg(&linker->text_section, dest_reg, left);
                                if (right != right_scratch) emit_mov_reg_reg(&linker->text_section, right_scratch, right);
                                
                                emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, dest_reg > 7); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); emit_modrm(&linker->text_section, 3, 0, dest_reg & 7);
                                emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, right_scratch > 7); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); emit_modrm(&linker->text_section, 3, 1, right_scratch & 7);
                            }
                            
                            // op
                            emit8(&linker->text_section, is_f32 ? 0xF3 : 0xF2); emit8(&linker->text_section, 0x0F);
                            if (inst->opcode == SIR_FADD) emit8(&linker->text_section, 0x58);
                            else if (inst->opcode == SIR_FSUB) emit8(&linker->text_section, 0x5C);
                            else if (inst->opcode == SIR_FMUL) emit8(&linker->text_section, 0x59);
                            else if (inst->opcode == SIR_FDIV) emit8(&linker->text_section, 0x5E);
                            emit_modrm(&linker->text_section, 3, 0, 1); // xmm0, xmm1
                            
                            // movd/movq dest_reg, xmm0
                            emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, dest_reg > 7); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x7E); emit_modrm(&linker->text_section, 3, 0, dest_reg & 7);
                            
                            store_result(&linker->text_section, &allocator, inst->dest, dest_reg);
                            break;
                        }
                        case SIR_DIV:
                        case SIR_MOD: {
                            bool is_unsigned = type_is_unsigned(inst->operands[0]->type);
                            
                            // 优化：除以 2 的幂转换为移位
                            if (inst->opcode == SIR_DIV && inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                                int left = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                                if (left != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, left);
                                int64_t imm = inst->operands[1]->as.int_val;
                                if (imm > 0 && (imm & (imm - 1)) == 0) {
                                    int shift = 0;
                                    while ((imm >> shift) > 1) shift++;
                                    if (shift > 0) {
                                        if (is_unsigned) {
                                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                                            emit8(&linker->text_section, 0xC1); // shr rax, imm8
                                            emit_modrm(&linker->text_section, 3, 5, REG_RAX);
                                            emit8(&linker->text_section, (uint8_t)shift);
                                        } else {
                                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                                            emit8(&linker->text_section, 0x99); // cqo
                                            
                                            emit_alu_reg_imm32(&linker->text_section, 1, 4, REG_RDX, (int32_t)((1ULL << shift) - 1)); // and rdx, mask
                                            emit_alu_reg_reg(&linker->text_section, 1, 0x01, REG_RAX, REG_RDX); // add rax, rdx
                                            
                                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                                            emit8(&linker->text_section, 0xC1); // sar rax, imm8
                                            emit_modrm(&linker->text_section, 3, 7, REG_RAX);
                                            emit8(&linker->text_section, (uint8_t)shift);
                                        }
                                    }
                                    store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                                    break;
                                }
                            }
                            
                            int right_color = (inst->operands[1] && inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                            int right_phys = (right_color != -1) ? get_phys_reg(right_color) : -1;
                            int left_scratch = (right_phys == REG_RAX) ? REG_RCX : REG_RAX;
                            
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], left_scratch, &ctx);
                            int right = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                            
                            if (left == REG_RCX && right == REG_RAX) {
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x93); // xchg rax, rcx
                            } else if (right == REG_RAX) {
                                emit_mov_reg_reg(&linker->text_section, REG_RCX, right);
                                emit_mov_reg_reg(&linker->text_section, REG_RAX, left);
                            } else {
                                if (left != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, left);
                                if (right != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, right);
                            }
                            
                            if (is_unsigned) {
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x31); // xor rdx, rdx
                                emit_modrm(&linker->text_section, 3, REG_RDX, REG_RDX);
                                
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0xF7); // div rcx
                                emit_modrm(&linker->text_section, 3, 6, REG_RCX);
                            } else {
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x99); // cqo
                                
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0xF7); // idiv rcx
                                emit_modrm(&linker->text_section, 3, 7, REG_RCX);
                            }
                            
                            if (inst->opcode == SIR_MOD) {
                                emit_mov_reg_reg(&linker->text_section, REG_RAX, REG_RDX);
                            }
                            store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            break;
                        }
                        case SIR_SHL:
                        case SIR_SHR: {
                            bool is_unsigned = type_is_unsigned(inst->operands[0]->type);
                            
                            int right_color = (inst->operands[1] && inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                            int right_phys = (right_color != -1) ? get_phys_reg(right_color) : -1;
                            int left_scratch = (right_phys == REG_RAX) ? REG_RCX : REG_RAX;
                            
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], left_scratch, &ctx);
                            int right = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                            
                            if (left == REG_RCX && right == REG_RAX) {
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x93); // xchg rax, rcx
                            } else if (right == REG_RAX) {
                                emit_mov_reg_reg(&linker->text_section, REG_RCX, right);
                                emit_mov_reg_reg(&linker->text_section, REG_RAX, left);
                            } else {
                                if (left != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, left);
                                if (right != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, right);
                            }
                            
                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                            emit8(&linker->text_section, 0xD3); // shl/shr/sar rax, cl
                            if (inst->opcode == SIR_SHL) {
                                emit_modrm(&linker->text_section, 3, 4, REG_RAX);
                            } else {
                                emit_modrm(&linker->text_section, 3, is_unsigned ? 5 : 7, REG_RAX);
                            }
                            
                            store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            break;
                        }
                        case SIR_MEMCPY: {
                            int size = (int)inst->operands[2]->as.int_val;
                            
                            int src_color = (inst->operands[1] && inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                            int src_phys = (src_color != -1) ? get_phys_reg(src_color) : -1;
                            
                            int dst_scratch = (src_phys == REG_RAX) ? REG_R8 : REG_RAX;
                            int dst_reg = load_operand(&linker->text_section, &allocator, inst->operands[0], dst_scratch, &ctx);
                            
                            int src_scratch = (dst_reg == REG_RDX) ? REG_R9 : REG_RDX;
                            int src_reg = load_operand(&linker->text_section, &allocator, inst->operands[1], src_scratch, &ctx);
                            
                            // 保护 rep movsb 会破坏的寄存器
                            emit8(&linker->text_section, 0x56); // push rsi
                            emit8(&linker->text_section, 0x57); // push rdi
                            emit8(&linker->text_section, 0x51); // push rcx
                            
                            if (dst_reg == REG_RSI && src_reg == REG_RDI) {
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x87); // xchg rdi, rsi
                                emit_modrm(&linker->text_section, 3, REG_RDI, REG_RSI);
                            } else if (src_reg == REG_RDI) {
                                emit_mov_reg_reg(&linker->text_section, REG_RSI, src_reg);
                                if (dst_reg != REG_RDI) emit_mov_reg_reg(&linker->text_section, REG_RDI, dst_reg);
                            } else {
                                if (dst_reg != REG_RDI) emit_mov_reg_reg(&linker->text_section, REG_RDI, dst_reg);
                                if (src_reg != REG_RSI) emit_mov_reg_reg(&linker->text_section, REG_RSI, src_reg);
                            }
                            
                            emit_mov_reg_imm32(&linker->text_section, REG_RCX, size);
                            
                            emit8(&linker->text_section, 0xFC); // cld
                            emit8(&linker->text_section, 0xF3); // rep
                            emit8(&linker->text_section, 0xA4); // movsb
                            
                            emit8(&linker->text_section, 0x59); // pop rcx
                            emit8(&linker->text_section, 0x5F); // pop rdi
                            emit8(&linker->text_section, 0x5E); // pop rsi
                            break;
                        }
                        case SIR_GEP: {
                            int dest_reg = REG_RAX;
                            if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                                int c = reg_alloc_get_color(&allocator, inst->dest->as.vreg);
                                if (c != -1) dest_reg = get_phys_reg(c);
                            }

                            int element_size = (int)inst->operands[2]->as.int_val;

                            if (inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                                int ptr = load_operand(&linker->text_section, &allocator, inst->operands[0], dest_reg, &ctx);
                                if (ptr != dest_reg) emit_mov_reg_reg(&linker->text_section, dest_reg, ptr);
                                
                                int32_t offset = (int32_t)(inst->operands[1]->as.int_val * element_size);
                                if (offset != 0) emit_alu_reg_imm32(&linker->text_section, 1, 0, dest_reg, offset);
                            } else {
                                int idx_color = (inst->operands[1] && inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                                int idx_phys = (idx_color != -1) ? get_phys_reg(idx_color) : -1;
                                
                                int ptr_scratch = (idx_phys == dest_reg) ? ((dest_reg == REG_RAX) ? REG_RCX : REG_RAX) : dest_reg;
                                int ptr = load_operand(&linker->text_section, &allocator, inst->operands[0], ptr_scratch, &ctx);
                                
                                int idx_scratch = (dest_reg == REG_RCX) ? REG_RDX : REG_RCX;
                                if (idx_scratch == ptr) idx_scratch = (idx_scratch == REG_RDX) ? REG_R8 : REG_RDX;
                                int idx = load_operand(&linker->text_section, &allocator, inst->operands[1], idx_scratch, &ctx);
                                
                                if (idx == dest_reg && ptr != dest_reg) {
                                    emit_mov_reg_reg(&linker->text_section, idx_scratch, idx);
                                    idx = idx_scratch;
                                    emit_mov_reg_reg(&linker->text_section, dest_reg, ptr);
                                } else {
                                    if (ptr != dest_reg) emit_mov_reg_reg(&linker->text_section, dest_reg, ptr);
                                    if (idx != idx_scratch) emit_mov_reg_reg(&linker->text_section, idx_scratch, idx);
                                    idx = idx_scratch;
                                }
                                
                                if (element_size == 1) {
                                    emit_alu_reg_reg(&linker->text_section, 1, 0x01, dest_reg, idx);
                                } else if ((element_size == 2 || element_size == 4 || element_size == 8) && (idx & 7) != 4) {
                                    // lea dest, [dest + idx * scale] (利用 SIB 寻址优化)
                                    int scale = (element_size == 2) ? 1 : (element_size == 4) ? 2 : 3;
                                    emit_rex(&linker->text_section, 1, dest_reg > 7, idx > 7, dest_reg > 7);
                                    emit8(&linker->text_section, 0x8D);
                                    int b = dest_reg & 7;
                                    if (b == 5) {
                                        emit_modrm(&linker->text_section, 1, dest_reg & 7, 4);
                                        emit_sib(&linker->text_section, scale, idx & 7, b);
                                        emit8(&linker->text_section, 0);
                                    } else {
                                        emit_modrm(&linker->text_section, 0, dest_reg & 7, 4);
                                        emit_sib(&linker->text_section, scale, idx & 7, b);
                                    }
                                } else {
                                    // imul idx, element_size
                                    emit_rex(&linker->text_section, 1, idx > 7, 0, idx > 7);
                                    if (element_size >= -128 && element_size <= 127) {
                                        emit8(&linker->text_section, 0x6B);
                                        emit_modrm(&linker->text_section, 3, idx & 7, idx & 7);
                                        emit8(&linker->text_section, (uint8_t)element_size);
                                    } else {
                                        emit8(&linker->text_section, 0x69);
                                        emit_modrm(&linker->text_section, 3, idx & 7, idx & 7);
                                        emit32(&linker->text_section, element_size);
                                    }
                                    emit_alu_reg_reg(&linker->text_section, 1, 0x01, dest_reg, idx);
                                }
                            }
                            store_result(&linker->text_section, &allocator, inst->dest, dest_reg);
                            break;
                        }
                        case SIR_ICMP_LT:
                        case SIR_ICMP_GT:
                        case SIR_ICMP_LE:
                        case SIR_ICMP_GE:
                        case SIR_ICMP_EQ:
                        case SIR_ICMP_NE: {
                            int right_color = (inst->operands[1] && inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                            int right_phys = (right_color != -1) ? get_phys_reg(right_color) : -1;
                            
                            int left_scratch = (right_phys == REG_RAX) ? REG_RCX : REG_RAX;
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], left_scratch, &ctx);
                            
                            int w = (inst->operands[0]->type && type_get_size(inst->operands[0]->type) <= 4) ? 0 : 1;
                            if (inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                                int32_t imm = (int32_t)inst->operands[1]->as.int_val;
                                if (imm == 0) {
                                    if (w || left > 7) emit_rex(&linker->text_section, w, left > 7, 0, left > 7);
                                    emit8(&linker->text_section, 0x85); // test left, left
                                    emit_modrm(&linker->text_section, 3, left & 7, left & 7);
                                } else {
                                    emit_alu_reg_imm32(&linker->text_section, w, 7, left, imm); // cmp left, imm
                                }
                            } else {
                                int right_scratch = (left == REG_RCX) ? REG_RDX : REG_RCX;
                                int right = load_operand(&linker->text_section, &allocator, inst->operands[1], right_scratch, &ctx);
                                if (w || right > 7 || left > 7) emit_rex(&linker->text_section, w, right > 7, 0, left > 7);
                                emit8(&linker->text_section, 0x39); // cmp left, right
                                emit_modrm(&linker->text_section, 3, right & 7, left & 7);
                            }
                            
                            bool is_unsigned = type_is_unsigned(inst->operands[0]->type);
                            
                            bool can_fuse = false;
                            SirInst* next_inst = inst->next;
                            if (next_inst && next_inst->opcode == SIR_BR && next_inst->operands[0]->kind == SIR_VAL_VREG && inst->dest->kind == SIR_VAL_VREG && next_inst->operands[0]->as.vreg == inst->dest->as.vreg) {
                                bool used_elsewhere = false;
                                for (SirInst* scan = next_inst->next; scan; scan = scan->next) {
                                    for (int i=0; i<scan->num_operands; i++) {
                                        if (scan->operands[i] && scan->operands[i]->kind == SIR_VAL_VREG && scan->operands[i]->as.vreg == inst->dest->as.vreg) {
                                            used_elsewhere = true; break;
                                        }
                                    }
                                    if (used_elsewhere) break;
                                }
                                if (!used_elsewhere) can_fuse = true;
                            }

                            if (can_fuse) {
                                uint8_t jcc = 0x84, inv_jcc = 0x85;
                                if (inst->opcode == SIR_ICMP_NE) { jcc = 0x85; inv_jcc = 0x84; }
                                else if (inst->opcode == SIR_ICMP_LT) { jcc = is_unsigned ? 0x82 : 0x8C; inv_jcc = is_unsigned ? 0x83 : 0x8D; }
                                else if (inst->opcode == SIR_ICMP_LE) { jcc = is_unsigned ? 0x86 : 0x8E; inv_jcc = is_unsigned ? 0x87 : 0x8F; }
                                else if (inst->opcode == SIR_ICMP_GT) { jcc = is_unsigned ? 0x87 : 0x8F; inv_jcc = is_unsigned ? 0x86 : 0x8E; }
                                else if (inst->opcode == SIR_ICMP_GE) { jcc = is_unsigned ? 0x83 : 0x8D; inv_jcc = is_unsigned ? 0x82 : 0x8C; }
                                
                                uint32_t t_id = next_inst->operands[1]->as.block->id;
                                uint32_t t_off = t_id < 1024 ? block_offsets[t_id] : 0;
                                uint32_t f_id = next_inst->operands[2]->as.block->id;
                                uint32_t f_off = f_id < 1024 ? block_offsets[f_id] : 0;

                                if (block->next && next_inst->operands[2]->as.block == block->next) {
                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, jcc);
                                    emit32(&linker->text_section, (uint32_t)(t_off - (linker->text_section.size + 4)));
                                } else if (block->next && next_inst->operands[1]->as.block == block->next) {
                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, inv_jcc);
                                    emit32(&linker->text_section, (uint32_t)(f_off - (linker->text_section.size + 4)));
                                } else {
                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, jcc);
                                    emit32(&linker->text_section, (uint32_t)(t_off - (linker->text_section.size + 4)));
                                    emit8(&linker->text_section, 0xE9); // jmp false_block
                                    emit32(&linker->text_section, (uint32_t)(f_off - (linker->text_section.size + 4)));
                                }
                                
                                inst = next_inst; // 跳过下一个 BR 指令
                            } else {
                                emit8(&linker->text_section, 0x0F); // setCC al
                                if (inst->opcode == SIR_ICMP_EQ) emit8(&linker->text_section, 0x94);
                                else if (inst->opcode == SIR_ICMP_NE) emit8(&linker->text_section, 0x95);
                                else if (inst->opcode == SIR_ICMP_LT) emit8(&linker->text_section, is_unsigned ? 0x92 : 0x9C);
                                else if (inst->opcode == SIR_ICMP_LE) emit8(&linker->text_section, is_unsigned ? 0x96 : 0x9E);
                                else if (inst->opcode == SIR_ICMP_GT) emit8(&linker->text_section, is_unsigned ? 0x97 : 0x9F);
                                else if (inst->opcode == SIR_ICMP_GE) emit8(&linker->text_section, is_unsigned ? 0x93 : 0x9D);
                                emit_modrm(&linker->text_section, 3, 0, REG_RAX);
                                
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0xB6); // movzx rax, al
                                emit_modrm(&linker->text_section, 3, REG_RAX, REG_RAX);
                                store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            }
                            break;
                        }
                        case SIR_FCMP_LT:
                        case SIR_FCMP_GT:
                        case SIR_FCMP_LE:
                        case SIR_FCMP_GE:
                        case SIR_FCMP_EQ:
                        case SIR_FCMP_NE: {
                            bool is_f32 = (inst->operands[0]->type && inst->operands[0]->type->kind == TY_F32);
                            
                            int right_color = (inst->operands[1] && inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                            int right_phys = (right_color != -1) ? get_phys_reg(right_color) : -1;
                            
                            int left_scratch = (right_phys == REG_RAX) ? REG_RCX : REG_RAX;
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], left_scratch, &ctx);
                            
                            int right_scratch = (left == REG_RCX) ? REG_RDX : REG_RCX;
                            int right = load_operand(&linker->text_section, &allocator, inst->operands[1], right_scratch, &ctx);
                            
                            if (left == REG_RCX && right == REG_RAX) {
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x93); // xchg rax, rcx
                            } else if (right == REG_RAX) {
                                emit_mov_reg_reg(&linker->text_section, REG_RCX, right);
                                emit_mov_reg_reg(&linker->text_section, REG_RAX, left);
                            } else {
                                if (left != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, left);
                                if (right != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, right);
                            }
                            
                            // movd/movq xmm0, rax
                            emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); emit_modrm(&linker->text_section, 3, 0, REG_RAX);
                            // movd/movq xmm1, rcx
                            emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); emit_modrm(&linker->text_section, 3, 1, REG_RCX);
                            
                            // ucomiss/ucomisd xmm0, xmm1
                            if (!is_f32) emit8(&linker->text_section, 0x66);
                            emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x2E); emit_modrm(&linker->text_section, 3, 0, 1);
                            
                            bool can_fuse = false;
                            SirInst* next_inst = inst->next;
                            if (next_inst && next_inst->opcode == SIR_BR && next_inst->operands[0]->kind == SIR_VAL_VREG && inst->dest->kind == SIR_VAL_VREG && next_inst->operands[0]->as.vreg == inst->dest->as.vreg) {
                                bool used_elsewhere = false;
                                for (SirInst* scan = next_inst->next; scan; scan = scan->next) {
                                    for (int i=0; i<scan->num_operands; i++) {
                                        if (scan->operands[i] && scan->operands[i]->kind == SIR_VAL_VREG && scan->operands[i]->as.vreg == inst->dest->as.vreg) {
                                            used_elsewhere = true; break;
                                        }
                                    }
                                    if (used_elsewhere) break;
                                }
                                if (!used_elsewhere) can_fuse = true;
                            }

                            if (can_fuse) {
                                uint8_t jcc = 0x84, inv_jcc = 0x85;
                                if (inst->opcode == SIR_FCMP_NE) { jcc = 0x85; inv_jcc = 0x84; }
                                else if (inst->opcode == SIR_FCMP_LT) { jcc = 0x82; inv_jcc = 0x83; }
                                else if (inst->opcode == SIR_FCMP_LE) { jcc = 0x86; inv_jcc = 0x87; }
                                else if (inst->opcode == SIR_FCMP_GT) { jcc = 0x87; inv_jcc = 0x86; }
                                else if (inst->opcode == SIR_FCMP_GE) { jcc = 0x83; inv_jcc = 0x82; }
                                
                                uint32_t t_id = next_inst->operands[1]->as.block->id;
                                uint32_t t_off = t_id < 1024 ? block_offsets[t_id] : 0;
                                uint32_t f_id = next_inst->operands[2]->as.block->id;
                                uint32_t f_off = f_id < 1024 ? block_offsets[f_id] : 0;

                                if (block->next && next_inst->operands[2]->as.block == block->next) {
                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, jcc);
                                    emit32(&linker->text_section, (uint32_t)(t_off - (linker->text_section.size + 4)));
                                } else if (block->next && next_inst->operands[1]->as.block == block->next) {
                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, inv_jcc);
                                    emit32(&linker->text_section, (uint32_t)(f_off - (linker->text_section.size + 4)));
                                } else {
                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, jcc);
                                    emit32(&linker->text_section, (uint32_t)(t_off - (linker->text_section.size + 4)));
                                    emit8(&linker->text_section, 0xE9); // jmp false_block
                                    emit32(&linker->text_section, (uint32_t)(f_off - (linker->text_section.size + 4)));
                                }
                                
                                inst = next_inst; // 跳过下一个 BR 指令
                            } else {
                                emit8(&linker->text_section, 0x0F); // setCC al
                                if (inst->opcode == SIR_FCMP_EQ) emit8(&linker->text_section, 0x94); // sete
                                else if (inst->opcode == SIR_FCMP_NE) emit8(&linker->text_section, 0x95); // setne
                                else if (inst->opcode == SIR_FCMP_LT) emit8(&linker->text_section, 0x92); // setb
                                else if (inst->opcode == SIR_FCMP_LE) emit8(&linker->text_section, 0x96); // setbe
                                else if (inst->opcode == SIR_FCMP_GT) emit8(&linker->text_section, 0x97); // seta
                                else if (inst->opcode == SIR_FCMP_GE) emit8(&linker->text_section, 0x93); // setae
                                emit_modrm(&linker->text_section, 3, 0, REG_RAX);
                                
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0xB6); // movzx rax, al
                                emit_modrm(&linker->text_section, 3, REG_RAX, REG_RAX);
                                store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            }
                            break;
                        }
                        case SIR_JMP: {
                            if (block->next && inst->operands[0]->as.block == block->next) {
                                break; // fall-through
                            }
                            uint32_t t_id = inst->operands[0]->as.block->id;
                            uint32_t t_off = t_id < 1024 ? block_offsets[t_id] : 0;
                            emit8(&linker->text_section, 0xE9); // jmp rel32
                            emit32(&linker->text_section, (uint32_t)(t_off - (linker->text_section.size + 4)));
                            break;
                        }
                        case SIR_CALL: {
                            if (inst->operands[0]->kind == SIR_VAL_GLOBAL && strcmp(inst->operands[0]->as.global_name, "scribe") == 0) {
                                ScoriaType* arg_type = inst->operands[1]->type;
                                if (arg_type && arg_type->kind == TY_VIA) arg_type = arg_type->as.inner;
                                bool is_str = (arg_type && arg_type->kind == TY_COHORS && arg_type->as.inner->kind == TY_LITTERA);
                                bool is_bool = (arg_type && arg_type->kind == TY_LOGICA) || (inst->operands[1]->kind == SIR_VAL_CONST_BOOL);
                                bool is_ptr = !is_str && (arg_type && (arg_type->kind == TY_VIA || arg_type->kind == TY_COHORS || arg_type->kind == TY_ACIES));
                                bool is_float = (arg_type && (arg_type->kind == TY_F32 || arg_type->kind == TY_F64)) || (inst->operands[1]->kind == SIR_VAL_CONST_FLOAT);
                                
                                int arg = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RAX, &ctx);
                                if (arg != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, arg);
                                
                                if (is_str) {
                                    // mov rdx, [rax + 8]
                                    emit_rex(&linker->text_section, 1, 0, 0, 0);
                                    emit8(&linker->text_section, 0x8B);
                                    emit_mem(&linker->text_section, REG_RDX, REG_RAX, 8);
                                    
                                    // mov rcx, [rax]
                                    emit_rex(&linker->text_section, 1, 0, 0, 0);
                                    emit8(&linker->text_section, 0x8B);
                                    emit_mem(&linker->text_section, REG_RCX, REG_RAX, 0);
                                    
                                    emit8(&linker->text_section, 0xE8); // call rel32
                                    if (pass == 1) g_print_str_relocs[g_print_str_reloc_count++] = (uint32_t)linker->text_section.size;
                                    emit32(&linker->text_section, 0); // 占位符
                                } else if (is_bool) {
                                    emit_mov_reg_reg(&linker->text_section, REG_RCX, REG_RAX);
                                    emit8(&linker->text_section, 0xE8); // call rel32
                                    if (pass == 1) g_print_bool_relocs[g_print_bool_reloc_count++] = (uint32_t)linker->text_section.size;
                                    emit32(&linker->text_section, 0);
                                } else if (is_ptr) {
                                    emit_mov_reg_reg(&linker->text_section, REG_RCX, REG_RAX);
                                    emit8(&linker->text_section, 0xE8); // call rel32
                                    if (pass == 1) g_print_hex_relocs[g_print_hex_reloc_count++] = (uint32_t)linker->text_section.size;
                                    emit32(&linker->text_section, 0);
                                } else if (is_float) {
                                    emit_mov_reg_reg(&linker->text_section, REG_RCX, REG_RAX);
                                    if (inst->operands[1]->type && inst->operands[1]->type->kind == TY_F32) {
                                        // movd xmm0, ecx
                                        emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, 0, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); emit_modrm(&linker->text_section, 3, 0, REG_RCX);
                                        // cvtss2sd xmm0, xmm0
                                        emit8(&linker->text_section, 0xF3); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x5A); emit_modrm(&linker->text_section, 3, 0, 0);
                                        // movq rcx, xmm0
                                        emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x7E); emit_modrm(&linker->text_section, 3, 0, REG_RCX);
                                    }
                                    emit8(&linker->text_section, 0xE8); // call rel32
                                    if (pass == 1) g_print_float_relocs[g_print_float_reloc_count++] = (uint32_t)linker->text_section.size;
                                    emit32(&linker->text_section, 0);
                                } else {
                                    emit_mov_reg_reg(&linker->text_section, REG_RCX, REG_RAX);
                                    emit8(&linker->text_section, 0xE8); // call rel32
                                    if (pass == 1) g_print_int_relocs[g_print_int_reloc_count++] = (uint32_t)linker->text_section.size;
                                    emit32(&linker->text_section, 0); // 占位符
                                }
                                break;
                            }
                            if (inst->operands[0]->kind == SIR_VAL_GLOBAL && strcmp(inst->operands[0]->as.global_name, "crea") == 0) {
                                int arg = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                                if (arg != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, arg);
                                emit8(&linker->text_section, 0xE8); // call rel32
                                if (pass == 1) g_crea_relocs[g_crea_reloc_count++] = (uint32_t)linker->text_section.size;
                                emit32(&linker->text_section, 0);
                                
                                if (inst->dest) store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                                break;
                            }
                            if (inst->operands[0]->kind == SIR_VAL_GLOBAL && strcmp(inst->operands[0]->as.global_name, "neca") == 0) {
                                int arg = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                                if (arg != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, arg);
                                emit8(&linker->text_section, 0xE8); // call rel32
                                if (pass == 1) g_neca_relocs[g_neca_reloc_count++] = (uint32_t)linker->text_section.size;
                                emit32(&linker->text_section, 0);
                                break;
                            }

                            int arg_regs[] = {REG_RCX, REG_RDX, 8, 9};
                            int num_args = inst->num_operands - 1;
                            
                            bool is_tail_call = false;
                            if (inst->next && inst->next->opcode == SIR_RET && inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                                if (inst->next->num_operands == 0 || (inst->dest && inst->next->operands[0]->kind == SIR_VAL_VREG && inst->next->operands[0]->as.vreg == inst->dest->as.vreg)) {
                                    if (num_args <= 4) is_tail_call = true; // 仅对参数<=4的调用进行 TCO，避免覆盖 Caller 栈参数
                                }
                            }
                            
                            // 1. 处理栈传递的参数 (i >= 4)
                            for (int i = num_args - 1; i >= 4; i--) {
                                int val = load_operand(&linker->text_section, &allocator, inst->operands[i+1], REG_RAX, &ctx);
                                emit_rex(&linker->text_section, 1, val > 7, 0, 0);
                                emit8(&linker->text_section, 0x89);
                                emit_mem(&linker->text_section, val, REG_RSP, 32 + (i - 4) * 8);
                            }
                            
                            // 2. 处理寄存器传递的参数 (i < 4)
                            int reg_args = num_args > 4 ? 4 : num_args;
                            if (reg_args == 1) {
                                bool already_in_rcx = false;
                                if (inst->prev && (inst->prev->opcode == SIR_ADD || inst->prev->opcode == SIR_SUB || inst->prev->opcode == SIR_MUL || inst->prev->opcode == SIR_AND || inst->prev->opcode == SIR_OR || inst->prev->opcode == SIR_XOR) && inst->prev->dest == inst->operands[1]) {
                                    if (inst->operands[1]->kind == SIR_VAL_VREG && allocator.use_count[inst->operands[1]->as.vreg] == 2) {
                                        already_in_rcx = true;
                                    }
                                }
                                if (!already_in_rcx) {
                                    int val = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RAX, &ctx);
                                    int w = (inst->operands[1]->type && type_get_size(inst->operands[1]->type) <= 4) ? 0 : 1;
                                    if (val != REG_RCX) emit_mov_reg_reg_w(&linker->text_section, w, REG_RCX, val);
                                }
                                bool is_float = (inst->operands[1]->type && (inst->operands[1]->type->kind == TY_F32 || inst->operands[1]->type->kind == TY_F64));
                                if (is_float) {
                                    bool is_f32 = (inst->operands[1]->type->kind == TY_F32);
                                    emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); 
                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); 
                                    emit_modrm(&linker->text_section, 3, 0, REG_RCX);
                                }
                            } else if (reg_args == 2) {
                                int val0 = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RAX, &ctx);
                                int val1_scratch = (val0 == REG_R10) ? REG_R11 : REG_R10;
                                int val1 = load_operand(&linker->text_section, &allocator, inst->operands[2], val1_scratch, &ctx);
                                
                                if (val0 == REG_RDX && val1 == REG_RCX) {
                                    emit_rex(&linker->text_section, 1, 0, 0, 0);
                                    emit8(&linker->text_section, 0x87); // xchg rcx, rdx
                                    emit_modrm(&linker->text_section, 3, REG_RCX, REG_RDX);
                                } else if (val1 == REG_RCX) {
                                    if (val1 != REG_RDX) emit_mov_reg_reg(&linker->text_section, REG_RDX, val1);
                                    if (val0 != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, val0);
                                } else {
                                    if (val0 != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, val0);
                                    if (val1 != REG_RDX) emit_mov_reg_reg(&linker->text_section, REG_RDX, val1);
                                }
                                
                                for (int i = 0; i < 2; i++) {
                                    bool is_float = (inst->operands[i+1]->type && (inst->operands[i+1]->type->kind == TY_F32 || inst->operands[i+1]->type->kind == TY_F64));
                                    if (is_float) {
                                        bool is_f32 = (inst->operands[i+1]->type->kind == TY_F32);
                                        emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); 
                                        emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); 
                                        emit_modrm(&linker->text_section, 3, i, arg_regs[i] & 7);
                                    }
                                }
                            } else {
                                for (int i = 0; i < reg_args; i++) {
                                    int val = load_operand(&linker->text_section, &allocator, inst->operands[i+1], REG_RAX, &ctx);
                                    emit_rex(&linker->text_section, 1, val > 7, 0, 0);
                                    emit8(&linker->text_section, 0x89); // mov [rsp + i*8], val
                                    emit_mem(&linker->text_section, val, REG_RSP, i * 8);
                                }
                                for (int i = 0; i < reg_args; i++) {
                                    emit_rex(&linker->text_section, 1, arg_regs[i] > 7, 0, 0);
                                    emit8(&linker->text_section, 0x8B); // mov arg_regs[i], [rsp + i*8]
                                    emit_mem(&linker->text_section, arg_regs[i] & 7, REG_RSP, i * 8);
                                    
                                    bool is_float = (inst->operands[i+1]->type && (inst->operands[i+1]->type->kind == TY_F32 || inst->operands[i+1]->type->kind == TY_F64));
                                    if (is_float) {
                                        bool is_f32 = (inst->operands[i+1]->type->kind == TY_F32);
                                        emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, arg_regs[i] > 7); 
                                        emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); 
                                        emit_modrm(&linker->text_section, 3, i, arg_regs[i] & 7);
                                    }
                                }
                            }
                            
                            bool is_extern = false;
                            int target_idx = 0;
                            if (inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                                int current_idx = 0;
                                for (SirExternFunc* ext = ctx.first_extern; ext; ext = ext->next) {
                                    if (strcmp(ext->name, inst->operands[0]->as.global_name) == 0) {
                                        is_extern = true;
                                        target_idx = current_idx;
                                        break;
                                    }
                                    current_idx++;
                                }
                            }

                            if (is_extern) {
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x31);
                                emit_modrm(&linker->text_section, 3, REG_RAX, REG_RAX);
                            }
                            
                            if (is_tail_call) {
                                // 尾调用优化：提前执行 Epilogue
                                if (stack_sub_size > 0) {
                                    emit_rex(&linker->text_section, 1, 0, 0, 0);
                                    if (stack_sub_size <= 127) {
                                        emit8(&linker->text_section, 0x83);
                                        emit_modrm(&linker->text_section, 3, 0, REG_RSP);
                                        emit8(&linker->text_section, (uint8_t)stack_sub_size);
                                    } else {
                                        emit8(&linker->text_section, 0x81);
                                        emit_modrm(&linker->text_section, 3, 0, REG_RSP);
                                        emit32(&linker->text_section, (uint32_t)stack_sub_size);
                                    }
                                }
                                if (allocator.used_callee_saved[6]) { emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x5F); }
                                if (allocator.used_callee_saved[5]) { emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x5E); }
                                if (allocator.used_callee_saved[4]) { emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x5D); }
                                if (allocator.used_callee_saved[3]) { emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x5C); }
                                if (allocator.used_callee_saved[2]) emit8(&linker->text_section, 0x5F);
                                if (allocator.used_callee_saved[1]) emit8(&linker->text_section, 0x5E);
                                if (allocator.used_callee_saved[0]) emit8(&linker->text_section, 0x5B);
                                
                                if (is_extern) {
                                    emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x25); // jmp [rip + rel32]
                                    if (ctx.pass == 1) {
                                        g_extern_relocs[g_extern_reloc_count] = (uint32_t)linker->text_section.size;
                                        g_extern_idxs[g_extern_reloc_count] = target_idx;
                                        g_extern_reloc_count++;
                                    }
                                    emit32(&linker->text_section, 0);
                                } else {
                                    uint32_t target_offset = 0;
                                    for (int f = 0; f < ctx.func_count; f++) {
                                        if (strcmp(ctx.funcs[f], inst->operands[0]->as.global_name) == 0) {
                                            target_offset = ctx.func_offsets[f];
                                            break;
                                        }
                                    }
                                    emit8(&linker->text_section, 0xE9); // jmp rel32
                                    emit32(&linker->text_section, (uint32_t)(target_offset - (linker->text_section.size + 4)));
                                }
                                
                                inst = inst->next; // 跳过紧跟的 RET 指令
                                break;
                            }

                            if (inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                                if (is_extern) {
                                    emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x15); // call [rip + rel32]
                                    if (ctx.pass == 1) {
                                        g_extern_relocs[g_extern_reloc_count] = (uint32_t)linker->text_section.size;
                                        g_extern_idxs[g_extern_reloc_count] = target_idx;
                                        g_extern_reloc_count++;
                                    }
                                    emit32(&linker->text_section, 0);
                                } else {
                                    uint32_t target_offset = 0;
                                    for (int f = 0; f < ctx.func_count; f++) {
                                        if (strcmp(ctx.funcs[f], inst->operands[0]->as.global_name) == 0) {
                                            target_offset = ctx.func_offsets[f];
                                            break;
                                        }
                                    }
                                    emit8(&linker->text_section, 0xE8); // call rel32
                                    emit32(&linker->text_section, (uint32_t)(target_offset - (linker->text_section.size + 4)));
                                }
                            } else {
                                int callee_reg = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_R10, &ctx);
                                emit_rex(&linker->text_section, 1, 0, 0, callee_reg > 7);
                                emit8(&linker->text_section, 0xFF);
                                emit_modrm(&linker->text_section, 3, 2, callee_reg & 7); // call r/m64
                            }
                            
                            if (inst->dest) {
                                bool ret_is_float = (inst->dest->type && (inst->dest->type->kind == TY_F32 || inst->dest->type->kind == TY_F64));
                                if (ret_is_float) {
                                    bool is_f32 = (inst->dest->type->kind == TY_F32);
                                    // movd/movq rax, xmm0
                                    emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); 
                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x7E); 
                                    emit_modrm(&linker->text_section, 3, 0, REG_RAX);
                                }
                                bool skip_store = false;
                                if (!ret_is_float && inst->next && (inst->next->opcode == SIR_ADD || inst->next->opcode == SIR_SUB || inst->next->opcode == SIR_MUL || inst->next->opcode == SIR_AND || inst->next->opcode == SIR_OR || inst->next->opcode == SIR_XOR)) {
                                    if (inst->next->operands[0] == inst->dest || inst->next->operands[1] == inst->dest) {
                                        if (allocator.use_count[inst->dest->as.vreg] == 2 || (inst->next->next && inst->next->next->opcode == SIR_RET)) {
                                            skip_store = true;
                                        }
                                    }
                                }
                                if (!skip_store) {
                                    store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                                }
                            }
                            break;
                        }
                        case SIR_BR: {
                            int cond = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                            if (cond > 7) emit_rex(&linker->text_section, 0, cond > 7, 0, cond > 7);
                            emit8(&linker->text_section, 0x85); // test cond, cond
                            emit_modrm(&linker->text_section, 3, cond & 7, cond & 7);
                            
                            uint32_t t_id = inst->operands[1]->as.block->id;
                            uint32_t t_off = t_id < 1024 ? block_offsets[t_id] : 0;
                            uint32_t f_id = inst->operands[2]->as.block->id;
                            uint32_t f_off = f_id < 1024 ? block_offsets[f_id] : 0;

                            if (block->next && inst->operands[2]->as.block == block->next) {
                                emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x85); // jne true_block
                                emit32(&linker->text_section, (uint32_t)(t_off - (linker->text_section.size + 4)));
                            } else if (block->next && inst->operands[1]->as.block == block->next) {
                                emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x84); // je false_block
                                emit32(&linker->text_section, (uint32_t)(f_off - (linker->text_section.size + 4)));
                            } else {
                                emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x85); // jne true_block
                                emit32(&linker->text_section, (uint32_t)(t_off - (linker->text_section.size + 4)));
                                emit8(&linker->text_section, 0xE9); // jmp false_block
                                emit32(&linker->text_section, (uint32_t)(f_off - (linker->text_section.size + 4)));
                            }
                            break;
                        }
                        case SIR_SWITCH: {
                            int cond_reg = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                            if (cond_reg != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, cond_reg);

                            int case_count = (inst->num_operands - 2) / 2;
                            SirBlock* def_block = inst->operands[1]->as.block;

                            bool can_jump_table = true;
                            int64_t min_val = INT64_MAX;
                            int64_t max_val = INT64_MIN;

                            for (int i = 0; i < case_count; i++) {
                                SirValue* cval = inst->operands[2 + i * 2];
                                if (cval->kind != SIR_VAL_CONST_INT) {
                                    can_jump_table = false;
                                    break;
                                }
                                if (cval->as.int_val < min_val) min_val = cval->as.int_val;
                                if (cval->as.int_val > max_val) max_val = cval->as.int_val;
                            }

                            if (case_count == 0) can_jump_table = false;
                            if (can_jump_table && (max_val - min_val > 256)) can_jump_table = false;

                            if (can_jump_table) {
                                // sub rax, min_val
                                if (min_val != 0) emit_alu_reg_imm32(&linker->text_section, 1, 5, REG_RAX, (int32_t)min_val);
                                // cmp rax, max_val - min_val
                                emit_alu_reg_imm32(&linker->text_section, 1, 7, REG_RAX, (int32_t)(max_val - min_val));

                                // ja default_block
                                uint32_t def_id = def_block->id;
                                uint32_t def_off = def_id < 1024 ? block_offsets[def_id] : 0;
                                emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x87);
                                emit32(&linker->text_section, (uint32_t)(def_off - (linker->text_section.size + 4)));

                                // lea rcx, [rip + table]
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x8D);
                                emit_modrm(&linker->text_section, 0, REG_RCX, 5);
                                uint32_t lea_rip_offset = (uint32_t)linker->text_section.size;
                                emit32(&linker->text_section, 0); // 占位符

                                // movsxd rdx, dword ptr [rcx + rax*4]
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x63);
                                emit_modrm(&linker->text_section, 0, REG_RDX, 4); // SIB
                                emit_sib(&linker->text_section, 2, REG_RAX, REG_RCX); // scale=4, index=rax, base=rcx

                                // add rdx, rcx
                                emit_alu_reg_reg(&linker->text_section, 1, 0x01, REG_RDX, REG_RCX);

                                // jmp rdx
                                emit8(&linker->text_section, 0xFF);
                                emit_modrm(&linker->text_section, 3, 4, REG_RDX);

                                // jmp over table
                                emit8(&linker->text_section, 0xE9);
                                uint32_t jmp_over_offset = (uint32_t)linker->text_section.size;
                                emit32(&linker->text_section, 0);

                                // 写入跳转表 (Table)
                                uint32_t table_start = (uint32_t)linker->text_section.size;

                                // 回填 lea rcx, [rip + table]
                                int32_t rel_table = (int32_t)(table_start - (lea_rip_offset + 4));
                                memcpy(linker->text_section.buffer + lea_rip_offset, &rel_table, 4);

                                for (int64_t v = min_val; v <= max_val; v++) {
                                    SirBlock* target = def_block;
                                    for (int i = 0; i < case_count; i++) {
                                        if (inst->operands[2 + i * 2]->as.int_val == v) {
                                            target = inst->operands[2 + i * 2 + 1]->as.block;
                                            break;
                                        }
                                    }
                                    uint32_t t_id = target->id;
                                    uint32_t t_off = t_id < 1024 ? block_offsets[t_id] : 0;
                                    int32_t rel_target = (int32_t)(t_off - table_start);
                                    emit32(&linker->text_section, (uint32_t)rel_target);
                                }

                                // 回填 jmp over table
                                int32_t rel_end = (int32_t)(linker->text_section.size - (jmp_over_offset + 4));
                                memcpy(linker->text_section.buffer + jmp_over_offset, &rel_end, 4);

                            } else {
                                // 退化为 If-Else 链
                                for (int i = 0; i < case_count; i++) {
                                    int val_reg = load_operand(&linker->text_section, &allocator, inst->operands[2 + i * 2], REG_RCX, &ctx);
                                    emit_rex(&linker->text_section, 1, val_reg > 7, 0, 0);
                                    emit8(&linker->text_section, 0x39); // cmp rax, val_reg
                                    emit_modrm(&linker->text_section, 3, val_reg & 7, REG_RAX);

                                    SirBlock* target = inst->operands[2 + i * 2 + 1]->as.block;
                                    uint32_t t_id = target->id;
                                    uint32_t t_off = t_id < 1024 ? block_offsets[t_id] : 0;

                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x84); // je
                                    emit32(&linker->text_section, (uint32_t)(t_off - (linker->text_section.size + 4)));
                                }
                                uint32_t def_id = def_block->id;
                                uint32_t def_off = def_id < 1024 ? block_offsets[def_id] : 0;
                                emit8(&linker->text_section, 0xE9); // jmp
                                emit32(&linker->text_section, (uint32_t)(def_off - (linker->text_section.size + 4)));
                            }
                            break;
                        }
                        case SIR_RET: {
                            if (inst->num_operands > 0) {
                                bool already_in_rax = false;
                                if (inst->prev && (inst->prev->opcode == SIR_ADD || inst->prev->opcode == SIR_SUB || inst->prev->opcode == SIR_MUL || inst->prev->opcode == SIR_AND || inst->prev->opcode == SIR_OR || inst->prev->opcode == SIR_XOR) && inst->prev->dest == inst->operands[0]) {
                                    if (inst->operands[0]->kind == SIR_VAL_VREG && (allocator.use_count[inst->operands[0]->as.vreg] == 2 || true)) {
                                        already_in_rax = true;
                                    }
                                }
                                if (!already_in_rax) {
                                    int val = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                                    int w = (inst->operands[0]->type && type_get_size(inst->operands[0]->type) <= 4) ? 0 : 1;
                                    if (val != REG_RAX) emit_mov_reg_reg_w(&linker->text_section, w, REG_RAX, val);
                                }
                                
                                bool ret_is_float = (inst->operands[0]->type && (inst->operands[0]->type->kind == TY_F32 || inst->operands[0]->type->kind == TY_F64));
                                if (ret_is_float) {
                                    bool is_f32 = (inst->operands[0]->type->kind == TY_F32);
                                    // movd/movq xmm0, rax
                                    emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); 
                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); 
                                    emit_modrm(&linker->text_section, 3, 0, REG_RAX);
                                }
                            }
                            // 跋 (Epilogue)
                            if (stack_sub_size > 0) {
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                if (stack_sub_size <= 127) {
                                    emit8(&linker->text_section, 0x83); // 优化: add rsp, imm8
                                    emit_modrm(&linker->text_section, 3, 0, REG_RSP);
                                    emit8(&linker->text_section, (uint8_t)stack_sub_size);
                                } else {
                                    emit8(&linker->text_section, 0x81); // add rsp, imm32
                                    emit_modrm(&linker->text_section, 3, 0, REG_RSP);
                                    emit32(&linker->text_section, (uint32_t)stack_sub_size);
                                }
                            }
                            
                            if (allocator.used_callee_saved[6]) { emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x5F); } // pop r15
                            if (allocator.used_callee_saved[5]) { emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x5E); } // pop r14
                            if (allocator.used_callee_saved[4]) { emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x5D); } // pop r13
                            if (allocator.used_callee_saved[3]) { emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x5C); } // pop r12
                            if (allocator.used_callee_saved[2]) emit8(&linker->text_section, 0x5F); // pop rdi
                            if (allocator.used_callee_saved[1]) emit8(&linker->text_section, 0x5E); // pop rsi
                            if (allocator.used_callee_saved[0]) emit8(&linker->text_section, 0x5B); // pop rbx
                            
                            emit8(&linker->text_section, 0xC3); // ret
                            break;
                        }
                        default: break;
                    }
                }
            }
            free(alloca_offsets);
            reg_alloc_free(&allocator);
        }

        // 追加内置汇编例程
        pe_builtins_generate(linker, g_princeps_offset, g_init_offset);
    }

    free(block_offsets);
    free(func_names);
    free(func_offsets);
    free(strings);
    free(string_lens);
    free(string_offsets);
    free(fp_infos);
}

bool pe_linker_generate_executable(PeLinker* linker, SirModule* module, const char* output_filename) {
    generate_machine_code(linker, module);

    FILE* out = fopen(output_filename, "wb");
    if (!out) return false;

    uint32_t file_align = 0x200;
    uint32_t sec_align = 0x1000;

    // 1. DOS Header
    DosHeader dos = {0};
    dos.e_magic = 0x5A4D; // "MZ"
    dos.e_lfanew = (uint32_t)sizeof(DosHeader);

    int num_sections = linker->data_section.size > 0 ? 3 : 2;

    // 2. COFF Header
    CoffHeader coff = {0};
    coff.Signature = 0x00004550; // "PE\0\0"
    coff.Machine = 0x8664;       // x86_64
    coff.NumberOfSections = (uint16_t)num_sections;
    coff.SizeOfOptionalHeader = (uint16_t)sizeof(OptionalHeader64);
    coff.Characteristics = 0x0022;

    // 3. Optional Header
    OptionalHeader64 opt = {0};
    opt.Magic = 0x020B; // PE32+
    opt.AddressOfEntryPoint = sec_align + linker->entry_point_offset;
    opt.BaseOfCode = sec_align;
    opt.ImageBase = 0x140000000ULL;
    opt.SectionAlignment = sec_align;
    opt.FileAlignment = file_align;
    opt.MajorOperatingSystemVersion = 5;
    opt.MinorOperatingSystemVersion = 2;
    opt.MajorSubsystemVersion = 5;
    opt.MinorSubsystemVersion = 2;
    opt.SizeOfHeaders = align_up((uint32_t)(sizeof(DosHeader) + sizeof(CoffHeader) + sizeof(OptionalHeader64) + sizeof(SectionHeader)), file_align);
    opt.SizeOfImage = align_up(opt.SizeOfHeaders, sec_align) + align_up((uint32_t)linker->text_section.size, sec_align);
    opt.Subsystem = 3; // Windows CUI
    opt.SizeOfStackReserve = 0x100000;
    opt.SizeOfStackCommit = 0x1000;
    opt.SizeOfHeapReserve = 0x100000;
    opt.SizeOfHeapCommit = 0x1000;
    opt.NumberOfRvaAndSizes = 16;

    // 4. Section Headers
    SectionHeader text_sec = {0};
    memcpy(text_sec.Name, ".text", 5);
    text_sec.VirtualSize = (uint32_t)linker->text_section.size;
    text_sec.VirtualAddress = sec_align;
    text_sec.SizeOfRawData = align_up((uint32_t)linker->text_section.size, file_align);
    text_sec.PointerToRawData = opt.SizeOfHeaders;
    text_sec.Characteristics = 0x60000020; // Code | Execute | Read

    SectionHeader rdata_sec = {0};
    memcpy(rdata_sec.Name, ".rdata", 6);
    rdata_sec.VirtualSize = (uint32_t)linker->rdata_section.size;
    rdata_sec.VirtualAddress = align_up(text_sec.VirtualAddress + text_sec.VirtualSize, sec_align);
    rdata_sec.SizeOfRawData = align_up((uint32_t)linker->rdata_section.size, file_align);
    rdata_sec.PointerToRawData = text_sec.PointerToRawData + text_sec.SizeOfRawData;
    rdata_sec.Characteristics = 0x40000040; // Initialized Data | Read

    // =========================================================
    // 构建导入表 (Import Directory)
    // =========================================================
    uint32_t rdata_rva = align_up(text_sec.VirtualAddress + text_sec.VirtualSize, sec_align);
    
    PeImportTable* idata = pe_idata_create();
    if (g_use_print_str || g_use_print_int) {
        pe_idata_add_import(idata, "kernel32.dll", "GetStdHandle");
        pe_idata_add_import(idata, "kernel32.dll", "WriteFile");
    }
    pe_idata_add_import(idata, "kernel32.dll", "ExitProcess");
    if (g_use_crea || g_use_neca) {
        pe_idata_add_import(idata, "kernel32.dll", "GetProcessHeap");
    }
    if (g_use_crea) pe_idata_add_import(idata, "kernel32.dll", "HeapAlloc");
    if (g_use_neca) pe_idata_add_import(idata, "kernel32.dll", "HeapFree");
    
    for (SirExternFunc* ext = module->first_extern; ext; ext = ext->next) {
        pe_idata_add_import(idata, ext->dll_name, ext->name);
    }
    
    uint32_t import_dir_offset = 0;
    uint32_t import_dir_size = 0;
    uint32_t iat_rva = 0;
    uint32_t iat_size = 0;
    pe_idata_build(idata, &linker->rdata_section, rdata_rva, &import_dir_offset, &import_dir_size, &iat_rva, &iat_size);
    
    opt.DataDirectory[1].VirtualAddress = rdata_rva + import_dir_offset;
    opt.DataDirectory[1].Size = import_dir_size;
    opt.DataDirectory[12].VirtualAddress = iat_rva;
    opt.DataDirectory[12].Size = iat_size;

    rdata_sec.VirtualSize = (uint32_t)linker->rdata_section.size;
    rdata_sec.SizeOfRawData = align_up((uint32_t)linker->rdata_section.size, file_align);
    
    SectionHeader data_sec = {0};
    if (num_sections == 3) {
        memcpy(data_sec.Name, ".data", 5);
        data_sec.VirtualSize = (uint32_t)linker->data_section.size;
        data_sec.VirtualAddress = align_up(rdata_sec.VirtualAddress + rdata_sec.VirtualSize, sec_align);
        data_sec.SizeOfRawData = align_up((uint32_t)linker->data_section.size, file_align);
        data_sec.PointerToRawData = rdata_sec.PointerToRawData + rdata_sec.SizeOfRawData;
        data_sec.Characteristics = 0xC0000040; // Initialized Data | Read | Write
    }

    opt.SizeOfCode = text_sec.SizeOfRawData;
    opt.SizeOfInitializedData = rdata_sec.SizeOfRawData + (num_sections == 3 ? data_sec.SizeOfRawData : 0);
    opt.SizeOfImage = align_up(num_sections == 3 ? data_sec.VirtualAddress + data_sec.VirtualSize : rdata_sec.VirtualAddress + rdata_sec.VirtualSize, sec_align);
    opt.SizeOfHeaders = align_up((uint32_t)(sizeof(DosHeader) + sizeof(CoffHeader) + sizeof(OptionalHeader64) + sizeof(SectionHeader) * num_sections), file_align);

    // 重新修正 PointerToRawData 因为 SizeOfHeaders 可能变了
    text_sec.PointerToRawData = opt.SizeOfHeaders;
    rdata_sec.PointerToRawData = text_sec.PointerToRawData + text_sec.SizeOfRawData;
    if (num_sections == 3) {
        data_sec.PointerToRawData = rdata_sec.PointerToRawData + rdata_sec.SizeOfRawData;
    }

    // 写入所有头部
    fwrite(&dos, 1, sizeof(dos), out);
    fwrite(&coff, 1, sizeof(coff), out);
    fwrite(&opt, 1, sizeof(opt), out);
    fwrite(&text_sec, 1, sizeof(text_sec), out);
    fwrite(&rdata_sec, 1, sizeof(rdata_sec), out);
    if (num_sections == 3) {
        fwrite(&data_sec, 1, sizeof(data_sec), out);
    }

    // 填充对齐到代码段起始位置
    uint8_t zero = 0;
    while ((uint32_t)ftell(out) < text_sec.PointerToRawData) fwrite(&zero, 1, 1, out);

    // 回填全局变量 RIP 相对寻址重定位
    for (int i = 0; i < g_data_reloc_count; i++) {
        uint32_t text_off = g_data_relocs[i];
        uint32_t target_rva = data_sec.VirtualAddress + g_data_offs[i];
        int32_t rel32 = (int32_t)(target_rva - (text_sec.VirtualAddress + text_off + 4));
        memcpy(linker->text_section.buffer + text_off, &rel32, 4);
    }

    // 回填函数指针 RIP 相对寻址重定位
    for (int i = 0; i < g_func_reloc_count; i++) {
        uint32_t text_off = g_func_relocs[i];
        uint32_t target_rva = text_sec.VirtualAddress + g_func_offs[i];
        int32_t rel32 = (int32_t)(target_rva - (text_sec.VirtualAddress + text_off + 4));
        memcpy(linker->text_section.buffer + text_off, &rel32, 4);
    }

    // 回填字符串 RIP 相对寻址重定位
    for (int i = 0; i < g_str_reloc_count; i++) {
        uint32_t text_off = g_str_relocs[i];
        uint32_t target_rva = rdata_sec.VirtualAddress + g_str_rdata_offs[i];
        int32_t rel32 = (int32_t)(target_rva - (text_sec.VirtualAddress + text_off + 4));
        memcpy(linker->text_section.buffer + text_off, &rel32, 4);
    }

    // 回填 __print_str 调用重定位
    if (g_use_print_str) {
        for (int i = 0; i < g_print_str_reloc_count; i++) {
            uint32_t text_off = g_print_str_relocs[i];
            int32_t rel32 = (int32_t)(g_print_str_offset - (text_off + 4));
            memcpy(linker->text_section.buffer + text_off, &rel32, 4);
        }
    }

    // 回填 __print_int 调用重定位
    if (g_use_print_int) {
        for (int i = 0; i < g_print_int_reloc_count; i++) {
            uint32_t text_off = g_print_int_relocs[i];
            int32_t rel32 = (int32_t)(g_print_int_offset - (text_off + 4));
            memcpy(linker->text_section.buffer + text_off, &rel32, 4);
        }
    }

    // 回填 __print_float 调用重定位
    if (g_use_print_float) {
        for (int i = 0; i < g_print_float_reloc_count; i++) {
            uint32_t text_off = g_print_float_relocs[i];
            int32_t rel32 = (int32_t)(g_print_float_offset - (text_off + 4));
            memcpy(linker->text_section.buffer + text_off, &rel32, 4);
        }

        // 回填 __print_float 内部的调用和数据引用
        int32_t rel_float_minus = (int32_t)((rdata_sec.VirtualAddress + g_minus_rdata_off) - (text_sec.VirtualAddress + g_print_float_offset + 23 + 4));
        memcpy(linker->text_section.buffer + g_print_float_offset + 23, &rel_float_minus, 4);

        int32_t rel_float_print_str1 = (int32_t)(g_print_str_offset - (g_print_float_offset + 35 + 4));
        memcpy(linker->text_section.buffer + g_print_float_offset + 35, &rel_float_print_str1, 4);

        int32_t rel_float_print_int1 = (int32_t)(g_print_int_offset - (g_print_float_offset + 67 + 4));
        memcpy(linker->text_section.buffer + g_print_float_offset + 67, &rel_float_print_int1, 4);
        
        int32_t rel_float_dot = (int32_t)((rdata_sec.VirtualAddress + g_dot_rdata_off) - (text_sec.VirtualAddress + g_print_float_offset + 74 + 4));
        memcpy(linker->text_section.buffer + g_print_float_offset + 74, &rel_float_dot, 4);
        
        int32_t rel_float_print_str2 = (int32_t)(g_print_str_offset - (g_print_float_offset + 86 + 4));
        memcpy(linker->text_section.buffer + g_print_float_offset + 86, &rel_float_print_str2, 4);
        
        int32_t rel_float_10 = (int32_t)((rdata_sec.VirtualAddress + g_float_10_rdata_off) - (text_sec.VirtualAddress + g_print_float_offset + 115 + 4));
        memcpy(linker->text_section.buffer + g_print_float_offset + 115, &rel_float_10, 4);
        
        int32_t rel_float_print_int2 = (int32_t)(g_print_int_offset - (g_print_float_offset + 138 + 4));
        memcpy(linker->text_section.buffer + g_print_float_offset + 138, &rel_float_print_int2, 4);
    }

    // 回填 __print_hex 调用重定位
    if (g_use_print_hex) {
        for (int i = 0; i < g_print_hex_reloc_count; i++) {
            uint32_t text_off = g_print_hex_relocs[i];
            int32_t rel32 = (int32_t)(g_print_hex_offset - (text_off + 4));
            memcpy(linker->text_section.buffer + text_off, &rel32, 4);
        }

        // 回填 __print_hex 内部的 __print_str 调用
        int32_t rel_hex_print_str = (int32_t)(g_print_str_offset - (g_print_hex_offset + 78 + 4));
        memcpy(linker->text_section.buffer + g_print_hex_offset + 78, &rel_hex_print_str, 4);
    }

    // 回填 __print_bool 调用重定位
    if (g_use_print_bool) {
        for (int i = 0; i < g_print_bool_reloc_count; i++) {
            uint32_t text_off = g_print_bool_relocs[i];
            int32_t rel32 = (int32_t)(g_print_bool_offset - (text_off + 4));
            memcpy(linker->text_section.buffer + text_off, &rel32, 4);
        }

        // 回填 __print_bool 内部的 __print_str 调用
        int32_t rel_bool_print_str = (int32_t)(g_print_str_offset - (g_print_bool_offset + 32 + 4));
        memcpy(linker->text_section.buffer + g_print_bool_offset + 32, &rel_bool_print_str, 4);

        // 回填 __print_bool 内部的 lea rcx, [rip + verum/falsum]
        int32_t rel_verum = (int32_t)((rdata_sec.VirtualAddress + g_verum_rdata_off) - (text_sec.VirtualAddress + g_print_bool_offset + 8 + 4));
        memcpy(linker->text_section.buffer + g_print_bool_offset + 8, &rel_verum, 4);
        
        int32_t rel_falsum = (int32_t)((rdata_sec.VirtualAddress + g_falsum_rdata_off) - (text_sec.VirtualAddress + g_print_bool_offset + 22 + 4));
        memcpy(linker->text_section.buffer + g_print_bool_offset + 22, &rel_falsum, 4);
    }

    // 回填 __crea 调用重定位
    if (g_use_crea) {
        for (int i = 0; i < g_crea_reloc_count; i++) {
            uint32_t text_off = g_crea_relocs[i];
            int32_t rel32 = (int32_t)(g_crea_offset - (text_off + 4));
            memcpy(linker->text_section.buffer + text_off, &rel32, 4);
        }
    }

    // 回填 __neca 调用重定位
    if (g_use_neca) {
        for (int i = 0; i < g_neca_reloc_count; i++) {
            uint32_t text_off = g_neca_relocs[i];
            int32_t rel32 = (int32_t)(g_neca_offset - (text_off + 4));
            memcpy(linker->text_section.buffer + text_off, &rel32, 4);
        }
    }

    // 回填 IAT 调用重定位 (Builtins)
    #define RELOC_IAT(reloc_var, dll, func) \
        do { \
            uint32_t iat_off = pe_idata_get_iat_offset(idata, dll, func); \
            int32_t rel32 = (int32_t)((rdata_sec.VirtualAddress + iat_off) - (text_sec.VirtualAddress + reloc_var + 4)); \
            memcpy(linker->text_section.buffer + reloc_var, &rel32, 4); \
        } while(0)

    if (g_use_print_str) {
        RELOC_IAT(g_call_getstdhandle_reloc, "kernel32.dll", "GetStdHandle");
        RELOC_IAT(g_call_writeconsolea_reloc, "kernel32.dll", "WriteFile");
    }
    if (g_use_print_int) {
        RELOC_IAT(g_call_getstdhandle_reloc2, "kernel32.dll", "GetStdHandle");
        RELOC_IAT(g_call_writeconsolea_reloc2, "kernel32.dll", "WriteFile");
    }
    RELOC_IAT(g_call_exitprocess_reloc, "kernel32.dll", "ExitProcess");
    if (g_use_crea) {
        RELOC_IAT(g_call_getprocessheap_reloc1, "kernel32.dll", "GetProcessHeap");
        RELOC_IAT(g_call_heapalloc_reloc, "kernel32.dll", "HeapAlloc");
    }
    if (g_use_neca) {
        RELOC_IAT(g_call_getprocessheap_reloc2, "kernel32.dll", "GetProcessHeap");
        RELOC_IAT(g_call_heapfree_reloc, "kernel32.dll", "HeapFree");
    }

    // 回填外部函数 (Externs) 调用重定位
    for (int i = 0; i < g_extern_reloc_count; i++) {
        uint32_t text_off = g_extern_relocs[i];
        int target_idx = g_extern_idxs[i];
        SirExternFunc* ext = module->first_extern;
        for (int j = 0; j < target_idx && ext; j++) ext = ext->next;
        
        if (ext) {
            uint32_t iat_off = pe_idata_get_iat_offset(idata, ext->dll_name, ext->name);
            int32_t rel32 = (int32_t)((rdata_sec.VirtualAddress + iat_off) - (text_sec.VirtualAddress + text_off + 4));
            memcpy(linker->text_section.buffer + text_off, &rel32, 4);
        }
    }
    
    pe_idata_free(idata);

    // 写入 .text 段
    fwrite(linker->text_section.buffer, 1, linker->text_section.size, out);
    while ((uint32_t)ftell(out) < rdata_sec.PointerToRawData) fwrite(&zero, 1, 1, out);

    // 写入 .rdata 段
    fwrite(linker->rdata_section.buffer, 1, linker->rdata_section.size, out);
    if (num_sections == 3) {
        while ((uint32_t)ftell(out) < data_sec.PointerToRawData) fwrite(&zero, 1, 1, out);

        // 写入 .data 段
        fwrite(linker->data_section.buffer, 1, linker->data_section.size, out);
        while ((uint32_t)ftell(out) < data_sec.PointerToRawData + data_sec.SizeOfRawData) fwrite(&zero, 1, 1, out);
    } else {
        while ((uint32_t)ftell(out) < rdata_sec.PointerToRawData + rdata_sec.SizeOfRawData) fwrite(&zero, 1, 1, out);
    }

    fclose(out);
    return true;
}
