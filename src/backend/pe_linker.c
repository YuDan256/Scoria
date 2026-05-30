#include "pe_linker.h"
#include "x86_mir.h"
#include "builtins.h"
#include "pe_idata.h"
#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct { uint16_t e_magic; uint8_t e_res[58]; uint32_t e_lfanew; } DosHeader;
typedef struct { uint32_t Signature; uint16_t Machine; uint16_t NumberOfSections; uint32_t TimeDateStamp; uint32_t PointerToSymbolTable; uint32_t NumberOfSymbols; uint16_t SizeOfOptionalHeader; uint16_t Characteristics; } CoffHeader;
typedef struct { uint16_t Magic; uint8_t MajorLinkerVersion; uint8_t MinorLinkerVersion; uint32_t SizeOfCode; uint32_t SizeOfInitializedData; uint32_t SizeOfUninitializedData; uint32_t AddressOfEntryPoint; uint32_t BaseOfCode; uint64_t ImageBase; uint32_t SectionAlignment; uint32_t FileAlignment; uint16_t MajorOperatingSystemVersion; uint16_t MinorOperatingSystemVersion; uint16_t MajorImageVersion; uint16_t MinorImageVersion; uint16_t MajorSubsystemVersion; uint16_t MinorSubsystemVersion; uint32_t Win32VersionValue; uint32_t SizeOfImage; uint32_t SizeOfHeaders; uint32_t CheckSum; uint16_t Subsystem; uint16_t DllCharacteristics; uint64_t SizeOfStackReserve; uint64_t SizeOfStackCommit; uint64_t SizeOfHeapReserve; uint64_t SizeOfHeapCommit; uint32_t LoaderFlags; uint32_t NumberOfRvaAndSizes; struct { uint32_t VirtualAddress; uint32_t Size; } DataDirectory[16]; } OptionalHeader64;
typedef struct { uint8_t Name[8]; uint32_t VirtualSize; uint32_t VirtualAddress; uint32_t SizeOfRawData; uint32_t PointerToRawData; uint32_t PointerToRelocations; uint32_t PointerToLinenumbers; uint16_t NumberOfRelocations; uint16_t NumberOfLinenumbers; uint32_t Characteristics; } SectionHeader;
#pragma pack(pop)

static void buf_init(PeCodeBuffer* cb) { cb->capacity = 4096; cb->size = 0; cb->buffer = (uint8_t*)malloc(cb->capacity); }
static void buf_free(PeCodeBuffer* cb) { free(cb->buffer); }
static void buf_append(PeCodeBuffer* cb, uint8_t byte) {
    if (cb->size >= cb->capacity) {
        cb->capacity *= 2;
        uint8_t* new_buf = (uint8_t*)realloc(cb->buffer, cb->capacity);
        if (!new_buf) exit(1); // 内存分配失败时安全退出
        cb->buffer = new_buf;
    }
    cb->buffer[cb->size++] = byte;
}

void pe_linker_init(PeLinker* linker) { buf_init(&linker->text_section); buf_init(&linker->rdata_section); buf_init(&linker->data_section); linker->entry_point_offset = 0; }
void pe_linker_free(PeLinker* linker) { buf_free(&linker->text_section); buf_free(&linker->rdata_section); buf_free(&linker->data_section); }
static uint32_t align_up(uint32_t val, uint32_t alignment) { return (val + alignment - 1) & ~(alignment - 1); }

void emit8(PeCodeBuffer* cb, uint8_t b) { buf_append(cb, b); }
void emit32(PeCodeBuffer* cb, uint32_t v) { emit8(cb, v & 0xFF); emit8(cb, (v >> 8) & 0xFF); emit8(cb, (v >> 16) & 0xFF); emit8(cb, (v >> 24) & 0xFF); }
void emit_rex(PeCodeBuffer* cb, int w, int r, int x, int b) { emit8(cb, (uint8_t)(0x40 | (w ? 8 : 0) | (r ? 4 : 0) | (x ? 2 : 0) | (b ? 1 : 0))); }
void emit_modrm(PeCodeBuffer* cb, int mod, int reg, int rm) { emit8(cb, (uint8_t)(((mod & 3) << 6) | ((reg & 7) << 3) | (rm & 7))); }
static void emit_sib(PeCodeBuffer* cb, int scale, int index, int base) { emit8(cb, (uint8_t)(((scale & 3) << 6) | ((index & 7) << 3) | (base & 7))); }

static void emit_mem(PeCodeBuffer* cb, int reg, int base, int32_t offset) {
    int r = reg & 7, b = base & 7;
    if (offset == 0 && b != 5) { emit_modrm(cb, 0, r, b); if (b == 4) emit_sib(cb, 0, 4, 4); }
    else if (offset >= -128 && offset <= 127) { emit_modrm(cb, 1, r, b); if (b == 4) emit_sib(cb, 0, 4, 4); emit8(cb, (uint8_t)offset); }
    else { emit_modrm(cb, 2, r, b); if (b == 4) emit_sib(cb, 0, 4, 4); emit32(cb, (uint32_t)offset); }
}

void emit_mov_reg_imm32(PeCodeBuffer* cb, int reg, int32_t imm) {
    if (imm == 0) { if (reg > 7) emit_rex(cb, 0, 1, 0, 1); emit8(cb, 0x31); emit_modrm(cb, 3, reg & 7, reg & 7); return; }
    if (reg > 7) emit_rex(cb, 0, 0, 0, 1);
    emit8(cb, (uint8_t)(0xB8 | (reg & 7))); emit32(cb, (uint32_t)imm);
}

void emit_mov_reg_imm64(PeCodeBuffer* cb, int reg, uint64_t imm) {
    if (imm == 0) { if (reg > 7) emit_rex(cb, 0, 1, 0, 1); emit8(cb, 0x31); emit_modrm(cb, 3, reg & 7, reg & 7); return; }
    emit_rex(cb, 1, 0, 0, reg > 7); emit8(cb, (uint8_t)(0xB8 | (reg & 7)));
    emit32(cb, (uint32_t)(imm & 0xFFFFFFFF)); emit32(cb, (uint32_t)(imm >> 32));
}

typedef struct {
    int pass;
    const char** strings; uint32_t* string_lens; uint32_t* string_offsets; int string_count;
    const char** globals; uint32_t* global_offsets; int global_count;
    const char** funcs; uint32_t* func_offsets; int func_count;
    SirExternFunc* first_extern;
    uint32_t max_block_id; uint32_t* block_offsets;
} LinkCtx;

#define MAX_RELOCS 65536
uint32_t g_str_relocs[MAX_RELOCS], g_str_rdata_offs[MAX_RELOCS]; int g_str_reloc_count = 0;
uint32_t g_data_relocs[MAX_RELOCS], g_data_offs[MAX_RELOCS]; int g_data_reloc_count = 0;
uint32_t g_func_relocs[MAX_RELOCS], g_func_offs[MAX_RELOCS]; int g_func_reloc_count = 0;
uint32_t g_extern_relocs[MAX_RELOCS]; int g_extern_idxs[MAX_RELOCS]; int g_extern_reloc_count = 0;

