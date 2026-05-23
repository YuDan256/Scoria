#include "reg_alloc.h"
#include <stdlib.h>
#include <stdio.h>

void reg_alloc_init(RegAllocator* allocator, uint32_t max_vreg) {
    allocator->current_offset = 0;
    allocator->max_vreg = max_vreg;
    allocator->vreg_offsets = (int*)calloc(max_vreg + 1, sizeof(int));
    allocator->vreg_colors = (int*)calloc(max_vreg + 1, sizeof(int));
    allocator->live_start = (int*)calloc(max_vreg + 1, sizeof(int));
    allocator->live_end = (int*)calloc(max_vreg + 1, sizeof(int));
    allocator->adj_matrix = (bool*)calloc((max_vreg + 1) * (max_vreg + 1), sizeof(bool));
    allocator->degree = (int*)calloc(max_vreg + 1, sizeof(int));

    if (!allocator->vreg_offsets || !allocator->vreg_colors || !allocator->live_start ||
        !allocator->live_end || !allocator->adj_matrix || !allocator->degree) {
        fprintf(stderr, "Fatal error: Out of memory in reg_alloc_init\n");
        exit(1);
    }

    for (uint32_t i = 0; i <= max_vreg; i++) allocator->vreg_colors[i] = -1;

    for (uint32_t i = 0; i <= max_vreg; i++) {
        allocator->live_start[i] = 999999999;
        allocator->live_end[i] = -1;
    }
}

void reg_alloc_free(RegAllocator* allocator) {
    free(allocator->vreg_offsets);
    free(allocator->vreg_colors);
    free(allocator->live_start);
    free(allocator->live_end);
    free(allocator->adj_matrix);
    free(allocator->degree);
}

static void update_live_interval(RegAllocator* allocator, SirValue* val, int inst_idx) {
    if (!val || val->kind != SIR_VAL_VREG) return;
    uint32_t vreg = val->as.vreg;
    if (vreg > allocator->max_vreg) return;
    if (inst_idx < allocator->live_start[vreg]) allocator->live_start[vreg] = inst_idx;
    if (inst_idx > allocator->live_end[vreg]) allocator->live_end[vreg] = inst_idx;
}

void reg_alloc_build_and_color(RegAllocator* allocator, SirFunction* func) {
    // 1. 计算活跃区间 (Live Intervals) - 简化的线性扫描法
    int inst_idx = 0;
    for (SirBlock* block = func->first_block; block; block = block->next) {
        for (SirInst* inst = block->first_inst; inst; inst = inst->next) {
            for (int i = 0; i < inst->num_operands; i++) {
                update_live_interval(allocator, inst->operands[i], inst_idx);
            }
            update_live_interval(allocator, inst->dest, inst_idx);
            inst_idx++;
        }
    }

    // 2. 构建冲突图 (Interference Graph)
    uint32_t max_v = allocator->max_vreg;
    for (uint32_t i = 1; i <= max_v; i++) {
        if (allocator->live_end[i] == -1) continue; // 未使用的寄存器
        for (uint32_t j = i + 1; j <= max_v; j++) {
            if (allocator->live_end[j] == -1) continue;
            // 检查区间是否重叠
            if (allocator->live_start[i] <= allocator->live_end[j] && 
                allocator->live_start[j] <= allocator->live_end[i]) {
                allocator->adj_matrix[i * (max_v + 1) + j] = true;
                allocator->adj_matrix[j * (max_v + 1) + i] = true;
                allocator->degree[i]++;
                allocator->degree[j]++;
            }
        }
    }

    // 3. 简化与着色 (Chaitin's Algorithm)
    int* stack = (int*)malloc(sizeof(int) * (max_v + 1));
    int top = 0;
    bool* removed = (bool*)calloc(max_v + 1, sizeof(bool));

    int remaining = 0;
    for (uint32_t i = 1; i <= max_v; i++) {
        if (allocator->live_end[i] == -1) {
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
                } else if (allocator->degree[i] >= NUM_PHYS_REGS && allocator->degree[i] > allocator->degree[best_node]) {
                    best_node = i; // 在必须溢出的节点中选度数大的 (启发式 Spill)
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
        for (int c = 0; c < NUM_PHYS_REGS; c++) {
            if (!used_colors[c]) {
                color = c;
                break;
            }
        }
        allocator->vreg_colors[node] = color; // 如果为 -1，则表示 Spilled
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
