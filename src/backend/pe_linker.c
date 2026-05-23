#include "pe_linker.h"
#include "reg_alloc.h"
#include "builtins.h"
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
    emit_rex(cb, 1, 0, 0, reg > 7);
    emit8(cb, 0xC7);
    emit_modrm(cb, 3, 0, reg & 7);
    emit32(cb, (uint32_t)imm);
}

void emit_mov_reg_imm64(PeCodeBuffer* cb, int reg, uint64_t imm) {
    emit_rex(cb, 1, 0, 0, reg > 7);
    emit8(cb, 0xB8 | (reg & 7));
    emit32(cb, (uint32_t)(imm & 0xFFFFFFFF));
    emit32(cb, (uint32_t)(imm >> 32));
}

static void emit_mov_reg_reg(PeCodeBuffer* cb, int dst, int src) {
    emit_rex(cb, 1, src > 7, 0, dst > 7);
    emit8(cb, 0x89);
    emit_modrm(cb, 3, src & 7, dst & 7);
}

static void emit_alu_reg_reg(PeCodeBuffer* cb, int opc, int dst, int src) {
    emit_rex(cb, 1, src > 7, 0, dst > 7);
    emit8(cb, (uint8_t)opc);
    emit_modrm(cb, 3, src & 7, dst & 7);
}

// opc_ext: 0=ADD, 5=SUB, 7=CMP
static void emit_alu_reg_imm32(PeCodeBuffer* cb, int opc_ext, int dst, int32_t imm) {
    emit_rex(cb, 1, 0, 0, dst > 7);
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
    int map[] = {REG_RBX, REG_RSI, REG_RDI, REG_R12, REG_R13, REG_R14, REG_R15, REG_R10, REG_R11};
    if (color >= 0 && color < 9) return map[color];
    return REG_RAX;
}

typedef struct {
    int pass;
    const char** strings;
    uint32_t* string_offsets;
    int string_count;
    const char** globals;
    uint32_t* global_offsets;
    int global_count;
    const char** funcs;
    uint32_t* func_offsets;
    int func_count;
} LinkCtx;

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
            if (strcmp(ctx->strings[i], val->as.string_val) == 0) {
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
        emit_rex(cb, 1, scratch > 7, 0, 0);
        emit8(cb, 0x8D); // lea r64, [rip + rel32]
        emit_modrm(cb, 0, scratch & 7, 5);
        
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
            if (ctx->pass == 1) {
                g_data_relocs[g_data_reloc_count] = (uint32_t)cb->size;
                g_data_offs[g_data_reloc_count] = target_off;
                g_data_reloc_count++;
            }
        } else {
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
                emit_rex(cb, 0, scratch > 7, 0, 0);
                emit8(cb, 0x8B); // mov r32, m32
            }
        } else {
            emit_rex(cb, 1, scratch > 7, 0, 0);
            emit8(cb, 0x8B); // mov r64, m64
        }
        emit_mem(cb, scratch, REG_RBP, offset);
        return scratch;
    }
    return scratch;
}