uint32_t g_print_str_relocs[MAX_RELOCS]; int g_print_str_reloc_count = 0;
uint32_t g_print_int_relocs[MAX_RELOCS]; int g_print_int_reloc_count = 0;
uint32_t g_print_uint_relocs[MAX_RELOCS]; int g_print_uint_reloc_count = 0;
uint32_t g_print_hex_relocs[MAX_RELOCS]; int g_print_hex_reloc_count = 0;
uint32_t g_print_float_relocs[MAX_RELOCS]; int g_print_float_reloc_count = 0;
uint32_t g_print_bool_relocs[MAX_RELOCS]; int g_print_bool_reloc_count = 0;
uint32_t g_crea_relocs[MAX_RELOCS]; int g_crea_reloc_count = 0;
uint32_t g_neca_relocs[MAX_RELOCS]; int g_neca_reloc_count = 0;
uint32_t g_lege_int_relocs[MAX_RELOCS]; int g_lege_int_reloc_count = 0;
uint32_t g_lege_float_relocs[MAX_RELOCS]; int g_lege_float_reloc_count = 0;
uint32_t g_lege_char_relocs[MAX_RELOCS]; int g_lege_char_reloc_count = 0;
uint32_t g_lege_bool_relocs[MAX_RELOCS]; int g_lege_bool_reloc_count = 0;

uint32_t g_verum_rdata_off = 0, g_falsum_rdata_off = 0, g_dot_rdata_off = 0, g_minus_rdata_off = 0, g_float_10_rdata_off = 0;
uint32_t g_princeps_offset = 0, g_init_offset = 0;

static void get_mem_rex(X86Operand* op, int* x, int* b) {
    *x = 0; *b = 0;
    if (op->kind == X86_OP_MEM_BASE_DISP) { if (op->as.mem_bd.base > 7) *b = 1; }
    else if (op->kind == X86_OP_MEM_SIB) { if (op->as.mem_sib.base > 7) *b = 1; if (op->as.mem_sib.index > 7) *x = 1; }
}

static void emit_mem_op(PeCodeBuffer* cb, int reg, X86Operand* op, LinkCtx* ctx) {
    if (op->kind == X86_OP_MEM_BASE_DISP) {
        emit_mem(cb, reg, op->as.mem_bd.base, op->as.mem_bd.disp);
    } else if (op->kind == X86_OP_MEM_SIB) {
        int b = op->as.mem_sib.base & 7;
        if (op->as.mem_sib.disp == 0 && b != 5) { emit_modrm(cb, 0, reg & 7, 4); emit_sib(cb, op->as.mem_sib.scale, op->as.mem_sib.index, b); }
        else if (op->as.mem_sib.disp >= -128 && op->as.mem_sib.disp <= 127) { emit_modrm(cb, 1, reg & 7, 4); emit_sib(cb, op->as.mem_sib.scale, op->as.mem_sib.index, b); emit8(cb, (uint8_t)op->as.mem_sib.disp); }
        else { emit_modrm(cb, 2, reg & 7, 4); emit_sib(cb, op->as.mem_sib.scale, op->as.mem_sib.index, b); emit32(cb, (uint32_t)op->as.mem_sib.disp); }
    } else if (op->kind == X86_OP_MEM_RIP || op->kind == X86_OP_STRING) {
        emit_modrm(cb, 0, reg & 7, 5);
        if (ctx->pass == 1) {
            if (op->kind == X86_OP_STRING) {
                uint32_t rdata_off = 0;
                for (int i = 0; i < ctx->string_count; i++) {
                    if (ctx->string_lens[i] == op->as.string.len && memcmp(ctx->strings[i], op->as.string.str, op->as.string.len) == 0) { rdata_off = ctx->string_offsets[i]; break; }
                }
                g_str_relocs[g_str_reloc_count] = (uint32_t)cb->size; g_str_rdata_offs[g_str_reloc_count++] = rdata_off;
            } else {
                const char* name = op->as.label; bool is_global = false;
                for (int i = 0; i < ctx->global_count; i++) {
                    if (strcmp(ctx->globals[i], name) == 0) { g_data_relocs[g_data_reloc_count] = (uint32_t)cb->size; g_data_offs[g_data_reloc_count++] = ctx->global_offsets[i]; is_global = true; break; }
                }
                if (!is_global) {
                    bool is_ext = false; int ext_idx = 0;
                    for (SirExternFunc* ext = ctx->first_extern; ext; ext = ext->next, ext_idx++) {
                        if (strcmp(ext->name, name) == 0) { g_extern_relocs[g_extern_reloc_count] = (uint32_t)cb->size; g_extern_idxs[g_extern_reloc_count++] = ext_idx; is_ext = true; break; }
                    }
                    if (!is_ext) {
                        for (int i = 0; i < ctx->func_count; i++) {
                            if (strcmp(ctx->funcs[i], name) == 0) { g_func_relocs[g_func_reloc_count] = (uint32_t)cb->size; g_func_offs[g_func_reloc_count++] = ctx->func_offsets[i]; break; }
                        }
                    }
                }
            }
        }
        emit32(cb, 0);
    }
}

