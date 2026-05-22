#include "pe_linker.h"
#include "reg_alloc.h"
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
        cb->buffer = (uint8_t*)realloc(cb->buffer, cb->capacity);
    }
    cb->buffer[cb->size++] = byte;
}

void pe_linker_init(PeLinker* linker) {
    buf_init(&linker->text_section);
    buf_init(&linker->rdata_section);
    linker->entry_point_offset = 0;
}

void pe_linker_free(PeLinker* linker) {
    buf_free(&linker->text_section);
    buf_free(&linker->rdata_section);
}

static uint32_t align_up(uint32_t val, uint32_t alignment) {
    return (val + alignment - 1) & ~(alignment - 1);
}

// =========================================================
// 工业级 x86_64 机器码发射器 (Machine Code Emitter)
// =========================================================
#define REG_RAX 0
#define REG_RCX 1
#define REG_RDX 2
#define REG_RBX 3
#define REG_RSP 4
#define REG_RBP 5
#define REG_RSI 6
#define REG_RDI 7
#define REG_R12 12
#define REG_R13 13
#define REG_R14 14

static void emit8(PeCodeBuffer* cb, uint8_t b) { buf_append(cb, b); }
static void emit32(PeCodeBuffer* cb, uint32_t v) {
    emit8(cb, (uint8_t)(v & 0xFF)); emit8(cb, (uint8_t)((v >> 8) & 0xFF));
    emit8(cb, (uint8_t)((v >> 16) & 0xFF)); emit8(cb, (uint8_t)((v >> 24) & 0xFF));
}

static void emit_rex(PeCodeBuffer* cb, int w, int r, int x, int b) {
    uint8_t rex = 0x40 | (w ? 8 : 0) | (r ? 4 : 0) | (x ? 2 : 0) | (b ? 1 : 0);
    emit8(cb, rex);
}

