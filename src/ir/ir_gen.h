#ifndef SCORIA_IR_GEN_H
#define SCORIA_IR_GEN_H

#include "../frontend/ast.h"
#include "ir_builder.h"

// 将类型检查通过的 AST 转换为 SIR 模块
void ir_gen_generate(IrBuilder* builder, AstNode** programs, int count);

#endif // SCORIA_IR_GEN_H