static void emit_x86_inst(PeCodeBuffer* cb, X86Inst* inst, LinkCtx* ctx) {
    int opc = inst->opcode;
    X86Operand* op0 = &inst->ops[0];
    X86Operand* op1 = &inst->ops[1];
    int w = (inst->num_ops > 0 && op0->size == 8) ? 1 : 0;
    if (inst->num_ops > 0 && op0->size == 2) emit8(cb, 0x66);

    switch (opc) {
        case X86_INST_MOV:
            if (op0->kind == X86_OP_REG && op1->kind == X86_OP_IMM) {
                if (op0->size == 1) { emit_rex(cb, 0, 0, 0, op0->as.reg > 7); emit8(cb, (uint8_t)(0xB0 | (op0->as.reg & 7))); emit8(cb, (uint8_t)op1->as.imm); }
                else if (op0->size == 2) { emit_rex(cb, 0, 0, 0, op0->as.reg > 7); emit8(cb, (uint8_t)(0xB8 | (op0->as.reg & 7))); emit8(cb, (uint8_t)(op1->as.imm & 0xFF)); emit8(cb, (uint8_t)((op1->as.imm >> 8) & 0xFF)); }
                else if (op0->size == 4) { emit_mov_reg_imm32(cb, op0->as.reg, (int32_t)op1->as.imm); }
                else { emit_mov_reg_imm64(cb, op0->as.reg, (uint64_t)op1->as.imm); }
            } else if (op0->kind == X86_OP_REG && op1->kind == X86_OP_REG) {
                if (op0->as.reg == op1->as.reg) break;
                if (op0->size == 1) { emit_rex(cb, 0, op1->as.reg > 7, 0, op0->as.reg > 7); emit8(cb, 0x88); }
                else { emit_rex(cb, w, op1->as.reg > 7, 0, op0->as.reg > 7); emit8(cb, 0x89); }
                emit_modrm(cb, 3, op1->as.reg & 7, op0->as.reg & 7);
            } else if (op0->kind == X86_OP_REG && op1->kind >= X86_OP_MEM_BASE_DISP) {
                int x=0, b=0; get_mem_rex(op1, &x, &b);
                if (op0->size == 1) { emit_rex(cb, 0, op0->as.reg > 7, x, b); emit8(cb, 0x8A); }
                else { emit_rex(cb, w, op0->as.reg > 7, x, b); emit8(cb, 0x8B); }
                emit_mem_op(cb, op0->as.reg, op1, ctx);
            } else if (op0->kind >= X86_OP_MEM_BASE_DISP && op1->kind == X86_OP_REG) {
                int x=0, b=0; get_mem_rex(op0, &x, &b);
                if (op1->size == 1) { emit_rex(cb, 0, op1->as.reg > 7, x, b); emit8(cb, 0x88); }
                else { emit_rex(cb, w, op1->as.reg > 7, x, b); emit8(cb, 0x89); }
                emit_mem_op(cb, op1->as.reg, op0, ctx);
            }
            break;
        case X86_INST_MOVSX: case X86_INST_MOVZX: {
            int x=0, b=0;
            if (op1->kind >= X86_OP_MEM_BASE_DISP) get_mem_rex(op1, &x, &b);
            else if (op1->kind == X86_OP_REG && op1->as.reg > 7) b = 1;
            if (op1->size == 4 && opc == X86_INST_MOVSX) { emit_rex(cb, 1, op0->as.reg > 7, x, b); emit8(cb, 0x63); }
            else { emit_rex(cb, w, op0->as.reg > 7, x, b); emit8(cb, 0x0F); if (opc == X86_INST_MOVSX) emit8(cb, (uint8_t)(op1->size == 1 ? 0xBE : 0xBF)); else emit8(cb, (uint8_t)(op1->size == 1 ? 0xB6 : 0xB7)); }
            if (op1->kind == X86_OP_REG) emit_modrm(cb, 3, op0->as.reg & 7, op1->as.reg & 7); else emit_mem_op(cb, op0->as.reg, op1, ctx);
            break;
        }
        case X86_INST_LEA: {
            int x=0, b=0; get_mem_rex(op1, &x, &b);
            emit_rex(cb, w, op0->as.reg > 7, x, b); emit8(cb, 0x8D); emit_mem_op(cb, op0->as.reg, op1, ctx);
            break;
        }
        case X86_INST_ADD: case X86_INST_SUB: case X86_INST_AND: case X86_INST_OR: case X86_INST_XOR: case X86_INST_CMP: case X86_INST_TEST: {
            uint8_t opc_byte = 0, opc_ext = 0;
            if (opc == X86_INST_ADD) { opc_byte = 0x01; opc_ext = 0; } else if (opc == X86_INST_SUB) { opc_byte = 0x29; opc_ext = 5; }
            else if (opc == X86_INST_AND) { opc_byte = 0x21; opc_ext = 4; } else if (opc == X86_INST_OR) { opc_byte = 0x09; opc_ext = 1; }
            else if (opc == X86_INST_XOR) { opc_byte = 0x31; opc_ext = 6; } else if (opc == X86_INST_CMP) { opc_byte = 0x39; opc_ext = 7; }
            else if (opc == X86_INST_TEST) { opc_byte = 0x85; }
            if (op1->kind == X86_OP_REG) {
                emit_rex(cb, w, op1->as.reg > 7, 0, op0->as.reg > 7); emit8(cb, (uint8_t)(op0->size == 1 ? (opc_byte - 1) : opc_byte)); emit_modrm(cb, 3, op1->as.reg & 7, op0->as.reg & 7);
            } else if (op1->kind == X86_OP_IMM) {
                if (opc == X86_INST_TEST) {
                    emit_rex(cb, w, 0, 0, op0->as.reg > 7); emit8(cb, op0->size == 1 ? 0xF6 : 0xF7); emit_modrm(cb, 3, 0, op0->as.reg & 7);
                    if (op0->size == 1) emit8(cb, (uint8_t)op1->as.imm); else emit32(cb, (uint32_t)op1->as.imm);
                } else {
                    emit_rex(cb, w, 0, 0, op0->as.reg > 7);
                    if (op0->size == 1) { emit8(cb, 0x80); emit_modrm(cb, 3, opc_ext, op0->as.reg & 7); emit8(cb, (uint8_t)op1->as.imm); }
                    else if (op1->as.imm >= -128 && op1->as.imm <= 127) { emit8(cb, 0x83); emit_modrm(cb, 3, opc_ext, op0->as.reg & 7); emit8(cb, (uint8_t)op1->as.imm); }
                    else { emit8(cb, 0x81); emit_modrm(cb, 3, opc_ext, op0->as.reg & 7); emit32(cb, (uint32_t)op1->as.imm); }
                }
            }
            break;
        }
        case X86_INST_IMUL:
            if (op1->kind == X86_OP_REG) { emit_rex(cb, w, op0->as.reg > 7, 0, op1->as.reg > 7); emit8(cb, 0x0F); emit8(cb, 0xAF); emit_modrm(cb, 3, op0->as.reg & 7, op1->as.reg & 7); }
            else if (op1->kind == X86_OP_IMM) {
                emit_rex(cb, w, op0->as.reg > 7, 0, op0->as.reg > 7);
                if (op1->as.imm >= -128 && op1->as.imm <= 127) { emit8(cb, 0x6B); emit_modrm(cb, 3, op0->as.reg & 7, op0->as.reg & 7); emit8(cb, (uint8_t)op1->as.imm); }
                else { emit8(cb, 0x69); emit_modrm(cb, 3, op0->as.reg & 7, op0->as.reg & 7); emit32(cb, (uint32_t)op1->as.imm); }
            }
            break;
        case X86_INST_IDIV: case X86_INST_DIV: case X86_INST_NEG: case X86_INST_NOT: case X86_INST_INC: case X86_INST_DEC: {
            int ext = 0;
            if (opc == X86_INST_IDIV) ext = 7; else if (opc == X86_INST_DIV) ext = 6; else if (opc == X86_INST_NEG) ext = 3;
            else if (opc == X86_INST_NOT) ext = 2; else if (opc == X86_INST_INC) ext = 0; else if (opc == X86_INST_DEC) ext = 1;
            emit_rex(cb, w, 0, 0, op0->as.reg > 7);
            if (opc == X86_INST_INC || opc == X86_INST_DEC) emit8(cb, (uint8_t)(op0->size == 1 ? 0xFE : 0xFF)); else emit8(cb, (uint8_t)(op0->size == 1 ? 0xF6 : 0xF7));
            emit_modrm(cb, 3, ext, op0->as.reg & 7);
            break;
        }
        case X86_INST_PUSH: if (op0->as.reg > 7) emit_rex(cb, 0, 0, 0, 1); emit8(cb, (uint8_t)(0x50 | (op0->as.reg & 7))); break;
        case X86_INST_POP: if (op0->as.reg > 7) emit_rex(cb, 0, 0, 0, 1); emit8(cb, (uint8_t)(0x58 | (op0->as.reg & 7))); break;
        case X86_INST_SETCC: {
            uint8_t cc = 0x90;
            if (inst->cond == X86_COND_E) cc = 0x94; else if (inst->cond == X86_COND_NE) cc = 0x95; else if (inst->cond == X86_COND_L) cc = 0x9C;
            else if (inst->cond == X86_COND_LE) cc = 0x9E; else if (inst->cond == X86_COND_G) cc = 0x9F; else if (inst->cond == X86_COND_GE) cc = 0x9D;
            else if (inst->cond == X86_COND_B) cc = 0x92; else if (inst->cond == X86_COND_BE) cc = 0x96; else if (inst->cond == X86_COND_A) cc = 0x97; else if (inst->cond == X86_COND_AE) cc = 0x93;
            emit_rex(cb, 0, 0, 0, op0->as.reg > 7); emit8(cb, 0x0F); emit8(cb, cc); emit_modrm(cb, 3, 0, op0->as.reg & 7);
            break;
        }
        case X86_INST_SHL: case X86_INST_SHR: case X86_INST_SAR: {
            int ext = (opc == X86_INST_SHL) ? 4 : (opc == X86_INST_SHR ? 5 : 7);
            emit_rex(cb, w, 0, 0, op0->as.reg > 7); emit8(cb, (uint8_t)(op0->size == 1 ? 0xD2 : 0xD3)); emit_modrm(cb, 3, ext, op0->as.reg & 7);
            break;
        }
        case X86_INST_JMP: case X86_INST_JCC: {
            uint32_t target_id = op0->as.block_id;
            uint32_t target_off = target_id <= ctx->max_block_id ? ctx->block_offsets[target_id] : 0;
            if (opc == X86_INST_JMP) { emit8(cb, 0xE9); } else {
                uint8_t cc = 0x80;
                if (inst->cond == X86_COND_E) cc = 0x84; else if (inst->cond == X86_COND_NE) cc = 0x85; else if (inst->cond == X86_COND_L) cc = 0x8C;
                else if (inst->cond == X86_COND_LE) cc = 0x8E; else if (inst->cond == X86_COND_G) cc = 0x8F; else if (inst->cond == X86_COND_GE) cc = 0x8D;
                else if (inst->cond == X86_COND_B) cc = 0x82; else if (inst->cond == X86_COND_BE) cc = 0x86; else if (inst->cond == X86_COND_A) cc = 0x87; else if (inst->cond == X86_COND_AE) cc = 0x83;
                emit8(cb, 0x0F); emit8(cb, cc);
            }
            emit32(cb, (uint32_t)(target_off - (cb->size + 4)));
            break;
        }
        case X86_INST_CALL:
            if (op0->kind == X86_OP_LABEL) {
                emit8(cb, 0xE8); const char* name = op0->as.label;
                if (ctx->pass == 1) {
                    if (strcmp(name, "__print_str") == 0) g_print_str_relocs[g_print_str_reloc_count++] = (uint32_t)cb->size;
                    else if (strcmp(name, "__print_int") == 0) g_print_int_relocs[g_print_int_reloc_count++] = (uint32_t)cb->size;
                    else if (strcmp(name, "__print_uint") == 0) g_print_uint_relocs[g_print_uint_reloc_count++] = (uint32_t)cb->size;
                    else if (strcmp(name, "__print_hex") == 0) g_print_hex_relocs[g_print_hex_reloc_count++] = (uint32_t)cb->size;
                    else if (strcmp(name, "__print_float") == 0) g_print_float_relocs[g_print_float_reloc_count++] = (uint32_t)cb->size;
                    else if (strcmp(name, "__print_bool") == 0) g_print_bool_relocs[g_print_bool_reloc_count++] = (uint32_t)cb->size;
                    else if (strcmp(name, "crea") == 0) g_crea_relocs[g_crea_reloc_count++] = (uint32_t)cb->size;
                    else if (strcmp(name, "neca") == 0) g_neca_relocs[g_neca_reloc_count++] = (uint32_t)cb->size;
                    else if (strcmp(name, "__lege_int") == 0) g_lege_int_relocs[g_lege_int_reloc_count++] = (uint32_t)cb->size;
                    else if (strcmp(name, "__lege_float") == 0) g_lege_float_relocs[g_lege_float_reloc_count++] = (uint32_t)cb->size;
                    else if (strcmp(name, "__lege_char") == 0) g_lege_char_relocs[g_lege_char_reloc_count++] = (uint32_t)cb->size;
                    else if (strcmp(name, "__lege_bool") == 0) g_lege_bool_relocs[g_lege_bool_reloc_count++] = (uint32_t)cb->size;
                    else {
                        for (int i = 0; i < ctx->func_count; i++) {
                            if (strcmp(ctx->funcs[i], name) == 0) { g_func_relocs[g_func_reloc_count] = (uint32_t)cb->size; g_func_offs[g_func_reloc_count++] = ctx->func_offsets[i]; break; }
                        }
                    }
                }
                emit32(cb, 0);
            } else if (op0->kind == X86_OP_REG) { emit_rex(cb, 1, 0, 0, op0->as.reg > 7); emit8(cb, 0xFF); emit_modrm(cb, 3, 2, op0->as.reg & 7); }
            break;
        case X86_INST_RET: emit8(cb, 0xC3); break;
        case X86_INST_CQO: emit_rex(cb, 1, 0, 0, 0); emit8(cb, 0x99); break;
        case X86_INST_CDQ: emit8(cb, 0x99); break;
        case X86_INST_CLD: emit8(cb, 0xFC); break;
        case X86_INST_REP_MOVSB: emit8(cb, 0xF3); emit8(cb, 0xA4); break;
        case X86_INST_UD2: emit8(cb, 0x0F); emit8(cb, 0x0B); break;
        case X86_INST_XCHG:
            if (op0->as.reg == X86_REG_RAX) { emit_rex(cb, w, 0, 0, op1->as.reg > 7); emit8(cb, (uint8_t)(0x90 | (op1->as.reg & 7))); }
            else if (op1->as.reg == X86_REG_RAX) { emit_rex(cb, w, 0, 0, op0->as.reg > 7); emit8(cb, (uint8_t)(0x90 | (op0->as.reg & 7))); }
            else { emit_rex(cb, w, op1->as.reg > 7, 0, op0->as.reg > 7); emit8(cb, 0x87); emit_modrm(cb, 3, op1->as.reg & 7, op0->as.reg & 7); }
            break;
        case X86_INST_CMOVCC: {
            uint8_t cc = 0x40;
            if (inst->cond == X86_COND_E) cc = 0x44; else if (inst->cond == X86_COND_NE) cc = 0x45; else if (inst->cond == X86_COND_L) cc = 0x4C;
            else if (inst->cond == X86_COND_LE) cc = 0x4E; else if (inst->cond == X86_COND_G) cc = 0x4F; else if (inst->cond == X86_COND_GE) cc = 0x4D;
            else if (inst->cond == X86_COND_B) cc = 0x42; else if (inst->cond == X86_COND_BE) cc = 0x46; else if (inst->cond == X86_COND_A) cc = 0x47; else if (inst->cond == X86_COND_AE) cc = 0x43;
            emit_rex(cb, w, op0->as.reg > 7, 0, op1->as.reg > 7); emit8(cb, 0x0F); emit8(cb, cc); emit_modrm(cb, 3, op0->as.reg & 7, op1->as.reg & 7);
            break;
        }
        case X86_INST_MOVD: case X86_INST_MOVQ: {
            emit8(cb, 0x66);
            bool is_xmm0 = (op0->as.reg >= X86_REG_XMM0), is_xmm1 = (op1->as.reg >= X86_REG_XMM0);
            int r0 = is_xmm0 ? op0->as.reg - X86_REG_XMM0 : op0->as.reg, r1 = is_xmm1 ? op1->as.reg - X86_REG_XMM0 : op1->as.reg;
            if (is_xmm0 && !is_xmm1) { emit_rex(cb, opc == X86_INST_MOVQ ? 1 : 0, r0 > 7, 0, r1 > 7); emit8(cb, 0x0F); emit8(cb, 0x6E); emit_modrm(cb, 3, r0 & 7, r1 & 7); }
            else if (!is_xmm0 && is_xmm1) { emit_rex(cb, opc == X86_INST_MOVQ ? 1 : 0, r1 > 7, 0, r0 > 7); emit8(cb, 0x0F); emit8(cb, 0x7E); emit_modrm(cb, 3, r1 & 7, r0 & 7); }
            break;
        }
        case X86_INST_CVTSI2SS: case X86_INST_CVTSI2SD: {
            emit8(cb, opc == X86_INST_CVTSI2SS ? 0xF3 : 0xF2); int r0 = op0->as.reg - X86_REG_XMM0;
            emit_rex(cb, 1, r0 > 7, 0, op1->as.reg > 7); emit8(cb, 0x0F); emit8(cb, 0x2A); emit_modrm(cb, 3, r0 & 7, op1->as.reg & 7);
            break;
        }
        case X86_INST_CVTTSS2SI: case X86_INST_CVTTSD2SI: {
            emit8(cb, opc == X86_INST_CVTTSS2SI ? 0xF3 : 0xF2); int r1 = op1->as.reg - X86_REG_XMM0;
            emit_rex(cb, 1, op0->as.reg > 7, 0, r1 > 7); emit8(cb, 0x0F); emit8(cb, 0x2C); emit_modrm(cb, 3, op0->as.reg & 7, r1 & 7);
            break;
        }
        case X86_INST_CVTSS2SD: case X86_INST_CVTSD2SS: {
            emit8(cb, opc == X86_INST_CVTSS2SD ? 0xF3 : 0xF2); int r0 = op0->as.reg - X86_REG_XMM0, r1 = op1->as.reg - X86_REG_XMM0;
            emit_rex(cb, 0, r0 > 7, 0, r1 > 7); emit8(cb, 0x0F); emit8(cb, 0x5A); emit_modrm(cb, 3, r0 & 7, r1 & 7);
            break;
        }
        case X86_INST_ADDSS: case X86_INST_ADDSD: case X86_INST_SUBSS: case X86_INST_SUBSD: case X86_INST_MULSS: case X86_INST_MULSD: case X86_INST_DIVSS: case X86_INST_DIVSD: {
            bool is_sd = (opc == X86_INST_ADDSD || opc == X86_INST_SUBSD || opc == X86_INST_MULSD || opc == X86_INST_DIVSD);
            emit8(cb, is_sd ? 0xF2 : 0xF3); int r0 = op0->as.reg - X86_REG_XMM0, r1 = op1->as.reg - X86_REG_XMM0;
            emit_rex(cb, 0, r0 > 7, 0, r1 > 7); emit8(cb, 0x0F);
            if (opc == X86_INST_ADDSS || opc == X86_INST_ADDSD) emit8(cb, 0x58); else if (opc == X86_INST_SUBSS || opc == X86_INST_SUBSD) emit8(cb, 0x5C);
            else if (opc == X86_INST_MULSS || opc == X86_INST_MULSD) emit8(cb, 0x59); else if (opc == X86_INST_DIVSS || opc == X86_INST_DIVSD) emit8(cb, 0x5E);
            emit_modrm(cb, 3, r0 & 7, r1 & 7);
            break;
        }
        case X86_INST_UCOMISS: case X86_INST_UCOMISD: {
            if (opc == X86_INST_UCOMISD) emit8(cb, 0x66);
            int r0 = op0->as.reg - X86_REG_XMM0, r1 = op1->as.reg - X86_REG_XMM0;
            emit_rex(cb, 0, r0 > 7, 0, r1 > 7); emit8(cb, 0x0F); emit8(cb, 0x2E); emit_modrm(cb, 3, r0 & 7, r1 & 7);
            break;
        }
        case X86_INST_MOVSS: case X86_INST_MOVSD: {
            emit8(cb, opc == X86_INST_MOVSS ? 0xF3 : 0xF2);
            int r0 = op0->as.reg - X86_REG_XMM0, r1 = op1->as.reg - X86_REG_XMM0;
            emit_rex(cb, 0, r0 > 7, 0, r1 > 7); emit8(cb, 0x0F); emit8(cb, 0x10); emit_modrm(cb, 3, r0 & 7, r1 & 7);
            break;
        }
        default: break;
    }
}

