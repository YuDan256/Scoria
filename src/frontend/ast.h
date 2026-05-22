#ifndef SCORIA_AST_H
#define SCORIA_AST_H

#include "token.h"
#include "../utils/memory_arena.h"

/**
 * @brief 抽象语法树节点类型枚举
 */
typedef enum {
    AST_PROGRAM,
    AST_MODULE_DECL,
    AST_IMPORT_DECL,
    AST_FUNC_DECL,
    AST_VAR_DECL,
    AST_CONST_DECL,
    AST_STRUCT_DECL,
    AST_BLOCK_STMT,
    AST_EXPR_STMT,
    AST_IF_STMT,
    AST_WHILE_STMT,
    AST_FOR_STMT,
    AST_BREAK_STMT,
    AST_CONTINUE_STMT,
    AST_RETURN_STMT,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_LITERAL_EXPR,
    AST_IDENT_EXPR,
    AST_CALL_EXPR,
    AST_INDEX_EXPR,
    AST_MEMBER_EXPR,
    AST_CAST_EXPR,
    AST_SCRIBE_EXPR,
    AST_VADE_EXPR,
    AST_RECEDE_EXPR,
    AST_CREA_EXPR,
    AST_NECA_EXPR,
    AST_ASSIGN_EXPR,
    AST_TYPE
} AstNodeKind;

typedef struct AstNode AstNode;

/**
 * @brief 抽象语法树节点 (基于标签联合)
 */
struct AstNode {
    AstNodeKind kind;
    Token token; // 关联的核心 Token，用于报错定位
    union {
        struct {
            AstNode** declarations;
            int decl_count;
        } program;

        struct {
            Token name;
        } module_decl;

        struct {
            Token module_name;
            Token* items;
            int item_count;
        } import_decl;

        struct {
            Token name;
            AstNode* return_type;
            AstNode** params;
            int param_count;
            AstNode* body;
            bool is_editus;
            bool is_barbarus;
        } func_decl;

        struct {
            Token name;
            AstNode* type;
            AstNode* initializer;
            bool is_editus;
        } var_decl;

        struct {
            Token name;
            bool is_editus;
            bool is_densa;
            AstNode** fields;
            int field_count;
        } struct_decl;

        struct {
            AstNode** statements;
            int stmt_count;
        } block;

        struct {
            AstNode* condition;
            AstNode* then_branch;
            AstNode* else_branch;
        } if_stmt;

        struct {
            AstNode* condition;
            AstNode* body;
        } while_stmt;

        struct {
            AstNode* initializer;
            AstNode* condition;
            AstNode* increment;
            AstNode* body;
        } for_stmt;

        struct {
            AstNode* value;
        } return_stmt;

        struct {
            AstNode* expr;
        } expr_stmt;

        struct {
            AstNode* left;
            Token op;
            AstNode* right;
        } binary;

        struct {
            Token op;
            AstNode* operand;
        } unary;

        struct {
            AstNode* callee;
            AstNode** args;
            int arg_count;
        } call;

        struct {
            AstNode* target;
            AstNode* index;
        } index_expr;

        struct {
            AstNode* object;
            Token property;
            bool is_pointer;
        } member_expr;

        struct {
            AstNode* target_type;
            AstNode* value;
        } cast_expr;

        struct {
            AstNode** args;
            int arg_count;
        } scribe_expr;

        struct {
            AstNode* pointer;
            AstNode* offset;
        } pointer_offset;

        struct {
            AstNode* type;
            AstNode* count;
        } crea_expr;

        struct {
            AstNode* pointer;
        } neca_expr;

        struct {
            AstNode* target;
            AstNode* value;
        } assign;

        struct {
            Token base_type;
            bool is_via;
            bool is_cohors;
            bool is_acies;
            AstNode* array_size;
        } type_node;
    } as;
};

/**
 * @brief 创建一个新的 AST 节点
 */
AstNode* ast_create_node(Arena* arena, AstNodeKind kind, Token token);

#endif // SCORIA_AST_H
