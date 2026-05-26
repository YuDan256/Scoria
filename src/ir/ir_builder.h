#ifndef SCORIA_IR_BUILDER_H
#define SCORIA_IR_BUILDER_H

#include "sir.h"
#include "../utils/memory_arena.h"

typedef struct AstNode AstNode;

// =========================================================
// IR 构建器 (IR Builder)
// =========================================================
typedef struct {
    Arena arena;                  // 内存池，用于分配所有 SIR 节点
    SirModule* module;            // 当前正在构建的模块
    SirFunction* current_func;    // 当前正在构建的函数
    SirBlock* current_block;      // 当前插入指令的基本块
    
    SirBlock* current_loop_cond;  // 当前循环的条件/步进块 (用于 perge)
    SirBlock* current_loop_exit;  // 当前循环的退出块 (用于 rumpe)
    
    SirValue* current_hidden_ret_ptr; // 隐藏返回指针 (用于返回 >8 字节的结构体/切片)
    AstNode* current_func_body;   // 当前函数的 AST 节点 (用于 Mem2Reg 逃逸分析)
    
    uint32_t next_vreg;           // 虚拟寄存器分配器计数器
    uint32_t next_block_id;       // 基本块 ID 分配器计数器
} IrBuilder;

void ir_builder_init(IrBuilder* builder, const char* module_name);
void ir_builder_free(IrBuilder* builder);

// ---------------------------------------------------------
// 结构构建 API
// ---------------------------------------------------------
SirGlobalVar* ir_builder_create_global(IrBuilder* builder, const char* name_start, int name_len, ScoriaType* type, int size);
void ir_builder_add_extern(IrBuilder* builder, const char* name_start, int name_len, const char* dll_start, int dll_len);
SirFunction* ir_builder_create_function(IrBuilder* builder, const char* name, ScoriaType* func_type);
SirBlock* ir_builder_create_block(IrBuilder* builder, const char* name);
SirBlock* ir_builder_get_or_create_label_block(IrBuilder* builder, const char* name_start, int name_len);
void ir_builder_set_insert_point(IrBuilder* builder, SirBlock* block);

// ---------------------------------------------------------
// 值创建 API
// ---------------------------------------------------------
SirValue* ir_const_int(IrBuilder* builder, ScoriaType* type, int64_t val);
SirValue* ir_const_float(IrBuilder* builder, ScoriaType* type, double val);
SirValue* ir_const_bool(IrBuilder* builder, bool val);
SirValue* ir_const_string(IrBuilder* builder, const char* val, uint32_t len);

// ---------------------------------------------------------
// 指令构建 API
// ---------------------------------------------------------
SirValue* ir_build_binary(IrBuilder* builder, SirOpcode op, SirValue* left, SirValue* right);
SirValue* ir_build_unary(IrBuilder* builder, SirOpcode op, SirValue* operand);
SirValue* ir_build_alloca(IrBuilder* builder, ScoriaType* type, int size);
SirValue* ir_build_load(IrBuilder* builder, SirValue* ptr);
void ir_build_store(IrBuilder* builder, SirValue* val, SirValue* ptr);
SirValue* ir_build_gep(IrBuilder* builder, SirValue* ptr, SirValue* index, int element_size, ScoriaType* res_type);
void ir_build_memcpy(IrBuilder* builder, SirValue* dest_ptr, SirValue* src_ptr, int size);
SirValue* ir_build_cast(IrBuilder* builder, SirValue* val, ScoriaType* target_type);
SirValue* ir_build_call(IrBuilder* builder, SirValue* callee, SirValue** args, int arg_count, ScoriaType* ret_type);
void ir_build_jmp(IrBuilder* builder, SirBlock* target);
void ir_build_br(IrBuilder* builder, SirValue* cond, SirBlock* true_block, SirBlock* false_block);
void ir_build_switch(IrBuilder* builder, SirValue* cond, SirBlock* default_block, SirValue** case_vals, SirBlock** case_blocks, int case_count);
void ir_build_ret(IrBuilder* builder, SirValue* val);
SirValue* ir_build_select(IrBuilder* builder, SirValue* cond, SirValue* true_val, SirValue* false_val);

// 获取函数的第 N 个参数 (作为虚拟寄存器)
SirValue* ir_get_param(IrBuilder* builder, int index, ScoriaType* type);

// ---------------------------------------------------------
// IR 优化 API
// ---------------------------------------------------------
void ir_optimize_module(IrBuilder* builder, int opt_level);

#endif // SCORIA_IR_BUILDER_H