static void generate_machine_code(PeLinker* linker, SirModule* module, int opt_level) {
    builtins_analyze_usage(module);
    X86Module* mir = x86_mir_build(module, opt_level);

    uint32_t max_block_id = 0;
    for (X86Function* func = mir->first_func; func; func = func->next) {
        for (X86Block* block = func->first_block; block; block = block->next) {
            if (block->id > max_block_id) max_block_id = block->id;
        }
    }
    uint32_t* block_offsets = (uint32_t*)calloc(max_block_id + 1, sizeof(uint32_t));
    
    int func_capacity = 256;
    const char** func_names = (const char**)malloc(func_capacity * sizeof(const char*));
    uint32_t* func_offsets = (uint32_t*)malloc(func_capacity * sizeof(uint32_t));
    int func_count = 0;

    int string_capacity = 1024;
    const char** strings = (const char**)malloc(string_capacity * sizeof(const char*));
    uint32_t* string_lens = (uint32_t*)malloc(string_capacity * sizeof(uint32_t));
    uint32_t* string_offsets = (uint32_t*)malloc(string_capacity * sizeof(uint32_t));
    int string_count = 0;

    int global_capacity = 1024;
    const char** global_names = (const char**)malloc(global_capacity * sizeof(const char*));
    uint32_t* global_offsets = (uint32_t*)malloc(global_capacity * sizeof(uint32_t));
    int global_count = 0;

    for (SirGlobalVar* g = module->first_global; g; g = g->next) {
        if (global_count >= global_capacity) {
            global_capacity *= 2;
            const char** new_gn = (const char**)realloc(global_names, global_capacity * sizeof(const char*));
            if (!new_gn) exit(1);
            global_names = new_gn;
            uint32_t* new_go = (uint32_t*)realloc(global_offsets, global_capacity * sizeof(uint32_t));
            if (!new_go) exit(1);
            global_offsets = new_go;
        }
        while (linker->data_section.size % 8 != 0) buf_append(&linker->data_section, 0);
        global_names[global_count] = g->name;
        global_offsets[global_count] = (uint32_t)linker->data_section.size;
        global_count++;
        if (g->init_data) { for (int i = 0; i < g->size; i++) buf_append(&linker->data_section, g->init_data[i]); }
        else { for (int i = 0; i < g->size; i++) buf_append(&linker->data_section, 0); }
    }

    for (X86Function* func = mir->first_func; func; func = func->next) {
        if (func_count >= func_capacity) {
            func_capacity *= 2;
            const char** new_fn = (const char**)realloc(func_names, func_capacity * sizeof(const char*));
            if (!new_fn) exit(1);
            func_names = new_fn;
            uint32_t* new_fo = (uint32_t*)realloc(func_offsets, func_capacity * sizeof(uint32_t));
            if (!new_fo) exit(1);
            func_offsets = new_fo;
        }
        func_names[func_count++] = func->name;
    }

    for (X86Function* func = mir->first_func; func; func = func->next) {
        for (X86Block* block = func->first_block; block; block = block->next) {
            for (X86Inst* inst = block->first_inst; inst; inst = inst->next) {
                for (int i = 0; i < inst->num_ops; i++) {
                    if (inst->ops[i].kind == X86_OP_STRING) {
                        bool found = false;
                        for (int j = 0; j < string_count; j++) {
                            if (string_lens[j] == inst->ops[i].as.string.len && memcmp(strings[j], inst->ops[i].as.string.str, inst->ops[i].as.string.len) == 0) { found = true; break; }
                        }
                        if (!found) {
                            if (string_count >= string_capacity) {
                                string_capacity *= 2;
                                const char** new_str = (const char**)realloc(strings, string_capacity * sizeof(const char*));
                                if (!new_str) exit(1);
                                strings = new_str;
                                uint32_t* new_slen = (uint32_t*)realloc(string_lens, string_capacity * sizeof(uint32_t));
                                if (!new_slen) exit(1);
                                string_lens = new_slen;
                                uint32_t* new_soff = (uint32_t*)realloc(string_offsets, string_capacity * sizeof(uint32_t));
                                if (!new_soff) exit(1);
                                string_offsets = new_soff;
                            }
                            strings[string_count] = inst->ops[i].as.string.str;
                            string_lens[string_count] = inst->ops[i].as.string.len;
                            string_offsets[string_count] = (uint32_t)linker->rdata_section.size;
                            const char* str = inst->ops[i].as.string.str;
                            uint32_t len = inst->ops[i].as.string.len;
                            for (size_t k = 0; k < len; k++) buf_append(&linker->rdata_section, (uint8_t)str[k]);
                            string_count++;
                        }
                    }
                }
            }
        }
    }

    if (g_use_print_bool) {
        g_verum_rdata_off = (uint32_t)linker->rdata_section.size; const char* str_verum = "verum";
        for (size_t k = 0; k < strlen(str_verum); k++) buf_append(&linker->rdata_section, (uint8_t)str_verum[k]);
        g_falsum_rdata_off = (uint32_t)linker->rdata_section.size; const char* str_falsum = "falsum";
        for (size_t k = 0; k < strlen(str_falsum); k++) buf_append(&linker->rdata_section, (uint8_t)str_falsum[k]);
    }

    if (g_use_print_float) {
        g_dot_rdata_off = (uint32_t)linker->rdata_section.size; buf_append(&linker->rdata_section, '.');
        g_minus_rdata_off = (uint32_t)linker->rdata_section.size; buf_append(&linker->rdata_section, '-');
        while (linker->rdata_section.size % 8 != 0) buf_append(&linker->rdata_section, 0);
        g_float_10_rdata_off = (uint32_t)linker->rdata_section.size; uint64_t float_10_bits = 4621819117588971520ULL;
        for (int i = 0; i < 8; i++) buf_append(&linker->rdata_section, (uint8_t)(float_10_bits >> (i * 8)));
    }

    for (int pass = 0; pass < 2; pass++) {
        linker->text_section.size = 0;
        g_print_str_reloc_count = 0; g_print_int_reloc_count = 0; g_print_uint_reloc_count = 0; g_print_hex_reloc_count = 0;
        g_print_float_reloc_count = 0; g_print_bool_reloc_count = 0; g_str_reloc_count = 0; g_crea_reloc_count = 0;
        g_neca_reloc_count = 0; g_lege_int_reloc_count = 0; g_lege_float_reloc_count = 0; g_lege_char_reloc_count = 0;
        g_lege_bool_reloc_count = 0; g_data_reloc_count = 0; g_func_reloc_count = 0; g_extern_reloc_count = 0;

        LinkCtx ctx;
        ctx.pass = pass; ctx.strings = strings; ctx.string_lens = string_lens; ctx.string_offsets = string_offsets; ctx.string_count = string_count;
        ctx.globals = global_names; ctx.global_offsets = global_offsets; ctx.global_count = global_count;
        ctx.funcs = func_names; ctx.func_offsets = func_offsets; ctx.func_count = func_count;
        ctx.first_extern = module->first_extern; ctx.max_block_id = max_block_id; ctx.block_offsets = block_offsets;

        int current_func_idx = 0;
        for (X86Function* func = mir->first_func; func; func = func->next) {
            while (linker->text_section.size % 16 != 0) emit8(&linker->text_section, 0x90);
            func_offsets[current_func_idx++] = (uint32_t)linker->text_section.size;

            if (strcmp(func->name, "princeps") == 0) g_princeps_offset = (uint32_t)linker->text_section.size;
            else if (strcmp(func->name, "__scoria_init") == 0) g_init_offset = (uint32_t)linker->text_section.size;

            for (X86Block* block = func->first_block; block; block = block->next) {
                if (block != func->first_block) { while (linker->text_section.size % 16 != 0) emit8(&linker->text_section, 0x90); }
                if (block->id <= max_block_id) block_offsets[block->id] = (uint32_t)linker->text_section.size;
                for (X86Inst* inst = block->first_inst; inst; inst = inst->next) emit_x86_inst(&linker->text_section, inst, &ctx);
            }
        }
        pe_builtins_generate(linker, g_princeps_offset, g_init_offset);
    }

    x86_mir_free(mir); free(block_offsets); free(func_names); free(func_offsets);
    free(strings); free(string_lens); free(string_offsets); free(global_names); free(global_offsets);
}

