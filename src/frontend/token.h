#ifndef SCORIA_TOKEN_H
#define SCORIA_TOKEN_H

#include <stdint.h>

/**
 * @brief Scoria 语言词法单元全景枚举
 */
typedef enum {
    TK_EOF = 0,
    TK_UNKNOWN,

    // ================= [文字、标识符与常量] =================
    TK_IDENTIFIER, TK_INT_CONST, TK_FLOAT_CONST, TK_BOOL_CONST,
    TK_CHAR_CONST, TK_STRING_CONST,

    // ================= [标点与结构符号] =================
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE, TK_LBRACKET, TK_RBRACKET,
    TK_COMMA, TK_DOT, TK_COLON, TK_SEMI, TK_ARROW,

    // ================= [算术与位操作符] =================
    TK_PLUS, TK_MINUS,        // + -
    TK_STAR, TK_SLASH,        // * /
    TK_MOD,                   // %

    TK_AMP,                   // & (按位与)
    TK_PIPE,                  // | (按位或)
    TK_CARET,                 // ^ (按位异或)
    TK_TILDE,                 // ~ (按位取反)
    TK_SHL, TK_SHR,           // << >> (左移，右移)

    // ================= [复合赋值符] =================
    TK_ASSIGN,                // =
    TK_PLUS_ASSIGN,           // +=
    TK_MINUS_ASSIGN,          // -=
    TK_STAR_ASSIGN,           // *=
    TK_SLASH_ASSIGN,          // /=
    TK_MOD_ASSIGN,            // %=
    TK_AMP_ASSIGN,            // &=
    TK_PIPE_ASSIGN,           // |=
    TK_CARET_ASSIGN,          // ^=
    TK_SHL_ASSIGN,            // <<=
    TK_SHR_ASSIGN,            // >>=

    // ================= [逻辑与比较符] =================
    TK_LOGIC_NOT,             // ! (逻辑非)
    TK_LOGIC_AND,             // &&
    TK_LOGIC_OR,              // ||
    TK_EQ, TK_NEQ,            // == !=
    TK_LT, TK_GT,             // < >
    TK_LTE, TK_GTE,           // <= >=

    // ================= [语言关键字 : 模块与边界] =================
    TK_KW_LIBER, TK_KW_CONSULE, TK_KW_DE, TK_KW_EXCERPE,
    TK_KW_EDITA, TK_KW_BARBARA, TK_KW_ETC, TK_KW_IMAGO,

    // ================= [语言关键字 : 流程与指令] =================
    TK_KW_ACTIO, TK_KW_REDDE, TK_KW_SIT, TK_KW_LEX,
    TK_KW_SI, TK_KW_ALITER, TK_KW_DUM, TK_KW_PER, TK_KW_MUTA,
    TK_KW_RUMPE, TK_KW_PERGE, TK_KW_MORERE,
    TK_KW_LOCUS, TK_KW_TENE, TK_KW_VADE, TK_KW_RECEDE,
    TK_KW_SCRIBE, TK_KW_LEGE, TK_KW_CREA, TK_KW_NECA, TK_KW_NIHIL, TK_KW_NULLUS, TK_KW_SALI,
    TK_KW_ELIGE, TK_KW_CASUS, TK_KW_UNIO, TK_KW_ORDO,

    // ================= [系统类型定义] =================
    // 现代工程缩写
    TK_TY_I8, TK_TY_I16, TK_TY_I32, TK_TY_I64,
    TK_TY_P8, TK_TY_P16, TK_TY_P32, TK_TY_P64,
    TK_TY_F32, TK_TY_F64,
    // 古典罗马词缀 (搭配使用，如 minimus purus，Parser将接收 TK_TY_I8 + TK_TY_PURUS 去融合成 p8)
    TK_TY_PURUS,

    TK_TY_LOGICA, TK_TY_LITTERA, TK_TY_VIA, TK_TY_COHORS,
    TK_TY_FORMA, TK_TY_DENSA, TK_TY_TEXTUS, TK_TY_ACIES
} TokenKind;

/**
 * @brief 零拷贝物理词法单元 (占用极小内存，完全依赖文件映射指针)
 */
typedef struct {
    TokenKind kind;
    const char* start;
    uint32_t length;
    uint32_t line;
    uint32_t column;
} Token;

#endif // SCORIA_TOKEN_H
