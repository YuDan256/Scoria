#ifndef SCORIA_AST_H
#define SCORIA_AST_H

#include <stdbool.h>
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
    AST_UNION_DECL,
    AST_TYPE_ALIAS_DECL,
    AST_ENUM_DECL,
    AST_BLOCK_STMT,
    AST_EXPR_STMT,
    AST_IF_STMT,
    AST_WHILE_STMT,
    AST_FOR_STMT,
    AST_BREAK_STMT,
    AST_CONTINUE_STMT,
    AST_TRAP_STMT,
    AST_RETURN_STMT,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_LITERAL_EXPR,
    AST_ARRAY_LITERAL,
    AST_STRUCT_LITERAL,
    AST_IDENT_EXPR,
    AST_CALL_EXPR,
    AST_INDEX_EXPR,
    AST_MEMBER_EXPR,
    AST_CAST_EXPR,
    AST_SCRIBE_EXPR,
    AST_LEGE_EXPR,
    AST_VADE_EXPR,
    AST_RECEDE_EXPR,
    AST_CREA_EXPR,
    AST_NECA_EXPR,
    AST_ASSIGN_EXPR,
    AST_GOTO_STMT,
    AST_LABEL_STMT,
    AST_SWITCH_STMT,
    AST_TYPE
} AstNodeKind;

typedef struct AstNode AstNode;
typedef struct ScoriaType ScoriaType; // 中端类型系统前置声明
typedef struct Symbol Symbol;         // 中端符号表前置声明

/**
 * @brief 抽象语法树节点 (基于标签联合)
 */
struct AstNode {
    AstNodeKind kind;
    Token token; // 关联的核心 Token，用于报错定位
    
    // --- 中端语义注解 (Semantic Annotations) ---
    ScoriaType* expr_type;     // 表达式求值后的具体类型 (Type Checker 填充)
    Symbol* resolved_symbol;   // 标识符解析后指向的符号表条目 (Symtab 填充)
    
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
            Token dll_name; // 用于 barbara("dll.dll")
            bool is_variadic;
            bool is_native_variadic;
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
            Token name;
            AstNode* target_type;
            bool is_editus;
        } type_alias_decl;

        struct {
            Token name;
            bool is_editus;
            Token* variant_names;
            AstNode** variant_values;
            int variant_count;
        } enum_decl;

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
            int64_t int_val;
            double float_val;
        } literal_expr;

        struct {
            AstNode** elements;
            int element_count;
        } array_literal;

        struct {
            AstNode* type_expr; // 指向 AST_IDENT_EXPR 或 AST_MEMBER_EXPR
            Token* field_names;
            AstNode** field_values;
            int field_count;
        } struct_literal;

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
            AstNode** args;
            int arg_count;
        } lege_expr;

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
            Token op;
            AstNode* target;
            AstNode* value;
        } assign;

        struct {
            Token label_name;
        } goto_stmt;

        struct {
            Token name;
        } label_stmt;

        struct {
            AstNode* condition;
            AstNode*** case_vals;
            int* case_val_counts;
            AstNode** case_stmts;
            int case_count;
            AstNode* default_branch;
        } switch_stmt;

        struct {
            Token module_prefix; // 模块前缀 (可选)
            Token base_type;
            bool is_via;
            bool is_cohors;
            bool is_acies;
            AstNode* array_size;
            AstNode* inner_type; // 支持多级嵌套类型 (如 via via i32)
        } type_node;
    } as;
};

/**
 * @brief 创建一个新的 AST 节点
 */
AstNode* ast_create_node(Arena* arena, AstNodeKind kind, Token token);

#endif // SCORIA_AST_H