bool pe_linker_generate_executable(PeLinker* linker, SirModule* module, const char* output_filename, int opt_level) {
    generate_machine_code(linker, module, opt_level);
    FILE* out = fopen(output_filename, "wb");
    if (!out) return false;

    uint32_t file_align = 0x200, sec_align = 0x1000;
    DosHeader dos = {0}; dos.e_magic = 0x5A4D; dos.e_lfanew = (uint32_t)sizeof(DosHeader);
    int num_sections = linker->data_section.size > 0 ? 3 : 2;

    CoffHeader coff = {0}; coff.Signature = 0x00004550; coff.Machine = 0x8664; coff.NumberOfSections = (uint16_t)num_sections;
    coff.SizeOfOptionalHeader = (uint16_t)sizeof(OptionalHeader64); coff.Characteristics = 0x0022;

    OptionalHeader64 opt = {0}; opt.Magic = 0x020B; opt.AddressOfEntryPoint = sec_align + linker->entry_point_offset;
    opt.BaseOfCode = sec_align; opt.ImageBase = 0x140000000ULL; opt.SectionAlignment = sec_align; opt.FileAlignment = file_align;
    opt.MajorOperatingSystemVersion = 5; opt.MinorOperatingSystemVersion = 2; opt.MajorSubsystemVersion = 5; opt.MinorSubsystemVersion = 2;
    opt.SizeOfHeaders = align_up((uint32_t)(sizeof(DosHeader) + sizeof(CoffHeader) + sizeof(OptionalHeader64) + sizeof(SectionHeader)), file_align);
    opt.SizeOfImage = align_up(opt.SizeOfHeaders, sec_align) + align_up((uint32_t)linker->text_section.size, sec_align);
    opt.Subsystem = 3; opt.SizeOfStackReserve = 0x100000; opt.SizeOfStackCommit = 0x1000; opt.SizeOfHeapReserve = 0x100000; opt.SizeOfHeapCommit = 0x1000;
    opt.NumberOfRvaAndSizes = 16;

    SectionHeader text_sec = {0}; memcpy(text_sec.Name, ".text", 5); text_sec.VirtualSize = (uint32_t)linker->text_section.size;
    text_sec.VirtualAddress = sec_align; text_sec.SizeOfRawData = align_up((uint32_t)linker->text_section.size, file_align);
    text_sec.PointerToRawData = opt.SizeOfHeaders; text_sec.Characteristics = 0x60000020;

    SectionHeader rdata_sec = {0}; memcpy(rdata_sec.Name, ".rdata", 6); rdata_sec.VirtualSize = (uint32_t)linker->rdata_section.size;
    rdata_sec.VirtualAddress = align_up(text_sec.VirtualAddress + text_sec.VirtualSize, sec_align);
    rdata_sec.SizeOfRawData = align_up((uint32_t)linker->rdata_section.size, file_align);
    rdata_sec.PointerToRawData = text_sec.PointerToRawData + text_sec.SizeOfRawData; rdata_sec.Characteristics = 0x40000040;

    uint32_t rdata_rva = align_up(text_sec.VirtualAddress + text_sec.VirtualSize, sec_align);
    PeImportTable* idata = pe_idata_create();
    if (g_use_print_str || g_use_print_int || g_use_print_uint) { pe_idata_add_import(idata, "kernel32.dll", "GetStdHandle"); pe_idata_add_import(idata, "kernel32.dll", "WriteFile"); }
    pe_idata_add_import(idata, "kernel32.dll", "ExitProcess");
    if (g_use_crea || g_use_neca) { pe_idata_add_import(idata, "kernel32.dll", "GetProcessHeap"); }
    if (g_use_lege) { pe_idata_add_import(idata, "kernel32.dll", "GetStdHandle"); pe_idata_add_import(idata, "kernel32.dll", "ReadFile"); }
    if (g_use_crea) pe_idata_add_import(idata, "kernel32.dll", "HeapAlloc");
    if (g_use_neca) pe_idata_add_import(idata, "kernel32.dll", "HeapFree");
    for (SirExternFunc* ext = module->first_extern; ext; ext = ext->next) pe_idata_add_import(idata, ext->dll_name, ext->name);
    
    uint32_t import_dir_offset = 0, import_dir_size = 0, iat_rva = 0, iat_size = 0;
    pe_idata_build(idata, &linker->rdata_section, rdata_rva, &import_dir_offset, &import_dir_size, &iat_rva, &iat_size);
    opt.DataDirectory[1].VirtualAddress = rdata_rva + import_dir_offset; opt.DataDirectory[1].Size = import_dir_size;
    opt.DataDirectory[12].VirtualAddress = iat_rva; opt.DataDirectory[12].Size = iat_size;

    rdata_sec.VirtualSize = (uint32_t)linker->rdata_section.size; rdata_sec.SizeOfRawData = align_up((uint32_t)linker->rdata_section.size, file_align);
    
    SectionHeader data_sec = {0};
    if (num_sections == 3) {
        memcpy(data_sec.Name, ".data", 5); data_sec.VirtualSize = (uint32_t)linker->data_section.size;
        data_sec.VirtualAddress = align_up(rdata_sec.VirtualAddress + rdata_sec.VirtualSize, sec_align);
        data_sec.SizeOfRawData = align_up((uint32_t)linker->data_section.size, file_align);
        data_sec.PointerToRawData = rdata_sec.PointerToRawData + rdata_sec.SizeOfRawData; data_sec.Characteristics = 0xC0000040;
    }

    opt.SizeOfCode = text_sec.SizeOfRawData; opt.SizeOfInitializedData = rdata_sec.SizeOfRawData + (num_sections == 3 ? data_sec.SizeOfRawData : 0);
    opt.SizeOfImage = align_up(num_sections == 3 ? data_sec.VirtualAddress + data_sec.VirtualSize : rdata_sec.VirtualAddress + rdata_sec.VirtualSize, sec_align);
    opt.SizeOfHeaders = align_up((uint32_t)(sizeof(DosHeader) + sizeof(CoffHeader) + sizeof(OptionalHeader64) + sizeof(SectionHeader) * num_sections), file_align);

    text_sec.PointerToRawData = opt.SizeOfHeaders; rdata_sec.PointerToRawData = text_sec.PointerToRawData + text_sec.SizeOfRawData;
    if (num_sections == 3) data_sec.PointerToRawData = rdata_sec.PointerToRawData + rdata_sec.SizeOfRawData;

    fwrite(&dos, 1, sizeof(dos), out); fwrite(&coff, 1, sizeof(coff), out); fwrite(&opt, 1, sizeof(opt), out);
    fwrite(&text_sec, 1, sizeof(text_sec), out); fwrite(&rdata_sec, 1, sizeof(rdata_sec), out);
    if (num_sections == 3) fwrite(&data_sec, 1, sizeof(data_sec), out);

    uint8_t zero = 0; while ((uint32_t)ftell(out) < text_sec.PointerToRawData) fwrite(&zero, 1, 1, out);

    for (int i = 0; i < g_data_reloc_count; i++) {
        uint32_t text_off = g_data_relocs[i]; uint32_t target_rva = data_sec.VirtualAddress + g_data_offs[i];
        int32_t rel32 = (int32_t)(target_rva - (text_sec.VirtualAddress + text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4);
    }
    for (int i = 0; i < g_func_reloc_count; i++) {
        uint32_t text_off = g_func_relocs[i]; uint32_t target_rva = text_sec.VirtualAddress + g_func_offs[i];
        int32_t rel32 = (int32_t)(target_rva - (text_sec.VirtualAddress + text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4);
    }
    for (int i = 0; i < g_str_reloc_count; i++) {
        uint32_t text_off = g_str_relocs[i]; uint32_t target_rva = rdata_sec.VirtualAddress + g_str_rdata_offs[i];
        int32_t rel32 = (int32_t)(target_rva - (text_sec.VirtualAddress + text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4);
    }

    if (g_use_print_str) { for (int i = 0; i < g_print_str_reloc_count; i++) { uint32_t text_off = g_print_str_relocs[i]; int32_t rel32 = (int32_t)(g_print_str_offset - (text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4); } }
    if (g_use_print_int) { for (int i = 0; i < g_print_int_reloc_count; i++) { uint32_t text_off = g_print_int_relocs[i]; int32_t rel32 = (int32_t)(g_print_int_offset - (text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4); } }
    if (g_use_print_uint) { for (int i = 0; i < g_print_uint_reloc_count; i++) { uint32_t text_off = g_print_uint_relocs[i]; int32_t rel32 = (int32_t)(g_print_uint_offset - (text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4); } }
    if (g_use_print_float) {
        for (int i = 0; i < g_print_float_reloc_count; i++) { uint32_t text_off = g_print_float_relocs[i]; int32_t rel32 = (int32_t)(g_print_float_offset - (text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4); }
        int32_t rel_float_minus = (int32_t)((rdata_sec.VirtualAddress + g_minus_rdata_off) - (text_sec.VirtualAddress + g_print_float_offset + 23 + 4)); memcpy(linker->text_section.buffer + g_print_float_offset + 23, &rel_float_minus, 4);
        int32_t rel_float_print_str1 = (int32_t)(g_print_str_offset - (g_print_float_offset + 35 + 4)); memcpy(linker->text_section.buffer + g_print_float_offset + 35, &rel_float_print_str1, 4);
        int32_t rel_float_print_int1 = (int32_t)(g_print_int_offset - (g_print_float_offset + 67 + 4)); memcpy(linker->text_section.buffer + g_print_float_offset + 67, &rel_float_print_int1, 4);
        int32_t rel_float_dot = (int32_t)((rdata_sec.VirtualAddress + g_dot_rdata_off) - (text_sec.VirtualAddress + g_print_float_offset + 74 + 4)); memcpy(linker->text_section.buffer + g_print_float_offset + 74, &rel_float_dot, 4);
        int32_t rel_float_print_str2 = (int32_t)(g_print_str_offset - (g_print_float_offset + 86 + 4)); memcpy(linker->text_section.buffer + g_print_float_offset + 86, &rel_float_print_str2, 4);
        int32_t rel_float_10 = (int32_t)((rdata_sec.VirtualAddress + g_float_10_rdata_off) - (text_sec.VirtualAddress + g_print_float_offset + 115 + 4)); memcpy(linker->text_section.buffer + g_print_float_offset + 115, &rel_float_10, 4);
        int32_t rel_float_print_int2 = (int32_t)(g_print_int_offset - (g_print_float_offset + 138 + 4)); memcpy(linker->text_section.buffer + g_print_float_offset + 138, &rel_float_print_int2, 4);
    }
    if (g_use_print_hex) {
        for (int i = 0; i < g_print_hex_reloc_count; i++) { uint32_t text_off = g_print_hex_relocs[i]; int32_t rel32 = (int32_t)(g_print_hex_offset - (text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4); }
        int32_t rel_hex_print_str = (int32_t)(g_print_str_offset - (g_print_hex_offset + 78 + 4)); memcpy(linker->text_section.buffer + g_print_hex_offset + 78, &rel_hex_print_str, 4);
    }
    if (g_use_print_bool) {
        for (int i = 0; i < g_print_bool_reloc_count; i++) { uint32_t text_off = g_print_bool_relocs[i]; int32_t rel32 = (int32_t)(g_print_bool_offset - (text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4); }
        int32_t rel_bool_print_str = (int32_t)(g_print_str_offset - (g_print_bool_offset + 32 + 4)); memcpy(linker->text_section.buffer + g_print_bool_offset + 32, &rel_bool_print_str, 4);
        int32_t rel_verum = (int32_t)((rdata_sec.VirtualAddress + g_verum_rdata_off) - (text_sec.VirtualAddress + g_print_bool_offset + 8 + 4)); memcpy(linker->text_section.buffer + g_print_bool_offset + 8, &rel_verum, 4);
        int32_t rel_falsum = (int32_t)((rdata_sec.VirtualAddress + g_falsum_rdata_off) - (text_sec.VirtualAddress + g_print_bool_offset + 22 + 4)); memcpy(linker->text_section.buffer + g_print_bool_offset + 22, &rel_falsum, 4);
    }
    if (g_use_lege) {
        for (int i = 0; i < g_lege_int_reloc_count; i++) { uint32_t text_off = g_lege_int_relocs[i]; int32_t rel32 = (int32_t)(g_lege_int_offset - (text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4); }
        for (int i = 0; i < g_lege_float_reloc_count; i++) { uint32_t text_off = g_lege_float_relocs[i]; int32_t rel32 = (int32_t)(g_lege_float_offset - (text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4); }
        for (int i = 0; i < g_lege_char_reloc_count; i++) { uint32_t text_off = g_lege_char_relocs[i]; int32_t rel32 = (int32_t)(g_lege_char_offset - (text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4); }
        for (int i = 0; i < g_lege_bool_reloc_count; i++) { uint32_t text_off = g_lege_bool_relocs[i]; int32_t rel32 = (int32_t)(g_lege_bool_offset - (text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4); }
    }
    if (g_use_crea) { for (int i = 0; i < g_crea_reloc_count; i++) { uint32_t text_off = g_crea_relocs[i]; int32_t rel32 = (int32_t)(g_crea_offset - (text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4); } }
    if (g_use_neca) { for (int i = 0; i < g_neca_reloc_count; i++) { uint32_t text_off = g_neca_relocs[i]; int32_t rel32 = (int32_t)(g_neca_offset - (text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4); } }

    #define RELOC_IAT(reloc_var, dll, func) do { uint32_t iat_off = pe_idata_get_iat_offset(idata, dll, func); int32_t rel32 = (int32_t)((rdata_sec.VirtualAddress + iat_off) - (text_sec.VirtualAddress + reloc_var + 4)); memcpy(linker->text_section.buffer + reloc_var, &rel32, 4); } while(0)
    if (g_use_print_str) { RELOC_IAT(g_call_getstdhandle_reloc, "kernel32.dll", "GetStdHandle"); RELOC_IAT(g_call_writeconsolea_reloc, "kernel32.dll", "WriteFile"); }
    if (g_use_print_int) { RELOC_IAT(g_call_getstdhandle_reloc2, "kernel32.dll", "GetStdHandle"); RELOC_IAT(g_call_writeconsolea_reloc2, "kernel32.dll", "WriteFile"); }
    if (g_use_print_uint) { RELOC_IAT(g_call_getstdhandle_reloc3, "kernel32.dll", "GetStdHandle"); RELOC_IAT(g_call_writeconsolea_reloc3, "kernel32.dll", "WriteFile"); }
    RELOC_IAT(g_call_exitprocess_reloc, "kernel32.dll", "ExitProcess");
    if (g_use_crea) { RELOC_IAT(g_call_getprocessheap_reloc1, "kernel32.dll", "GetProcessHeap"); RELOC_IAT(g_call_heapalloc_reloc, "kernel32.dll", "HeapAlloc"); }
    if (g_use_neca) { RELOC_IAT(g_call_getprocessheap_reloc2, "kernel32.dll", "GetProcessHeap"); RELOC_IAT(g_call_heapfree_reloc, "kernel32.dll", "HeapFree"); }
    if (g_use_lege) { RELOC_IAT(g_call_getstdhandle_reloc_read, "kernel32.dll", "GetStdHandle"); RELOC_IAT(g_call_readfile_reloc, "kernel32.dll", "ReadFile"); }

    for (int i = 0; i < g_extern_reloc_count; i++) {
        uint32_t text_off = g_extern_relocs[i]; int target_idx = g_extern_idxs[i]; SirExternFunc* ext = module->first_extern;
        for (int j = 0; j < target_idx && ext; j++) ext = ext->next;
        if (ext) { uint32_t iat_off = pe_idata_get_iat_offset(idata, ext->dll_name, ext->name); int32_t rel32 = (int32_t)((rdata_sec.VirtualAddress + iat_off) - (text_sec.VirtualAddress + text_off + 4)); memcpy(linker->text_section.buffer + text_off, &rel32, 4); }
    }
    pe_idata_free(idata);

    fwrite(linker->text_section.buffer, 1, linker->text_section.size, out);
    while ((uint32_t)ftell(out) < rdata_sec.PointerToRawData) fwrite(&zero, 1, 1, out);
    fwrite(linker->rdata_section.buffer, 1, linker->rdata_section.size, out);
    if (num_sections == 3) {
        while ((uint32_t)ftell(out) < data_sec.PointerToRawData) fwrite(&zero, 1, 1, out);
        fwrite(linker->data_section.buffer, 1, linker->data_section.size, out);
        while ((uint32_t)ftell(out) < data_sec.PointerToRawData + data_sec.SizeOfRawData) fwrite(&zero, 1, 1, out);
    } else {
        while ((uint32_t)ftell(out) < rdata_sec.PointerToRawData + rdata_sec.SizeOfRawData) fwrite(&zero, 1, 1, out);
    }
    fclose(out); return true;
}
