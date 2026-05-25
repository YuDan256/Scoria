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
    builder->module->first_global = NULL;
    builder->module->last_global = NULL;
    builder->module->first_extern = NULL;
    builder->module->last_extern = NULL;
    
    builder->current_func = NULL;
    builder->current_block = NULL;
    builder->current_loop_cond = NULL;
    builder->current_loop_exit = NULL;
    builder->current_hidden_ret_ptr = NULL;
    builder->next_vreg = 1; // 虚拟寄存器从 %1 开始
    builder->next_block_id = 1;
}

void ir_builder_free(IrBuilder* builder) {
    arena_free(&builder->arena);
}

// =========================================================
// 结构构建 API
// =========================================================
SirGlobalVar* ir_builder_create_global(IrBuilder* builder, const char* name_start, int name_len, ScoriaType* type, int size) {
    SirGlobalVar* gvar = (SirGlobalVar*)arena_alloc(&builder->arena, sizeof(SirGlobalVar));
    char* name = (char*)arena_alloc(&builder->arena, name_len + 1);
    strncpy(name, name_start, name_len);
    name[name_len] = '\0';
    gvar->name = name;
    gvar->type = type;
    gvar->size = size;
    gvar->next = NULL;
    
    if (!builder->module->first_global) {
        builder->module->first_global = gvar;
    } else {
        builder->module->last_global->next = gvar;
    }
    builder->module->last_global = gvar;
    return gvar;
}

void ir_builder_add_extern(IrBuilder* builder, const char* name_start, int name_len, const char* dll_start, int dll_len) {
    SirExternFunc* ext = (SirExternFunc*)arena_alloc(&builder->arena, sizeof(SirExternFunc));
    char* name = (char*)arena_alloc(&builder->arena, name_len + 1);
    strncpy(name, name_start, name_len);
    name[name_len] = '\0';
    ext->name = name;
    
    if (dll_start && dll_len >= 2) {
        // 去除首尾引号
        int actual_len = dll_len - 2;
        char* dll = (char*)arena_alloc(&builder->arena, actual_len + 1);
        strncpy(dll, dll_start + 1, actual_len);
        dll[actual_len] = '\0';
        ext->dll_name = dll;
    } else {
        ext->dll_name = "msvcrt.dll"; // 默认 fallback
    }
    
    ext->next = NULL;
    
    if (!builder->module->first_extern) {
        builder->module->first_extern = ext;
    } else {
        builder->module->last_extern->next = ext;
    }
    builder->module->last_extern = ext;
}

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

