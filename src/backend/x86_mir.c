#include "x86_mir.h"
#include "reg_alloc.h"
#include <stdlib.h>
#include <string.h>

// =========================================================
// 辅助函数：操作数与指令构造
// =========================================================

static X86Operand op_reg(X86Reg reg, int size) {
    X86Operand op = {X86_OP_REG, size, {0}};
    op.as.reg = reg;
    return op;
}

static X86Operand op_imm(int64_t imm, int size) {
    X86Operand op = {X86_OP_IMM, size, {0}};
    op.as.imm = imm;
    return op;
}

static X86Operand op_mem_bd(X86Reg base, int32_t disp, int size) {
    X86Operand op = {X86_OP_MEM_BASE_DISP, size, {0}};
    op.as.mem_bd.base = base;
    op.as.mem_bd.disp = disp;
    return op;
}

static X86Operand op_mem_rip(const char* label, int size) {
    X86Operand op = {X86_OP_MEM_RIP, size, {0}};
    op.as.label = label;
    return op;
}

static X86Operand op_label(const char* label) {
    X86Operand op = {X86_OP_LABEL, 0, {0}};
    op.as.label = label;
    return op;
}

static X86Operand op_block(uint32_t block_id) {
    X86Operand op = {X86_OP_BLOCK, 0, {0}};
    op.as.block_id = block_id;
    return op;
}

static X86Inst* create_inst(X86Opcode opcode) {
    X86Inst* inst = (X86Inst*)calloc(1, sizeof(X86Inst));
    inst->opcode = opcode;
    inst->cond = X86_COND_NONE;
    return inst;
}

static void append_inst(X86Block* block, X86Inst* inst) {
    if (!block->first_inst) {
        block->first_inst = inst;
        block->last_inst = inst;
    } else {
        block->last_inst->next = inst;
        block->last_inst = inst;
    }
}

static X86Inst* emit_inst0(X86Block* block, X86Opcode opcode) {
    X86Inst* inst = create_inst(opcode);
    inst->num_ops = 0;
    append_inst(block, inst);
    return inst;
}

static X86Inst* emit_inst1(X86Block* block, X86Opcode opcode, X86Operand op1) {
    X86Inst* inst = create_inst(opcode);
    inst->num_ops = 1;
    inst->ops[0] = op1;
    append_inst(block, inst);
    return inst;
}

static X86Inst* emit_inst2(X86Block* block, X86Opcode opcode, X86Operand op1, X86Operand op2) {
    X86Inst* inst = create_inst(opcode);
    inst->num_ops = 2;
    inst->ops[0] = op1;
    inst->ops[1] = op2;
    append_inst(block, inst);
    return inst;
}

static X86Reg get_phys_reg(int color) {
    X86Reg map[] = {X86_REG_RBX, X86_REG_RSI, X86_REG_RDI, X86_REG_R12, X86_REG_R13, X86_REG_R14, X86_REG_R15, X86_REG_R8, X86_REG_R9, X86_REG_R10, X86_REG_R11};
    if (color >= 0 && color < 11) return map[color];
    return X86_REG_RAX;
}

// 智能操作数加载器 (降级到 MIR)
static X86Reg load_operand_mir(X86Block* block, RegAllocator* alloc, SirValue* val, X86Reg scratch, int frame_size) {
    if (!val) {
        emit_inst2(block, X86_INST_XOR, op_reg(scratch, 8), op_reg(scratch, 8));
        return scratch;
    }
    if (val->kind == SIR_VAL_CONST_INT) {
        emit_inst2(block, X86_INST_MOV, op_reg(scratch, 8), op_imm(val->as.int_val, 8));
        return scratch;
    } else if (val->kind == SIR_VAL_CONST_FLOAT) {
        uint64_t bits = 0;
        if (val->type && val->type->kind == TY_F32) {
            float f = (float)val->as.float_val;
            memcpy(&bits, &f, 4);
            emit_inst2(block, X86_INST_MOV, op_reg(scratch, 4), op_imm(bits, 4));
        } else {
            double d = val->as.float_val;
            memcpy(&bits, &d, 8);
            emit_inst2(block, X86_INST_MOV, op_reg(scratch, 8), op_imm(bits, 8));
        }
        return scratch;
    } else if (val->kind == SIR_VAL_CONST_BOOL) {
        emit_inst2(block, X86_INST_MOV, op_reg(scratch, 4), op_imm(val->as.bool_val ? 1 : 0, 4));
        return scratch;
    } else if (val->kind == SIR_VAL_CONST_STRING) {
        X86Operand op = {X86_OP_STRING, 8, {0}};
        op.as.string.str = val->as.string_val.str;
        op.as.string.len = val->as.string_val.len;
        emit_inst2(block, X86_INST_LEA, op_reg(scratch, 8), op);
        return scratch;
    } else if (val->kind == SIR_VAL_GLOBAL) {
        emit_inst2(block, X86_INST_LEA, op_reg(scratch, 8), op_mem_rip(val->as.global_name, 8));
        return scratch;
    } else if (val->kind == SIR_VAL_VREG) {
        int color = reg_alloc_get_color(alloc, val->as.vreg);
        if (color != -1) return get_phys_reg(color);
        
        int offset = reg_alloc_get_offset(alloc, val->as.vreg, 8);
        int size = val->type ? type_get_size(val->type) : 8;
        if (size == 0 || size > 8) size = 8;
        bool is_signed = val->type ? type_is_signed(val->type) : false;
        
        X86Opcode opc = X86_INST_MOV;
        if (size == 1 || size == 2) opc = is_signed ? X86_INST_MOVSX : X86_INST_MOVZX;
        else if (size == 4 && is_signed) opc = X86_INST_MOVSX;
        
        emit_inst2(block, opc, op_reg(scratch, 8), op_mem_bd(X86_REG_RSP, frame_size + offset, size));
        return scratch;
    }
    return scratch;
}

static void store_result_mir(X86Block* block, RegAllocator* alloc, SirValue* val, X86Reg src, int frame_size) {
    if (!val || val->kind != SIR_VAL_VREG) return;
    int size = (val->type && type_get_size(val->type) <= 4) ? 4 : 8;
    int color = reg_alloc_get_color(alloc, val->as.vreg);
    if (color != -1) {
        X86Reg dst = get_phys_reg(color);
        if (dst != src) emit_inst2(block, X86_INST_MOV, op_reg(dst, size), op_reg(src, size));
    } else {
        int offset = reg_alloc_get_offset(alloc, val->as.vreg, 8);
        emit_inst2(block, X86_INST_MOV, op_mem_bd(X86_REG_RSP, frame_size + offset, size), op_reg(src, size));
    }
}

// MIR 窥孔优化器 (独立于后端的统一优化)
static void mir_peephole_optimize(X86Function* func) {
    for (X86Block* block = func->first_block; block; block = block->next) {
        X86Inst* prev = NULL;
        X86Inst* inst = block->first_inst;
        while (inst) {
            // 模式: SETCC -> MOVZX -> TEST -> JCC NE (分支融合)
            // 将比较指令和跳转指令直接融合，消除中间的布尔值计算
            if (inst->opcode == X86_INST_SETCC) {
                X86Inst* i2 = inst->next;
                if (i2 && i2->opcode == X86_INST_MOVZX && i2->ops[0].as.reg == inst->ops[0].as.reg) {
                    X86Inst* i3 = i2->next;
                    if (i3 && i3->opcode == X86_INST_TEST && i3->ops[0].as.reg == inst->ops[0].as.reg) {
                        X86Inst* i4 = i3->next;
                        if (i4 && i4->opcode == X86_INST_JCC && i4->cond == X86_COND_NE) {
                            i4->cond = inst->cond; // 融合条件
                            if (prev) prev->next = i4;
                            else block->first_inst = i4;
                            free(inst); free(i2); free(i3);
                            inst = i4;
                            continue;
                        }
                    }
                }
            }
            prev = inst;
            inst = inst->next;
        }
    }
}

