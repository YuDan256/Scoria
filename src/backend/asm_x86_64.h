#ifndef SCORIA_ASM_X86_64_H
#define SCORIA_ASM_X86_64_H

#include "../ir/sir.h"
#include <stdio.h>

// 将 SIR 模块编译为 x86_64 汇编代码 (AT&T 语法)
void asm_x86_64_generate(FILE* out, SirModule* module, int opt_level);

#endif // SCORIA_ASM_X86_64_H