// 智能结果存储器
static void store_result(PeCodeBuffer* cb, RegAllocator* alloc, SirValue* val, int src) {
    if (!val || val->kind != SIR_VAL_VREG) return;
    int color = reg_alloc_get_color(alloc, val->as.vreg);
    if (color != -1) {
        int dst = get_phys_reg(color);
        if (dst != src) emit_mov_reg_reg(cb, dst, src);
    } else {
        int offset = reg_alloc_get_offset(alloc, val->as.vreg, 8);
        emit_rex(cb, 1, src > 7, 0, 0);
        emit8(cb, 0x89); // mov m64, r64
        emit_mem(cb, src, REG_RBP, offset);
    }
}

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
    uint32_t* block_offsets = (uint32_t*)calloc(1024, sizeof(uint32_t)); // 记录基本块的机器码偏移量
    
    const char** func_names = (const char**)malloc(256 * sizeof(const char*));
    uint32_t* func_offsets = (uint32_t*)malloc(256 * sizeof(uint32_t));
    int func_count = 0;

    const char** strings = (const char**)malloc(1024 * sizeof(const char*));
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

    // 预扫描：收集所有字符串常量并写入 .rdata 段
    for (SirFunction* func = module->first_func; func; func = func->next) {
        for (SirBlock* block = func->first_block; block; block = block->next) {
            for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                for (int i = 0; i < inst->num_operands; i++) {
                    if (inst->operands[i] && inst->operands[i]->kind == SIR_VAL_CONST_STRING) {
                        bool found = false;
                        for (int j = 0; j < string_count; j++) {
                            if (strcmp(strings[j], inst->operands[i]->as.string_val) == 0) {
                                found = true; break;
                            }
                        }
                        if (!found) {
                            strings[string_count] = inst->operands[i]->as.string_val;
                            string_offsets[string_count] = (uint32_t)linker->rdata_section.size;
                            string_count++;
                            const char* str = inst->operands[i]->as.string_val;
                            for (size_t k = 0; k <= strlen(str); k++) buf_append(&linker->rdata_section, (uint8_t)str[k]);
                        }
                    }
                }
            }
        }
    }

    // 强制添加 verum 和 falsum 到 rdata
    g_verum_rdata_off = (uint32_t)linker->rdata_section.size;
    const char* str_verum = "verum";
    for (size_t k = 0; k <= strlen(str_verum); k++) buf_append(&linker->rdata_section, (uint8_t)str_verum[k]);
    
    g_falsum_rdata_off = (uint32_t)linker->rdata_section.size;
    const char* str_falsum = "falsum";
    for (size_t k = 0; k <= strlen(str_falsum); k++) buf_append(&linker->rdata_section, (uint8_t)str_falsum[k]);

    g_dot_rdata_off = (uint32_t)linker->rdata_section.size;
    buf_append(&linker->rdata_section, '.'); buf_append(&linker->rdata_section, 0);

    g_minus_rdata_off = (uint32_t)linker->rdata_section.size;
    buf_append(&linker->rdata_section, '-'); buf_append(&linker->rdata_section, 0);

    while (linker->rdata_section.size % 8 != 0) buf_append(&linker->rdata_section, 0);
    g_float_10_rdata_off = (uint32_t)linker->rdata_section.size;
    uint64_t float_10_bits = 4621819117588971520ULL; // 10.0
    for (int i = 0; i < 8; i++) buf_append(&linker->rdata_section, (uint8_t)(float_10_bits >> (i * 8)));

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

        LinkCtx ctx;
        ctx.pass = pass;
        ctx.strings = strings;
        ctx.string_offsets = string_offsets;
        ctx.string_count = string_count;
        ctx.globals = global_names;
        ctx.global_offsets = global_offsets;
        ctx.global_count = global_count;
        ctx.funcs = func_names;
        ctx.func_offsets = func_offsets;
        ctx.func_count = func_count;

        int current_func_idx = 0;
        for (SirFunction* func = module->first_func; func; func = func->next) {
            func_offsets[current_func_idx++] = (uint32_t)linker->text_section.size;

            if (strcmp(func->name, "princeps") == 0) {
                g_princeps_offset = (uint32_t)linker->text_section.size;
            } else if (strcmp(func->name, "__scoria_init") == 0) {
                g_init_offset = (uint32_t)linker->text_section.size;
            }

            uint32_t max_vreg = 0;
            int local_stack_size = 72; // 56字节给7个callee-saved + 16字节给caller-saved(r10,r11)
            int max_call_args = 0;
            int* alloca_offsets = calloc(10000, sizeof(int));

            // 预扫描：计算最大寄存器、ALLOCA 空间和最大调用参数数
            for (SirBlock* block = func->first_block; block; block = block->next) {
                for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                    if (inst->opcode == SIR_CALL) {
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

            int call_stack_space = max_call_args > 4 ? (max_call_args - 4) * 8 : 0;
            int stack_size = allocator.current_offset + 32 + call_stack_space; // 预留 Shadow Space 和溢出参数空间
            // 保持 16 字节对齐，并补偿 9 个 push (1 个 ret addr + 8 个 rbp/rbx 等) 造成的 8 字节偏移
            stack_size = (stack_size + 15) & ~15;
            stack_size += 8;

            // 序言 (Prologue)
            emit8(&linker->text_section, 0x55); // push rbp
            emit_mov_reg_reg(&linker->text_section, REG_RBP, REG_RSP);
            
            // 保存 callee-saved 寄存器
            emit8(&linker->text_section, 0x53); // push rbx
            emit8(&linker->text_section, 0x56); // push rsi
            emit8(&linker->text_section, 0x57); // push rdi
            emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x54); // push r12
            emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x55); // push r13
            emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x56); // push r14
            emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x57); // push r15

            if (stack_size > 0) {
                emit_rex(&linker->text_section, 1, 0, 0, 0);
                emit8(&linker->text_section, 0x81); // sub rsp, imm32
                emit_modrm(&linker->text_section, 3, 5, REG_RSP);
                emit32(&linker->text_section, (uint32_t)stack_size);
            }
            
            // 将前 4 个参数寄存器保存到 Shadow Space (支持浮点数)
            int param_count = func->type->as.func_type.param_count;
            for (int i = 0; i < param_count && i < 4; i++) {
                ScoriaType* ptype = func->type->as.func_type.param_types[i];
                bool is_float = (ptype && (ptype->kind == TY_F32 || ptype->kind == TY_F64));
                bool is_f32 = (ptype && ptype->kind == TY_F32);
                int offset = 16 + i * 8;
                
                if (is_float) {
                    // movss/movsd [rbp+offset], xmmI
                    emit8(&linker->text_section, is_f32 ? 0xF3 : 0xF2);
                    emit_rex(&linker->text_section, 0, 0, 0, 0);
                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x11);
                    emit_mem(&linker->text_section, i, REG_RBP, offset);
                } else {
                    int regs[] = {REG_RCX, REG_RDX, 8, 9};
                    emit_rex(&linker->text_section, 1, regs[i] > 7, 0, 0);
                    emit8(&linker->text_section, 0x89);
                    emit_mem(&linker->text_section, regs[i] & 7, REG_RBP, offset);
                }
            }

            for (SirBlock* block = func->first_block; block; block = block->next) {
                if (block->id < 1024) block_offsets[block->id] = (uint32_t)linker->text_section.size;

                for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                    switch (inst->opcode) {
                        case SIR_GET_PARAM: {
                            int param_idx = (int)inst->operands[0]->as.int_val;
                            int offset = 16 + param_idx * 8; // 前4个在 shadow space (16~40)，后面的紧跟其后 (48+)
                            int size = type_get_size(inst->dest->type);
                            bool is_signed = type_is_signed(inst->dest->type);
                            
                            if (size == 1) {
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, is_signed ? 0xBE : 0xB6); // movsx/movzx rax, byte ptr
                            } else if (size == 2) {
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, is_signed ? 0xBF : 0xB7); // movsx/movzx rax, word ptr
                            } else if (size == 4) {
                                if (is_signed) {
                                    emit_rex(&linker->text_section, 1, 0, 0, 0);
                                    emit8(&linker->text_section, 0x63); // movsxd rax, dword ptr
                                } else {
                                    emit_rex(&linker->text_section, 0, 0, 0, 0);
                                    emit8(&linker->text_section, 0x8B); // mov eax, dword ptr
                                }
                            } else {
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x8B); // mov rax, qword ptr
                            }
                            emit_mem(&linker->text_section, REG_RAX, REG_RBP, offset);
                            store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            break;
                        }
                        case SIR_ALLOCA: {
                            int offset = alloca_offsets[inst->dest->as.vreg];
                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                            emit8(&linker->text_section, 0x8D); // lea rax, [rbp + offset]
                            emit_mem(&linker->text_section, REG_RAX, REG_RBP, offset);
                            store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            break;
                        }
                        case SIR_STORE: {
                            int size = type_get_size(inst->operands[0]->type);
                            int val_reg = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                            int ptr_reg = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                            
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
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                            if (left != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, left);
                            
                            if (inst->opcode != SIR_MUL && inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                                int32_t imm = (int32_t)inst->operands[1]->as.int_val;
                                int opc_ext = 0;
                                if (inst->opcode == SIR_ADD) opc_ext = 0;
                                else if (inst->opcode == SIR_SUB) opc_ext = 5;
                                else if (inst->opcode == SIR_AND) opc_ext = 4;
                                else if (inst->opcode == SIR_OR) opc_ext = 1;
                                else if (inst->opcode == SIR_XOR) opc_ext = 6;
                                emit_alu_reg_imm32(&linker->text_section, opc_ext, REG_RAX, imm);
                            } else {
                                int right = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                                if (inst->opcode == SIR_ADD) emit_alu_reg_reg(&linker->text_section, 0x01, REG_RAX, right);
                                else if (inst->opcode == SIR_SUB) emit_alu_reg_reg(&linker->text_section, 0x2B, REG_RAX, right);
                                else if (inst->opcode == SIR_AND) emit_alu_reg_reg(&linker->text_section, 0x23, REG_RAX, right);
                                else if (inst->opcode == SIR_OR) emit_alu_reg_reg(&linker->text_section, 0x0B, REG_RAX, right);
                                else if (inst->opcode == SIR_XOR) emit_alu_reg_reg(&linker->text_section, 0x33, REG_RAX, right);
                                else {
                                    emit_rex(&linker->text_section, 1, 0, 0, right > 7);
                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0xAF); // imul rax, right
                                    emit_modrm(&linker->text_section, 3, REG_RAX, right & 7);
                                }
                            }
                            store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            break;
                        }
                        case SIR_FADD:
                        case SIR_FSUB:
                        case SIR_FMUL:
                        case SIR_FDIV: {
                            bool is_f32 = (inst->operands[0]->type && inst->operands[0]->type->kind == TY_F32);
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                            if (left != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, left);
                            int right = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                            if (right != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, right);
                            
                            // movd/movq xmm0, rax
                            emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); emit_modrm(&linker->text_section, 3, 0, REG_RAX);
                            // movd/movq xmm1, rcx
                            emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); emit_modrm(&linker->text_section, 3, 1, REG_RCX);
                            
                            // op
                            emit8(&linker->text_section, is_f32 ? 0xF3 : 0xF2); emit8(&linker->text_section, 0x0F);
                            if (inst->opcode == SIR_FADD) emit8(&linker->text_section, 0x58);
                            else if (inst->opcode == SIR_FSUB) emit8(&linker->text_section, 0x5C);
                            else if (inst->opcode == SIR_FMUL) emit8(&linker->text_section, 0x59);
                            else if (inst->opcode == SIR_FDIV) emit8(&linker->text_section, 0x5E);
                            emit_modrm(&linker->text_section, 3, 0, 1); // xmm0, xmm1
                            
                            // movd/movq rax, xmm0
                            emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x7E); emit_modrm(&linker->text_section, 3, 0, REG_RAX);
                            
                            store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            break;
                        }
                        case SIR_DIV:
                        case SIR_MOD: {
                            bool is_unsigned = type_is_unsigned(inst->operands[0]->type);
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                            if (left != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, left);
                            int right = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                            if (right != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, right);
                            
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
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                            if (left != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, left);
                            int right = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                            if (right != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, right);
                            
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
                            
                            // 保护 rep movsb 会破坏的寄存器
                            emit8(&linker->text_section, 0x56); // push rsi
                            emit8(&linker->text_section, 0x57); // push rdi
                            emit8(&linker->text_section, 0x51); // push rcx
                            
                            int dst_reg = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                            if (dst_reg != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, dst_reg);
                            
                            int src_reg = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RDX, &ctx);
                            if (src_reg != REG_RDX) emit_mov_reg_reg(&linker->text_section, REG_RDX, src_reg);
                            
                            emit_mov_reg_reg(&linker->text_section, REG_RDI, REG_RAX);
                            emit_mov_reg_reg(&linker->text_section, REG_RSI, REG_RDX);
                            emit_mov_reg_imm32(&linker->text_section, REG_RCX, size);
                            
                            emit8(&linker->text_section, 0xF3); // rep
                            emit8(&linker->text_section, 0xA4); // movsb
                            
                            emit8(&linker->text_section, 0x59); // pop rcx
                            emit8(&linker->text_section, 0x5F); // pop rdi
                            emit8(&linker->text_section, 0x5E); // pop rsi
                            break;
                        }
                        case SIR_GEP: {
                            int ptr = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                            if (ptr != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, ptr);
                            
                            int element_size = (int)inst->operands[2]->as.int_val;

                            if (inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                                int32_t offset = (int32_t)(inst->operands[1]->as.int_val * element_size);
                                if (offset != 0) emit_alu_reg_imm32(&linker->text_section, 0, REG_RAX, offset);
                            } else {
                                int idx = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                                if (idx != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, idx);
                                
                                if (element_size > 1) {
                                    // imul rcx, element_size
                                    emit_rex(&linker->text_section, 1, 0, 0, 0);
                                    emit8(&linker->text_section, 0x69);
                                    emit_modrm(&linker->text_section, 3, REG_RCX, REG_RCX);
                                    emit32(&linker->text_section, element_size);
                                }
                                
                                // add rax, rcx
                                emit_alu_reg_reg(&linker->text_section, 0x01, REG_RAX, REG_RCX);
                            }
                            store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            break;
                        }
                        case SIR_ICMP_LT:
                        case SIR_ICMP_GT:
                        case SIR_ICMP_LE:
                        case SIR_ICMP_GE:
                        case SIR_ICMP_EQ:
                        case SIR_ICMP_NE: {
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                            if (inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                                int32_t imm = (int32_t)inst->operands[1]->as.int_val;
                                emit_alu_reg_imm32(&linker->text_section, 7, left, imm); // cmp left, imm
                            } else {
                                int right = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                                emit_rex(&linker->text_section, 1, right > 7, 0, left > 7);
                                emit8(&linker->text_section, 0x39); // cmp left, right
                                emit_modrm(&linker->text_section, 3, right & 7, left & 7);
                            }
                            
                            bool is_unsigned = type_is_unsigned(inst->operands[0]->type);
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
                            break;
                        }
                        case SIR_FCMP_LT:
                        case SIR_FCMP_GT:
                        case SIR_FCMP_LE:
                        case SIR_FCMP_GE:
                        case SIR_FCMP_EQ:
                        case SIR_FCMP_NE: {
                            bool is_f32 = (inst->operands[0]->type && inst->operands[0]->type->kind == TY_F32);
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                            if (left != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, left);
                            int right = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                            if (right != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, right);
                            
                            // movd/movq xmm0, rax
                            emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); emit_modrm(&linker->text_section, 3, 0, REG_RAX);
                            // movd/movq xmm1, rcx
                            emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); emit_modrm(&linker->text_section, 3, 1, REG_RCX);
                            
                            // ucomiss/ucomisd xmm0, xmm1
                            if (!is_f32) emit8(&linker->text_section, 0x66);
                            emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x2E); emit_modrm(&linker->text_section, 3, 0, 1);
                            
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
                            break;
                        }
                        case SIR_JMP: {
                            uint32_t t_id = inst->operands[0]->as.block->id;
                            uint32_t t_off = t_id < 1024 ? block_offsets[t_id] : 0;
                            emit8(&linker->text_section, 0xE9); // jmp rel32
                            emit32(&linker->text_section, (uint32_t)(t_off - (linker->text_section.size + 4)));
                            break;
                        }
                        case SIR_CALL: {
                            // 保护 Caller-Saved 寄存器 (r10, r11)
                            emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x89); emit_mem(&linker->text_section, 2, REG_RBP, -64); // mov [rbp-64], r10
                            emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x89); emit_mem(&linker->text_section, 3, REG_RBP, -72); // mov [rbp-72], r11

                            if (inst->operands[0]->kind == SIR_VAL_GLOBAL && strcmp(inst->operands[0]->as.global_name, "scribe") == 0) {
                                bool is_str = (inst->operands[1]->type && inst->operands[1]->type->kind == TY_TEXTUS) || (inst->operands[1]->kind == SIR_VAL_CONST_STRING);
                                bool is_bool = (inst->operands[1]->type && inst->operands[1]->type->kind == TY_LOGICA) || (inst->operands[1]->kind == SIR_VAL_CONST_BOOL);
                                bool is_ptr = (inst->operands[1]->type && (inst->operands[1]->type->kind == TY_VIA || inst->operands[1]->type->kind == TY_COHORS || inst->operands[1]->type->kind == TY_ACIES));
                                bool is_float = (inst->operands[1]->type && (inst->operands[1]->type->kind == TY_F32 || inst->operands[1]->type->kind == TY_F64)) || (inst->operands[1]->kind == SIR_VAL_CONST_FLOAT);
                                
                                int arg = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                                if (arg != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, arg);
                                
                                if (is_str) {
                                    int str_len = 0;
                                    if (inst->operands[1]->kind == SIR_VAL_CONST_STRING) {
                                        str_len = (int)strlen(inst->operands[1]->as.string_val);
                                    }
                                    emit_mov_reg_imm32(&linker->text_section, REG_RDX, str_len);
                                    
                                    emit8(&linker->text_section, 0xE8); // call rel32
                                    if (pass == 1) g_print_str_relocs[g_print_str_reloc_count++] = (uint32_t)linker->text_section.size;
                                    emit32(&linker->text_section, 0); // 占位符
                                } else if (is_bool) {
                                    emit8(&linker->text_section, 0xE8); // call rel32
                                    if (pass == 1) g_print_bool_relocs[g_print_bool_reloc_count++] = (uint32_t)linker->text_section.size;
                                    emit32(&linker->text_section, 0);
                                } else if (is_ptr) {
                                    emit8(&linker->text_section, 0xE8); // call rel32
                                    if (pass == 1) g_print_hex_relocs[g_print_hex_reloc_count++] = (uint32_t)linker->text_section.size;
                                    emit32(&linker->text_section, 0);
                                } else if (is_float) {
                                    emit8(&linker->text_section, 0xE8); // call rel32
                                    if (pass == 1) g_print_float_relocs[g_print_float_reloc_count++] = (uint32_t)linker->text_section.size;
                                    emit32(&linker->text_section, 0);
                                } else {
                                    emit8(&linker->text_section, 0xE8); // call rel32
                                    if (pass == 1) g_print_int_relocs[g_print_int_reloc_count++] = (uint32_t)linker->text_section.size;
                                    emit32(&linker->text_section, 0); // 占位符
                                }
                                
                                // 恢复 Caller-Saved 寄存器
                                emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x8B); emit_mem(&linker->text_section, 2, REG_RBP, -64); // mov r10, [rbp-64]
                                emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x8B); emit_mem(&linker->text_section, 3, REG_RBP, -72); // mov r11, [rbp-72]
                                break;
                            }
                            if (inst->operands[0]->kind == SIR_VAL_GLOBAL && strcmp(inst->operands[0]->as.global_name, "crea") == 0) {
                                int arg = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                                if (arg != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, arg);
                                emit8(&linker->text_section, 0xE8); // call rel32
                                if (pass == 1) g_crea_relocs[g_crea_reloc_count++] = (uint32_t)linker->text_section.size;
                                emit32(&linker->text_section, 0);
                                
                                // 恢复 Caller-Saved 寄存器
                                emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x8B); emit_mem(&linker->text_section, 2, REG_RBP, -64);
                                emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x8B); emit_mem(&linker->text_section, 3, REG_RBP, -72);
                                
                                if (inst->dest) store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                                break;
                            }
                            if (inst->operands[0]->kind == SIR_VAL_GLOBAL && strcmp(inst->operands[0]->as.global_name, "neca") == 0) {
                                int arg = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, &ctx);
                                if (arg != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, arg);
                                emit8(&linker->text_section, 0xE8); // call rel32
                                if (pass == 1) g_neca_relocs[g_neca_reloc_count++] = (uint32_t)linker->text_section.size;
                                emit32(&linker->text_section, 0);
                                
                                // 恢复 Caller-Saved 寄存器
                                emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x8B); emit_mem(&linker->text_section, 2, REG_RBP, -64);
                                emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x8B); emit_mem(&linker->text_section, 3, REG_RBP, -72);
                                break;
                            }

                            int arg_regs[] = {REG_RCX, REG_RDX, 8, 9};
                            for (int i = 0; i < inst->num_operands - 1; i++) {
                                int val = load_operand(&linker->text_section, &allocator, inst->operands[i+1], REG_RAX, &ctx);
                                if (i < 4) {
                                    if (val != arg_regs[i]) emit_mov_reg_reg(&linker->text_section, arg_regs[i], val);
                                    
                                    bool is_float = (inst->operands[i+1]->type && (inst->operands[i+1]->type->kind == TY_F32 || inst->operands[i+1]->type->kind == TY_F64));
                                    if (is_float) {
                                        bool is_f32 = (inst->operands[i+1]->type->kind == TY_F32);
                                        // movd/movq xmmX, arg_regs[i]
                                        emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, arg_regs[i] > 7); 
                                        emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x6E); 
                                        emit_modrm(&linker->text_section, 3, i, arg_regs[i] & 7);
                                    }
                                } else {
                                    emit_rex(&linker->text_section, 1, val > 7, 0, 0);
                                    emit8(&linker->text_section, 0x89);
                                    emit_mem(&linker->text_section, val, REG_RSP, 32 + (i - 4) * 8);
                                }
                            }
                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                            emit8(&linker->text_section, 0x31);
                            emit_modrm(&linker->text_section, 3, REG_RAX, REG_RAX);
                            
                            if (inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                                uint32_t target_offset = 0;
                                for (int f = 0; f < ctx.func_count; f++) {
                                    if (strcmp(ctx.funcs[f], inst->operands[0]->as.global_name) == 0) {
                                        target_offset = ctx.func_offsets[f];
                                        break;
                                    }
                                }
                                emit8(&linker->text_section, 0xE8); // call rel32
                                emit32(&linker->text_section, (uint32_t)(target_offset - (linker->text_section.size + 4)));
                            } else {
                                int callee_reg = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_R10, &ctx);
                                emit_rex(&linker->text_section, 1, 0, 0, callee_reg > 7);
                                emit8(&linker->text_section, 0xFF);
                                emit_modrm(&linker->text_section, 3, 2, callee_reg & 7); // call r/m64
                            }
                            
                            // 恢复 Caller-Saved 寄存器
                            emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x8B); emit_mem(&linker->text_section, 2, REG_RBP, -64);
                            emit_rex(&linker->text_section, 1, 1, 0, 0); emit8(&linker->text_section, 0x8B); emit_mem(&linker->text_section, 3, REG_RBP, -72);
                            
                            if (inst->dest) {
                                bool ret_is_float = (inst->dest->type && (inst->dest->type->kind == TY_F32 || inst->dest->type->kind == TY_F64));
                                if (ret_is_float) {
                                    bool is_f32 = (inst->dest->type->kind == TY_F32);
                                    // movd/movq rax, xmm0
                                    emit8(&linker->text_section, 0x66); emit_rex(&linker->text_section, is_f32 ? 0 : 1, 0, 0, 0); 
                                    emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x7E); 
                                    emit_modrm(&linker->text_section, 3, 0, REG_RAX);
                                }
                                store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            }
                            break;
                        }
                        case SIR_BR: {
                            int cond = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                            emit_alu_reg_imm32(&linker->text_section, 7, cond, 0); // cmp cond, 0
                            
                            uint32_t t_id = inst->operands[1]->as.block->id;
                            uint32_t t_off = t_id < 1024 ? block_offsets[t_id] : 0;
                            emit8(&linker->text_section, 0x0F); emit8(&linker->text_section, 0x85); // jne true_block
                            emit32(&linker->text_section, (uint32_t)(t_off - (linker->text_section.size + 4)));
                            
                            uint32_t f_id = inst->operands[2]->as.block->id;
                            uint32_t f_off = f_id < 1024 ? block_offsets[f_id] : 0;
                            emit8(&linker->text_section, 0xE9); // jmp false_block
                            emit32(&linker->text_section, (uint32_t)(f_off - (linker->text_section.size + 4)));
                            break;
                        }
                        case SIR_RET: {
                            if (inst->num_operands > 0) {
                                int val = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, &ctx);
                                if (val != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, val);
                                
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
                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                            emit8(&linker->text_section, 0x8D);
                            emit_mem(&linker->text_section, REG_RSP, REG_RBP, -56); // lea rsp, [rbp - 56]
                            
                            emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x5F); // pop r15
                            emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x5E); // pop r14
                            emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x5D); // pop r13
                            emit_rex(&linker->text_section, 0, 0, 0, 1); emit8(&linker->text_section, 0x5C); // pop r12
                            emit8(&linker->text_section, 0x5F); // pop rdi
                            emit8(&linker->text_section, 0x5E); // pop rsi
                            emit8(&linker->text_section, 0x5B); // pop rbx
                            
                            emit8(&linker->text_section, 0x5D); // pop rbp
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
    free(string_offsets);
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

    // 2. COFF Header
    CoffHeader coff = {0};
    coff.Signature = 0x00004550; // "PE\0\0"
    coff.Machine = 0x8664;       // x86_64
    coff.NumberOfSections = 3;   // .text, .rdata, .data
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
    // 确保 IAT 8 字节对齐
    while (linker->rdata_section.size % 8 != 0) buf_append(&linker->rdata_section, 0);

    uint32_t rdata_rva = align_up(text_sec.VirtualAddress + text_sec.VirtualSize, sec_align);
    uint32_t current_rdata_offset = (uint32_t)linker->rdata_section.size;

    // 1. IAT (FirstThunk)
    uint32_t iat_offset = current_rdata_offset;
    uint64_t iat[] = {0, 0, 0, 0, 0, 0, 0};
    for (size_t i=0; i<sizeof(iat); i++) buf_append(&linker->rdata_section, ((uint8_t*)&iat)[i]);

    // 2. INT (OriginalFirstThunk)
    uint32_t int_offset = (uint32_t)linker->rdata_section.size;
    uint64_t int_array[] = {0, 0, 0, 0, 0, 0, 0};
    for (size_t i=0; i<sizeof(int_array); i++) buf_append(&linker->rdata_section, ((uint8_t*)&int_array)[i]);

    // 3. Hint/Name
    uint32_t hn_getstdhandle_offset = (uint32_t)linker->rdata_section.size;
    buf_append(&linker->rdata_section, 0); buf_append(&linker->rdata_section, 0);
    const char* name1 = "GetStdHandle";
    for (size_t i=0; i<=strlen(name1); i++) buf_append(&linker->rdata_section, (uint8_t)name1[i]);
    if (linker->rdata_section.size % 2 != 0) buf_append(&linker->rdata_section, 0);

    uint32_t hn_writefile_offset = (uint32_t)linker->rdata_section.size;
    buf_append(&linker->rdata_section, 0); buf_append(&linker->rdata_section, 0);
    const char* name2 = "WriteFile";
    for (size_t i=0; i<=strlen(name2); i++) buf_append(&linker->rdata_section, (uint8_t)name2[i]);
    if (linker->rdata_section.size % 2 != 0) buf_append(&linker->rdata_section, 0);

    uint32_t hn_exitprocess_offset = (uint32_t)linker->rdata_section.size;
    buf_append(&linker->rdata_section, 0); buf_append(&linker->rdata_section, 0);
    const char* name3 = "ExitProcess";
    for (size_t i=0; i<=strlen(name3); i++) buf_append(&linker->rdata_section, (uint8_t)name3[i]);
    if (linker->rdata_section.size % 2 != 0) buf_append(&linker->rdata_section, 0);

    uint32_t hn_getprocessheap_offset = (uint32_t)linker->rdata_section.size;
    buf_append(&linker->rdata_section, 0); buf_append(&linker->rdata_section, 0);
    const char* name4 = "GetProcessHeap";
    for (size_t i=0; i<=strlen(name4); i++) buf_append(&linker->rdata_section, (uint8_t)name4[i]);
    if (linker->rdata_section.size % 2 != 0) buf_append(&linker->rdata_section, 0);

    uint32_t hn_heapalloc_offset = (uint32_t)linker->rdata_section.size;
    buf_append(&linker->rdata_section, 0); buf_append(&linker->rdata_section, 0);
    const char* name5 = "HeapAlloc";
    for (size_t i=0; i<=strlen(name5); i++) buf_append(&linker->rdata_section, (uint8_t)name5[i]);
    if (linker->rdata_section.size % 2 != 0) buf_append(&linker->rdata_section, 0);

    uint32_t hn_heapfree_offset = (uint32_t)linker->rdata_section.size;
    buf_append(&linker->rdata_section, 0); buf_append(&linker->rdata_section, 0);
    const char* name6 = "HeapFree";
    for (size_t i=0; i<=strlen(name6); i++) buf_append(&linker->rdata_section, (uint8_t)name6[i]);
    if (linker->rdata_section.size % 2 != 0) buf_append(&linker->rdata_section, 0);

    // 4. DLL Name
    uint32_t dll_name_offset = (uint32_t)linker->rdata_section.size;
    const char* dll_name = "kernel32.dll";
    for (size_t i=0; i<=strlen(dll_name); i++) buf_append(&linker->rdata_section, (uint8_t)dll_name[i]);

    // 5. Import Descriptor
    uint32_t import_desc_offset = (uint32_t)linker->rdata_section.size;
    ImageImportDescriptor desc[2] = {0};
    desc[0].OriginalFirstThunk = rdata_rva + int_offset;
    desc[0].Name = rdata_rva + dll_name_offset;
    desc[0].FirstThunk = rdata_rva + iat_offset;
    for (size_t i=0; i<sizeof(desc); i++) buf_append(&linker->rdata_section, ((uint8_t*)&desc)[i]);

    // 回填 IAT 和 INT
    uint64_t hn1_rva = rdata_rva + hn_getstdhandle_offset;
    uint64_t hn2_rva = rdata_rva + hn_writefile_offset;
    uint64_t hn3_rva = rdata_rva + hn_exitprocess_offset;
    uint64_t hn4_rva = rdata_rva + hn_getprocessheap_offset;
    uint64_t hn5_rva = rdata_rva + hn_heapalloc_offset;
    uint64_t hn6_rva = rdata_rva + hn_heapfree_offset;
    memcpy(linker->rdata_section.buffer + iat_offset, &hn1_rva, 8);
    memcpy(linker->rdata_section.buffer + iat_offset + 8, &hn2_rva, 8);
    memcpy(linker->rdata_section.buffer + iat_offset + 16, &hn3_rva, 8);
    memcpy(linker->rdata_section.buffer + iat_offset + 24, &hn4_rva, 8);
    memcpy(linker->rdata_section.buffer + iat_offset + 32, &hn5_rva, 8);
    memcpy(linker->rdata_section.buffer + iat_offset + 40, &hn6_rva, 8);
    memcpy(linker->rdata_section.buffer + int_offset, &hn1_rva, 8);
    memcpy(linker->rdata_section.buffer + int_offset + 8, &hn2_rva, 8);
    memcpy(linker->rdata_section.buffer + int_offset + 16, &hn3_rva, 8);
    memcpy(linker->rdata_section.buffer + int_offset + 24, &hn4_rva, 8);
    memcpy(linker->rdata_section.buffer + int_offset + 32, &hn5_rva, 8);
    memcpy(linker->rdata_section.buffer + int_offset + 40, &hn6_rva, 8);

    // 更新 Optional Header
    opt.DataDirectory[1].VirtualAddress = rdata_rva + import_desc_offset; // Import Table RVA
    opt.DataDirectory[1].Size = (uint32_t)sizeof(desc);                   // Import Table Size
    opt.DataDirectory[12].VirtualAddress = rdata_rva + iat_offset;        // IAT RVA
    opt.DataDirectory[12].Size = 56;                                      // IAT Size (7 entries * 8 bytes)

    rdata_sec.VirtualSize = (uint32_t)linker->rdata_section.size;
    rdata_sec.SizeOfRawData = align_up((uint32_t)linker->rdata_section.size, file_align);
    
    SectionHeader data_sec = {0};
    memcpy(data_sec.Name, ".data", 5);
    data_sec.VirtualSize = (uint32_t)linker->data_section.size;
    data_sec.VirtualAddress = align_up(rdata_sec.VirtualAddress + rdata_sec.VirtualSize, sec_align);
    data_sec.SizeOfRawData = align_up((uint32_t)linker->data_section.size, file_align);
    data_sec.PointerToRawData = rdata_sec.PointerToRawData + rdata_sec.SizeOfRawData;
    data_sec.Characteristics = 0xC0000040; // Initialized Data | Read | Write

    opt.SizeOfCode = text_sec.SizeOfRawData;
    opt.SizeOfInitializedData = rdata_sec.SizeOfRawData + data_sec.SizeOfRawData;
    opt.SizeOfImage = align_up(data_sec.VirtualAddress + data_sec.VirtualSize, sec_align);
    opt.SizeOfHeaders = align_up((uint32_t)(sizeof(DosHeader) + sizeof(CoffHeader) + sizeof(OptionalHeader64) + sizeof(SectionHeader) * 3), file_align);

    // 重新修正 PointerToRawData 因为 SizeOfHeaders 可能变了
    text_sec.PointerToRawData = opt.SizeOfHeaders;
    rdata_sec.PointerToRawData = text_sec.PointerToRawData + text_sec.SizeOfRawData;
    data_sec.PointerToRawData = rdata_sec.PointerToRawData + rdata_sec.SizeOfRawData;

    // 写入所有头部
    fwrite(&dos, 1, sizeof(dos), out);
    fwrite(&coff, 1, sizeof(coff), out);
    fwrite(&opt, 1, sizeof(opt), out);
    fwrite(&text_sec, 1, sizeof(text_sec), out);
    fwrite(&rdata_sec, 1, sizeof(rdata_sec), out);
    fwrite(&data_sec, 1, sizeof(data_sec), out);

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
    for (int i = 0; i < g_print_str_reloc_count; i++) {
        uint32_t text_off = g_print_str_relocs[i];
        int32_t rel32 = (int32_t)(g_print_str_offset - (text_off + 4));
        memcpy(linker->text_section.buffer + text_off, &rel32, 4);
    }

    // 回填 __print_int 调用重定位
    for (int i = 0; i < g_print_int_reloc_count; i++) {
        uint32_t text_off = g_print_int_relocs[i];
        int32_t rel32 = (int32_t)(g_print_int_offset - (text_off + 4));
        memcpy(linker->text_section.buffer + text_off, &rel32, 4);
    }

    // 回填 __print_float 调用重定位
    for (int i = 0; i < g_print_float_reloc_count; i++) {
        uint32_t text_off = g_print_float_relocs[i];
        int32_t rel32 = (int32_t)(g_print_float_offset - (text_off + 4));
        memcpy(linker->text_section.buffer + text_off, &rel32, 4);
    }

    // 回填 __print_hex 调用重定位
    for (int i = 0; i < g_print_hex_reloc_count; i++) {
        uint32_t text_off = g_print_hex_relocs[i];
        int32_t rel32 = (int32_t)(g_print_hex_offset - (text_off + 4));
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

    // 回填 __print_bool 调用重定位
    for (int i = 0; i < g_print_bool_reloc_count; i++) {
        uint32_t text_off = g_print_bool_relocs[i];
        int32_t rel32 = (int32_t)(g_print_bool_offset - (text_off + 4));
        memcpy(linker->text_section.buffer + text_off, &rel32, 4);
    }

    // 回填 __print_hex 内部的 __print_str 调用
    int32_t rel_hex_print_str = (int32_t)(g_print_str_offset - (g_print_hex_offset + 74 + 4));
    memcpy(linker->text_section.buffer + g_print_hex_offset + 74, &rel_hex_print_str, 4);

    // 回填 __print_bool 内部的 __print_str 调用
    int32_t rel_bool_print_str = (int32_t)(g_print_str_offset - (g_print_bool_offset + 32 + 4));
    memcpy(linker->text_section.buffer + g_print_bool_offset + 32, &rel_bool_print_str, 4);

    // 回填 __print_bool 内部的 lea rcx, [rip + verum/falsum]
    int32_t rel_verum = (int32_t)((rdata_sec.VirtualAddress + g_verum_rdata_off) - (text_sec.VirtualAddress + g_print_bool_offset + 8 + 4));
    memcpy(linker->text_section.buffer + g_print_bool_offset + 8, &rel_verum, 4);
    
    int32_t rel_falsum = (int32_t)((rdata_sec.VirtualAddress + g_falsum_rdata_off) - (text_sec.VirtualAddress + g_print_bool_offset + 22 + 4));
    memcpy(linker->text_section.buffer + g_print_bool_offset + 22, &rel_falsum, 4);

    // 回填 __crea 调用重定位
    for (int i = 0; i < g_crea_reloc_count; i++) {
        uint32_t text_off = g_crea_relocs[i];
        int32_t rel32 = (int32_t)(g_crea_offset - (text_off + 4));
        memcpy(linker->text_section.buffer + text_off, &rel32, 4);
    }

    // 回填 __neca 调用重定位
    for (int i = 0; i < g_neca_reloc_count; i++) {
        uint32_t text_off = g_neca_relocs[i];
        int32_t rel32 = (int32_t)(g_neca_offset - (text_off + 4));
        memcpy(linker->text_section.buffer + text_off, &rel32, 4);
    }

    // 回填 IAT 调用重定位
    int32_t rel_getstdhandle = (int32_t)((rdata_sec.VirtualAddress + iat_offset) - (text_sec.VirtualAddress + g_call_getstdhandle_reloc + 4));
    memcpy(linker->text_section.buffer + g_call_getstdhandle_reloc, &rel_getstdhandle, 4);

    int32_t rel_writefile = (int32_t)((rdata_sec.VirtualAddress + iat_offset + 8) - (text_sec.VirtualAddress + g_call_writeconsolea_reloc + 4));
    memcpy(linker->text_section.buffer + g_call_writeconsolea_reloc, &rel_writefile, 4);

    int32_t rel_getstdhandle2 = (int32_t)((rdata_sec.VirtualAddress + iat_offset) - (text_sec.VirtualAddress + g_call_getstdhandle_reloc2 + 4));
    memcpy(linker->text_section.buffer + g_call_getstdhandle_reloc2, &rel_getstdhandle2, 4);

    int32_t rel_writefile2 = (int32_t)((rdata_sec.VirtualAddress + iat_offset + 8) - (text_sec.VirtualAddress + g_call_writeconsolea_reloc2 + 4));
    memcpy(linker->text_section.buffer + g_call_writeconsolea_reloc2, &rel_writefile2, 4);

    int32_t rel_exitprocess = (int32_t)((rdata_sec.VirtualAddress + iat_offset + 16) - (text_sec.VirtualAddress + g_call_exitprocess_reloc + 4));
    memcpy(linker->text_section.buffer + g_call_exitprocess_reloc, &rel_exitprocess, 4);

    int32_t rel_getprocessheap1 = (int32_t)((rdata_sec.VirtualAddress + iat_offset + 24) - (text_sec.VirtualAddress + g_call_getprocessheap_reloc1 + 4));
    memcpy(linker->text_section.buffer + g_call_getprocessheap_reloc1, &rel_getprocessheap1, 4);

    int32_t rel_getprocessheap2 = (int32_t)((rdata_sec.VirtualAddress + iat_offset + 24) - (text_sec.VirtualAddress + g_call_getprocessheap_reloc2 + 4));
    memcpy(linker->text_section.buffer + g_call_getprocessheap_reloc2, &rel_getprocessheap2, 4);

    int32_t rel_heapalloc = (int32_t)((rdata_sec.VirtualAddress + iat_offset + 32) - (text_sec.VirtualAddress + g_call_heapalloc_reloc + 4));
    memcpy(linker->text_section.buffer + g_call_heapalloc_reloc, &rel_heapalloc, 4);

    int32_t rel_heapfree = (int32_t)((rdata_sec.VirtualAddress + iat_offset + 40) - (text_sec.VirtualAddress + g_call_heapfree_reloc + 4));
    memcpy(linker->text_section.buffer + g_call_heapfree_reloc, &rel_heapfree, 4);

    // 写入 .text 段
    fwrite(linker->text_section.buffer, 1, linker->text_section.size, out);
    while ((uint32_t)ftell(out) < rdata_sec.PointerToRawData) fwrite(&zero, 1, 1, out);

    // 写入 .rdata 段
    fwrite(linker->rdata_section.buffer, 1, linker->rdata_section.size, out);
    while ((uint32_t)ftell(out) < data_sec.PointerToRawData) fwrite(&zero, 1, 1, out);

    // 写入 .data 段
    fwrite(linker->data_section.buffer, 1, linker->data_section.size, out);
    while ((uint32_t)ftell(out) < data_sec.PointerToRawData + data_sec.SizeOfRawData) fwrite(&zero, 1, 1, out);

    fclose(out);
    return true;
}