SirBlock* ir_builder_get_or_create_label_block(IrBuilder* builder, const char* name_start, int name_len) {
    char* name = (char*)arena_alloc(&builder->arena, name_len + 5);
    strcpy(name, "lbl_");
    strncat(name, name_start, name_len);
    name[name_len + 4] = '\0';
    
    if (builder->current_func) {
        for (SirBlock* b = builder->current_func->first_block; b; b = b->next) {
            if (strcmp(b->name, name) == 0) {
                return b;
            }
        }
    }
    return ir_builder_create_block(builder, name);
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

SirValue* ir_const_string(IrBuilder* builder, const char* val, uint32_t len) {
    SirValue* v = create_value(builder, SIR_VAL_CONST_STRING, type_get_via(type_get_basic(TY_LITTERA)));
    v->as.string_val.str = val;
    v->as.string_val.len = len;
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
        if (builder->current_block->last_inst) {
            SirOpcode last_op = builder->current_block->last_inst->opcode;
            if (last_op == SIR_RET || last_op == SIR_JMP || last_op == SIR_BR || last_op == SIR_SWITCH) {
                // 这是一个不可达的死指令，直接丢弃
                return inst;
            }
        }
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
    if (!left || !right) return NULL;

    // 常量折叠 (Constant Folding)
    if (left->kind == SIR_VAL_CONST_INT && right->kind == SIR_VAL_CONST_INT) {
        int64_t l = left->as.int_val;
        int64_t r = right->as.int_val;
        int64_t res = 0;
        bool is_cmp = false;
        bool cmp_res = false;
        switch (op) {
            case SIR_ADD: res = l + r; break;
            case SIR_SUB: res = l - r; break;
            case SIR_MUL: res = l * r; break;
            case SIR_DIV: if (r != 0) res = l / r; else goto no_fold; break;
            case SIR_MOD: if (r != 0) res = l % r; else goto no_fold; break;
            case SIR_AND: res = l & r; break;
            case SIR_OR:  res = l | r; break;
            case SIR_XOR: res = l ^ r; break;
            case SIR_SHL: res = l << r; break;
            case SIR_SHR: res = l >> r; break;
            case SIR_ICMP_EQ: is_cmp = true; cmp_res = (l == r); break;
            case SIR_ICMP_NE: is_cmp = true; cmp_res = (l != r); break;
            case SIR_ICMP_LT: is_cmp = true; cmp_res = (l < r); break;
            case SIR_ICMP_LE: is_cmp = true; cmp_res = (l <= r); break;
            case SIR_ICMP_GT: is_cmp = true; cmp_res = (l > r); break;
            case SIR_ICMP_GE: is_cmp = true; cmp_res = (l >= r); break;
            default: goto no_fold;
        }
        if (is_cmp) return ir_const_bool(builder, cmp_res);
        return ir_const_int(builder, left->type, res);
    } else if (left->kind == SIR_VAL_CONST_FLOAT && right->kind == SIR_VAL_CONST_FLOAT) {
        double l = left->as.float_val;
        double r = right->as.float_val;
        double res = 0.0;
        bool is_cmp = false;
        bool cmp_res = false;
        switch (op) {
            case SIR_FADD: res = l + r; break;
            case SIR_FSUB: res = l - r; break;
            case SIR_FMUL: res = l * r; break;
            case SIR_FDIV: if (r != 0.0) res = l / r; else goto no_fold; break;
            case SIR_FCMP_EQ: is_cmp = true; cmp_res = (l == r); break;
            case SIR_FCMP_NE: is_cmp = true; cmp_res = (l != r); break;
            case SIR_FCMP_LT: is_cmp = true; cmp_res = (l < r); break;
            case SIR_FCMP_LE: is_cmp = true; cmp_res = (l <= r); break;
            case SIR_FCMP_GT: is_cmp = true; cmp_res = (l > r); break;
            case SIR_FCMP_GE: is_cmp = true; cmp_res = (l >= r); break;
            default: goto no_fold;
        }
        if (is_cmp) return ir_const_bool(builder, cmp_res);
        return ir_const_float(builder, left->type, res);
    }

no_fold:;
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
    if (!operand) return NULL;
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
    if (!ptr) return NULL;
    
    // 冗余 Load 消除 (Store-to-Load Forwarding)
    if (builder->current_block) {
        SirInst* scan = builder->current_block->last_inst;
        while (scan) {
            if (scan->opcode == SIR_STORE) {
                if (scan->operands[1] == ptr) {
                    return scan->operands[0]; // 直接转发最近一次 Store 的值
                }
                break; // 遇到其他 Store，为防止指针别名 (Aliasing)，停止扫描
            }
            if (scan->opcode == SIR_CALL || scan->opcode == SIR_MEMCPY) break; // Call 和 Memcpy 可能修改内存
            scan = scan->prev;
        }
    }

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
    if (!val || !ptr) return;
    SirInst* inst = create_inst(builder, SIR_STORE, 2);
    inst->operands[0] = val;
    inst->operands[1] = ptr;
    // store 没有返回值 (dest 为 NULL)
}

SirValue* ir_build_gep(IrBuilder* builder, SirValue* ptr, SirValue* index, int element_size, ScoriaType* res_type) {
    if (!ptr || !index) return NULL;
    SirInst* inst = create_inst(builder, SIR_GEP, 3);
    inst->operands[0] = ptr;
    inst->operands[1] = index;
    inst->operands[2] = ir_const_int(builder, type_get_basic(TY_I32), element_size);
    inst->dest = create_vreg(builder, res_type);
    return inst->dest;
}

void ir_build_memcpy(IrBuilder* builder, SirValue* dest_ptr, SirValue* src_ptr, int size) {
    if (!dest_ptr || !src_ptr) return;
    SirInst* inst = create_inst(builder, SIR_MEMCPY, 3);
    inst->operands[0] = dest_ptr;
    inst->operands[1] = src_ptr;
    inst->operands[2] = ir_const_int(builder, type_get_basic(TY_I32), size);
}

SirValue* ir_build_cast(IrBuilder* builder, SirValue* val, ScoriaType* target_type) {
    if (!val) return NULL;
    
    // 常量类型转换折叠
    if (val->kind == SIR_VAL_CONST_INT) {
        if (target_type->kind == TY_F32 || target_type->kind == TY_F64) {
            return ir_const_float(builder, target_type, (double)val->as.int_val);
        }
        return ir_const_int(builder, target_type, val->as.int_val);
    } else if (val->kind == SIR_VAL_CONST_FLOAT) {
        if (target_type->kind != TY_F32 && target_type->kind != TY_F64) {
            return ir_const_int(builder, target_type, (int64_t)val->as.float_val);
        }
        return ir_const_float(builder, target_type, val->as.float_val);
    }
    
    SirInst* inst = create_inst(builder, SIR_CAST, 1);
    inst->operands[0] = val;
    inst->dest = create_vreg(builder, target_type);
    return inst->dest;
}

SirValue* ir_build_call(IrBuilder* builder, SirValue* callee, SirValue** args, int arg_count, ScoriaType* ret_type) {
    if (!callee) return NULL;
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
    if (!cond) return;
    
    // 分支折叠 (Branch Folding)
    if (cond->kind == SIR_VAL_CONST_BOOL) {
        ir_build_jmp(builder, cond->as.bool_val ? true_block : false_block);
        return;
    }
    
    SirInst* inst = create_inst(builder, SIR_BR, 3);
    inst->operands[0] = cond;
    
    SirValue* t_val = create_value(builder, SIR_VAL_BLOCK, type_get_basic(TY_UNKNOWN));
    t_val->as.block = true_block;
    inst->operands[1] = t_val;
    
    SirValue* f_val = create_value(builder, SIR_VAL_BLOCK, type_get_basic(TY_UNKNOWN));
    f_val->as.block = false_block;
    inst->operands[2] = f_val;
}

void ir_build_switch(IrBuilder* builder, SirValue* cond, SirBlock* default_block, SirValue** case_vals, SirBlock** case_blocks, int case_count) {
    SirInst* inst = create_inst(builder, SIR_SWITCH, 2 + case_count * 2);
    inst->operands[0] = cond;
    SirValue* def_val = create_value(builder, SIR_VAL_BLOCK, type_get_basic(TY_UNKNOWN));
    def_val->as.block = default_block;
    inst->operands[1] = def_val;
    for (int i = 0; i < case_count; i++) {
        inst->operands[2 + i * 2] = case_vals[i];
        SirValue* cb_val = create_value(builder, SIR_VAL_BLOCK, type_get_basic(TY_UNKNOWN));
        cb_val->as.block = case_blocks[i];
        inst->operands[2 + i * 2 + 1] = cb_val;
    }
}

void ir_build_ret(IrBuilder* builder, SirValue* val) {
    if (val) {
        SirInst* inst = create_inst(builder, SIR_RET, 1);
        inst->operands[0] = val;
    } else {
        create_inst(builder, SIR_RET, 0);
    }
}

SirValue* ir_build_select(IrBuilder* builder, SirValue* cond, SirValue* true_val, SirValue* false_val) {
    if (!cond || !true_val || !false_val) return NULL;
    if (cond->kind == SIR_VAL_CONST_BOOL) {
        return cond->as.bool_val ? true_val : false_val;
    }
    SirInst* inst = create_inst(builder, SIR_SELECT, 3);
    inst->operands[0] = cond;
    inst->operands[1] = true_val;
    inst->operands[2] = false_val;
    inst->dest = create_vreg(builder, true_val->type);
    return inst->dest;
}

SirValue* ir_get_param(IrBuilder* builder, int index, ScoriaType* type) {
    SirInst* inst = create_inst(builder, SIR_GET_PARAM, 1);
    inst->operands[0] = ir_const_int(builder, type_get_basic(TY_I32), index);
    inst->dest = create_vreg(builder, type);
    return inst->dest;
}

// =========================================================
// IR 优化 (IR Optimization)
// =========================================================

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

static bool is_side_effect_free(SirOpcode opcode) {
    switch (opcode) {
        case SIR_ADD: case SIR_SUB: case SIR_MUL: case SIR_DIV: case SIR_MOD:
        case SIR_FADD: case SIR_FSUB: case SIR_FMUL: case SIR_FDIV:
        case SIR_AND: case SIR_OR: case SIR_XOR: case SIR_SHL: case SIR_SHR:
        case SIR_ICMP_EQ: case SIR_ICMP_NE: case SIR_ICMP_LT: case SIR_ICMP_LE: case SIR_ICMP_GT: case SIR_ICMP_GE:
        case SIR_FCMP_EQ: case SIR_FCMP_NE: case SIR_FCMP_LT: case SIR_FCMP_LE: case SIR_FCMP_GT: case SIR_FCMP_GE:
        case SIR_LOAD: case SIR_GEP: case SIR_CAST: case SIR_GET_PARAM: case SIR_SELECT:
            return true;
        default:
            return false;
    }
}

void ir_optimize_module(IrBuilder* builder, int opt_level) {
    if (!builder || !builder->module) return;
    if (opt_level == 0) return;

    for (SirFunction* func = builder->module->first_func; func; func = func->next) {
        bool changed;
        
        // 1. 死代码消除 (Dead Code Elimination)
        do {
            changed = false;
            uint32_t* use_counts = (uint32_t*)calloc(builder->next_vreg, sizeof(uint32_t));
            
            // 统计使用次数
            for (SirBlock* block = func->first_block; block; block = block->next) {
                for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                    for (int i = 0; i < inst->num_operands; i++) {
                        if (inst->operands[i] && inst->operands[i]->kind == SIR_VAL_VREG) {
                            use_counts[inst->operands[i]->as.vreg]++;
                        }
                    }
                }
            }
            
            // 移除死指令
            for (SirBlock* block = func->first_block; block; block = block->next) {
                SirInst* inst = block->first_inst;
                while (inst) {
                    SirInst* next_inst = inst->next;
                    if (inst->dest && inst->dest->kind == SIR_VAL_VREG && is_side_effect_free(inst->opcode)) {
                        if (use_counts[inst->dest->as.vreg] == 0) {
                            // 从链表中移除
                            if (inst->prev) inst->prev->next = inst->next;
                            else block->first_inst = inst->next;
                            
                            if (inst->next) inst->next->prev = inst->prev;
                            else block->last_inst = inst->prev;
                            
                            changed = true;
                        }
                    }
                    inst = next_inst;
                }
            }
            free(use_counts);
        } while (changed);

        // 2. 序言前置快路径剥离 (Shrink-Wrapping / Fast Path Peephole)
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
                            func->has_fast_path = true;
                            func->fp_imm = (int32_t)i2->operands[1]->as.int_val;
                            func->fp_w = (i1->dest->type && type_get_size(i1->dest->type) <= 4) ? 0 : 1;
                            
                            bool is_unsigned = type_is_unsigned(i1->dest->type);
                            
                            // ASM JCC
                            if (i2->opcode == SIR_ICMP_EQ) func->fp_jcc_asm = cond_is_true ? "jne" : "je";
                            else if (i2->opcode == SIR_ICMP_NE) func->fp_jcc_asm = cond_is_true ? "je" : "jne";
                            else if (i2->opcode == SIR_ICMP_LT) func->fp_jcc_asm = cond_is_true ? (is_unsigned ? "jae" : "jge") : (is_unsigned ? "jb" : "jl");
                            else if (i2->opcode == SIR_ICMP_LE) func->fp_jcc_asm = cond_is_true ? (is_unsigned ? "ja" : "jg") : (is_unsigned ? "jbe" : "jle");
                            else if (i2->opcode == SIR_ICMP_GT) func->fp_jcc_asm = cond_is_true ? (is_unsigned ? "jbe" : "jle") : (is_unsigned ? "ja" : "jg");
                            else if (i2->opcode == SIR_ICMP_GE) func->fp_jcc_asm = cond_is_true ? (is_unsigned ? "jb" : "jl") : (is_unsigned ? "jae" : "jge");
                            
                            // PE JCC
                            if (i2->opcode == SIR_ICMP_EQ) func->fp_jcc_pe = cond_is_true ? 0x85 : 0x84;
                            else if (i2->opcode == SIR_ICMP_NE) func->fp_jcc_pe = cond_is_true ? 0x84 : 0x85;
                            else if (i2->opcode == SIR_ICMP_LT) func->fp_jcc_pe = cond_is_true ? (is_unsigned ? 0x83 : 0x8D) : (is_unsigned ? 0x82 : 0x8C);
                            else if (i2->opcode == SIR_ICMP_LE) func->fp_jcc_pe = cond_is_true ? (is_unsigned ? 0x87 : 0x8F) : (is_unsigned ? 0x86 : 0x8E);
                            else if (i2->opcode == SIR_ICMP_GT) func->fp_jcc_pe = cond_is_true ? (is_unsigned ? 0x86 : 0x8E) : (is_unsigned ? 0x87 : 0x8F);
                            else if (i2->opcode == SIR_ICMP_GE) func->fp_jcc_pe = cond_is_true ? (is_unsigned ? 0x82 : 0x8C) : (is_unsigned ? 0x83 : 0x8D);
                            
                            // 切断树根，死块大扫除
                            i1->next = i3;
                            i3->prev = i1;
                            i3->opcode = SIR_JMP;
                            i3->num_operands = 1;
                            i3->operands[0] = cond_is_true ? i3->operands[2] : i3->operands[1];
                            
                            prune_dead_blocks(func, func->first_block);
                        }
                    }
                }
            }
        }

        // 3. 跳转穿透 (Jump Threading) - 消除空跳转块
        do {
            changed = false;
            for (SirBlock* block = func->first_block; block; block = block->next) {
                // 查找所有指令，更新跳转目标
                for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                    if (inst->opcode == SIR_JMP) {
                        SirBlock* target = inst->operands[0]->as.block;
                        if (target != block && target->first_inst && target->first_inst == target->last_inst && target->first_inst->opcode == SIR_JMP) {
                            inst->operands[0]->as.block = target->first_inst->operands[0]->as.block;
                            changed = true;
                        }
                    } else if (inst->opcode == SIR_BR) {
                        SirBlock* t_target = inst->operands[1]->as.block;
                        if (t_target != block && t_target->first_inst && t_target->first_inst == t_target->last_inst && t_target->first_inst->opcode == SIR_JMP) {
                            inst->operands[1]->as.block = t_target->first_inst->operands[0]->as.block;
                            changed = true;
                        }
                        SirBlock* f_target = inst->operands[2]->as.block;
                        if (f_target != block && f_target->first_inst && f_target->first_inst == f_target->last_inst && f_target->first_inst->opcode == SIR_JMP) {
                            inst->operands[2]->as.block = f_target->first_inst->operands[0]->as.block;
                            changed = true;
                        }
                    } else if (inst->opcode == SIR_SWITCH) {
                        SirBlock* def_target = inst->operands[1]->as.block;
                        if (def_target != block && def_target->first_inst && def_target->first_inst == def_target->last_inst && def_target->first_inst->opcode == SIR_JMP) {
                            inst->operands[1]->as.block = def_target->first_inst->operands[0]->as.block;
                            changed = true;
                        }
                        int case_count = (inst->num_operands - 2) / 2;
                        for (int i = 0; i < case_count; i++) {
                            SirBlock* c_target = inst->operands[2 + i * 2 + 1]->as.block;
                            if (c_target != block && c_target->first_inst && c_target->first_inst == c_target->last_inst && c_target->first_inst->opcode == SIR_JMP) {
                                inst->operands[2 + i * 2 + 1]->as.block = c_target->first_inst->operands[0]->as.block;
                                changed = true;
                            }
                        }
                    }
                }
            }
        } while (changed);
    }
}
