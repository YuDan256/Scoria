#include "reg_alloc.h"
#include <stdlib.h>
#include <stdio.h>

void reg_alloc_init(RegAllocator* allocator, uint32_t max_vreg) {
    allocator->current_offset = 0;
    allocator->max_vreg = max_vreg;
    allocator->vreg_offsets = (int*)calloc(max_vreg + 1, sizeof(int));
    allocator->vreg_colors = (int*)calloc(max_vreg + 1, sizeof(int));
    allocator->adj_matrix = (bool*)calloc((max_vreg + 1) * (max_vreg + 1), sizeof(bool));
    allocator->degree = (int*)calloc(max_vreg + 1, sizeof(int));
    allocator->use_count = (int*)calloc(max_vreg + 1, sizeof(int));
    allocator->crosses_call = (bool*)calloc(max_vreg + 1, sizeof(bool));

    if (!allocator->vreg_offsets || !allocator->vreg_colors || !allocator->crosses_call ||
        !allocator->adj_matrix || !allocator->degree || !allocator->use_count) {
        fprintf(stderr, "Clades fatalis: Memoria non sufficit in reg_alloc_init.\n");
        exit(1);
    }

    for (uint32_t i = 0; i <= max_vreg; i++) allocator->vreg_colors[i] = -1;
    for (int i = 0; i < NUM_PHYS_REGS; i++) allocator->used_callee_saved[i] = false;
}

void reg_alloc_free(RegAllocator* allocator) {
    free(allocator->vreg_offsets);
    free(allocator->vreg_colors);
    free(allocator->adj_matrix);
    free(allocator->degree);
    free(allocator->use_count);
    free(allocator->crosses_call);
}

typedef struct {
    uint32_t* uses;
    int use_count;
    uint32_t* defs;
    int def_count;
    bool* live_in;
    bool* live_out;
} BlockLiveness;

