#ifndef SCORIA_SYMTAB_H
#define SCORIA_SYMTAB_H

#include "types.h"
#include "../frontend/ast.h"

typedef enum {
    SYM_VAR,
    SYM_CONST,
    SYM_FUNC,
    SYM_STRUCT,
    SYM_UNION,
    SYM_MODULE,
    SYM_TYPE_ALIAS
} SymbolKind;

struct SirValue; // 前置声明

struct Symbol {
    Token name;
    SymbolKind kind;
    ScoriaType* type;
    AstNode* node; // 声明该符号的 AST 节点
    bool is_editus;
    bool is_resolving; // 用于检测类型别名的循环依赖
    struct SirValue* ir_val; // 后端 IR 生成时绑定的虚拟寄存器/内存地址
    struct Scope* module_scope; // 仅用于 SYM_MODULE，指向该模块的全局作用域
    struct Symbol* alias_target; // 指向真实的符号 (用于跨模块导入)
    struct Symbol* next; // 用于哈希表冲突链
};

typedef struct Scope {
    struct Symbol** hash_table;
    int capacity;
    int count;
    struct Scope* parent;
    struct Scope* next_in_all;
} Scope;

typedef struct {
    Scope* current_scope;
    Scope* universe_scope; // 宇宙作用域，存放所有模块
    Scope* all_scopes;
} Symtab;

void symtab_init(Symtab* symtab);
void symtab_free(Symtab* symtab);

void symtab_enter_scope(Symtab* symtab);
void symtab_leave_scope(Symtab* symtab);

// 在当前作用域定义符号
bool symtab_define(Symtab* symtab, Token name, SymbolKind kind, ScoriaType* type, AstNode* node, bool is_editus);

// 向上查找符号
Symbol* symtab_lookup(Symtab* symtab, Token name);

// 仅在当前作用域查找符号
Symbol* symtab_lookup_current(Symtab* symtab, Token name);

// 在指定作用域查找符号
Symbol* symtab_lookup_in_scope(Scope* scope, Token name);

// 在当前作用域插入一个已存在符号的别名 (用于跨模块导入)
bool symtab_insert_alias(Symtab* symtab, Token name, Symbol* target);

#endif // SCORIA_SYMTAB_H
