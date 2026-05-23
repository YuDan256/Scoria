#include "ir_builder.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// =========================================================
// 初始化与销毁
// =========================================================
void ir_builder_init(IrBuilder* builder, const char* module_name) {
    // 为 IR 节点分配 1MB 的初始内存池
    arena_init(&builder->arena, 1024 * 1024); 
    
    builder->module = (SirModule*)arena_alloc(&builder->arena, sizeof(SirModule));
    builder->module->name = module_name;
    builder->module->first_func = NULL;
    builder->module->last_func = NULL;
    
    builder->current_func = NULL;
    builder->current_block = NULL;
    builder->current_loop_cond = NULL;
    builder->current_loop_exit = NULL;
    builder->next_vreg = 1; // 虚拟寄存器从 %1 开始
    builder->next_block_id = 1;
}

void ir_builder_free(IrBuilder* builder) {
    arena_free(&builder->arena);
}

// =========================================================
// 结构构建 API
// =========================================================
SirFunction* ir_builder_create_function(IrBuilder* builder, const char* name, ScoriaType* func_type) {
    SirFunction* func = (SirFunction*)arena_alloc(&builder->arena, sizeof(SirFunction));
    func->name = name;
    func->type = func_type;
    func->first_block = NULL;
    func->last_block = NULL;
    func->next = NULL;
    
    if (!builder->module->first_func) {
        builder->module->first_func = func;
    } else {
        builder->module->last_func->next = func;
    }
    builder->module->last_func = func;
    
    builder->current_func = func;
    builder->next_vreg = 1; // 每个函数重置虚拟寄存器计数
    
    return func;
}

SirBlock* ir_builder_create_block(IrBuilder* builder, const char* name) {
    SirBlock* block = (SirBlock*)arena_alloc(&builder->arena, sizeof(SirBlock));
    block->id = builder->next_block_id++;
    block->name = name;
    block->first_inst = NULL;
    block->last_inst = NULL;
    block->next = NULL;
    
    if (builder->current_func) {
        if (!builder->current_func->first_block) {
            builder->current_func->first_block = block;
        } else {
            builder->current_func->last_block->next = block;
        }
        builder->current_func->last_block = block;
    }
    
    return block;
}

void ir_builder_set_insert_point(IrBuilder* builder, SirBlock* block) {
    builder->current_block = block;
}

// =========================================================
// 值创建 API
// =========================================================
static SirValue* create_value(IrBuilder* builder, SirValueKind kind, ScoriaType* type) {
    SirValue* val = (SirValue*)arena_alloc(&builder->arena, sizeof(SirValue));
    val->kind = kind;
    val->type = type;
    return val;
}

static SirValue* create_vreg(IrBuilder* builder, ScoriaType* type) {
    SirValue* val = create_value(builder, SIR_VAL_VREG, type);
    val->as.vreg = builder->next_vreg++;
    return val;
}

SirValue* ir_const_int(IrBuilder* builder, ScoriaType* type, int64_t val) {
    SirValue* v = create_value(builder, SIR_VAL_CONST_INT, type);
    v->as.int_val = val;
    return v;
}

SirValue* ir_const_float(IrBuilder* builder, ScoriaType* type, double val) {
    SirValue* v = create_value(builder, SIR_VAL_CONST_FLOAT, type);
    v->as.float_val = val;
    return v;
}

SirValue* ir_const_bool(IrBuilder* builder, bool val) {
    SirValue* v = create_value(builder, SIR_VAL_CONST_BOOL, type_get_basic(TY_LOGICA));
    v->as.bool_val = val;
    return v;
}

SirValue* ir_const_string(IrBuilder* builder, const char* val) {
    SirValue* v = create_value(builder, SIR_VAL_CONST_STRING, type_get_basic(TY_TEXTUS));
    v->as.string_val = val;
    return v;
}

// =========================================================
// 指令构建 API
// =========================================================
static SirInst* create_inst(IrBuilder* builder, SirOpcode opcode, int num_operands) {
    SirInst* inst = (SirInst*)arena_alloc(&builder->arena, sizeof(SirInst));
    inst->opcode = opcode;
    inst->dest = NULL;
    inst->num_operands = num_operands;
    if (num_operands > 0) {
        inst->operands = (SirValue**)arena_alloc(&builder->arena, sizeof(SirValue*) * num_operands);
    } else {
        inst->operands = NULL;
    }
    inst->prev = NULL;
    inst->next = NULL;
    
    // 自动插入到当前基本块的尾部
    if (builder->current_block) {
        if (!builder->current_block->first_inst) {
            builder->current_block->first_inst = inst;
        } else {
            builder->current_block->last_inst->next = inst;
            inst->prev = builder->current_block->last_inst;
        }
        builder->current_block->last_inst = inst;
    }
    
    return inst;
}