void reg_alloc_build_and_color(RegAllocator* allocator, SirFunction* func, int opt_level) {
    if (opt_level == 0) return; // -O0: 禁用寄存器分配，全部溢出到栈内存以支持调试

    uint32_t max_block_id = 0;
    for (SirBlock* block = func->first_block; block; block = block->next) {
        if (block->id > max_block_id) max_block_id = block->id;
    }

    BlockLiveness* liveness = (BlockLiveness*)calloc(max_block_id + 1, sizeof(BlockLiveness));
    for (uint32_t i = 0; i <= max_block_id; i++) {
        liveness[i].live_in = (bool*)calloc(allocator->max_vreg + 1, sizeof(bool));
        liveness[i].live_out = (bool*)calloc(allocator->max_vreg + 1, sizeof(bool));
        liveness[i].uses = (uint32_t*)calloc(allocator->max_vreg + 1, sizeof(uint32_t));
        liveness[i].defs = (uint32_t*)calloc(allocator->max_vreg + 1, sizeof(uint32_t));
    }

    // 1. 计算每个块的 Use 和 Def
    for (SirBlock* block = func->first_block; block; block = block->next) {
        BlockLiveness* bl = &liveness[block->id];
        for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
            for (int i = 0; i < inst->num_operands; i++) {
                SirValue* val = inst->operands[i];
                if (val && val->kind == SIR_VAL_VREG) {
                    uint32_t vreg = val->as.vreg;
                    if (vreg <= allocator->max_vreg) {
                        allocator->use_count[vreg]++; // 简单统计使用频率
                        // 如果在 def 之前被 use，则加入 uses
                        bool is_defed = false;
                        for (int d = 0; d < bl->def_count; d++) {
                            if (bl->defs[d] == vreg) { is_defed = true; break; }
                        }
                        if (!is_defed) {
                            bool already_used = false;
                            for (int u = 0; u < bl->use_count; u++) {
                                if (bl->uses[u] == vreg) { already_used = true; break; }
                            }
                            if (!already_used) bl->uses[bl->use_count++] = vreg;
                        }
                    }
                }
            }
            if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                uint32_t vreg = inst->dest->as.vreg;
                if (vreg <= allocator->max_vreg) {
                    allocator->use_count[vreg]++;
                    bool already_defed = false;
                    for (int d = 0; d < bl->def_count; d++) {
                        if (bl->defs[d] == vreg) { already_defed = true; break; }
                    }
                    if (!already_defed) bl->defs[bl->def_count++] = vreg;
                }
            }
        }
    }

    // 2. 迭代计算 LiveIn 和 LiveOut
    bool changed = true;
    while (changed) {
        changed = false;
        SirBlock** blocks = (SirBlock**)malloc(sizeof(SirBlock*) * (max_block_id + 1));
        int b_count = 0;
        for (SirBlock* block = func->first_block; block; block = block->next) {
            blocks[b_count++] = block;
        }

        for (int i = b_count - 1; i >= 0; i--) {
            SirBlock* block = blocks[i];
            BlockLiveness* bl = &liveness[block->id];

            // 计算 LiveOut = union(LiveIn of successors)
            SirInst* last = block->last_inst;
            if (last) {
                if (last->opcode == SIR_JMP) {
                    SirBlock* succ = last->operands[0]->as.block;
                    for (uint32_t v = 1; v <= allocator->max_vreg; v++) {
                        if (liveness[succ->id].live_in[v]) bl->live_out[v] = true;
                    }
                } else if (last->opcode == SIR_BR) {
                    SirBlock* succ1 = last->operands[1]->as.block;
                    SirBlock* succ2 = last->operands[2]->as.block;
                    for (uint32_t v = 1; v <= allocator->max_vreg; v++) {
                        if (liveness[succ1->id].live_in[v] || liveness[succ2->id].live_in[v]) bl->live_out[v] = true;
                    }
                } else if (last->opcode == SIR_SWITCH) {
                    SirBlock* def_succ = last->operands[1]->as.block;
                    for (uint32_t v = 1; v <= allocator->max_vreg; v++) {
                        if (liveness[def_succ->id].live_in[v]) bl->live_out[v] = true;
                    }
                    int case_count = (last->num_operands - 2) / 2;
                    for (int c = 0; c < case_count; c++) {
                        SirBlock* succ = last->operands[2 + c * 2 + 1]->as.block;
                        for (uint32_t v = 1; v <= allocator->max_vreg; v++) {
                            if (liveness[succ->id].live_in[v]) bl->live_out[v] = true;
                        }
                    }
                }
            }

            // 计算 LiveIn = Use U (LiveOut - Def)
            for (uint32_t v = 1; v <= allocator->max_vreg; v++) {
                bool new_in = false;
                for (int u = 0; u < bl->use_count; u++) {
                    if (bl->uses[u] == v) { new_in = true; break; }
                }
                if (!new_in && bl->live_out[v]) {
                    bool is_def = false;
                    for (int d = 0; d < bl->def_count; d++) {
                        if (bl->defs[d] == v) { is_def = true; break; }
                    }
                    if (!is_def) new_in = true;
                }

                if (new_in != bl->live_in[v]) {
                    bl->live_in[v] = new_in;
                    changed = true;
                }
            }
        }
        free(blocks);
    }

    // 3. 构建冲突图
    uint32_t max_v = allocator->max_vreg;
    for (SirBlock* block = func->first_block; block; block = block->next) {
        BlockLiveness* bl = &liveness[block->id];
        bool* current_live = (bool*)malloc(sizeof(bool) * (max_v + 1));
        for (uint32_t v = 1; v <= max_v; v++) {
            current_live[v] = bl->live_out[v];
        }

        int i_count = 0;
        for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
            i_count++;
        }
        SirInst** insts = (SirInst**)malloc(sizeof(SirInst*) * (i_count > 0 ? i_count : 1));
        i_count = 0;
        for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
            insts[i_count++] = inst;
        }

        for (int i = i_count - 1; i >= 0; i--) {
            SirInst* inst = insts[i];
            
            if (inst->dest && inst->dest->kind == SIR_VAL_VREG) {
                uint32_t def_vreg = inst->dest->as.vreg;
                if (def_vreg <= max_v) {
                    for (uint32_t v = 1; v <= max_v; v++) {
                        if (current_live[v] && v != def_vreg) {
                            if (!allocator->adj_matrix[def_vreg * (max_v + 1) + v]) {
                                allocator->adj_matrix[def_vreg * (max_v + 1) + v] = true;
                                allocator->adj_matrix[v * (max_v + 1) + def_vreg] = true;
                                allocator->degree[def_vreg]++;
                                allocator->degree[v]++;
                            }
                        }
                    }
                    current_live[def_vreg] = false;
                }
            }

            if (inst->opcode == SIR_CALL || inst->opcode == SIR_SYS_ALLOC || inst->opcode == SIR_SYS_FREE ||
                inst->opcode == SIR_SYS_WRITE || inst->opcode == SIR_SYS_READ || inst->opcode == SIR_SYS_EXIT) {
                for (uint32_t v = 1; v <= max_v; v++) {
                    if (current_live[v]) allocator->crosses_call[v] = true;
                }
            }

            for (int op_idx = 0; op_idx < inst->num_operands; op_idx++) {
                SirValue* val = inst->operands[op_idx];
                if (val && val->kind == SIR_VAL_VREG) {
                    uint32_t use_vreg = val->as.vreg;
                    if (use_vreg <= max_v) {
                        current_live[use_vreg] = true;
                    }
                }
            }
        }
        free(insts);
        free(current_live);
    }

    for (uint32_t i = 0; i <= max_block_id; i++) {
        free(liveness[i].live_in);
        free(liveness[i].live_out);
        free(liveness[i].uses);
        free(liveness[i].defs);
    }
    free(liveness);

    // 3. 简化与着色 (Chaitin's Algorithm)
    int* stack = (int*)malloc(sizeof(int) * (max_v + 1));
    int top = 0;
    bool* removed = (bool*)calloc(max_v + 1, sizeof(bool));

    int remaining = 0;
    for (uint32_t i = 1; i <= max_v; i++) {
        if (allocator->use_count[i] == 0) {
            removed[i] = true; // 忽略未使用的
        } else {
            remaining++;
        }
    }

    // Simplify 阶段
    while (remaining > 0) {
        int best_node = -1;
        for (uint32_t i = 1; i <= max_v; i++) {
            if (!removed[i]) {
                if (best_node == -1) {
                    best_node = i;
                } else if (allocator->degree[i] < NUM_PHYS_REGS && allocator->degree[best_node] >= NUM_PHYS_REGS) {
                    best_node = i; // 优先选择度数小于 K 的节点
                } else if (allocator->degree[i] < NUM_PHYS_REGS && allocator->degree[i] > allocator->degree[best_node]) {
                    best_node = i; // 在可着色节点中选度数大的，尽早移除
                } else if (allocator->degree[i] >= NUM_PHYS_REGS && allocator->degree[best_node] >= NUM_PHYS_REGS) {
                    // Chaitin-Briggs 启发式 Spill: 选择 (使用代价 / 冲突度数) 最小的节点溢出
                    double weight_i = (double)allocator->use_count[i] / allocator->degree[i];
                    double weight_best = (double)allocator->use_count[best_node] / allocator->degree[best_node];
                    if (weight_i < weight_best) {
                        best_node = i;
                    } else if (weight_i == weight_best && allocator->degree[i] > allocator->degree[best_node]) {
                        best_node = i;
                    }
                }
            }
        }

        removed[best_node] = true;
        stack[top++] = best_node;
        remaining--;

        // 更新邻居度数
        for (uint32_t j = 1; j <= max_v; j++) {
            if (allocator->adj_matrix[best_node * (max_v + 1) + j] && !removed[j]) {
                allocator->degree[j]--;
            }
        }
    }

    // Select 阶段
    while (top > 0) {
        int node = stack[--top];
        bool used_colors[NUM_PHYS_REGS] = {false};
        
        for (uint32_t j = 1; j <= max_v; j++) {
            if (allocator->adj_matrix[node * (max_v + 1) + j]) {
                int c = allocator->vreg_colors[j];
                if (c != -1) used_colors[c] = true;
            }
        }
        
        int color = -1;
        // 优先尝试 Caller-Saved (7-10) 如果它不跨越调用
        if (!allocator->crosses_call[node]) {
            for (int c = 7; c < NUM_PHYS_REGS; c++) {
                if (!used_colors[c]) {
                    color = c;
                    break;
                }
            }
        }
        // 如果没找到，或者跨越了调用，尝试 Callee-Saved (0-6)
        if (color == -1) {
            for (int c = 0; c < 7; c++) {
                if (!used_colors[c]) {
                    color = c;
                    break;
                }
            }
        }
        allocator->vreg_colors[node] = color; // 如果为 -1，则表示 Spilled
        if (color != -1) {
            allocator->used_callee_saved[color] = true;
        }
    }

    free(stack);
    free(removed);
}

int reg_alloc_get_color(RegAllocator* allocator, uint32_t vreg) {
    if (vreg > allocator->max_vreg) return -1;
    return allocator->vreg_colors[vreg];
}

int reg_alloc_get_offset(RegAllocator* allocator, uint32_t vreg, int size) {
    if (vreg > allocator->max_vreg) return 0;
    
    if (allocator->vreg_offsets[vreg] == 0) {
        int aligned_size = size < 8 ? 8 : size;
        allocator->current_offset += aligned_size;
        allocator->vreg_offsets[vreg] = -allocator->current_offset;
    }
    
    return allocator->vreg_offsets[vreg];
}
