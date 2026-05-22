#ifndef SCORIA_PARSER_H
#define SCORIA_PARSER_H

#include "lexer.h"
#include "ast.h"
#include <stdbool.h>

/**
 * @brief 语法解析器核心状态机
 */
typedef struct {
    Lexer lexer;
    Token current;
    Token previous;
    bool had_error;
    bool panic_mode;
} Parser;

/**
 * @brief 初始化语法解析器
 */
void parser_init(Parser* parser, const char* source);

/**
 * @brief 解析整个程序，返回 AST 根节点
 */
AstNode* parse_program(Parser* parser);

#endif // SCORIA_PARSER_H
