#ifndef SCORIA_REG_ALLOC_H
#define SCORIA_REG_ALLOC_H

#include "../ir/sir.h"
#include <stdint.h>

#define NUM_PHYS_REGS 11 // 7个 Callee-Saved + 4个 Caller-Saved (r8-r11)

// 图着色寄存器分配器 (Graph Coloring Register Allocator)
typedef struct {
    int current_offset;
    int* vreg_offsets; // 栈偏移量 (用于被溢出的寄存器)
    int* vreg_colors;  // 物理寄存器 ID (0 到 NUM_PHYS_REGS-1)，-1 表示溢出 (Spilled)
    uint32_t max_vreg;

    // 冲突图 (Interference Graph)
    bool* adj_matrix;
    int* degree;
    int* use_count; // 动态使用频率 (用于 Spill 启发式评估)
    bool* crosses_call; // 记录虚拟寄存器是否跨越了函数调用
    bool used_callee_saved[NUM_PHYS_REGS]; // 记录哪些物理寄存器被实际使用
} RegAllocator;

void reg_alloc_init(RegAllocator* allocator, uint32_t max_vreg);
void reg_alloc_free(RegAllocator* allocator);

// 执行活跃分析、构建冲突图并进行着色
void reg_alloc_build_and_color(RegAllocator* allocator, SirFunction* func);

// 获取分配的物理寄存器颜色 (-1 表示溢出到栈)
int reg_alloc_get_color(RegAllocator* allocator, uint32_t vreg);

// 为溢出的虚拟寄存器分配栈空间 (返回相对于 %rbp 的负偏移量)
int reg_alloc_get_offset(RegAllocator* allocator, uint32_t vreg, int size);

#endif // SCORIA_REG_ALLOC_H
