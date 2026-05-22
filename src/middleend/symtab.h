#ifndef SCORIA_SYMTAB_H
#define SCORIA_SYMTAB_H

#include "types.h"
#include "../frontend/ast.h"

typedef enum {
    SYM_VAR,
    SYM_CONST,
    SYM_FUNC,
    SYM_STRUCT
} SymbolKind;

struct SirValue; // 前置声明

struct Symbol {
    Token name;
    SymbolKind kind;
    ScoriaType* type;
    AstNode* node; // 声明该符号的 AST 节点
    bool is_editus;
    struct SirValue* ir_val; // 后端 IR 生成时绑定的虚拟寄存器/内存地址
    struct Symbol* next; // 用于哈希表冲突链
};

typedef struct Scope {
    struct Symbol** hash_table;
    int capacity;
    int count;
    struct Scope* parent;
} Scope;

typedef struct {
    Scope* current_scope;
    Scope* global_scope;
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

#endif // SCORIA_SYMTAB_H