static void emit_modrm(PeCodeBuffer* cb, int mod, int reg, int rm) {
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

static void emit_mov_reg_imm32(PeCodeBuffer* cb, int reg, int32_t imm) {
    emit_rex(cb, 1, 0, 0, reg > 7);
    emit8(cb, 0xC7);
    emit_modrm(cb, 3, 0, reg & 7);
    emit32(cb, (uint32_t)imm);
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
    int map[] = {REG_RBX, REG_RSI, REG_RDI, REG_R12, REG_R13, REG_R14};
    if (color >= 0 && color < 6) return map[color];
    return REG_RAX;
}

// 智能操作数加载器：处理常量、物理寄存器、栈溢出和字符串 RIP 寻址
static int load_operand(PeCodeBuffer* cb, RegAllocator* alloc, SirValue* val, int scratch, 
                        uint32_t* str_relocs, uint32_t* str_rdata_offs, int* str_reloc_count, 
                        const char** strings, uint32_t* string_offsets, int string_count) {
    if (!val) return scratch;
    if (val->kind == SIR_VAL_CONST_INT) {
        emit_mov_reg_imm32(cb, scratch, (int32_t)val->as.int_val);
        return scratch;
    } else if (val->kind == SIR_VAL_CONST_BOOL) {
        emit_mov_reg_imm32(cb, scratch, val->as.bool_val ? 1 : 0);
        return scratch;
    } else if (val->kind == SIR_VAL_CONST_STRING) {
        uint32_t rdata_off = 0;
        for (int i = 0; i < string_count; i++) {
            if (strcmp(strings[i], val->as.string_val) == 0) {
                rdata_off = string_offsets[i];
                break;
            }
        }
        emit_rex(cb, 1, scratch > 7, 0, 0);
        emit8(cb, 0x8D); // lea r64, [rip + rel32]
        emit_modrm(cb, 0, scratch & 7, 5);
        if (str_relocs && str_rdata_offs && str_reloc_count) {
            str_relocs[*str_reloc_count] = (uint32_t)cb->size;
            str_rdata_offs[*str_reloc_count] = rdata_off;
            (*str_reloc_count)++;
        }
        emit32(cb, 0); // 占位符，链接时回填
        return scratch;
    } else if (val->kind == SIR_VAL_VREG) {
        int color = reg_alloc_get_color(alloc, val->as.vreg);
        if (color != -1) return get_phys_reg(color);
        int offset = reg_alloc_get_offset(alloc, val->as.vreg, 8);
        emit_rex(cb, 1, scratch > 7, 0, 0);
        emit8(cb, 0x8B); // mov r64, m64
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

// 全局重定位表 (用于跨函数回填)
#define MAX_STR_RELOCS 1024
uint32_t g_str_relocs[MAX_STR_RELOCS];
uint32_t g_str_rdata_offs[MAX_STR_RELOCS];
int g_str_reloc_count = 0;

uint32_t g_print_str_relocs[1024];
int g_print_str_reloc_count = 0;
uint32_t g_print_str_offset = 0;

uint32_t g_print_int_relocs[1024];
int g_print_int_reloc_count = 0;
uint32_t g_print_int_offset = 0;

uint32_t g_call_getstdhandle_reloc = 0;
uint32_t g_call_writeconsolea_reloc = 0;
uint32_t g_call_getstdhandle_reloc2 = 0;
uint32_t g_call_writeconsolea_reloc2 = 0;
uint32_t g_call_exitprocess_reloc = 0;
uint32_t g_princeps_offset = 0;

uint32_t g_crea_relocs[1024];
int g_crea_reloc_count = 0;
uint32_t g_crea_offset = 0;

uint32_t g_neca_relocs[1024];
int g_neca_reloc_count = 0;
uint32_t g_neca_offset = 0;

uint32_t g_call_getprocessheap_reloc1 = 0;
uint32_t g_call_getprocessheap_reloc2 = 0;
uint32_t g_call_heapalloc_reloc = 0;
uint32_t g_call_heapfree_reloc = 0;

static void generate_machine_code(PeLinker* linker, SirModule* module) {
    uint32_t block_offsets[1024] = {0}; // 记录基本块的机器码偏移量
    
    const char* func_names[256];
    uint32_t func_offsets[256];
    int func_count = 0;

    const char* strings[1024];
    uint32_t string_offsets[1024];
    int string_count = 0;
    g_str_reloc_count = 0;

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

    // 两遍汇编 (Two-Pass Assembly)：第一遍计算跳转偏移，第二遍真正写入
    for (int pass = 0; pass < 2; pass++) {
        linker->text_section.size = 0;
        g_print_str_reloc_count = 0;
        g_print_int_reloc_count = 0;
        g_str_reloc_count = 0;
        g_crea_reloc_count = 0;
        g_neca_reloc_count = 0;
        func_count = 0;

        for (SirFunction* func = module->first_func; func; func = func->next) {
            func_names[func_count] = func->name;
            func_offsets[func_count] = (uint32_t)linker->text_section.size;
            func_count++;

            if (strcmp(func->name, "princeps") == 0) {
                g_princeps_offset = (uint32_t)linker->text_section.size;
            }

            uint32_t max_vreg = 0;
            int local_stack_size = 0;
            int* alloca_offsets = calloc(10000, sizeof(int));

            // 预扫描：计算最大寄存器和 ALLOCA 空间
            for (SirBlock* block = func->first_block; block; block = block->next) {
                for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                    if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                        if (inst->dest->as.vreg > max_vreg) max_vreg = inst->dest->as.vreg;
                    }
                    for (int i=0; i<inst->num_operands; i++) {
                        if (inst->operands[i] && inst->operands[i]->kind == SIR_VAL_VREG) {
                            if (inst->operands[i]->as.vreg > max_vreg) max_vreg = inst->operands[i]->as.vreg;
                        }
                    }
                    if (inst->opcode == SIR_ALLOCA) {
                        local_stack_size += 8;
                        alloca_offsets[inst->dest->as.vreg] = -local_stack_size;
                    }
                }
            }

            RegAllocator allocator;
            reg_alloc_init(&allocator, max_vreg);
            allocator.current_offset = local_stack_size;
            reg_alloc_build_and_color(&allocator, func);

            int stack_size = allocator.current_offset + 32; // 预留 32 字节 Shadow Space (Windows ABI)
            if (stack_size % 16 != 0) stack_size += 16 - (stack_size % 16);

            // 序言 (Prologue)
            emit8(&linker->text_section, 0x55); // push rbp
            emit_mov_reg_reg(&linker->text_section, REG_RBP, REG_RSP);
            if (stack_size > 0) {
                emit_rex(&linker->text_section, 1, 0, 0, 0);
                emit8(&linker->text_section, 0x81); // sub rsp, imm32
                emit_modrm(&linker->text_section, 3, 5, REG_RSP);
                emit32(&linker->text_section, (uint32_t)stack_size);
            }

            for (SirBlock* block = func->first_block; block; block = block->next) {
                if (block->id < 1024) block_offsets[block->id] = (uint32_t)linker->text_section.size;

                for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                    switch (inst->opcode) {
                        case SIR_GET_PARAM: {
                            int param_idx = (int)inst->operands[0]->as.int_val;
                            int dst = REG_RAX;
                            if (param_idx == 0) dst = REG_RCX;
                            else if (param_idx == 1) dst = REG_RDX;
                            else if (param_idx == 2) dst = 8; // R8
                            else if (param_idx == 3) dst = 9; // R9
                            else {
                                // 从栈中读取参数: [rbp + 48 + (param_idx - 4) * 8]
                                // 48 = 16 (rbp+ret) + 32 (shadow space)
                                int offset = 48 + (param_idx - 4) * 8;
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0x8B); // mov rax, [rbp + offset]
                                emit_mem(&linker->text_section, REG_RAX, REG_RBP, offset);
                            }
                            store_result(&linker->text_section, &allocator, inst->dest, dst);
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
                            int val_reg = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                            int ptr_reg = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                            emit_rex(&linker->text_section, 1, val_reg > 7, 0, ptr_reg > 7);
                            emit8(&linker->text_section, 0x89); // mov [ptr_reg], val_reg
                            emit_mem(&linker->text_section, val_reg, ptr_reg, 0);
                            break;
                        }
                        case SIR_LOAD: {
                            int ptr_reg = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RCX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                            emit_rex(&linker->text_section, 1, 0, 0, ptr_reg > 7);
                            emit8(&linker->text_section, 0x8B); // mov rax, [ptr_reg]
                            emit_mem(&linker->text_section, REG_RAX, ptr_reg, 0);
                            store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            break;
                        }
                        case SIR_CAST: {
                            int src = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                            if (src != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, src);
                            store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            break;
                        }
                        case SIR_ADD:
                        case SIR_SUB:
                        case SIR_MUL:
                        case SIR_AND:
                        case SIR_OR:
                        case SIR_XOR: {
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
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
                                int right = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
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
                        case SIR_DIV:
                        case SIR_MOD: {
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                            if (left != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, left);
                            int right = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                            if (right != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, right);
                            
                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                            emit8(&linker->text_section, 0x99); // cqo
                            
                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                            emit8(&linker->text_section, 0xF7); // idiv rcx
                            emit_modrm(&linker->text_section, 3, 7, REG_RCX);
                            
                            if (inst->opcode == SIR_MOD) {
                                emit_mov_reg_reg(&linker->text_section, REG_RAX, REG_RDX);
                            }
                            store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            break;
                        }
                        case SIR_SHL:
                        case SIR_SHR: {
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                            if (left != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, left);
                            int right = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                            if (right != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, right);
                            
                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                            emit8(&linker->text_section, 0xD3); // shl/shr rax, cl
                            emit_modrm(&linker->text_section, 3, inst->opcode == SIR_SHL ? 4 : 5, REG_RAX);
                            
                            store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            break;
                        }
                        case SIR_GEP: {
                            int ptr = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                            if (ptr != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, ptr);
                            
                            if (inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                                int32_t offset = (int32_t)(inst->operands[1]->as.int_val * 8);
                                if (offset != 0) emit_alu_reg_imm32(&linker->text_section, 0, REG_RAX, offset);
                            } else {
                                int idx = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                                if (idx != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, idx);
                                // shl rcx, 3 (乘以 8 字节对齐)
                                emit_rex(&linker->text_section, 1, 0, 0, 0);
                                emit8(&linker->text_section, 0xC1);
                                emit_modrm(&linker->text_section, 3, 4, REG_RCX);
                                emit8(&linker->text_section, 3);
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
                            int left = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                            if (inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                                int32_t imm = (int32_t)inst->operands[1]->as.int_val;
                                emit_alu_reg_imm32(&linker->text_section, 7, left, imm); // cmp left, imm
                            } else {
                                int right = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                                emit_rex(&linker->text_section, 1, right > 7, 0, left > 7);
                                emit8(&linker->text_section, 0x39); // cmp left, right
                                emit_modrm(&linker->text_section, 3, right & 7, left & 7);
                            }
                            
                            emit8(&linker->text_section, 0x0F); // setCC al
                            if (inst->opcode == SIR_ICMP_LT) emit8(&linker->text_section, 0x9C);
                            else if (inst->opcode == SIR_ICMP_GT) emit8(&linker->text_section, 0x9F);
                            else if (inst->opcode == SIR_ICMP_LE) emit8(&linker->text_section, 0x9E);
                            else if (inst->opcode == SIR_ICMP_GE) emit8(&linker->text_section, 0x9D);
                            else if (inst->opcode == SIR_ICMP_EQ) emit8(&linker->text_section, 0x94);
                            else if (inst->opcode == SIR_ICMP_NE) emit8(&linker->text_section, 0x95);
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
                            if (inst->operands[0]->kind == SIR_VAL_GLOBAL && strcmp(inst->operands[0]->as.global_name, "scribe") == 0) {
                                bool is_str = (inst->operands[1]->type && inst->operands[1]->type->kind == TY_TEXTUS) || (inst->operands[1]->kind == SIR_VAL_CONST_STRING);
                                
                                int arg = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
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
                                } else {
                                    emit8(&linker->text_section, 0xE8); // call rel32
                                    if (pass == 1) g_print_int_relocs[g_print_int_reloc_count++] = (uint32_t)linker->text_section.size;
                                    emit32(&linker->text_section, 0); // 占位符
                                }
                                break;
                            }
                            if (inst->operands[0]->kind == SIR_VAL_GLOBAL && strcmp(inst->operands[0]->as.global_name, "crea") == 0) {
                                int arg = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                                if (arg != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, arg);
                                emit8(&linker->text_section, 0xE8); // call rel32
                                if (pass == 1) g_crea_relocs[g_crea_reloc_count++] = (uint32_t)linker->text_section.size;
                                emit32(&linker->text_section, 0);
                                if (inst->dest) store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                                break;
                            }
                            if (inst->operands[0]->kind == SIR_VAL_GLOBAL && strcmp(inst->operands[0]->as.global_name, "neca") == 0) {
                                int arg = load_operand(&linker->text_section, &allocator, inst->operands[1], REG_RCX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                                if (arg != REG_RCX) emit_mov_reg_reg(&linker->text_section, REG_RCX, arg);
                                emit8(&linker->text_section, 0xE8); // call rel32
                                if (pass == 1) g_neca_relocs[g_neca_reloc_count++] = (uint32_t)linker->text_section.size;
                                emit32(&linker->text_section, 0);
                                break;
                            }

                            int arg_regs[] = {REG_RCX, REG_RDX, 8, 9};
                            for (int i = 0; i < inst->num_operands - 1; i++) {
                                int val = load_operand(&linker->text_section, &allocator, inst->operands[i+1], REG_RAX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                                if (i < 4) {
                                    if (val != arg_regs[i]) emit_mov_reg_reg(&linker->text_section, arg_regs[i], val);
                                } else {
                                    emit_rex(&linker->text_section, 1, val > 7, 0, 0);
                                    emit8(&linker->text_section, 0x89);
                                    emit_mem(&linker->text_section, val, REG_RSP, 32 + (i - 4) * 8);
                                }
                            }
                            emit_rex(&linker->text_section, 1, 0, 0, 0);
                            emit8(&linker->text_section, 0x31);
                            emit_modrm(&linker->text_section, 3, REG_RAX, REG_RAX);
                            
                            uint32_t target_offset = 0;
                            if (inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                                for (int f = 0; f < func_count; f++) {
                                    if (strcmp(func_names[f], inst->operands[0]->as.global_name) == 0) {
                                        target_offset = func_offsets[f];
                                        break;
                                    }
                                }
                            }
                            
                            emit8(&linker->text_section, 0xE8); // call rel32
                            emit32(&linker->text_section, (uint32_t)(target_offset - (linker->text_section.size + 4)));
                            
                            if (inst->dest) {
                                store_result(&linker->text_section, &allocator, inst->dest, REG_RAX);
                            }
                            break;
                        }
                        case SIR_BR: {
                            int cond = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
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
                                int val = load_operand(&linker->text_section, &allocator, inst->operands[0], REG_RAX, pass == 1 ? g_str_relocs : NULL, g_str_rdata_offs, &g_str_reloc_count, strings, string_offsets, string_count);
                                if (val != REG_RAX) emit_mov_reg_reg(&linker->text_section, REG_RAX, val);
                            }
                            // 跋 (Epilogue)
                            emit_mov_reg_reg(&linker->text_section, REG_RSP, REG_RBP);
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
        for (size_t i = 0; i < sizeof(print_int_code); i++) buf_append(&linker->text_section, print_int_code[i]);
        g_call_getstdhandle_reloc2 = g_print_int_offset + 107;
        g_call_writeconsolea_reloc2 = g_print_int_offset + 140;

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
        // call princeps
        emit8(&linker->text_section, 0xE8);
        int32_t rel_princeps = (int32_t)(g_princeps_offset - (linker->text_section.size + 4));
        emit32(&linker->text_section, (uint32_t)rel_princeps);
        // mov rcx, rax (exit code)
        emit_rex(&linker->text_section, 1, 0, 0, 0); emit8(&linker->text_section, 0x89); emit8(&linker->text_section, 0xC1);
        // call [rip + IAT_ExitProcess]
        emit8(&linker->text_section, 0xFF); emit8(&linker->text_section, 0x15);
        g_call_exitprocess_reloc = (uint32_t)linker->text_section.size;
        emit32(&linker->text_section, 0);
    }
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
    coff.NumberOfSections = 2;   // .text 和 .rdata
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

    opt.SizeOfCode = text_sec.SizeOfRawData;
    opt.SizeOfInitializedData = rdata_sec.SizeOfRawData;
    opt.SizeOfImage = align_up(rdata_sec.VirtualAddress + rdata_sec.VirtualSize, sec_align);
    opt.SizeOfHeaders = align_up((uint32_t)(sizeof(DosHeader) + sizeof(CoffHeader) + sizeof(OptionalHeader64) + sizeof(SectionHeader) * 2), file_align);

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
    opt.SizeOfInitializedData = rdata_sec.SizeOfRawData;
    opt.SizeOfImage = align_up(rdata_sec.VirtualAddress + rdata_sec.VirtualSize, sec_align);

    // 重新修正 PointerToRawData 因为 SizeOfHeaders 可能变了
    text_sec.PointerToRawData = opt.SizeOfHeaders;
    rdata_sec.PointerToRawData = text_sec.PointerToRawData + text_sec.SizeOfRawData;

    // 写入所有头部
    fwrite(&dos, 1, sizeof(dos), out);
    fwrite(&coff, 1, sizeof(coff), out);
    fwrite(&opt, 1, sizeof(opt), out);
    fwrite(&text_sec, 1, sizeof(text_sec), out);
    fwrite(&rdata_sec, 1, sizeof(rdata_sec), out);

    // 填充对齐到代码段起始位置
    uint8_t zero = 0;
    while ((uint32_t)ftell(out) < text_sec.PointerToRawData) fwrite(&zero, 1, 1, out);

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
    while ((uint32_t)ftell(out) < rdata_sec.PointerToRawData + rdata_sec.SizeOfRawData) fwrite(&zero, 1, 1, out);

    fclose(out);
    return true;
}
