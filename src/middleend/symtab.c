#include "symtab.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 64

static uint32_t hash_token(Token token) {
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < token.length; i++) {
        hash ^= (uint8_t)token.start[i];
        hash *= 16777619;
    }
    return hash;
}

static bool token_equals(Token a, Token b) {
    if (a.length != b.length) return false;
    return memcmp(a.start, b.start, a.length) == 0;
}

static Scope* create_scope(Symtab* symtab, Scope* parent) {
    Scope* scope = (Scope*)malloc(sizeof(Scope));
    scope->capacity = INITIAL_CAPACITY;
    scope->count = 0;
    scope->hash_table = (Symbol**)calloc(scope->capacity, sizeof(Symbol*));
    scope->parent = parent;
    scope->next_in_all = symtab->all_scopes;
    symtab->all_scopes = scope;
    return scope;
}

static void free_scope(Scope* scope) {
    for (int i = 0; i < scope->capacity; i++) {
        Symbol* sym = scope->hash_table[i];
        while (sym) {
            Symbol* next = sym->next;
            free(sym);
            sym = next;
        }
    }
    free(scope->hash_table);
    free(scope);
}

void symtab_init(Symtab* symtab) {
    symtab->all_scopes = NULL;
    symtab->universe_scope = create_scope(symtab, NULL);
    symtab->current_scope = symtab->universe_scope;
}

void symtab_free(Symtab* symtab) {
    Scope* scope = symtab->all_scopes;
    while (scope) {
        Scope* next = scope->next_in_all;
        free_scope(scope);
        scope = next;
    }
    symtab->current_scope = NULL;
    symtab->universe_scope = NULL;
    symtab->all_scopes = NULL;
}

void symtab_enter_scope(Symtab* symtab) {
    symtab->current_scope = create_scope(symtab, symtab->current_scope);
}

void symtab_leave_scope(Symtab* symtab) {
    if (symtab->current_scope->parent) {
        symtab->current_scope = symtab->current_scope->parent;
    }
}

bool symtab_define(Symtab* symtab, Token name, SymbolKind kind, ScoriaType* type, AstNode* node, bool is_editus) {
    Scope* scope = symtab->current_scope;
    
    // 检查是否已存在
    if (symtab_lookup_current(symtab, name)) {
        return false; // 重复定义
    }

    uint32_t index = hash_token(name) % scope->capacity;
    Symbol* sym = (Symbol*)malloc(sizeof(Symbol));
    sym->name = name;
    sym->kind = kind;
    sym->type = type;
    sym->node = node;
    sym->is_editus = is_editus;
    sym->is_resolving = false;
    sym->ir_val = NULL;
    sym->module_scope = NULL;
    sym->alias_target = NULL;
    
    // 将符号反向绑定到 AST 节点，方便 IR 生成器直接读取
    if (node) node->resolved_symbol = sym;

    sym->next = scope->hash_table[index];
    scope->hash_table[index] = sym;
    scope->count++;
    
    return true;
}

Symbol* symtab_lookup_in_scope(Scope* scope, Token name) {
    if (!scope) return NULL;
    uint32_t index = hash_token(name) % scope->capacity;
    Symbol* sym = scope->hash_table[index];
    while (sym) {
        if (token_equals(sym->name, name)) {
            return sym;
        }
        sym = sym->next;
    }
    return NULL;
}

Symbol* symtab_lookup_current(Symtab* symtab, Token name) {
    return symtab_lookup_in_scope(symtab->current_scope, name);
}

bool symtab_insert_alias(Symtab* symtab, Token name, Symbol* target) {
    Scope* scope = symtab->current_scope;
    if (symtab_lookup_current(symtab, name)) return false;

    uint32_t index = hash_token(name) % scope->capacity;
    Symbol* sym = (Symbol*)malloc(sizeof(Symbol));
    sym->name = name;
    sym->kind = target->kind;
    sym->type = target->type;
    sym->node = target->node;
    sym->is_editus = false;
    sym->is_resolving = false;
    sym->ir_val = NULL;
    sym->module_scope = target->module_scope;
    sym->alias_target = target; // 记录真实目标

    sym->next = scope->hash_table[index];
    scope->hash_table[index] = sym;
    scope->count++;
    return true;
}

Symbol* symtab_lookup(Symtab* symtab, Token name) {
    Scope* scope = symtab->current_scope;
    while (scope) {
        uint32_t index = hash_token(name) % scope->capacity;
        Symbol* sym = scope->hash_table[index];
        while (sym) {
            if (token_equals(sym->name, name)) {
                return sym;
            }
            sym = sym->next;
        }
        scope = scope->parent;
    }
    return NULL;
}