X86Module* x86_mir_build(SirModule* module, int opt_level) {
    X86Module* mir_mod = (X86Module*)calloc(1, sizeof(X86Module));
    mir_mod->sir_module = module;
    
    for (SirFunction* sfunc = module->first_func; sfunc; sfunc = sfunc->next) {
        X86Function* xfunc = (X86Function*)calloc(1, sizeof(X86Function));
        xfunc->name = sfunc->name;
        xfunc->has_fast_path = sfunc->has_fast_path;
        
        if (!mir_mod->first_func) mir_mod->first_func = xfunc;
        else mir_mod->last_func->next = xfunc;
        mir_mod->last_func = xfunc;

        // 1. 预扫描：计算最大虚拟寄存器、ALLOCA 空间和最大调用参数个数
        uint32_t max_vreg = 0;
        int max_call_args = 0;
        for (SirBlock* sblock = sfunc->first_block; sblock; sblock = sblock->next) {
            for (SirInst* inst = sblock->first_inst; inst; inst = inst->next) {
                if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                    if (inst->dest->as.vreg > max_vreg) max_vreg = inst->dest->as.vreg;
                }
                for (int i=0; i<inst->num_operands; i++) {
                    if (inst->operands[i] && inst->operands[i]->kind == SIR_VAL_VREG) {
                        if (inst->operands[i]->as.vreg > max_vreg) max_vreg = inst->operands[i]->as.vreg;
                    }
                }
                if (inst->opcode == SIR_CALL) {
                    int num_args = inst->num_operands - 1;
                    // SIR_CALL 需要额外的暂存区，总共需要 4 (shadow) + num_args 个 8 字节槽
                    int required_slots = 4 + num_args;
                    if (required_slots > max_call_args) max_call_args = required_slots;
                } else if (inst->opcode == SIR_SYS_WRITE || inst->opcode == SIR_SYS_READ) {
                    // 使用了 48(%rsp) 作为局部指针，总共需要 56 字节 (7 个槽)
                    if (7 > max_call_args) max_call_args = 7;
                } else if (inst->opcode == SIR_SYS_ALLOC || inst->opcode == SIR_SYS_FREE) {
                    // 使用了 32(%rsp) 作为暂存，总共需要 40 字节 (5 个槽)
                    if (5 > max_call_args) max_call_args = 5;
                }
            }
        }

        int local_stack_size = 0;
        int* alloca_offsets = (int*)calloc(max_vreg + 1, sizeof(int));
        for (SirBlock* sblock = sfunc->first_block; sblock; sblock = sblock->next) {
            for (SirInst* inst = sblock->first_inst; inst; inst = inst->next) {
                if (inst->opcode == SIR_ALLOCA) {
                    int alloc_size = (int)inst->operands[0]->as.int_val;
                    alloc_size = (alloc_size + 7) & ~7;
                    local_stack_size += alloc_size;
                    alloca_offsets[inst->dest->as.vreg] = -local_stack_size;
                }
            }
        }

        // 2. 寄存器分配
        RegAllocator allocator;
        reg_alloc_init(&allocator, max_vreg);
        allocator.current_offset = local_stack_size;
        reg_alloc_build_and_color(&allocator, sfunc, opt_level);

        for (uint32_t i = 1; i <= max_vreg; i++) {
            if (reg_alloc_get_color(&allocator, i) == -1) {
                reg_alloc_get_offset(&allocator, i, 8);
            }
        }

        // 动态计算传参空间 (Outgoing Space)
        // 保证至少 32 字节的 Shadow Space，超出 4 个参数的部分每个 8 字节
        int outgoing_space = (max_call_args > 4 ? max_call_args : 4) * 8;
        // 强制 16 字节对齐
        int aligned_outgoing_space = (outgoing_space + 15) & ~15;
        int max_call_area = aligned_outgoing_space;
        int stack_sub_size = allocator.current_offset + max_call_area;
        int num_callee_pushes = 0;
        for (int i = 0; i < 7; i++) {
            if (allocator.used_callee_saved[i]) {
                xfunc->used_callee_saved[i] = true;
                num_callee_pushes++;
            }
        }
        if ((stack_sub_size + num_callee_pushes * 8 + 8) % 16 != 0) {
            stack_sub_size += 8;
        }
        xfunc->frame_size = stack_sub_size;

        // 3. 指令选择 (Instruction Selection)
        X86Block* xblocks[1024] = {0}; // 简化：假设 block id < 1024
        for (SirBlock* sblock = sfunc->first_block; sblock; sblock = sblock->next) {
            X86Block* xblock = (X86Block*)calloc(1, sizeof(X86Block));
            xblock->id = sblock->id;
            xblock->name = sblock->name;
            xblocks[sblock->id] = xblock;
            
            if (!xfunc->first_block) xfunc->first_block = xblock;
            else xfunc->last_block->next = xblock;
            xfunc->last_block = xblock;
        }

        bool prologue_emitted = false;
        for (SirBlock* sblock = sfunc->first_block; sblock; sblock = sblock->next) {
            X86Block* xblock = xblocks[sblock->id];

            if (!sblock->is_frameless && !prologue_emitted) {
                if (xfunc->used_callee_saved[0]) emit_inst1(xblock, X86_INST_PUSH, op_reg(X86_REG_RBX, 8));
                if (xfunc->used_callee_saved[1]) emit_inst1(xblock, X86_INST_PUSH, op_reg(X86_REG_RSI, 8));
                if (xfunc->used_callee_saved[2]) emit_inst1(xblock, X86_INST_PUSH, op_reg(X86_REG_RDI, 8));
                if (xfunc->used_callee_saved[3]) emit_inst1(xblock, X86_INST_PUSH, op_reg(X86_REG_R12, 8));
                if (xfunc->used_callee_saved[4]) emit_inst1(xblock, X86_INST_PUSH, op_reg(X86_REG_R13, 8));
                if (xfunc->used_callee_saved[5]) emit_inst1(xblock, X86_INST_PUSH, op_reg(X86_REG_R14, 8));
                if (xfunc->used_callee_saved[6]) emit_inst1(xblock, X86_INST_PUSH, op_reg(X86_REG_R15, 8));
                if (stack_sub_size > 0) {
                    emit_inst2(xblock, X86_INST_SUB, op_reg(X86_REG_RSP, 8), op_imm(stack_sub_size, 8));
                }
                prologue_emitted = true;
            }

            for (SirInst* inst = sblock->first_inst; inst; inst = inst->next) {
                switch (inst->opcode) {
                    case SIR_ALLOCA: {
                        int offset = alloca_offsets[inst->dest->as.vreg];
                        X86Reg dest_reg = X86_REG_RAX;
                        if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                            int c = reg_alloc_get_color(&allocator, inst->dest->as.vreg);
                            if (c != -1) dest_reg = get_phys_reg(c);
                        }
                        emit_inst2(xblock, X86_INST_LEA, op_reg(dest_reg, 8), op_mem_bd(X86_REG_RSP, xfunc->frame_size + offset, 8));
                        store_result_mir(xblock, &allocator, inst->dest, dest_reg, xfunc->frame_size);
                        break;
                    }
                    case SIR_LOAD: {
                        int size = type_get_size(inst->dest->type);
                        if (size == 0 || size > 8) size = 8;
                        bool is_signed = type_is_signed(inst->dest->type);
                        X86Reg ptr_reg = load_operand_mir(xblock, &allocator, inst->operands[0], X86_REG_RCX, xfunc->frame_size);
                        
                        X86Reg dest_reg = X86_REG_RAX;
                        if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                            int c = reg_alloc_get_color(&allocator, inst->dest->as.vreg);
                            if (c != -1) dest_reg = get_phys_reg(c);
                        }
                        
                        X86Opcode opc = X86_INST_MOV;
                        if (size == 1 || size == 2) opc = is_signed ? X86_INST_MOVSX : X86_INST_MOVZX;
                        else if (size == 4 && is_signed) opc = X86_INST_MOVSX;
                        
                        emit_inst2(xblock, opc, op_reg(dest_reg, size < 4 ? 4 : size), op_mem_bd(ptr_reg, 0, size));
                        store_result_mir(xblock, &allocator, inst->dest, dest_reg, xfunc->frame_size);
                        break;
                    }
                    case SIR_STORE: {
                        int size = 8;
                        if (inst->operands[1]->type && inst->operands[1]->type->kind == TY_VIA) {
                            size = type_get_size(inst->operands[1]->type->as.inner);
                        }
                        if (size == 0 && inst->operands[0]->type) {
                            size = type_get_size(inst->operands[0]->type);
                        }
                        if (size == 0 || size > 8 || inst->operands[0]->kind == SIR_VAL_CONST_STRING || inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                            size = 8;
                        }
                        
                        X86Reg ptr_color = (inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                        X86Reg ptr_phys = (ptr_color != -1) ? get_phys_reg(ptr_color) : -1;
                        X86Reg val_scratch = (ptr_phys == X86_REG_RAX) ? X86_REG_RCX : X86_REG_RAX;
                        
                        X86Reg val_reg = load_operand_mir(xblock, &allocator, inst->operands[0], val_scratch, xfunc->frame_size);
                        X86Reg ptr_scratch = (val_reg == X86_REG_RCX) ? X86_REG_RDX : X86_REG_RCX;
                        if (ptr_scratch == val_scratch) ptr_scratch = (ptr_scratch == X86_REG_RDX) ? X86_REG_R8 : X86_REG_RDX;
                        X86Reg ptr_reg = load_operand_mir(xblock, &allocator, inst->operands[1], ptr_scratch, xfunc->frame_size);
                        
                        emit_inst2(xblock, X86_INST_MOV, op_mem_bd(ptr_reg, 0, size), op_reg(val_reg, size));
                        break;
                    }
                    case SIR_ADD:
                    case SIR_SUB:
                    case SIR_MUL:
                    case SIR_AND:
                    case SIR_OR:
                    case SIR_XOR: {
                        X86Reg dest_reg = X86_REG_RAX;
                        if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                            int c = reg_alloc_get_color(&allocator, inst->dest->as.vreg);
                            if (c != -1) dest_reg = get_phys_reg(c);
                        }
                        int size = (inst->dest && inst->dest->type && type_get_size(inst->dest->type) <= 4) ? 4 : 8;
                        
                        X86Reg right_color = (inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                        X86Reg right_phys = (right_color != -1) ? get_phys_reg(right_color) : -1;
                        
                        X86Reg work_reg = dest_reg;
                        if (right_phys == dest_reg) {
                            work_reg = (dest_reg == X86_REG_RAX) ? X86_REG_RCX : X86_REG_RAX;
                        }
                        
                        X86Reg left = load_operand_mir(xblock, &allocator, inst->operands[0], work_reg, xfunc->frame_size);
                        if (left != work_reg) emit_inst2(xblock, X86_INST_MOV, op_reg(work_reg, size), op_reg(left, size));
                        
                        X86Opcode opc = X86_INST_ADD;
                        if (inst->opcode == SIR_SUB) opc = X86_INST_SUB;
                        else if (inst->opcode == SIR_MUL) opc = X86_INST_IMUL;
                        else if (inst->opcode == SIR_AND) opc = X86_INST_AND;
                        else if (inst->opcode == SIR_OR) opc = X86_INST_OR;
                        else if (inst->opcode == SIR_XOR) opc = X86_INST_XOR;
                        
                        if (inst->operands[1]->kind == SIR_VAL_CONST_INT && 
                            inst->operands[1]->as.int_val >= -2147483648LL && inst->operands[1]->as.int_val <= 2147483647LL) {
                            emit_inst2(xblock, opc, op_reg(work_reg, size), op_imm(inst->operands[1]->as.int_val, size));
                        } else {
                            X86Reg right_scratch = (work_reg == X86_REG_RCX) ? X86_REG_RDX : X86_REG_RCX;
                            X86Reg right = load_operand_mir(xblock, &allocator, inst->operands[1], right_scratch, xfunc->frame_size);
                            emit_inst2(xblock, opc, op_reg(work_reg, size), op_reg(right, size));
                        }
                        
                        if (work_reg != dest_reg) emit_inst2(xblock, X86_INST_MOV, op_reg(dest_reg, size), op_reg(work_reg, size));
                        store_result_mir(xblock, &allocator, inst->dest, dest_reg, xfunc->frame_size);
                        break;
                    }
                    case SIR_DIV:
                    case SIR_MOD: {
                        bool is_unsigned = type_is_unsigned(inst->operands[0]->type);
                        int size = (inst->dest && inst->dest->type && type_get_size(inst->dest->type) <= 4) ? 4 : 8;
                        
                        X86Reg right_color = (inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                        X86Reg right_phys = (right_color != -1) ? get_phys_reg(right_color) : -1;
                        X86Reg left_scratch = (right_phys == X86_REG_RAX) ? X86_REG_R8 : X86_REG_RAX;
                        
                        // 常量除法优化 (降级为极其高效的位移和位与)
                        if (opt_level > 0 && is_unsigned && inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                            uint64_t imm = inst->operands[1]->as.int_val;
                            if (imm > 0 && (imm & (imm - 1)) == 0) { // 判断是否为 2 的幂
                                int log2 = 0;
                                uint64_t temp = imm;
                                while (temp > 1) { log2++; temp >>= 1; }
                                
                                X86Reg left = load_operand_mir(xblock, &allocator, inst->operands[0], left_scratch, xfunc->frame_size);
                                if (left != X86_REG_RAX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RAX, size), op_reg(left, size));
                                
                                if (inst->opcode == SIR_DIV) {
                                    if (log2 > 0) emit_inst2(xblock, X86_INST_SHR, op_reg(X86_REG_RAX, size), op_imm(log2, 1));
                                } else {
                                    if (log2 > 0) {
                                        if ((imm - 1) <= 2147483647LL) {
                                            emit_inst2(xblock, X86_INST_AND, op_reg(X86_REG_RAX, size), op_imm(imm - 1, size));
                                        } else {
                                            X86Reg mask_reg = X86_REG_RCX;
                                            emit_inst2(xblock, X86_INST_MOV, op_reg(mask_reg, size), op_imm(imm - 1, size));
                                            emit_inst2(xblock, X86_INST_AND, op_reg(X86_REG_RAX, size), op_reg(mask_reg, size));
                                        }
                                    }
                                    else emit_inst2(xblock, X86_INST_XOR, op_reg(X86_REG_RAX, size), op_reg(X86_REG_RAX, size));
                                }
                                store_result_mir(xblock, &allocator, inst->dest, X86_REG_RAX, xfunc->frame_size);
                                break;
                            }
                        }
                        
                        X86Reg left = load_operand_mir(xblock, &allocator, inst->operands[0], left_scratch, xfunc->frame_size);
                        X86Reg right_scratch = (left == X86_REG_RCX) ? X86_REG_R9 : X86_REG_RCX;
                        X86Reg right = load_operand_mir(xblock, &allocator, inst->operands[1], right_scratch, xfunc->frame_size);
                        
                        if (left == X86_REG_RCX && right == X86_REG_RAX) {
                            emit_inst2(xblock, X86_INST_XCHG, op_reg(X86_REG_RAX, size), op_reg(X86_REG_RCX, size));
                        } else if (right == X86_REG_RAX) {
                            emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, size), op_reg(right, size));
                            if (left != X86_REG_RAX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RAX, size), op_reg(left, size));
                        } else {
                            if (left != X86_REG_RAX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RAX, size), op_reg(left, size));
                            if (right != X86_REG_RCX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, size), op_reg(right, size));
                        }
                        
                        if (is_unsigned) {
                            emit_inst2(xblock, X86_INST_XOR, op_reg(X86_REG_RDX, size), op_reg(X86_REG_RDX, size));
                            emit_inst1(xblock, X86_INST_DIV, op_reg(X86_REG_RCX, size));
                        } else {
                            emit_inst0(xblock, size == 4 ? X86_INST_CDQ : X86_INST_CQO);
                            emit_inst1(xblock, X86_INST_IDIV, op_reg(X86_REG_RCX, size));
                        }
                        
                        X86Reg res_reg = (inst->opcode == SIR_MOD) ? X86_REG_RDX : X86_REG_RAX;
                        store_result_mir(xblock, &allocator, inst->dest, res_reg, xfunc->frame_size);
                        break;
                    }
                    case SIR_SHL:
                    case SIR_SHR: {
                        bool is_unsigned = type_is_unsigned(inst->operands[0]->type);
                        int size = (inst->dest && inst->dest->type && type_get_size(inst->dest->type) <= 4) ? 4 : 8;
                        
                        X86Reg right_color = (inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                        X86Reg right_phys = (right_color != -1) ? get_phys_reg(right_color) : -1;
                        X86Reg left_scratch = (right_phys == X86_REG_RAX) ? X86_REG_R8 : X86_REG_RAX;
                        
                        X86Reg left = load_operand_mir(xblock, &allocator, inst->operands[0], left_scratch, xfunc->frame_size);
                        X86Reg right_scratch = (left == X86_REG_RCX) ? X86_REG_R9 : X86_REG_RCX;
                        X86Reg right = load_operand_mir(xblock, &allocator, inst->operands[1], right_scratch, xfunc->frame_size);
                        
                        if (left == X86_REG_RCX && right == X86_REG_RAX) {
                            emit_inst2(xblock, X86_INST_XCHG, op_reg(X86_REG_RAX, size), op_reg(X86_REG_RCX, size));
                        } else if (right == X86_REG_RAX) {
                            emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, 1), op_reg(right, 1));
                            if (left != X86_REG_RAX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RAX, size), op_reg(left, size));
                        } else {
                            if (left != X86_REG_RAX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RAX, size), op_reg(left, size));
                            if (right != X86_REG_RCX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, 1), op_reg(right, 1));
                        }
                        
                        X86Opcode opc = (inst->opcode == SIR_SHL) ? X86_INST_SHL : (is_unsigned ? X86_INST_SHR : X86_INST_SAR);
                        emit_inst2(xblock, opc, op_reg(X86_REG_RAX, size), op_reg(X86_REG_RCX, 1));
                        
                        store_result_mir(xblock, &allocator, inst->dest, X86_REG_RAX, xfunc->frame_size);
                        break;
                    }
                    case SIR_ICMP_EQ:
                    case SIR_ICMP_NE:
                    case SIR_ICMP_LT:
                    case SIR_ICMP_LE:
                    case SIR_ICMP_GT:
                    case SIR_ICMP_GE: {
                        bool is_unsigned = type_is_unsigned(inst->operands[0]->type);
                        int size = (inst->operands[0]->type && type_get_size(inst->operands[0]->type) <= 4) ? 4 : 8;
                        
                        X86Reg right_color = (inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                        X86Reg right_phys = (right_color != -1) ? get_phys_reg(right_color) : -1;
                        X86Reg left_scratch = (right_phys == X86_REG_RAX) ? X86_REG_RCX : X86_REG_RAX;
                        
                        X86Reg left = load_operand_mir(xblock, &allocator, inst->operands[0], left_scratch, xfunc->frame_size);
                        
                        if (inst->operands[1]->kind == SIR_VAL_CONST_INT && 
                            inst->operands[1]->as.int_val >= -2147483648LL && inst->operands[1]->as.int_val <= 2147483647LL) {
                            emit_inst2(xblock, X86_INST_CMP, op_reg(left, size), op_imm(inst->operands[1]->as.int_val, size));
                        } else {
                            X86Reg right_scratch = (left == X86_REG_RCX) ? X86_REG_RDX : X86_REG_RCX;
                            X86Reg right = load_operand_mir(xblock, &allocator, inst->operands[1], right_scratch, xfunc->frame_size);
                            emit_inst2(xblock, X86_INST_CMP, op_reg(left, size), op_reg(right, size));
                        }
                        
                        X86Condition cond = X86_COND_NONE;
                        if (inst->opcode == SIR_ICMP_EQ) cond = X86_COND_E;
                        else if (inst->opcode == SIR_ICMP_NE) cond = X86_COND_NE;
                        else if (inst->opcode == SIR_ICMP_LT) cond = is_unsigned ? X86_COND_B : X86_COND_L;
                        else if (inst->opcode == SIR_ICMP_LE) cond = is_unsigned ? X86_COND_BE : X86_COND_LE;
                        else if (inst->opcode == SIR_ICMP_GT) cond = is_unsigned ? X86_COND_A : X86_COND_G;
                        else if (inst->opcode == SIR_ICMP_GE) cond = is_unsigned ? X86_COND_AE : X86_COND_GE;
                        
                        X86Inst* setcc = emit_inst1(xblock, X86_INST_SETCC, op_reg(X86_REG_RAX, 1));
                        setcc->cond = cond;
                        
                        emit_inst2(xblock, X86_INST_MOVZX, op_reg(X86_REG_RAX, 8), op_reg(X86_REG_RAX, 1));
                        store_result_mir(xblock, &allocator, inst->dest, X86_REG_RAX, xfunc->frame_size);
                        break;
                    }
                    case SIR_JMP: {
                        emit_inst1(xblock, X86_INST_JMP, op_block(inst->operands[0]->as.block->id));
                        break;
                    }
                    case SIR_BR: {
                        X86Reg cond = load_operand_mir(xblock, &allocator, inst->operands[0], X86_REG_RAX, xfunc->frame_size);
                        emit_inst2(xblock, X86_INST_TEST, op_reg(cond, 4), op_reg(cond, 4));
                        
                        X86Inst* jcc = emit_inst1(xblock, X86_INST_JCC, op_block(inst->operands[1]->as.block->id));
                        jcc->cond = X86_COND_NE;
                        
                        emit_inst1(xblock, X86_INST_JMP, op_block(inst->operands[2]->as.block->id));
                        break;
                    }
                    case SIR_SELECT: {
                        int dest_reg = X86_REG_RAX;
                        if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                            int c = reg_alloc_get_color(&allocator, inst->dest->as.vreg);
                            if (c != -1) dest_reg = get_phys_reg(c);
                        }
                        
                        int t_scratch = (dest_reg == X86_REG_RCX) ? X86_REG_RDX : X86_REG_RCX;
                        int cond_scratch = (t_scratch == X86_REG_RCX) ? X86_REG_R8 : X86_REG_RCX;
                        if (cond_scratch == dest_reg) cond_scratch = X86_REG_R9;
                        
                        int cond = load_operand_mir(xblock, &allocator, inst->operands[0], cond_scratch, xfunc->frame_size);
                        if (cond == dest_reg || cond == t_scratch) {
                            emit_inst2(xblock, X86_INST_MOV, op_reg(cond_scratch, 4), op_reg(cond, 4));
                            cond = cond_scratch;
                        }
                        
                        int t_color = (inst->operands[1] && inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                        int t_phys = (t_color != -1) ? get_phys_reg(t_color) : -1;
                        int size = (inst->dest && inst->dest->type && type_get_size(inst->dest->type) <= 4) ? 4 : 8;
                        
                        if (t_phys == dest_reg) {
                            int t_val = load_operand_mir(xblock, &allocator, inst->operands[1], t_scratch, xfunc->frame_size);
                            if (t_val != t_scratch) emit_inst2(xblock, X86_INST_MOV, op_reg(t_scratch, size), op_reg(t_val, size));
                            
                            int f_val = load_operand_mir(xblock, &allocator, inst->operands[2], dest_reg, xfunc->frame_size);
                            if (f_val != dest_reg) emit_inst2(xblock, X86_INST_MOV, op_reg(dest_reg, size), op_reg(f_val, size));
                        } else {
                            int f_val = load_operand_mir(xblock, &allocator, inst->operands[2], dest_reg, xfunc->frame_size);
                            if (f_val != dest_reg) emit_inst2(xblock, X86_INST_MOV, op_reg(dest_reg, size), op_reg(f_val, size));
                            
                            int t_val = load_operand_mir(xblock, &allocator, inst->operands[1], t_scratch, xfunc->frame_size);
                            if (t_val != t_scratch) emit_inst2(xblock, X86_INST_MOV, op_reg(t_scratch, size), op_reg(t_val, size));
                        }
                        
                        emit_inst2(xblock, X86_INST_TEST, op_reg(cond, 4), op_reg(cond, 4));
                        
                        X86Inst* cmov = emit_inst2(xblock, X86_INST_CMOVCC, op_reg(dest_reg, size), op_reg(t_scratch, size));
                        cmov->cond = X86_COND_NE;
                        
                        store_result_mir(xblock, &allocator, inst->dest, dest_reg, xfunc->frame_size);
                        break;
                    }
                    case SIR_GET_PARAM: {
                        int param_idx = (int)inst->operands[0]->as.int_val;
                        int max_reg_args = (opt_level >= 2) ? 6 : 4;
                        if (param_idx < max_reg_args) {
                            X86Reg regs[] = {X86_REG_RCX, X86_REG_RDX, X86_REG_R8, X86_REG_R9, X86_REG_R10, X86_REG_R11};
                            X86Reg src_reg = regs[param_idx];
                            bool is_float = (inst->dest->type && (inst->dest->type->kind == TY_F32 || inst->dest->type->kind == TY_F64));
                            if (is_float) {
                                bool is_f32 = (inst->dest->type->kind == TY_F32);
                                emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_RAX, is_f32 ? 4 : 8), op_reg(X86_REG_XMM0 + param_idx, is_f32 ? 4 : 8));
                                store_result_mir(xblock, &allocator, inst->dest, X86_REG_RAX, xfunc->frame_size);
                            } else {
                                store_result_mir(xblock, &allocator, inst->dest, src_reg, xfunc->frame_size);
                            }
                        } else {
                            int offset = 8 + xfunc->frame_size + num_callee_pushes * 8 + (param_idx - max_reg_args) * 8;
                            int size = type_get_size(inst->dest->type);
                            if (size == 0 || size > 8) size = 8;
                            bool is_signed = type_is_signed(inst->dest->type);
                            
                            X86Reg dest_reg = X86_REG_RAX;
                            if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                                int c = reg_alloc_get_color(&allocator, inst->dest->as.vreg);
                                if (c != -1) dest_reg = get_phys_reg(c);
                            }
                            
                            X86Opcode opc = X86_INST_MOV;
                            if (size == 1 || size == 2) opc = is_signed ? X86_INST_MOVSX : X86_INST_MOVZX;
                            else if (size == 4 && is_signed) opc = X86_INST_MOVSX;
                            
                            emit_inst2(xblock, opc, op_reg(dest_reg, size < 4 ? 4 : size), op_mem_bd(X86_REG_RSP, offset, size));
                            store_result_mir(xblock, &allocator, inst->dest, dest_reg, xfunc->frame_size);
                        }
                        break;
                    }
                    case SIR_MEMCPY: {
                        int size = (int)inst->operands[2]->as.int_val;
                        
                        int src_color = (inst->operands[1] && inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                        int src_phys = (src_color != -1) ? get_phys_reg(src_color) : -1;
                        
                        X86Reg dst_scratch = (src_phys == X86_REG_RAX) ? X86_REG_R8 : X86_REG_RAX;
                        X86Reg dst_reg = load_operand_mir(xblock, &allocator, inst->operands[0], dst_scratch, xfunc->frame_size);
                        
                        X86Reg src_scratch = (dst_reg == X86_REG_RDX) ? X86_REG_R9 : X86_REG_RDX;
                        X86Reg src_reg = load_operand_mir(xblock, &allocator, inst->operands[1], src_scratch, xfunc->frame_size);
                        
                        emit_inst1(xblock, X86_INST_PUSH, op_reg(X86_REG_RSI, 8));
                        emit_inst1(xblock, X86_INST_PUSH, op_reg(X86_REG_RDI, 8));
                        emit_inst1(xblock, X86_INST_PUSH, op_reg(X86_REG_RCX, 8));
                        
                        if (dst_reg == X86_REG_RSI && src_reg == X86_REG_RDI) {
                            emit_inst2(xblock, X86_INST_XCHG, op_reg(X86_REG_RDI, 8), op_reg(X86_REG_RSI, 8));
                        } else if (src_reg == X86_REG_RDI) {
                            emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RSI, 8), op_reg(src_reg, 8));
                            if (dst_reg != X86_REG_RDI) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RDI, 8), op_reg(dst_reg, 8));
                        } else {
                            if (dst_reg != X86_REG_RDI) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RDI, 8), op_reg(dst_reg, 8));
                            if (src_reg != X86_REG_RSI) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RSI, 8), op_reg(src_reg, 8));
                        }
                        
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, 4), op_imm(size, 4));
                        emit_inst0(xblock, X86_INST_CLD);
                        emit_inst0(xblock, X86_INST_REP_MOVSB);
                        
                        emit_inst1(xblock, X86_INST_POP, op_reg(X86_REG_RCX, 8));
                        emit_inst1(xblock, X86_INST_POP, op_reg(X86_REG_RDI, 8));
                        emit_inst1(xblock, X86_INST_POP, op_reg(X86_REG_RSI, 8));
                        break;
                    }
                    case SIR_GEP: {
                        X86Reg dest_reg = X86_REG_RAX;
                        if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                            int c = reg_alloc_get_color(&allocator, inst->dest->as.vreg);
                            if (c != -1) dest_reg = get_phys_reg(c);
                        }
                        int element_size = (int)inst->operands[2]->as.int_val;
                        
                        if (inst->operands[1]->kind == SIR_VAL_CONST_INT) {
                            int32_t offset = (int32_t)(inst->operands[1]->as.int_val * element_size);
                            X86Reg ptr = load_operand_mir(xblock, &allocator, inst->operands[0], dest_reg, xfunc->frame_size);
                            if (offset == 0) {
                                if (ptr != dest_reg) emit_inst2(xblock, X86_INST_MOV, op_reg(dest_reg, 8), op_reg(ptr, 8));
                            } else {
                                emit_inst2(xblock, X86_INST_LEA, op_reg(dest_reg, 8), op_mem_bd(ptr, offset, 8));
                            }
                        } else {
                            X86Reg idx_color = (inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                            X86Reg idx_phys = (idx_color != -1) ? get_phys_reg(idx_color) : -1;
                            
                            X86Reg ptr_scratch = (dest_reg == X86_REG_RAX) ? X86_REG_RCX : X86_REG_RAX;
                            if (ptr_scratch == idx_phys) ptr_scratch = (ptr_scratch == X86_REG_RCX) ? X86_REG_RDX : X86_REG_RCX;
                            
                            X86Reg ptr = load_operand_mir(xblock, &allocator, inst->operands[0], ptr_scratch, xfunc->frame_size);
                            
                            X86Reg idx_scratch = (dest_reg == X86_REG_RCX) ? X86_REG_RDX : X86_REG_RCX;
                            if (idx_scratch == ptr) idx_scratch = (idx_scratch == X86_REG_RDX) ? X86_REG_R8 : X86_REG_RDX;
                            X86Reg idx = load_operand_mir(xblock, &allocator, inst->operands[1], idx_scratch, xfunc->frame_size);
                            
                            if (idx == dest_reg && ptr != dest_reg) {
                                emit_inst2(xblock, X86_INST_MOV, op_reg(idx_scratch, 8), op_reg(idx, 8));
                                idx = idx_scratch;
                                emit_inst2(xblock, X86_INST_MOV, op_reg(dest_reg, 8), op_reg(ptr, 8));
                            } else {
                                if (ptr != dest_reg) emit_inst2(xblock, X86_INST_MOV, op_reg(dest_reg, 8), op_reg(ptr, 8));
                                if (idx != idx_scratch) emit_inst2(xblock, X86_INST_MOV, op_reg(idx_scratch, 8), op_reg(idx, 8));
                                idx = idx_scratch;
                            }
                            
                            if (element_size == 1) {
                                emit_inst2(xblock, X86_INST_ADD, op_reg(dest_reg, 8), op_reg(idx, 8));
                            } else if (element_size == 2 || element_size == 4 || element_size == 8) {
                                int scale = (element_size == 2) ? 1 : (element_size == 4) ? 2 : 3;
                                X86Operand sib = {X86_OP_MEM_SIB, 8, {0}};
                                sib.as.mem_sib.base = dest_reg;
                                sib.as.mem_sib.index = idx;
                                sib.as.mem_sib.scale = scale;
                                sib.as.mem_sib.disp = 0;
                                emit_inst2(xblock, X86_INST_LEA, op_reg(dest_reg, 8), sib);
                            } else {
                                emit_inst2(xblock, X86_INST_IMUL, op_reg(idx, 8), op_imm(element_size, 8));
                                emit_inst2(xblock, X86_INST_ADD, op_reg(dest_reg, 8), op_reg(idx, 8));
                            }
                        }
                        store_result_mir(xblock, &allocator, inst->dest, dest_reg, xfunc->frame_size);
                        break;
                    }
                    case SIR_CAST: {
                        ScoriaType* src_type = inst->operands[0]->type;
                        ScoriaType* dst_type = inst->dest->type;
                        bool src_is_float = (src_type && (src_type->kind == TY_F32 || src_type->kind == TY_F64));
                        bool dst_is_float = (dst_type && (dst_type->kind == TY_F32 || dst_type->kind == TY_F64));

                        X86Reg src = load_operand_mir(xblock, &allocator, inst->operands[0], X86_REG_RAX, xfunc->frame_size);
                        if (src != X86_REG_RAX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RAX, 8), op_reg(src, 8));
                        
                        int src_size = type_get_size(src_type);
                        if (src_size == 0 || src_size > 8) src_size = 8;
                        if (src_size < 8 && !src_is_float) {
                            if (type_is_signed(src_type)) {
                                X86Opcode opc = (src_size == 4) ? X86_INST_MOVSX : X86_INST_MOVSX;
                                emit_inst2(xblock, opc, op_reg(X86_REG_RAX, 8), op_reg(X86_REG_RAX, src_size));
                            } else {
                                X86Opcode opc = (src_size == 4) ? X86_INST_MOV : X86_INST_MOVZX;
                                emit_inst2(xblock, opc, op_reg(X86_REG_RAX, src_size == 4 ? 4 : 8), op_reg(X86_REG_RAX, src_size));
                            }
                        }

                        if (src_is_float && !dst_is_float) {
                            bool is_f32 = (src_type->kind == TY_F32);
                            emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_XMM0, is_f32 ? 4 : 8), op_reg(X86_REG_RAX, is_f32 ? 4 : 8));
                            emit_inst2(xblock, is_f32 ? X86_INST_CVTTSS2SI : X86_INST_CVTTSD2SI, op_reg(X86_REG_RAX, 8), op_reg(X86_REG_XMM0, is_f32 ? 4 : 8));
                        } else if (!src_is_float && dst_is_float) {
                            bool is_f32 = (dst_type->kind == TY_F32);
                            emit_inst2(xblock, is_f32 ? X86_INST_CVTSI2SS : X86_INST_CVTSI2SD, op_reg(X86_REG_XMM0, is_f32 ? 4 : 8), op_reg(X86_REG_RAX, 8));
                            emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_RAX, is_f32 ? 4 : 8), op_reg(X86_REG_XMM0, is_f32 ? 4 : 8));
                        } else if (src_is_float && dst_is_float && src_type->kind != dst_type->kind) {
                            bool src_is_f32 = (src_type->kind == TY_F32);
                            emit_inst2(xblock, src_is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_XMM0, src_is_f32 ? 4 : 8), op_reg(X86_REG_RAX, src_is_f32 ? 4 : 8));
                            emit_inst2(xblock, src_is_f32 ? X86_INST_CVTSS2SD : X86_INST_CVTSD2SS, op_reg(X86_REG_XMM0, src_is_f32 ? 8 : 4), op_reg(X86_REG_XMM0, src_is_f32 ? 4 : 8));
                            emit_inst2(xblock, src_is_f32 ? X86_INST_MOVQ : X86_INST_MOVD, op_reg(X86_REG_RAX, src_is_f32 ? 8 : 4), op_reg(X86_REG_XMM0, src_is_f32 ? 8 : 4));
                        } else {
                            int dst_size = type_get_size(dst_type);
                            if (dst_size == 0 || dst_size > 8) dst_size = 8;
                            if (dst_size < 8) {
                                if (type_is_signed(dst_type)) {
                                    X86Opcode opc = (dst_size == 4) ? X86_INST_MOVSX : X86_INST_MOVSX;
                                    emit_inst2(xblock, opc, op_reg(X86_REG_RAX, 8), op_reg(X86_REG_RAX, dst_size));
                                } else {
                                    X86Opcode opc = (dst_size == 4) ? X86_INST_MOV : X86_INST_MOVZX;
                                    emit_inst2(xblock, opc, op_reg(X86_REG_RAX, dst_size == 4 ? 4 : 8), op_reg(X86_REG_RAX, dst_size));
                                }
                            }
                        }
                        
                        store_result_mir(xblock, &allocator, inst->dest, X86_REG_RAX, xfunc->frame_size);
                        break;
                    }
                    case SIR_FADD:
                    case SIR_FSUB:
                    case SIR_FMUL:
                    case SIR_FDIV: {
                        bool is_f32 = (inst->operands[0]->type && inst->operands[0]->type->kind == TY_F32);
                        X86Reg dest_reg = X86_REG_RAX;
                        if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                            int c = reg_alloc_get_color(&allocator, inst->dest->as.vreg);
                            if (c != -1) dest_reg = get_phys_reg(c);
                        }
                        
                        X86Reg right_color = (inst->operands[1] && inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                        X86Reg right_phys = (right_color != -1) ? get_phys_reg(right_color) : -1;
                        X86Reg left_scratch = (right_phys == dest_reg) ? ((dest_reg == X86_REG_RAX) ? X86_REG_RCX : X86_REG_RAX) : dest_reg;
                        
                        X86Reg left = load_operand_mir(xblock, &allocator, inst->operands[0], left_scratch, xfunc->frame_size);
                        
                        X86Reg right_scratch = (dest_reg == X86_REG_RCX) ? X86_REG_RDX : X86_REG_RCX;
                        if (right_scratch == left) right_scratch = (right_scratch == X86_REG_RDX) ? X86_REG_R8 : X86_REG_RDX;
                        X86Reg right = load_operand_mir(xblock, &allocator, inst->operands[1], right_scratch, xfunc->frame_size);
                        
                        if (right == dest_reg && left != dest_reg) {
                            emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_XMM0, is_f32 ? 4 : 8), op_reg(left, is_f32 ? 4 : 8));
                            emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_XMM1, is_f32 ? 4 : 8), op_reg(right, is_f32 ? 4 : 8));
                        } else {
                            if (left != dest_reg) emit_inst2(xblock, X86_INST_MOV, op_reg(dest_reg, 8), op_reg(left, 8));
                            if (right != right_scratch) emit_inst2(xblock, X86_INST_MOV, op_reg(right_scratch, 8), op_reg(right, 8));
                            
                            emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_XMM0, is_f32 ? 4 : 8), op_reg(dest_reg, is_f32 ? 4 : 8));
                            emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_XMM1, is_f32 ? 4 : 8), op_reg(right_scratch, is_f32 ? 4 : 8));
                        }
                        
                        X86Opcode opc = X86_INST_ADDSS;
                        if (inst->opcode == SIR_FADD) opc = is_f32 ? X86_INST_ADDSS : X86_INST_ADDSD;
                        else if (inst->opcode == SIR_FSUB) opc = is_f32 ? X86_INST_SUBSS : X86_INST_SUBSD;
                        else if (inst->opcode == SIR_FMUL) opc = is_f32 ? X86_INST_MULSS : X86_INST_MULSD;
                        else if (inst->opcode == SIR_FDIV) opc = is_f32 ? X86_INST_DIVSS : X86_INST_DIVSD;
                        
                        emit_inst2(xblock, opc, op_reg(X86_REG_XMM0, is_f32 ? 4 : 8), op_reg(X86_REG_XMM1, is_f32 ? 4 : 8));
                        emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(dest_reg, is_f32 ? 4 : 8), op_reg(X86_REG_XMM0, is_f32 ? 4 : 8));
                        
                        store_result_mir(xblock, &allocator, inst->dest, dest_reg, xfunc->frame_size);
                        break;
                    }
                    case SIR_FCMP_EQ:
                    case SIR_FCMP_NE:
                    case SIR_FCMP_LT:
                    case SIR_FCMP_LE:
                    case SIR_FCMP_GT:
                    case SIR_FCMP_GE: {
                        bool is_f32 = (inst->operands[0]->type && inst->operands[0]->type->kind == TY_F32);
                        
                        X86Reg right_color = (inst->operands[1] && inst->operands[1]->kind == SIR_VAL_VREG) ? reg_alloc_get_color(&allocator, inst->operands[1]->as.vreg) : -1;
                        X86Reg right_phys = (right_color != -1) ? get_phys_reg(right_color) : -1;
                        
                        X86Reg left_scratch = (right_phys == X86_REG_RAX) ? X86_REG_RCX : X86_REG_RAX;
                        X86Reg left = load_operand_mir(xblock, &allocator, inst->operands[0], left_scratch, xfunc->frame_size);
                        
                        X86Reg right_scratch = (left == X86_REG_RCX) ? X86_REG_RDX : X86_REG_RCX;
                        X86Reg right = load_operand_mir(xblock, &allocator, inst->operands[1], right_scratch, xfunc->frame_size);
                        
                        if (left == X86_REG_RCX && right == X86_REG_RAX) {
                            emit_inst2(xblock, X86_INST_XCHG, op_reg(X86_REG_RAX, 8), op_reg(X86_REG_RCX, 8));
                        } else if (right == X86_REG_RAX) {
                            emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, 8), op_reg(right, 8));
                            emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RAX, 8), op_reg(left, 8));
                        } else {
                            if (left != X86_REG_RAX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RAX, 8), op_reg(left, 8));
                            if (right != X86_REG_RCX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, 8), op_reg(right, 8));
                        }
                        
                        emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_XMM0, is_f32 ? 4 : 8), op_reg(X86_REG_RAX, is_f32 ? 4 : 8));
                        emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_XMM1, is_f32 ? 4 : 8), op_reg(X86_REG_RCX, is_f32 ? 4 : 8));
                        
                        emit_inst2(xblock, is_f32 ? X86_INST_UCOMISS : X86_INST_UCOMISD, op_reg(X86_REG_XMM0, is_f32 ? 4 : 8), op_reg(X86_REG_XMM1, is_f32 ? 4 : 8));
                        
                        X86Condition cond = X86_COND_NONE;
                        if (inst->opcode == SIR_FCMP_EQ) cond = X86_COND_E;
                        else if (inst->opcode == SIR_FCMP_NE) cond = X86_COND_NE;
                        else if (inst->opcode == SIR_FCMP_LT) cond = X86_COND_B;
                        else if (inst->opcode == SIR_FCMP_LE) cond = X86_COND_BE;
                        else if (inst->opcode == SIR_FCMP_GT) cond = X86_COND_A;
                        else if (inst->opcode == SIR_FCMP_GE) cond = X86_COND_AE;
                        
                        X86Inst* setcc = emit_inst1(xblock, X86_INST_SETCC, op_reg(X86_REG_RAX, 1));
                        setcc->cond = cond;
                        
                        emit_inst2(xblock, X86_INST_MOVZX, op_reg(X86_REG_RAX, 8), op_reg(X86_REG_RAX, 1));
                        store_result_mir(xblock, &allocator, inst->dest, X86_REG_RAX, xfunc->frame_size);
                        break;
                    }
                    case SIR_SWITCH: {
                        X86Reg cond_reg = load_operand_mir(xblock, &allocator, inst->operands[0], X86_REG_RAX, xfunc->frame_size);
                        if (cond_reg != X86_REG_RAX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RAX, 8), op_reg(cond_reg, 8));

                        int case_count = (inst->num_operands - 2) / 2;
                        SirBlock* def_block = inst->operands[1]->as.block;

                        for (int i = 0; i < case_count; i++) {
                            if (inst->operands[2 + i * 2]->kind == SIR_VAL_CONST_INT && 
                                inst->operands[2 + i * 2]->as.int_val >= -2147483648LL && inst->operands[2 + i * 2]->as.int_val <= 2147483647LL) {
                                emit_inst2(xblock, X86_INST_CMP, op_reg(X86_REG_RAX, 8), op_imm(inst->operands[2 + i * 2]->as.int_val, 8));
                            } else {
                                X86Reg val_reg = load_operand_mir(xblock, &allocator, inst->operands[2 + i * 2], X86_REG_RCX, xfunc->frame_size);
                                emit_inst2(xblock, X86_INST_CMP, op_reg(X86_REG_RAX, 8), op_reg(val_reg, 8));
                            }

                            SirBlock* target = inst->operands[2 + i * 2 + 1]->as.block;
                            X86Inst* jcc = emit_inst1(xblock, X86_INST_JCC, op_block(target->id));
                            jcc->cond = X86_COND_E;
                        }
                        emit_inst1(xblock, X86_INST_JMP, op_block(def_block->id));
                        break;
                    }
                    case SIR_TRAP: {
                        emit_inst0(xblock, X86_INST_UD2);
                        break;
                    }
                    case SIR_SYS_ALLOC: {
                        X86Reg size_reg = load_operand_mir(xblock, &allocator, inst->operands[0], X86_REG_RAX, xfunc->frame_size);
                        emit_inst2(xblock, X86_INST_MOV, op_mem_bd(X86_REG_RSP, 32, 8), op_reg(size_reg, 8));
                        emit_inst1(xblock, X86_INST_CALL, op_mem_rip("GetProcessHeap", 8));
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, 8), op_reg(X86_REG_RAX, 8));
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RDX, 8), op_imm(8, 8));
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_R8, 8), op_mem_bd(X86_REG_RSP, 32, 8));
                        emit_inst1(xblock, X86_INST_CALL, op_mem_rip("HeapAlloc", 8));
                        store_result_mir(xblock, &allocator, inst->dest, X86_REG_RAX, xfunc->frame_size);
                        break;
                    }
                    case SIR_SYS_FREE: {
                        X86Reg ptr_reg = load_operand_mir(xblock, &allocator, inst->operands[0], X86_REG_RAX, xfunc->frame_size);
                        emit_inst2(xblock, X86_INST_MOV, op_mem_bd(X86_REG_RSP, 32, 8), op_reg(ptr_reg, 8));
                        emit_inst1(xblock, X86_INST_CALL, op_mem_rip("GetProcessHeap", 8));
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, 8), op_reg(X86_REG_RAX, 8));
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RDX, 8), op_imm(0, 8));
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_R8, 8), op_mem_bd(X86_REG_RSP, 32, 8));
                        emit_inst1(xblock, X86_INST_CALL, op_mem_rip("HeapFree", 8));
                        break;
                    }
                    case SIR_SYS_WRITE: {
                        X86Reg buf_reg = load_operand_mir(xblock, &allocator, inst->operands[1], X86_REG_RAX, xfunc->frame_size);
                        emit_inst2(xblock, X86_INST_MOV, op_mem_bd(X86_REG_RSP, 32, 8), op_reg(buf_reg, 8));
                        X86Reg len_reg = load_operand_mir(xblock, &allocator, inst->operands[2], X86_REG_R10, xfunc->frame_size);
                        emit_inst2(xblock, X86_INST_MOV, op_mem_bd(X86_REG_RSP, 40, 8), op_reg(len_reg, 8));
                        
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, 4), op_imm(-11, 4));
                        emit_inst1(xblock, X86_INST_CALL, op_mem_rip("GetStdHandle", 8));
                        
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, 8), op_reg(X86_REG_RAX, 8));
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RDX, 8), op_mem_bd(X86_REG_RSP, 32, 8));
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_R8, 4), op_mem_bd(X86_REG_RSP, 40, 4));
                        emit_inst2(xblock, X86_INST_LEA, op_reg(X86_REG_R9, 8), op_mem_bd(X86_REG_RSP, 48, 8));
                        emit_inst2(xblock, X86_INST_MOV, op_mem_bd(X86_REG_RSP, 32, 8), op_imm(0, 8));
                        emit_inst1(xblock, X86_INST_CALL, op_mem_rip("WriteFile", 8));
                        
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RAX, 4), op_mem_bd(X86_REG_RSP, 48, 4));
                        store_result_mir(xblock, &allocator, inst->dest, X86_REG_RAX, xfunc->frame_size);
                        break;
                    }
                    case SIR_SYS_READ: {
                        X86Reg buf_reg = load_operand_mir(xblock, &allocator, inst->operands[1], X86_REG_RAX, xfunc->frame_size);
                        emit_inst2(xblock, X86_INST_MOV, op_mem_bd(X86_REG_RSP, 32, 8), op_reg(buf_reg, 8));
                        X86Reg len_reg = load_operand_mir(xblock, &allocator, inst->operands[2], X86_REG_R10, xfunc->frame_size);
                        emit_inst2(xblock, X86_INST_MOV, op_mem_bd(X86_REG_RSP, 40, 8), op_reg(len_reg, 8));
                        
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, 4), op_imm(-10, 4));
                        emit_inst1(xblock, X86_INST_CALL, op_mem_rip("GetStdHandle", 8));
                        
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, 8), op_reg(X86_REG_RAX, 8));
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RDX, 8), op_mem_bd(X86_REG_RSP, 32, 8));
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_R8, 4), op_mem_bd(X86_REG_RSP, 40, 4));
                        emit_inst2(xblock, X86_INST_MOV, op_mem_bd(X86_REG_RSP, 48, 4), op_imm(0, 4));
                        emit_inst2(xblock, X86_INST_LEA, op_reg(X86_REG_R9, 8), op_mem_bd(X86_REG_RSP, 48, 8));
                        emit_inst2(xblock, X86_INST_MOV, op_mem_bd(X86_REG_RSP, 32, 8), op_imm(0, 8));
                        emit_inst1(xblock, X86_INST_CALL, op_mem_rip("ReadFile", 8));
                        
                        emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RAX, 4), op_mem_bd(X86_REG_RSP, 48, 4));
                        store_result_mir(xblock, &allocator, inst->dest, X86_REG_RAX, xfunc->frame_size);
                        break;
                    }
                    case SIR_SYS_EXIT: {
                        X86Reg code_reg = load_operand_mir(xblock, &allocator, inst->operands[0], X86_REG_RCX, xfunc->frame_size);
                        if (code_reg != X86_REG_RCX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, 4), op_reg(code_reg, 4));
                        emit_inst1(xblock, X86_INST_CALL, op_mem_rip("ExitProcess", 8));
                        break;
                    }
                    case SIR_CALL: {
                        bool is_extern = false;
                        if (inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                            for (SirExternFunc* ext = module->first_extern; ext; ext = ext->next) {
                                if (strcmp(ext->name, inst->operands[0]->as.global_name) == 0) {
                                    is_extern = true; break;
                                }
                            }
                        }

                        int num_args = inst->num_operands - 1;
                        int max_reg_args = (is_extern || opt_level < 2) ? 4 : 6;
                        int reg_args = num_args > max_reg_args ? max_reg_args : num_args;
                        X86Reg arg_regs_ext[] = {X86_REG_RCX, X86_REG_RDX, X86_REG_R8, X86_REG_R9};
                        X86Reg arg_regs_int[] = {X86_REG_RCX, X86_REG_RDX, X86_REG_R8, X86_REG_R9, X86_REG_R10, X86_REG_R11};
                        X86Reg* arg_regs = is_extern ? arg_regs_ext : arg_regs_int;

                        int stack_args = num_args > max_reg_args ? num_args - max_reg_args : 0;
                        int scratch_base = 32 + stack_args * 8;

                        for (int i = num_args - 1; i >= max_reg_args; i--) {
                            X86Reg val = load_operand_mir(xblock, &allocator, inst->operands[i+1], X86_REG_RAX, xfunc->frame_size);
                            int offset = ((is_extern || opt_level < 2) ? 32 : 0) + (i - max_reg_args) * 8;
                            emit_inst2(xblock, X86_INST_MOV, op_mem_bd(X86_REG_RSP, offset, 8), op_reg(val, 8));
                        }
                        
                        if (reg_args == 1) {
                            X86Reg val = load_operand_mir(xblock, &allocator, inst->operands[1], X86_REG_RAX, xfunc->frame_size);
                            int w = (inst->operands[1]->type && type_get_size(inst->operands[1]->type) <= 4) ? 4 : 8;
                            if (val != X86_REG_RCX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, w), op_reg(val, w));
                            
                            bool is_float = (inst->operands[1]->type && (inst->operands[1]->type->kind == TY_F32 || inst->operands[1]->type->kind == TY_F64));
                            if (is_float) {
                                bool is_f32 = (inst->operands[1]->type->kind == TY_F32);
                                emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_XMM0, is_f32 ? 4 : 8), op_reg(X86_REG_RCX, is_f32 ? 4 : 8));
                            }
                        } else if (reg_args == 2) {
                            X86Reg val0 = load_operand_mir(xblock, &allocator, inst->operands[1], X86_REG_RAX, xfunc->frame_size);
                            X86Reg val1_scratch = (val0 == X86_REG_R10) ? X86_REG_R11 : X86_REG_R10;
                            X86Reg val1 = load_operand_mir(xblock, &allocator, inst->operands[2], val1_scratch, xfunc->frame_size);
                            
                            if (val0 == X86_REG_RDX && val1 == X86_REG_RCX) {
                                emit_inst2(xblock, X86_INST_XCHG, op_reg(X86_REG_RCX, 8), op_reg(X86_REG_RDX, 8));
                            } else if (val1 == X86_REG_RCX) {
                                if (val1 != X86_REG_RDX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RDX, 8), op_reg(val1, 8));
                                if (val0 != X86_REG_RCX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, 8), op_reg(val0, 8));
                            } else {
                                if (val0 != X86_REG_RCX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RCX, 8), op_reg(val0, 8));
                                if (val1 != X86_REG_RDX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RDX, 8), op_reg(val1, 8));
                            }
                            
                            for (int i = 0; i < 2; i++) {
                                bool is_float = (inst->operands[i+1]->type && (inst->operands[i+1]->type->kind == TY_F32 || inst->operands[i+1]->type->kind == TY_F64));
                                if (is_float) {
                                    bool is_f32 = (inst->operands[i+1]->type->kind == TY_F32);
                                    emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_XMM0 + i, is_f32 ? 4 : 8), op_reg(arg_regs[i], is_f32 ? 4 : 8));
                                }
                            }
                        } else {
                            for (int i = 0; i < reg_args; i++) {
                                X86Reg val = load_operand_mir(xblock, &allocator, inst->operands[i+1], X86_REG_RAX, xfunc->frame_size);
                                emit_inst2(xblock, X86_INST_MOV, op_mem_bd(X86_REG_RSP, scratch_base + i * 8, 8), op_reg(val, 8));
                            }
                            for (int i = 0; i < reg_args; i++) {
                                emit_inst2(xblock, X86_INST_MOV, op_reg(arg_regs[i], 8), op_mem_bd(X86_REG_RSP, scratch_base + i * 8, 8));
                                
                                bool is_float = (inst->operands[i+1]->type && (inst->operands[i+1]->type->kind == TY_F32 || inst->operands[i+1]->type->kind == TY_F64));
                                if (is_float) {
                                    bool is_f32 = (inst->operands[i+1]->type->kind == TY_F32);
                                    emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_XMM0 + i, is_f32 ? 4 : 8), op_reg(arg_regs[i], is_f32 ? 4 : 8));
                                }
                            }
                        }

                        if (is_extern) {
                            emit_inst2(xblock, X86_INST_XOR, op_reg(X86_REG_RAX, 8), op_reg(X86_REG_RAX, 8));
                        }
                        
                        if (inst->operands[0]->kind == SIR_VAL_GLOBAL) {
                            if (is_extern) {
                                emit_inst1(xblock, X86_INST_CALL, op_mem_rip(inst->operands[0]->as.global_name, 8));
                            } else {
                                emit_inst1(xblock, X86_INST_CALL, op_label(inst->operands[0]->as.global_name));
                            }
                        } else {
                            X86Reg callee_reg = load_operand_mir(xblock, &allocator, inst->operands[0], X86_REG_R10, xfunc->frame_size);
                            emit_inst1(xblock, X86_INST_CALL, op_reg(callee_reg, 8));
                        }
                        
                        if (inst->dest) {
                            bool ret_is_float = (inst->dest->type && (inst->dest->type->kind == TY_F32 || inst->dest->type->kind == TY_F64));
                            if (ret_is_float) {
                                bool is_f32 = (inst->dest->type->kind == TY_F32);
                                emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_RAX, is_f32 ? 4 : 8), op_reg(X86_REG_XMM0, is_f32 ? 4 : 8));
                            }
                            store_result_mir(xblock, &allocator, inst->dest, X86_REG_RAX, xfunc->frame_size);
                        }
                        break;
                    }
                    case SIR_RET: {
                        if (inst->num_operands > 0) {
                            bool is_float = (inst->operands[0]->type && (inst->operands[0]->type->kind == TY_F32 || inst->operands[0]->type->kind == TY_F64));
                            X86Reg val = load_operand_mir(xblock, &allocator, inst->operands[0], X86_REG_RAX, xfunc->frame_size);
                            if (is_float) {
                                bool is_f32 = (inst->operands[0]->type->kind == TY_F32);
                                emit_inst2(xblock, is_f32 ? X86_INST_MOVD : X86_INST_MOVQ, op_reg(X86_REG_XMM0, is_f32 ? 4 : 8), op_reg(val, is_f32 ? 4 : 8));
                            } else {
                                if (val != X86_REG_RAX) emit_inst2(xblock, X86_INST_MOV, op_reg(X86_REG_RAX, 8), op_reg(val, 8));
                            }
                        }
                        if (!sblock->is_frameless) {
                            if (stack_sub_size > 0) emit_inst2(xblock, X86_INST_ADD, op_reg(X86_REG_RSP, 8), op_imm(stack_sub_size, 8));
                            if (xfunc->used_callee_saved[6]) emit_inst1(xblock, X86_INST_POP, op_reg(X86_REG_R15, 8));
                            if (xfunc->used_callee_saved[5]) emit_inst1(xblock, X86_INST_POP, op_reg(X86_REG_R14, 8));
                            if (xfunc->used_callee_saved[4]) emit_inst1(xblock, X86_INST_POP, op_reg(X86_REG_R13, 8));
                            if (xfunc->used_callee_saved[3]) emit_inst1(xblock, X86_INST_POP, op_reg(X86_REG_R12, 8));
                            if (xfunc->used_callee_saved[2]) emit_inst1(xblock, X86_INST_POP, op_reg(X86_REG_RDI, 8));
                            if (xfunc->used_callee_saved[1]) emit_inst1(xblock, X86_INST_POP, op_reg(X86_REG_RSI, 8));
                            if (xfunc->used_callee_saved[0]) emit_inst1(xblock, X86_INST_POP, op_reg(X86_REG_RBX, 8));
                        }
                        emit_inst0(xblock, X86_INST_RET);
                        break;
                    }
                    // TODO: 其他指令的降级逻辑将在后续步骤中逐步迁移
                    default:
                        // 占位符，防止编译失败
                        break;
                }
            }
        }
        
        if (opt_level > 0) {
            mir_peephole_optimize(xfunc);
        }
        
        free(alloca_offsets);
        reg_alloc_free(&allocator);
    }
    
    return mir_mod;
}

void x86_mir_free(X86Module* module) {
    if (!module) return;
    X86Function* func = module->first_func;
    while (func) {
        X86Block* block = func->first_block;
        while (block) {
            X86Inst* inst = block->first_inst;
            while (inst) {
                X86Inst* next_inst = inst->next;
                free(inst);
                inst = next_inst;
            }
            X86Block* next_block = block->next;
            free(block);
            block = next_block;
        }
        X86Function* next_func = func->next;
        free(func);
        func = next_func;
    }
    free(module);
}