SirValue* ir_build_binary(IrBuilder* builder, SirOpcode op, SirValue* left, SirValue* right) {
    SirInst* inst = create_inst(builder, op, 2);
    inst->operands[0] = left;
    inst->operands[1] = right;
    
    // 简单推导结果类型：如果是比较指令，返回 logica；否则返回左操作数类型
    ScoriaType* res_type = left->type;
    if (op >= SIR_ICMP_EQ && op <= SIR_FCMP_GE) {
        res_type = type_get_basic(TY_LOGICA);
    }
    
    inst->dest = create_vreg(builder, res_type);
    return inst->dest;
}

SirValue* ir_build_unary(IrBuilder* builder, SirOpcode op, SirValue* operand) {
    SirInst* inst = create_inst(builder, op, 1);
    inst->operands[0] = operand;
    inst->dest = create_vreg(builder, operand->type);
    return inst->dest;
}

SirValue* ir_build_alloca(IrBuilder* builder, ScoriaType* type, int size) {
    SirInst* inst = create_inst(builder, SIR_ALLOCA, 1);
    inst->operands[0] = ir_const_int(builder, type_get_basic(TY_I32), size);
    inst->dest = create_vreg(builder, type_get_via(type)); // alloca 总是返回 via T
    return inst->dest;
}

SirValue* ir_build_load(IrBuilder* builder, SirValue* ptr) {
    SirInst* inst = create_inst(builder, SIR_LOAD, 1);
    inst->operands[0] = ptr;
    
    // ptr 必须是 via T，load 返回 T
    ScoriaType* res_type = type_get_basic(TY_UNKNOWN);
    if (ptr->type && ptr->type->kind == TY_VIA) {
        res_type = ptr->type->as.inner;
    }
    
    inst->dest = create_vreg(builder, res_type);
    return inst->dest;
}

void ir_build_store(IrBuilder* builder, SirValue* val, SirValue* ptr) {
    SirInst* inst = create_inst(builder, SIR_STORE, 2);
    inst->operands[0] = val;
    inst->operands[1] = ptr;
    // store 没有返回值 (dest 为 NULL)
}

SirValue* ir_build_gep(IrBuilder* builder, SirValue* ptr, SirValue* index, int element_size, ScoriaType* res_type) {
    SirInst* inst = create_inst(builder, SIR_GEP, 3);
    inst->operands[0] = ptr;
    inst->operands[1] = index;
    inst->operands[2] = ir_const_int(builder, type_get_basic(TY_I32), element_size);
    inst->dest = create_vreg(builder, res_type);
    return inst->dest;
}

SirValue* ir_build_cast(IrBuilder* builder, SirValue* val, ScoriaType* target_type) {
    SirInst* inst = create_inst(builder, SIR_CAST, 1);
    inst->operands[0] = val;
    inst->dest = create_vreg(builder, target_type);
    return inst->dest;
}

SirValue* ir_build_call(IrBuilder* builder, SirValue* callee, SirValue** args, int arg_count, ScoriaType* ret_type) {
    SirInst* inst = create_inst(builder, SIR_CALL, arg_count + 1);
    inst->operands[0] = callee;
    for (int i = 0; i < arg_count; i++) {
        inst->operands[i + 1] = args[i];
    }
    if (ret_type && ret_type->kind != TY_NIHIL) {
        inst->dest = create_vreg(builder, ret_type);
    }
    return inst->dest;
}

void ir_build_jmp(IrBuilder* builder, SirBlock* target) {
    SirInst* inst = create_inst(builder, SIR_JMP, 1);
    SirValue* target_val = create_value(builder, SIR_VAL_BLOCK, type_get_basic(TY_UNKNOWN));
    target_val->as.block = target;
    inst->operands[0] = target_val;
}

void ir_build_br(IrBuilder* builder, SirValue* cond, SirBlock* true_block, SirBlock* false_block) {
    SirInst* inst = create_inst(builder, SIR_BR, 3);
    inst->operands[0] = cond;
    
    SirValue* t_val = create_value(builder, SIR_VAL_BLOCK, type_get_basic(TY_UNKNOWN));
    t_val->as.block = true_block;
    inst->operands[1] = t_val;
    
    SirValue* f_val = create_value(builder, SIR_VAL_BLOCK, type_get_basic(TY_UNKNOWN));
    f_val->as.block = false_block;
    inst->operands[2] = f_val;
}

void ir_build_ret(IrBuilder* builder, SirValue* val) {
    if (val) {
        SirInst* inst = create_inst(builder, SIR_RET, 1);
        inst->operands[0] = val;
    } else {
        create_inst(builder, SIR_RET, 0);
    }
}

SirValue* ir_get_param(IrBuilder* builder, int index, ScoriaType* type) {
    SirInst* inst = create_inst(builder, SIR_GET_PARAM, 1);
    inst->operands[0] = ir_const_int(builder, type_get_basic(TY_I32), index);
    inst->dest = create_vreg(builder, type);
    return inst->dest;
}
