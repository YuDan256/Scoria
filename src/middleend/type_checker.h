#ifndef SCORIA_TYPE_CHECKER_H
#define SCORIA_TYPE_CHECKER_H

#include "../frontend/ast.h"
#include "symtab.h"
#include <stdbool.h>

typedef struct {
    Symtab symtab;
    bool had_error;
    ScoriaType* current_function_return_type; // 用于校验 redde 语句
    int loop_depth;                           // 用于校验 rumpe/perge 语句
} TypeChecker;

void type_checker_init(TypeChecker* checker);
void type_checker_free(TypeChecker* checker);

// 执行类型检查 (包含多遍遍历：模块注册、收集声明、处理导入、深度检查)
bool type_checker_run(TypeChecker* checker, AstNode** programs, int count);

#endif // SCORIA_TYPE_CHECKER_H
