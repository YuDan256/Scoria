#ifndef SCORIA_LEXER_H
#define SCORIA_LEXER_H

#include "token.h"
#include <stdbool.h>

/**
 * @brief 词法扫描器核心状态机
 */
typedef struct {
    const char* start;   // 当前 Token 分析的起始绝对地址
    const char* current; // 游走的探路指针
    uint32_t line;       // 当前所在源码行
    uint32_t column;     // 从行首到当前 current 的列偏移
} Lexer;

/**
 * @brief 装填并初始化词法扫描器
 */
void lexer_init(Lexer* lexer, const char* source);

/**
 * @brief 提取并返回下一个极其轻量的 Token 结构体
 */
Token lexer_next_token(Lexer* lexer);

#endif // SCORIA_LEXER_H