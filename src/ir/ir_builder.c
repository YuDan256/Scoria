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
    func->is_pure = false;
    func->has_fast_path = false;
    
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
    block->is_frameless = false;
    
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
            builder->current_block->last_inst->next = inst;
            inst->prev = builder->current_block->last_inst;
            builder->current_block->last_inst = inst;
        } else {
            builder->current_block->first_inst = inst;
            builder->current_block->last_inst = inst;
        }
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
            if (target && target != (SirBlock*)-1 && !reachable[target->id]) {
                reachable[target->id] = true;
                stack[top++] = target;
            }
        } else if (b->last_inst->opcode == SIR_BR) {
            SirBlock* t_target = b->last_inst->operands[1]->as.block;
            SirBlock* f_target = b->last_inst->operands[2]->as.block;
            if (t_target && t_target != (SirBlock*)-1 && !reachable[t_target->id]) {
                reachable[t_target->id] = true;
                stack[top++] = t_target;
            }
            if (f_target && f_target != (SirBlock*)-1 && !reachable[f_target->id]) {
                reachable[f_target->id] = true;
                stack[top++] = f_target;
            }
        } else if (b->last_inst->opcode == SIR_SWITCH) {
            SirBlock* def_target = b->last_inst->operands[1]->as.block;
            if (def_target && def_target != (SirBlock*)-1 && !reachable[def_target->id]) {
                reachable[def_target->id] = true;
                stack[top++] = def_target;
            }
            int case_count = (b->last_inst->num_operands - 2) / 2;
            for (int i = 0; i < case_count; i++) {
                SirBlock* c_target = b->last_inst->operands[2 + i * 2 + 1]->as.block;
                if (c_target && c_target != (SirBlock*)-1 && !reachable[c_target->id]) {
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

static bool values_equal(SirValue* a, SirValue* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->kind == SIR_VAL_VREG) return a->as.vreg == b->as.vreg;
    if (a->kind == SIR_VAL_CONST_INT) return a->as.int_val == b->as.int_val;
    if (a->kind == SIR_VAL_CONST_FLOAT) return a->as.float_val == b->as.float_val;
    if (a->kind == SIR_VAL_CONST_BOOL) return a->as.bool_val == b->as.bool_val;
    if (a->kind == SIR_VAL_GLOBAL) return strcmp(a->as.global_name, b->as.global_name) == 0;
    return false;
}

static bool is_pure_compute(SirOpcode opcode) {
    switch (opcode) {
        case SIR_ADD: case SIR_SUB: case SIR_MUL: case SIR_DIV: case SIR_MOD:
        case SIR_FADD: case SIR_FSUB: case SIR_FMUL: case SIR_FDIV:
        case SIR_AND: case SIR_OR: case SIR_XOR: case SIR_SHL: case SIR_SHR:
        case SIR_ICMP_EQ: case SIR_ICMP_NE: case SIR_ICMP_LT: case SIR_ICMP_LE: case SIR_ICMP_GT: case SIR_ICMP_GE:
        case SIR_FCMP_EQ: case SIR_FCMP_NE: case SIR_FCMP_LT: case SIR_FCMP_LE: case SIR_FCMP_GT: case SIR_FCMP_GE:
        case SIR_CAST: case SIR_GEP: case SIR_SELECT:
            return true;
        default:
            return false;
    }
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

static bool can_inline(SirFunction* func) {
    if (!func || !func->first_block) return false;
    int inst_count = 0;
    int ret_count = 0;
    for (SirBlock* b = func->first_block; b; b = b->next) {
        for (SirInst* i = b->first_inst; i; i = i->next) {
            inst_count++;
            if (i->opcode == SIR_RET) ret_count++;
            if (i->opcode == SIR_ALLOCA) return false;
            if (i->opcode == SIR_CALL && i->operands[0]->kind == SIR_VAL_GLOBAL) {
                if (strcmp(i->operands[0]->as.global_name, func->name) == 0) {
                    return false;
                }
            }
        }
    }
    return inst_count <= 60 && ret_count > 0;
}

static SirValue* map_value(IrBuilder* builder, SirValue* val, SirValue** vreg_map, SirBlock** block_map) {
    if (!val) return NULL;
    if (val->kind == SIR_VAL_VREG) {
        if (!vreg_map[val->as.vreg]) {
            vreg_map[val->as.vreg] = create_vreg(builder, val->type);
        }
        return vreg_map[val->as.vreg];
    }
    if (val->kind == SIR_VAL_BLOCK) {
        SirValue* new_val = (SirValue*)arena_alloc(&builder->arena, sizeof(SirValue));
        new_val->kind = SIR_VAL_BLOCK;
        new_val->type = val->type;
        if (val->as.block && val->as.block != (SirBlock*)-1) {
            if (!block_map[val->as.block->id]) {
                // 幽灵基本块代偿：填补被优化抹除的不可达区块
                block_map[val->as.block->id] = ir_builder_create_block(builder, "ghost_block");
            }
            new_val->as.block = block_map[val->as.block->id];
        } else {
            // 脏数据代偿
            new_val->as.block = ir_builder_create_block(builder, "ghost_block");
        }
        return new_val;
    }
    return val;
}

static bool try_evaluate_pure_func(SirFunction* func, int64_t arg_val, int64_t* out_res, int depth) {
    if (depth > 15) return false;
    if (!func || !func->first_block) return false;

    if (func->has_fast_path) {
        bool fp_taken = false;
        int64_t imm = func->fp_imm;
        switch (func->fp_jcc_pe) {
            case 0x84: fp_taken = (arg_val == imm); break;
            case 0x85: fp_taken = (arg_val != imm); break;
            case 0x8C: fp_taken = (arg_val < imm); break;
            case 0x82: fp_taken = ((uint64_t)arg_val < (uint64_t)imm); break;
            case 0x8E: fp_taken = (arg_val <= imm); break;
            case 0x86: fp_taken = ((uint64_t)arg_val <= (uint64_t)imm); break;
            case 0x8F: fp_taken = (arg_val > imm); break;
            case 0x87: fp_taken = ((uint64_t)arg_val > (uint64_t)imm); break;
            case 0x8D: fp_taken = (arg_val >= imm); break;
            case 0x83: fp_taken = ((uint64_t)arg_val >= (uint64_t)imm); break;
        }
        if (fp_taken) {
            *out_res = arg_val;
            return true;
        }
    }

    uint32_t max_vreg = 0;
    for (SirBlock* b = func->first_block; b; b = b->next) {
        for (SirInst* i = b->first_inst; i; i = i->next) {
            if (i->dest && i->dest->kind == SIR_VAL_VREG) {
                if (i->dest->as.vreg > max_vreg) max_vreg = i->dest->as.vreg;
            }
        }
    }

    bool* vreg_valid = (bool*)calloc(max_vreg + 1, sizeof(bool));
    int64_t* vreg_vals = (int64_t*)calloc(max_vreg + 1, sizeof(int64_t));
    if (!vreg_valid || !vreg_vals) {
        if (vreg_valid) free(vreg_valid);
        if (vreg_vals) free(vreg_vals);
        return false;
    }

    SirBlock* curr_block = func->first_block;
    bool success = false;

    while (curr_block) {
        SirBlock* next_block = NULL;
        for (SirInst* inst = curr_block->first_inst; inst; inst = inst->next) {
            switch (inst->opcode) {
                case SIR_GET_PARAM: {
                    if (inst->operands[0]->as.int_val == 0) {
                        if (!inst->dest) goto fail;
                        vreg_valid[inst->dest->as.vreg] = true;
                        vreg_vals[inst->dest->as.vreg] = arg_val;
                    } else {
                        goto fail;
                    }
                    break;
                }
                case SIR_ADD: case SIR_SUB: case SIR_MUL: case SIR_DIV: case SIR_MOD:
                case SIR_AND: case SIR_OR: case SIR_XOR: case SIR_SHL: case SIR_SHR:
                case SIR_ICMP_EQ: case SIR_ICMP_NE: case SIR_ICMP_LT: case SIR_ICMP_LE: case SIR_ICMP_GT: case SIR_ICMP_GE: {
                    int64_t l, r;
                    if (inst->operands[0]->kind == SIR_VAL_CONST_INT) l = inst->operands[0]->as.int_val;
                    else if (inst->operands[0]->kind == SIR_VAL_CONST_BOOL) l = inst->operands[0]->as.bool_val;
                    else if (inst->operands[0]->kind == SIR_VAL_VREG && vreg_valid[inst->operands[0]->as.vreg]) l = vreg_vals[inst->operands[0]->as.vreg];
                    else goto fail;

                    if (inst->operands[1]->kind == SIR_VAL_CONST_INT) r = inst->operands[1]->as.int_val;
                    else if (inst->operands[1]->kind == SIR_VAL_CONST_BOOL) r = inst->operands[1]->as.bool_val;
                    else if (inst->operands[1]->kind == SIR_VAL_VREG && vreg_valid[inst->operands[1]->as.vreg]) r = vreg_vals[inst->operands[1]->as.vreg];
                    else goto fail;

                    int64_t res = 0;
                    switch (inst->opcode) {
                        case SIR_ADD: res = l + r; break;
                        case SIR_SUB: res = l - r; break;
                        case SIR_MUL: res = l * r; break;
                        case SIR_DIV: if (r == 0) goto fail; res = l / r; break;
                        case SIR_MOD: if (r == 0) goto fail; res = l % r; break;
                        case SIR_AND: res = l & r; break;
                        case SIR_OR:  res = l | r; break;
                        case SIR_XOR: res = l ^ r; break;
                        case SIR_SHL: res = l << r; break;
                        case SIR_SHR: res = l >> r; break;
                        case SIR_ICMP_EQ: res = (l == r); break;
                        case SIR_ICMP_NE: res = (l != r); break;
                        case SIR_ICMP_LT: res = (l < r); break;
                        case SIR_ICMP_LE: res = (l <= r); break;
                        case SIR_ICMP_GT: res = (l > r); break;
                        case SIR_ICMP_GE: res = (l >= r); break;
                        default: goto fail;
                    }
                    if (!inst->dest) goto fail;
                    vreg_valid[inst->dest->as.vreg] = true;
                    vreg_vals[inst->dest->as.vreg] = res;
                    break;
                }
                case SIR_CAST: {
                    int64_t val;
                    if (inst->operands[0]->kind == SIR_VAL_CONST_INT) val = inst->operands[0]->as.int_val;
                    else if (inst->operands[0]->kind == SIR_VAL_CONST_BOOL) val = inst->operands[0]->as.bool_val;
                    else if (inst->operands[0]->kind == SIR_VAL_VREG && vreg_valid[inst->operands[0]->as.vreg]) val = vreg_vals[inst->operands[0]->as.vreg];
                    else goto fail;
                    if (!inst->dest) goto fail;
                    vreg_valid[inst->dest->as.vreg] = true;
                    vreg_vals[inst->dest->as.vreg] = val;
                    break;
                }
                case SIR_SELECT: {
                    int64_t cond, t_val, f_val;
                    if (inst->operands[0]->kind == SIR_VAL_CONST_BOOL) cond = inst->operands[0]->as.bool_val;
                    else if (inst->operands[0]->kind == SIR_VAL_VREG && vreg_valid[inst->operands[0]->as.vreg]) cond = vreg_vals[inst->operands[0]->as.vreg];
                    else goto fail;

                    if (inst->operands[1]->kind == SIR_VAL_CONST_INT) t_val = inst->operands[1]->as.int_val;
                    else if (inst->operands[1]->kind == SIR_VAL_VREG && vreg_valid[inst->operands[1]->as.vreg]) t_val = vreg_vals[inst->operands[1]->as.vreg];
                    else goto fail;

                    if (inst->operands[2]->kind == SIR_VAL_CONST_INT) f_val = inst->operands[2]->as.int_val;
                    else if (inst->operands[2]->kind == SIR_VAL_VREG && vreg_valid[inst->operands[2]->as.vreg]) f_val = vreg_vals[inst->operands[2]->as.vreg];
                    else goto fail;

                    if (!inst->dest) goto fail;
                    vreg_valid[inst->dest->as.vreg] = true;
                    vreg_vals[inst->dest->as.vreg] = cond ? t_val : f_val;
                    break;
                }
                case SIR_CALL: {
                    if (inst->operands[0]->kind != SIR_VAL_GLOBAL) goto fail;
                    if (strcmp(inst->operands[0]->as.global_name, func->name) != 0) goto fail;
                    if (inst->num_operands != 2) goto fail;

                    int64_t arg;
                    if (inst->operands[1]->kind == SIR_VAL_CONST_INT) arg = inst->operands[1]->as.int_val;
                    else if (inst->operands[1]->kind == SIR_VAL_VREG && vreg_valid[inst->operands[1]->as.vreg]) arg = vreg_vals[inst->operands[1]->as.vreg];
                    else goto fail;

                    int64_t ret_val;
                    if (!try_evaluate_pure_func(func, arg, &ret_val, depth + 1)) goto fail;

                    if (inst->dest) {
                        vreg_valid[inst->dest->as.vreg] = true;
                        vreg_vals[inst->dest->as.vreg] = ret_val;
                    }
                    break;
                }
                case SIR_JMP: {
                    next_block = inst->operands[0]->as.block;
                    break;
                }
                case SIR_BR: {
                    int64_t cond;
                    if (inst->operands[0]->kind == SIR_VAL_CONST_BOOL) cond = inst->operands[0]->as.bool_val;
                    else if (inst->operands[0]->kind == SIR_VAL_VREG && vreg_valid[inst->operands[0]->as.vreg]) cond = vreg_vals[inst->operands[0]->as.vreg];
                    else goto fail;

                    next_block = cond ? inst->operands[1]->as.block : inst->operands[2]->as.block;
                    break;
                }
                case SIR_SWITCH: {
                    int64_t cond;
                    if (inst->operands[0]->kind == SIR_VAL_CONST_INT) cond = inst->operands[0]->as.int_val;
                    else if (inst->operands[0]->kind == SIR_VAL_VREG && vreg_valid[inst->operands[0]->as.vreg]) cond = vreg_vals[inst->operands[0]->as.vreg];
                    else goto fail;

                    next_block = inst->operands[1]->as.block;
                    int case_count = (inst->num_operands - 2) / 2;
                    for (int i = 0; i < case_count; i++) {
                        if (inst->operands[2 + i * 2]->as.int_val == cond) {
                            next_block = inst->operands[2 + i * 2 + 1]->as.block;
                            break;
                        }
                    }
                    break;
                }
                case SIR_RET: {
                    if (inst->num_operands > 0) {
                        if (inst->operands[0]->kind == SIR_VAL_CONST_INT) *out_res = inst->operands[0]->as.int_val;
                        else if (inst->operands[0]->kind == SIR_VAL_VREG && vreg_valid[inst->operands[0]->as.vreg]) *out_res = vreg_vals[inst->operands[0]->as.vreg];
                        else goto fail;
                    } else {
                        *out_res = 0;
                    }
                    success = true;
                    goto end;
                }
                default:
                    goto fail;
            }
        }
        curr_block = next_block;
    }

fail:
    success = false;
end:
    free(vreg_valid);
    free(vreg_vals);
    return success;
}

static bool vm_execute(SirModule* module, SirFunction* func, int64_t* args, int64_t* out_res, int depth, uint64_t* global_steps) {
    if (depth > 20000) return false; // 防止爆栈
    if (*global_steps > 10000000000ULL) return false; // 100亿步超时保护

    if (func->has_fast_path) {
        bool fp_taken = false;
        int64_t arg_val = args[0];
        int64_t imm = func->fp_imm;
        switch (func->fp_jcc_pe) {
            case 0x84: fp_taken = (arg_val == imm); break;
            case 0x85: fp_taken = (arg_val != imm); break;
            case 0x8C: fp_taken = (arg_val < imm); break;
            case 0x82: fp_taken = ((uint64_t)arg_val < (uint64_t)imm); break;
            case 0x8E: fp_taken = (arg_val <= imm); break;
            case 0x86: fp_taken = ((uint64_t)arg_val <= (uint64_t)imm); break;
            case 0x8F: fp_taken = (arg_val > imm); break;
            case 0x87: fp_taken = ((uint64_t)arg_val > (uint64_t)imm); break;
            case 0x8D: fp_taken = (arg_val >= imm); break;
            case 0x83: fp_taken = ((uint64_t)arg_val >= (uint64_t)imm); break;
        }
        if (fp_taken) {
            *out_res = arg_val;
            return true;
        }
    }

    // 极速优化：在 C 栈上分配虚拟寄存器堆，彻底消除 calloc/free 的系统调用开销
    // 256 个寄存器足以覆盖 99.9% 的纯函数
    int64_t vregs[256] = {0};
    bool vreg_valid[256] = {0};

    SirBlock* pc = func->first_block;
    bool success = false;

    #define VM_GET_VAL(op) \
        ((op)->kind == SIR_VAL_CONST_INT ? (op)->as.int_val : \
         ((op)->kind == SIR_VAL_CONST_BOOL ? (op)->as.bool_val : \
          ((op)->kind == SIR_VAL_VREG && vreg_valid[(op)->as.vreg] ? vregs[(op)->as.vreg] : 0)))

    #define VM_CHECK_VAL(op) \
        ((op)->kind == SIR_VAL_CONST_INT || (op)->kind == SIR_VAL_CONST_BOOL || \
         ((op)->kind == SIR_VAL_VREG && vreg_valid[(op)->as.vreg]))

    // 解释器主循环 (Interpreter Loop)
    while (pc) {
        SirBlock* next_pc = NULL;
        for (SirInst* inst = pc->first_inst; inst; inst = inst->next) {
            (*global_steps)++;
            if (*global_steps > 10000000000ULL) goto fail;

            switch (inst->opcode) {
                case SIR_GET_PARAM: {
                    if (!inst->dest || inst->dest->as.vreg >= 256) goto fail;
                    int idx = (int)inst->operands[0]->as.int_val;
                    vregs[inst->dest->as.vreg] = args[idx];
                    vreg_valid[inst->dest->as.vreg] = true;
                    break;
                }
                case SIR_ADD: case SIR_SUB: case SIR_MUL: case SIR_DIV: case SIR_MOD:
                case SIR_AND: case SIR_OR: case SIR_XOR: case SIR_SHL: case SIR_SHR:
                case SIR_ICMP_EQ: case SIR_ICMP_NE: case SIR_ICMP_LT: case SIR_ICMP_LE: case SIR_ICMP_GT: case SIR_ICMP_GE: {
                    if (!VM_CHECK_VAL(inst->operands[0]) || !VM_CHECK_VAL(inst->operands[1])) goto fail;
                    int64_t l = VM_GET_VAL(inst->operands[0]);
                    int64_t r = VM_GET_VAL(inst->operands[1]);
                    int64_t res = 0;
                    switch (inst->opcode) {
                        case SIR_ADD: res = l + r; break;
                        case SIR_SUB: res = l - r; break;
                        case SIR_MUL: res = l * r; break;
                        case SIR_DIV: if (r == 0) goto fail; res = l / r; break;
                        case SIR_MOD: if (r == 0) goto fail; res = l % r; break;
                        case SIR_AND: res = l & r; break;
                        case SIR_OR:  res = l | r; break;
                        case SIR_XOR: res = l ^ r; break;
                        case SIR_SHL: res = l << r; break;
                        case SIR_SHR: res = l >> r; break;
                        case SIR_ICMP_EQ: res = (l == r); break;
                        case SIR_ICMP_NE: res = (l != r); break;
                        case SIR_ICMP_LT: res = (l < r); break;
                        case SIR_ICMP_LE: res = (l <= r); break;
                        case SIR_ICMP_GT: res = (l > r); break;
                        case SIR_ICMP_GE: res = (l >= r); break;
                        default: goto fail;
                    }
                    if (!inst->dest || inst->dest->as.vreg >= 256) goto fail;
                    vregs[inst->dest->as.vreg] = res;
                    vreg_valid[inst->dest->as.vreg] = true;
                    break;
                }
                case SIR_CAST: {
                    if (!VM_CHECK_VAL(inst->operands[0])) goto fail;
                    if (!inst->dest || inst->dest->as.vreg >= 256) goto fail;
                    vregs[inst->dest->as.vreg] = VM_GET_VAL(inst->operands[0]);
                    vreg_valid[inst->dest->as.vreg] = true;
                    break;
                }
                case SIR_SELECT: {
                    if (!VM_CHECK_VAL(inst->operands[0]) || !VM_CHECK_VAL(inst->operands[1]) || !VM_CHECK_VAL(inst->operands[2])) goto fail;
                    int64_t cond = VM_GET_VAL(inst->operands[0]);
                    int64_t t_val = VM_GET_VAL(inst->operands[1]);
                    int64_t f_val = VM_GET_VAL(inst->operands[2]);
                    if (!inst->dest || inst->dest->as.vreg >= 256) goto fail;
                    vregs[inst->dest->as.vreg] = cond ? t_val : f_val;
                    vreg_valid[inst->dest->as.vreg] = true;
                    break;
                }
                case SIR_CALL: {
                    if (inst->operands[0]->kind != SIR_VAL_GLOBAL) goto fail;
                    SirFunction* callee = NULL;
                    for (SirFunction* f = module->first_func; f; f = f->next) {
                        if (strcmp(f->name, inst->operands[0]->as.global_name) == 0) { callee = f; break; }
                    }
                    if (!callee || !callee->is_pure) goto fail;

                    int c_arg_count = inst->num_operands - 1;
                    int64_t c_args[16];
                    if (c_arg_count > 16) goto fail;
                    for (int i = 0; i < c_arg_count; i++) {
                        if (!VM_CHECK_VAL(inst->operands[i+1])) goto fail;
                        c_args[i] = VM_GET_VAL(inst->operands[i+1]);
                    }

                    int64_t ret_val;
                    if (!vm_execute(module, callee, c_args, &ret_val, depth + 1, global_steps)) goto fail;

                    if (inst->dest) {
                        if (inst->dest->as.vreg >= 256) goto fail;
                        vregs[inst->dest->as.vreg] = ret_val;
                        vreg_valid[inst->dest->as.vreg] = true;
                    }
                    break;
                }
                case SIR_JMP: {
                    next_pc = inst->operands[0]->as.block;
                    break;
                }
                case SIR_BR: {
                    if (!VM_CHECK_VAL(inst->operands[0])) goto fail;
                    int64_t cond = VM_GET_VAL(inst->operands[0]);
                    next_pc = cond ? inst->operands[1]->as.block : inst->operands[2]->as.block;
                    break;
                }
                case SIR_SWITCH: {
                    if (!VM_CHECK_VAL(inst->operands[0])) goto fail;
                    int64_t cond = VM_GET_VAL(inst->operands[0]);
                    next_pc = inst->operands[1]->as.block;
                    int case_count = (inst->num_operands - 2) / 2;
                    for (int i = 0; i < case_count; i++) {
                        if (inst->operands[2 + i * 2]->as.int_val == cond) {
                            next_pc = inst->operands[2 + i * 2 + 1]->as.block;
                            break;
                        }
                    }
                    break;
                }
                case SIR_RET: {
                    if (inst->num_operands > 0) {
                        if (!VM_CHECK_VAL(inst->operands[0])) goto fail;
                        *out_res = VM_GET_VAL(inst->operands[0]);
                    } else {
                        *out_res = 0;
                    }
                    success = true;
                    goto end;
                }
                default:
                    goto fail;
            }
        }
        pc = next_pc;
    }

fail:
    success = false;
end:
    return success;
    #undef VM_GET_VAL
    #undef VM_CHECK_VAL
}

void ir_optimize_module(IrBuilder* builder, int opt_level) {
    if (!builder || !builder->module) return;
    if (opt_level == 0) return;

    if (opt_level >= 2) {
        bool inline_changed;
        do {
            inline_changed = false;
            for (SirFunction* func = builder->module->first_func; func; func = func->next) {
                builder->current_func = func;
                for (SirBlock* block = func->first_block; block; block = block->next) {
                    for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                        if (inst->opcode == SIR_CALL && inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                            SirFunction* callee = NULL;
                            for (SirFunction* f = builder->module->first_func; f; f = f->next) {
                                if (strcmp(f->name, inst->operands[0]->as.global_name) == 0) {
                                    callee = f;
                                    break;
                                }
                            }
                            int current_insts = 0;
                            for (SirBlock* b = func->first_block; b; b = b->next) {
                                for (SirInst* i = b->first_inst; i; i = i->next) current_insts++;
                            }
                            
                            bool is_recursive = (callee == func);
                            bool allowed_to_inline = false;
                            
                            if (callee && can_inline(callee)) {
                                if (!is_recursive && current_insts < 200) allowed_to_inline = true;
                            }
                            
                            if (allowed_to_inline) {
                                uint32_t callee_max_vreg = builder->next_vreg;
                                uint32_t callee_max_block = builder->next_block_id;
                                
                                SirValue** vreg_map = (SirValue**)calloc(callee_max_vreg + 1, sizeof(SirValue*));
                                SirBlock** block_map = (SirBlock**)calloc(callee_max_block + 1, sizeof(SirBlock*));
                                
                                // 预先收集原始基本块，防止递归内联时遍历到新追加的块导致死循环
                                SirBlock** orig_blocks = (SirBlock**)malloc(sizeof(SirBlock*) * (callee_max_block + 1));
                                int orig_count = 0;
                                int ret_count = 0;
                                for (SirBlock* cb = callee->first_block; cb; cb = cb->next) {
                                    orig_blocks[orig_count++] = cb;
                                    for (SirInst* ci = cb->first_inst; ci; ci = ci->next) {
                                        if (ci->opcode == SIR_RET) ret_count++;
                                    }
                                }
                                
                                SirValue* ret_alloca = NULL;
                                if (inst->dest && ret_count > 1) {
                                    SirInst* alloc_inst = (SirInst*)arena_alloc(&builder->arena, sizeof(SirInst));
                                    alloc_inst->opcode = SIR_ALLOCA;
                                    alloc_inst->num_operands = 1;
                                    alloc_inst->operands = (SirValue**)arena_alloc(&builder->arena, sizeof(SirValue*));
                                    alloc_inst->operands[0] = ir_const_int(builder, type_get_basic(TY_I32), type_get_size(inst->dest->type));
                                    alloc_inst->dest = create_vreg(builder, type_get_via(inst->dest->type));
                                    alloc_inst->prev = NULL;
                                    alloc_inst->next = func->first_block->first_inst;
                                    if (func->first_block->first_inst) func->first_block->first_inst->prev = alloc_inst;
                                    else func->first_block->last_inst = alloc_inst;
                                    func->first_block->first_inst = alloc_inst;
                                    ret_alloca = alloc_inst->dest;
                                }
                                
                                for (int b_idx = 0; b_idx < orig_count; b_idx++) {
                                    block_map[orig_blocks[b_idx]->id] = ir_builder_create_block(builder, orig_blocks[b_idx]->name);
                                }
                                
                                SirBlock* return_block = ir_builder_create_block(builder, "post_inline");
                                SirInst* curr = inst->next;
                                while (curr) {
                                    SirInst* next = curr->next;
                                    curr->prev = return_block->last_inst;
                                    if (return_block->last_inst) return_block->last_inst->next = curr;
                                    else return_block->first_inst = curr;
                                    return_block->last_inst = curr;
                                    curr->next = NULL;
                                    curr = next;
                                }
                                block->last_inst = inst->prev;
                                if (block->last_inst) block->last_inst->next = NULL;
                                else block->first_inst = NULL;
                                
                                ir_builder_set_insert_point(builder, block);
                                ir_build_jmp(builder, block_map[orig_blocks[0]->id]);
                                
                                for (int b_idx = 0; b_idx < orig_count; b_idx++) {
                                    SirBlock* cb = orig_blocks[b_idx];
                                    ir_builder_set_insert_point(builder, block_map[cb->id]);
                                    for (SirInst* ci = cb->first_inst; ci; ci = ci->next) {
                                        if (ci->opcode == SIR_GET_PARAM) {
                                            int param_idx = (int)ci->operands[0]->as.int_val;
                                            if (ci->dest && param_idx + 1 < inst->num_operands) {
                                                vreg_map[ci->dest->as.vreg] = inst->operands[param_idx + 1];
                                            }
                                        } else if (ci->opcode == SIR_RET) {
                                            if (ci->num_operands > 0 && inst->dest) {
                                                SirValue* mapped_ret = map_value(builder, ci->operands[0], vreg_map, block_map);
                                                if (ret_count > 1) {
                                                    ir_build_store(builder, mapped_ret, ret_alloca);
                                                } else {
                                                    SirInst* copy = create_inst(builder, SIR_CAST, 1);
                                                    copy->operands[0] = mapped_ret;
                                                    copy->dest = inst->dest;
                                                }
                                            }
                                            ir_build_jmp(builder, return_block);
                                        } else {
                                            SirInst* clone = create_inst(builder, ci->opcode, ci->num_operands);
                                            if (ci->dest) {
                                                clone->dest = create_vreg(builder, ci->dest->type);
                                                vreg_map[ci->dest->as.vreg] = clone->dest;
                                            }
                                            for (int op = 0; op < ci->num_operands; op++) {
                                                clone->operands[op] = map_value(builder, ci->operands[op], vreg_map, block_map);
                                            }
                                        }
                                    }
                                }
                                
                                if (inst->dest && ret_count > 1) {
                                    SirInst* load_inst = (SirInst*)arena_alloc(&builder->arena, sizeof(SirInst));
                                    load_inst->opcode = SIR_LOAD;
                                    load_inst->num_operands = 1;
                                    load_inst->operands = (SirValue**)arena_alloc(&builder->arena, sizeof(SirValue*));
                                    load_inst->operands[0] = ret_alloca;
                                    load_inst->dest = inst->dest;
                                    
                                    load_inst->prev = NULL;
                                    load_inst->next = return_block->first_inst;
                                    if (return_block->first_inst) return_block->first_inst->prev = load_inst;
                                    else return_block->last_inst = load_inst;
                                    return_block->first_inst = load_inst;
                                }
                                
                                free(vreg_map);
                                free(block_map);
                                free(orig_blocks);
                                
                                inline_changed = true;
                                break;
                            }
                        }
                    }
                    if (inline_changed) break;
                }
                if (inline_changed) {
                    prune_dead_blocks(func, func->first_block);
                    break;
                }
            }
        } while (inline_changed);
    }

    for (SirFunction* func = builder->module->first_func; func; func = func->next) {
        builder->current_func = func;
        uint32_t max_vreg = 0;
        for (SirBlock* block = func->first_block; block; block = block->next) {
            for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                    if (inst->dest->as.vreg > max_vreg) max_vreg = inst->dest->as.vreg;
                }
                for (int i = 0; i < inst->num_operands; i++) {
                    if (inst->operands[i] && inst->operands[i]->kind == SIR_VAL_VREG) {
                        if (inst->operands[i]->as.vreg > max_vreg) max_vreg = inst->operands[i]->as.vreg;
                    }
                }
            }
        }

        bool changed;
        
        if (opt_level >= 2) {
            // 0. 常量传播、复写传播与局部公共子表达式消除 (Local CSE)
            bool prop_changed;
            bool cfg_changed = false;
            do {
                prop_changed = false;
                SirValue** replacements = (SirValue**)calloc(max_vreg + 1, sizeof(SirValue*));
                
                for (SirBlock* block = func->first_block; block; block = block->next) {
                    int seen_capacity = 64;
                    SirInst** seen_insts = (SirInst**)malloc(sizeof(SirInst*) * seen_capacity);
                    int seen_count = 0;

                    for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                        // 替换操作数
                        for (int i = 0; i < inst->num_operands; i++) {
                            if (inst->operands[i] && inst->operands[i]->kind == SIR_VAL_VREG) {
                                SirValue* rep = replacements[inst->operands[i]->as.vreg];
                                if (rep) {
                                    inst->operands[i] = rep;
                                    prop_changed = true;
                                }
                            }
                        }
                        
                        // 分支折叠 (Branch Folding)
                        if (inst->opcode == SIR_BR && inst->operands[0]->kind == SIR_VAL_CONST_BOOL) {
                            inst->opcode = SIR_JMP;
                            inst->num_operands = 1;
                            inst->operands[0] = inst->operands[0]->as.bool_val ? inst->operands[1] : inst->operands[2];
                            prop_changed = true;
                            cfg_changed = true;
                        } else if (inst->opcode == SIR_SWITCH && inst->operands[0]->kind == SIR_VAL_CONST_INT) {
                            int64_t val = inst->operands[0]->as.int_val;
                            SirValue* target_block = inst->operands[1]; // default block
                            int case_count = (inst->num_operands - 2) / 2;
                            for (int i = 0; i < case_count; i++) {
                                if (inst->operands[2 + i * 2]->as.int_val == val) {
                                    target_block = inst->operands[2 + i * 2 + 1];
                                    break;
                                }
                            }
                            inst->opcode = SIR_JMP;
                            inst->num_operands = 1;
                            inst->operands[0] = target_block;
                            prop_changed = true;
                            cfg_changed = true;
                        }
                        
                        // 发现新的复写/常量
                        if (inst->dest && inst->dest->kind == SIR_VAL_VREG && !replacements[inst->dest->as.vreg]) {
                            SirValue* rep = NULL;
                            if (inst->opcode == SIR_CAST && type_equals(inst->operands[0]->type, inst->dest->type)) {
                                rep = inst->operands[0];
                            } else if (inst->opcode == SIR_ADD) {
                                if (inst->operands[1]->kind == SIR_VAL_CONST_INT && inst->operands[1]->as.int_val == 0) rep = inst->operands[0];
                                else if (inst->operands[0]->kind == SIR_VAL_CONST_INT && inst->operands[0]->as.int_val == 0) rep = inst->operands[1];
                            } else if (inst->opcode == SIR_SUB && inst->operands[1]->kind == SIR_VAL_CONST_INT && inst->operands[1]->as.int_val == 0) {
                                rep = inst->operands[0];
                            } else if (inst->opcode == SIR_MUL) {
                                if (inst->operands[1]->kind == SIR_VAL_CONST_INT && inst->operands[1]->as.int_val == 1) rep = inst->operands[0];
                                else if (inst->operands[0]->kind == SIR_VAL_CONST_INT && inst->operands[0]->as.int_val == 1) rep = inst->operands[1];
                            } else if (inst->opcode == SIR_DIV && inst->operands[1]->kind == SIR_VAL_CONST_INT && inst->operands[1]->as.int_val == 1) {
                                rep = inst->operands[0];
                            } else if (inst->opcode == SIR_SELECT && inst->operands[0]->kind == SIR_VAL_CONST_BOOL) {
                                rep = inst->operands[0]->as.bool_val ? inst->operands[1] : inst->operands[2];
                            } else if (inst->opcode >= SIR_ADD && inst->opcode <= SIR_SHR) {
                                if (inst->operands[0]->kind == SIR_VAL_CONST_INT && inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                                    int64_t l = inst->operands[0]->as.int_val;
                                    int64_t r = inst->operands[1]->as.int_val;
                                    int64_t res = 0;
                                    bool can_fold = true;
                                    switch (inst->opcode) {
                                        case SIR_ADD: res = l + r; break;
                                        case SIR_SUB: res = l - r; break;
                                        case SIR_MUL: res = l * r; break;
                                        case SIR_DIV: if (r != 0) res = l / r; else can_fold = false; break;
                                        case SIR_MOD: if (r != 0) res = l % r; else can_fold = false; break;
                                        case SIR_AND: res = l & r; break;
                                        case SIR_OR:  res = l | r; break;
                                        case SIR_XOR: res = l ^ r; break;
                                        case SIR_SHL: res = l << r; break;
                                        case SIR_SHR: res = l >> r; break;
                                        default: can_fold = false; break;
                                    }
                                    if (can_fold) rep = ir_const_int(builder, inst->dest->type, res);
                                }
                            } else if (inst->opcode >= SIR_ICMP_EQ && inst->opcode <= SIR_ICMP_GE) {
                                if (inst->operands[0]->kind == SIR_VAL_CONST_INT && inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                                    int64_t l = inst->operands[0]->as.int_val;
                                    int64_t r = inst->operands[1]->as.int_val;
                                    bool res = false;
                                    switch (inst->opcode) {
                                        case SIR_ICMP_EQ: res = (l == r); break;
                                        case SIR_ICMP_NE: res = (l != r); break;
                                        case SIR_ICMP_LT: res = (l < r); break;
                                        case SIR_ICMP_LE: res = (l <= r); break;
                                        case SIR_ICMP_GT: res = (l > r); break;
                                        case SIR_ICMP_GE: res = (l >= r); break;
                                        default: break;
                                    }
                                    rep = ir_const_bool(builder, res);
                                }
                            } else if (inst->opcode >= SIR_FADD && inst->opcode <= SIR_FDIV) {
                                if (inst->operands[0]->kind == SIR_VAL_CONST_FLOAT && inst->operands[1]->kind == SIR_VAL_CONST_FLOAT) {
                                    double l = inst->operands[0]->as.float_val;
                                    double r = inst->operands[1]->as.float_val;
                                    double res = 0.0;
                                    bool can_fold = true;
                                    switch (inst->opcode) {
                                        case SIR_FADD: res = l + r; break;
                                        case SIR_FSUB: res = l - r; break;
                                        case SIR_FMUL: res = l * r; break;
                                        case SIR_FDIV: if (r != 0.0) res = l / r; else can_fold = false; break;
                                        default: can_fold = false; break;
                                    }
                                    if (can_fold) rep = ir_const_float(builder, inst->dest->type, res);
                                }
                            } else if (inst->opcode >= SIR_FCMP_EQ && inst->opcode <= SIR_FCMP_GE) {
                                if (inst->operands[0]->kind == SIR_VAL_CONST_FLOAT && inst->operands[1]->kind == SIR_VAL_CONST_FLOAT) {
                                    double l = inst->operands[0]->as.float_val;
                                    double r = inst->operands[1]->as.float_val;
                                    bool res = false;
                                    switch (inst->opcode) {
                                        case SIR_FCMP_EQ: res = (l == r); break;
                                        case SIR_FCMP_NE: res = (l != r); break;
                                        case SIR_FCMP_LT: res = (l < r); break;
                                        case SIR_FCMP_LE: res = (l <= r); break;
                                        case SIR_FCMP_GT: res = (l > r); break;
                                        case SIR_FCMP_GE: res = (l >= r); break;
                                        default: break;
                                    }
                                    rep = ir_const_bool(builder, res);
                                }
                            }
                            
                            if (rep) {
                                replacements[inst->dest->as.vreg] = rep;
                            } else if (is_pure_compute(inst->opcode)) {
                                // 局部公共子表达式消除 (Local CSE)
                                bool found_cse = false;
                                for (int s = 0; s < seen_count; s++) {
                                    SirInst* seen = seen_insts[s];
                                    if (seen->opcode == inst->opcode && seen->num_operands == inst->num_operands && type_equals(seen->dest->type, inst->dest->type)) {
                                        bool ops_match = true;
                                        for (int o = 0; o < inst->num_operands; o++) {
                                            if (!values_equal(seen->operands[o], inst->operands[o])) {
                                                ops_match = false;
                                                break;
                                            }
                                        }
                                        // 交换律支持 (Commutativity)
                                        if (!ops_match && inst->num_operands == 2 && (inst->opcode == SIR_ADD || inst->opcode == SIR_MUL || inst->opcode == SIR_FADD || inst->opcode == SIR_FMUL || inst->opcode == SIR_AND || inst->opcode == SIR_OR || inst->opcode == SIR_XOR || inst->opcode == SIR_ICMP_EQ || inst->opcode == SIR_ICMP_NE || inst->opcode == SIR_FCMP_EQ || inst->opcode == SIR_FCMP_NE)) {
                                            if (values_equal(seen->operands[0], inst->operands[1]) && values_equal(seen->operands[1], inst->operands[0])) {
                                                ops_match = true;
                                            }
                                        }
                                        if (ops_match) {
                                            replacements[inst->dest->as.vreg] = seen->dest;
                                            found_cse = true;
                                            break;
                                        }
                                    }
                                }
                                if (!found_cse) {
                                    if (seen_count >= seen_capacity) {
                                        seen_capacity *= 2;
                                        SirInst** new_seen = (SirInst**)realloc(seen_insts, sizeof(SirInst*) * seen_capacity);
                                        if (!new_seen) {
                                            fprintf(stderr, "Clades fatalis: Memoria non sufficit in Local CSE.\n");
                                            exit(1);
                                        }
                                        seen_insts = new_seen;
                                    }
                                    seen_insts[seen_count++] = inst;
                                }
                            }
                        }
                    }
                    free(seen_insts);
                }
                free(replacements);
            } while (prop_changed);
            
            if (cfg_changed) {
                prune_dead_blocks(func, func->first_block);
            }
        }
        
        // 1. 死代码消除 (Dead Code Elimination)
        do {
            changed = false;
            uint32_t* use_counts = (uint32_t*)calloc(max_vreg + 1, sizeof(uint32_t));
            
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
                            
                            // ASM JCC (正向条件：满足快路径时跳转)
                            if (i2->opcode == SIR_ICMP_EQ) func->fp_jcc_asm = cond_is_true ? "je" : "jne";
                            else if (i2->opcode == SIR_ICMP_NE) func->fp_jcc_asm = cond_is_true ? "jne" : "je";
                            else if (i2->opcode == SIR_ICMP_LT) func->fp_jcc_asm = cond_is_true ? (is_unsigned ? "jb" : "jl") : (is_unsigned ? "jae" : "jge");
                            else if (i2->opcode == SIR_ICMP_LE) func->fp_jcc_asm = cond_is_true ? (is_unsigned ? "jbe" : "jle") : (is_unsigned ? "ja" : "jg");
                            else if (i2->opcode == SIR_ICMP_GT) func->fp_jcc_asm = cond_is_true ? (is_unsigned ? "ja" : "jg") : (is_unsigned ? "jbe" : "jle");
                            else if (i2->opcode == SIR_ICMP_GE) func->fp_jcc_asm = cond_is_true ? (is_unsigned ? "jae" : "jge") : (is_unsigned ? "jb" : "jl");
                            
                            // PE JCC (正向条件：满足快路径时跳转)
                            if (i2->opcode == SIR_ICMP_EQ) func->fp_jcc_pe = cond_is_true ? 0x84 : 0x85;
                            else if (i2->opcode == SIR_ICMP_NE) func->fp_jcc_pe = cond_is_true ? 0x85 : 0x84;
                            else if (i2->opcode == SIR_ICMP_LT) func->fp_jcc_pe = cond_is_true ? (is_unsigned ? 0x82 : 0x8C) : (is_unsigned ? 0x83 : 0x8D);
                            else if (i2->opcode == SIR_ICMP_LE) func->fp_jcc_pe = cond_is_true ? (is_unsigned ? 0x86 : 0x8E) : (is_unsigned ? 0x87 : 0x8F);
                            else if (i2->opcode == SIR_ICMP_GT) func->fp_jcc_pe = cond_is_true ? (is_unsigned ? 0x87 : 0x8F) : (is_unsigned ? 0x86 : 0x8E);
                            else if (i2->opcode == SIR_ICMP_GE) func->fp_jcc_pe = cond_is_true ? (is_unsigned ? 0x83 : 0x8D) : (is_unsigned ? 0x82 : 0x8C);
                            
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
                        if (target && target != (SirBlock*)-1 && target != block && target->first_inst && target->first_inst == target->last_inst && target->first_inst->opcode == SIR_JMP) {
                            SirBlock* new_target = target->first_inst->operands[0]->as.block;
                            if (inst->operands[0]->as.block != new_target) {
                                inst->operands[0]->as.block = new_target;
                                changed = true;
                            }
                        }
                    } else if (inst->opcode == SIR_BR) {
                        SirBlock* t_target = inst->operands[1]->as.block;
                        if (t_target && t_target != (SirBlock*)-1 && t_target != block && t_target->first_inst && t_target->first_inst == t_target->last_inst && t_target->first_inst->opcode == SIR_JMP) {
                            SirBlock* new_target = t_target->first_inst->operands[0]->as.block;
                            if (inst->operands[1]->as.block != new_target) {
                                inst->operands[1]->as.block = new_target;
                                changed = true;
                            }
                        }
                        SirBlock* f_target = inst->operands[2]->as.block;
                        if (f_target && f_target != (SirBlock*)-1 && f_target != block && f_target->first_inst && f_target->first_inst == f_target->last_inst && f_target->first_inst->opcode == SIR_JMP) {
                            SirBlock* new_target = f_target->first_inst->operands[0]->as.block;
                            if (inst->operands[2]->as.block != new_target) {
                                inst->operands[2]->as.block = new_target;
                                changed = true;
                            }
                        }
                    } else if (inst->opcode == SIR_SWITCH) {
                        SirBlock* def_target = inst->operands[1]->as.block;
                        if (def_target && def_target != (SirBlock*)-1 && def_target != block && def_target->first_inst && def_target->first_inst == def_target->last_inst && def_target->first_inst->opcode == SIR_JMP) {
                            SirBlock* new_target = def_target->first_inst->operands[0]->as.block;
                            if (inst->operands[1]->as.block != new_target) {
                                inst->operands[1]->as.block = new_target;
                                changed = true;
                            }
                        }
                        int case_count = (inst->num_operands - 2) / 2;
                        for (int i = 0; i < case_count; i++) {
                            SirBlock* c_target = inst->operands[2 + i * 2 + 1]->as.block;
                            if (c_target && c_target != (SirBlock*)-1 && c_target != block && c_target->first_inst && c_target->first_inst == c_target->last_inst && c_target->first_inst->opcode == SIR_JMP) {
                                SirBlock* new_target = c_target->first_inst->operands[0]->as.block;
                                if (inst->operands[2 + i * 2 + 1]->as.block != new_target) {
                                    inst->operands[2 + i * 2 + 1]->as.block = new_target;
                                    changed = true;
                                }
                            }
                        }
                    }
                }
            }
        } while (changed);
    }

    // 4. 基础边界延展 (Base Case Expansion)
    if (opt_level >= 2) {
        bool bce_changed = false;
        for (SirFunction* func = builder->module->first_func; func; func = func->next) {
            if (!func->first_block || !func->first_block->first_inst) continue;
            
            // 检查是否是单参数整型函数
            SirInst* first_inst = func->first_block->first_inst;
            if (first_inst->opcode != SIR_GET_PARAM || first_inst->operands[0]->as.int_val != 0) continue;
            
            SirValue* param_val = first_inst->dest;
            if (!param_val || !param_val->type) continue;
            
            // 检查函数是否包含自递归调用
            bool has_recursion = false;
            for (SirBlock* b = func->first_block; b; b = b->next) {
                for (SirInst* i = b->first_inst; i; i = i->next) {
                    if (i->opcode == SIR_CALL && i->operands[0]->kind == SIR_VAL_GLOBAL && strcmp(i->operands[0]->as.global_name, func->name) == 0) {
                        has_recursion = true;
                        break;
                    }
                }
                if (has_recursion) break;
            }
            if (!has_recursion) continue;

            // 尝试计算 n=2, 3, 4 的结果
            for (int test_val = 2; test_val <= 4; test_val++) {
                int64_t res;
                if (try_evaluate_pure_func(func, test_val, &res, 0)) {
                    builder->current_func = func;
                    
                    SirBlock* old_entry = func->first_block;
                    SirBlock* new_entry = ir_builder_create_block(builder, "bce_entry");
                    SirBlock* ret_block = ir_builder_create_block(builder, "bce_ret");
                    new_entry->is_frameless = true;
                    ret_block->is_frameless = true;
                    
                    // 从链表中摘除 new_entry 和 ret_block
                    SirBlock* curr = func->first_block;
                    while (curr && curr->next) {
                        if (curr->next == new_entry) {
                            curr->next = new_entry->next;
                            if (func->last_block == new_entry) func->last_block = curr;
                        } else if (curr->next == ret_block) {
                            curr->next = ret_block->next;
                            if (func->last_block == ret_block) func->last_block = curr;
                        } else {
                            curr = curr->next;
                        }
                    }
                    
                    // 插入到头部
                    ret_block->next = old_entry;
                    new_entry->next = ret_block;
                    func->first_block = new_entry;
                    
                    // 在 new_entry 中构建：
                    // %param = get_param 0
                    // %cmp = icmp_eq %param, test_val
                    // br %cmp, ret_block, old_entry
                    ir_builder_set_insert_point(builder, new_entry);
                    SirValue* p = ir_get_param(builder, 0, param_val->type);
                    SirValue* cmp = ir_build_binary(builder, SIR_ICMP_EQ, p, ir_const_int(builder, param_val->type, test_val));
                    ir_build_br(builder, cmp, ret_block, old_entry);
                    
                    // 在 ret_block 中构建：
                    // ret res
                    ir_builder_set_insert_point(builder, ret_block);
                    ir_build_ret(builder, ir_const_int(builder, param_val->type, res));
                    
                    bce_changed = true;
                }
            }
        }
        if (bce_changed) {
            for (SirFunction* func = builder->module->first_func; func; func = func->next) {
                prune_dead_blocks(func, func->first_block);
            }
        }
    }

    // 5. O3: Const-Eval VM (Compile-Time Function Execution)
    if (opt_level >= 3) {
        // 第一阶段：绝对纯度鉴定器 (Strict Purity Analysis)
        for (SirFunction* f = builder->module->first_func; f; f = f->next) {
            f->is_pure = (f->first_block != NULL);
        }
        bool purity_changed;
        do {
            purity_changed = false;
            for (SirFunction* f = builder->module->first_func; f; f = f->next) {
                if (!f->is_pure) continue;
                bool pure = true;
                for (SirBlock* b = f->first_block; b && pure; b = b->next) {
                    for (SirInst* i = b->first_inst; i && pure; i = i->next) {
                        if (i->opcode == SIR_CALL) {
                            if (i->operands[0]->kind != SIR_VAL_GLOBAL) { pure = false; break; }
                            SirFunction* callee = NULL;
                            for (SirFunction* cf = builder->module->first_func; cf; cf = cf->next) {
                                if (strcmp(cf->name, i->operands[0]->as.global_name) == 0) { callee = cf; break; }
                            }
                            if (!callee || !callee->is_pure) { pure = false; break; }
                        } else if (i->opcode == SIR_ALLOCA || i->opcode == SIR_LOAD || i->opcode == SIR_STORE || i->opcode == SIR_GEP || i->opcode == SIR_MEMCPY) {
                            pure = false; // 严格模式：禁止任何内存操作
                        } else if ((i->opcode >= SIR_FADD && i->opcode <= SIR_FDIV) || (i->opcode >= SIR_FCMP_EQ && i->opcode <= SIR_FCMP_GE)) {
                            pure = false; // 严格模式：禁止浮点数以避免跨平台精度不一致
                        }
                    }
                }
                if (!pure) {
                    f->is_pure = false;
                    purity_changed = true;
                }
            }
        } while (purity_changed);

        bool vm_changed = false;
        // 第二阶段 & 第三阶段：探针拦截与虚拟机执行
        for (SirFunction* func = builder->module->first_func; func; func = func->next) {
            for (SirBlock* block = func->first_block; block; block = block->next) {
                for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                    if (inst->opcode == SIR_CALL && inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                        SirFunction* callee = NULL;
                        for (SirFunction* cf = builder->module->first_func; cf; cf = cf->next) {
                            if (strcmp(cf->name, inst->operands[0]->as.global_name) == 0) { callee = cf; break; }
                        }
                        if (callee && callee->is_pure) {
                            bool all_const = true;
                            int c_arg_count = inst->num_operands - 1;
                            int64_t c_args[16];
                            if (c_arg_count > 16) all_const = false;
                            for (int i = 0; i < c_arg_count && all_const; i++) {
                                if (inst->operands[i+1]->kind == SIR_VAL_CONST_INT) {
                                    c_args[i] = inst->operands[i+1]->as.int_val;
                                } else if (inst->operands[i+1]->kind == SIR_VAL_CONST_BOOL) {
                                    c_args[i] = inst->operands[i+1]->as.bool_val;
                                } else {
                                    all_const = false;
                                }
                            }

                            if (all_const) {
                                int64_t ret_val;
                                uint64_t global_steps = 0;
                                if (vm_execute(builder->module, callee, c_args, &ret_val, 0, &global_steps)) {
                                    // 第四阶段：物理抹杀 (IR Replacement)
                                    if (inst->dest) {
                                        inst->opcode = SIR_CAST;
                                        inst->num_operands = 1;
                                        inst->operands[0] = ir_const_int(builder, inst->dest->type, ret_val);
                                    } else {
                                        // 无返回值的调用，直接替换为空操作
                                        inst->opcode = SIR_ADD;
                                        inst->num_operands = 2;
                                        inst->operands[0] = ir_const_int(builder, type_get_basic(TY_I32), 0);
                                        inst->operands[1] = ir_const_int(builder, type_get_basic(TY_I32), 0);
                                    }
                                    vm_changed = true;
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // 第五阶段：清理战场 (Local CSE & DCE for newly generated constants)
        if (vm_changed) {
            for (SirFunction* func = builder->module->first_func; func; func = func->next) {
                uint32_t max_vreg = 0;
                for (SirBlock* block = func->first_block; block; block = block->next) {
                    for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                        if (inst->dest && inst->dest->kind == SIR_VAL_VREG && inst->dest->as.vreg > max_vreg) max_vreg = inst->dest->as.vreg;
                    }
                }
                
                bool prop_changed;
                do {
                    prop_changed = false;
                    SirValue** replacements = (SirValue**)calloc(max_vreg + 1, sizeof(SirValue*));
                    for (SirBlock* block = func->first_block; block; block = block->next) {
                        for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                            for (int i = 0; i < inst->num_operands; i++) {
                                if (inst->operands[i] && inst->operands[i]->kind == SIR_VAL_VREG && replacements[inst->operands[i]->as.vreg]) {
                                    inst->operands[i] = replacements[inst->operands[i]->as.vreg];
                                    prop_changed = true;
                                }
                            }
                            if (inst->dest && inst->dest->kind == SIR_VAL_VREG && !replacements[inst->dest->as.vreg]) {
                                SirValue* rep = NULL;
                                if (inst->opcode == SIR_CAST && inst->operands[0]->kind == SIR_VAL_CONST_INT && type_equals(inst->operands[0]->type, inst->dest->type)) {
                                    rep = inst->operands[0];
                                } else if (inst->opcode == SIR_ADD && inst->operands[1]->kind == SIR_VAL_CONST_INT && inst->operands[1]->as.int_val == 0) {
                                    rep = inst->operands[0];
                                }
                                if (rep) replacements[inst->dest->as.vreg] = rep;
                            }
                        }
                    }
                    free(replacements);
                } while (prop_changed);
                
                bool dead_changed;
                do {
                    dead_changed = false;
                    uint32_t* use_counts = (uint32_t*)calloc(max_vreg + 1, sizeof(uint32_t));
                    for (SirBlock* block = func->first_block; block; block = block->next) {
                        for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
                            for (int i = 0; i < inst->num_operands; i++) {
                                if (inst->operands[i] && inst->operands[i]->kind == SIR_VAL_VREG) use_counts[inst->operands[i]->as.vreg]++;
                            }
                        }
                    }
                    for (SirBlock* block = func->first_block; block; block = block->next) {
                        SirInst* inst = block->first_inst;
                        while (inst) {
                            SirInst* next_inst = inst->next;
                            if (inst->dest && inst->dest->kind == SIR_VAL_VREG && is_side_effect_free(inst->opcode)) {
                                if (use_counts[inst->dest->as.vreg] == 0) {
                                    if (inst->prev) inst->prev->next = inst->next; else block->first_inst = inst->next;
                                    if (inst->next) inst->next->prev = inst->prev; else block->last_inst = inst->prev;
                                    dead_changed = true;
                                }
                            }
                            inst = next_inst;
                        }
                    }
                    free(use_counts);
                } while (dead_changed);
            }
        }
    }

    // 6. 全局死代码消除 (Global Dead Code Elimination)
    if (opt_level >= 2) {
        int func_count = 0;
        for (SirFunction* f = builder->module->first_func; f; f = f->next) func_count++;
        
        if (func_count > 0) {
            SirFunction** func_array = (SirFunction**)malloc(sizeof(SirFunction*) * func_count);
            bool* reachable = (bool*)calloc(func_count, sizeof(bool));
            
            int idx = 0;
            for (SirFunction* f = builder->module->first_func; f; f = f->next) {
                func_array[idx++] = f;
            }
            
            for (int i = 0; i < func_count; i++) {
                if (strcmp(func_array[i]->name, "princeps") == 0 || strcmp(func_array[i]->name, "__scoria_init") == 0) {
                    reachable[i] = true;
                }
            }
            
            bool changed;
            do {
                changed = false;
                for (int i = 0; i < func_count; i++) {
                    if (!reachable[i]) continue;
                    
                    SirFunction* f = func_array[i];
                    for (SirBlock* b = f->first_block; b; b = b->next) {
                        for (SirInst* inst = b->first_inst; inst; inst = inst->next) {
                            for (int op = 0; op < inst->num_operands; op++) {
                                if (inst->operands[op] && inst->operands[op]->kind == SIR_VAL_GLOBAL) {
                                    for (int j = 0; j < func_count; j++) {
                                        if (!reachable[j] && strcmp(func_array[j]->name, inst->operands[op]->as.global_name) == 0) {
                                            reachable[j] = true;
                                            changed = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            } while (changed);
            
            SirFunction* prev = NULL;
            SirFunction* curr = builder->module->first_func;
            idx = 0;
            while (curr) {
                if (!reachable[idx]) {
                    if (prev) prev->next = curr->next;
                    else builder->module->first_func = curr->next;
                    if (curr == builder->module->last_func) builder->module->last_func = prev;
                    curr = curr->next;
                } else {
                    prev = curr;
                    curr = curr->next;
                }
                idx++;
            }
            
            free(func_array);
            free(reachable);
        }
    }
}
