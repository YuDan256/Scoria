#include "types.h"
#include <stdlib.h>
#include <string.h>

// 基础类型单例
static ScoriaType basic_types[] = {
    {TY_UNKNOWN}, {TY_NIHIL},
    {TY_I8}, {TY_I16}, {TY_I32}, {TY_I64},
    {TY_P8}, {TY_P16}, {TY_P32}, {TY_P64},
    {TY_F32}, {TY_F64},
    {TY_LOGICA}, {TY_LITTERA}, {TY_TEXTUS}
};

// 简单的链表用于 Type Interning
typedef struct TypeNode {
    ScoriaType* type;
    struct TypeNode* next;
} TypeNode;

static TypeNode* interned_types = NULL;

void types_init(void) {
    interned_types = NULL;
}

ScoriaType* type_get_basic(TypeKind kind) {
    if (kind >= TY_UNKNOWN && kind <= TY_TEXTUS) {
        return &basic_types[kind];
    }
    return &basic_types[TY_UNKNOWN];
}

bool type_equals(ScoriaType* a, ScoriaType* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    // 容错处理：如果其中一个是未知类型，直接放行，防止级联报错
    if (a->kind == TY_UNKNOWN || b->kind == TY_UNKNOWN) return true;
    if (a->kind != b->kind) return false;
    
    switch (a->kind) {
        case TY_VIA:
        case TY_COHORS:
            return type_equals(a->as.inner, b->as.inner);
        case TY_ACIES:
            return a->as.array.length == b->as.array.length && type_equals(a->as.array.inner, b->as.array.inner);
        case TY_FORMA:
            // 结构体通过名称比较
            return a->as.struct_type.name.length == b->as.struct_type.name.length &&
                   memcmp(a->as.struct_type.name.start, b->as.struct_type.name.start, a->as.struct_type.name.length) == 0;
        case TY_ACTIO:
            if (a->as.func_type.param_count != b->as.func_type.param_count) return false;
            if (!type_equals(a->as.func_type.return_type, b->as.func_type.return_type)) return false;
            for (int i = 0; i < a->as.func_type.param_count; i++) {
                if (!type_equals(a->as.func_type.param_types[i], b->as.func_type.param_types[i])) return false;
            }
            return true;
        default:
            return true; // 基础类型且 kind 相同
    }
}

static ScoriaType* intern_type(ScoriaType* new_type) {
    for (TypeNode* node = interned_types; node != NULL; node = node->next) {
        if (type_equals(node->type, new_type)) {
            free(new_type); // 已经存在，释放新分配的
            return node->type;
        }
    }
    
    TypeNode* node = (TypeNode*)malloc(sizeof(TypeNode));
    node->type = new_type;
    node->next = interned_types;
    interned_types = node;
    return new_type;
}

ScoriaType* type_get_via(ScoriaType* inner) {
    ScoriaType* t = (ScoriaType*)malloc(sizeof(ScoriaType));
    t->kind = TY_VIA;
    t->as.inner = inner;
    return intern_type(t);
}

ScoriaType* type_get_cohors(ScoriaType* inner) {
    ScoriaType* t = (ScoriaType*)malloc(sizeof(ScoriaType));
    t->kind = TY_COHORS;
    t->as.inner = inner;
    return intern_type(t);
}

ScoriaType* type_get_acies(ScoriaType* inner, uint32_t length) {
    ScoriaType* t = (ScoriaType*)malloc(sizeof(ScoriaType));
    t->kind = TY_ACIES;
    t->as.array.inner = inner;
    t->as.array.length = length;
    return intern_type(t);
}

ScoriaType* type_create_forma(Token name, bool is_densa) {
    ScoriaType* t = (ScoriaType*)malloc(sizeof(ScoriaType));
    t->kind = TY_FORMA;
    t->as.struct_type.name = name;
    t->as.struct_type.fields = NULL;
    t->as.struct_type.field_count = 0;
    t->as.struct_type.is_densa = is_densa;
    return t; // 结构体通过名字区分，不放入 intern 池
}

void type_forma_add_field(ScoriaType* forma_type, Token name, ScoriaType* field_type) {
    if (forma_type->kind != TY_FORMA) return;
    
    int count = forma_type->as.struct_type.field_count;
    forma_type->as.struct_type.fields = realloc(forma_type->as.struct_type.fields, sizeof(StructField) * (count + 1));
    forma_type->as.struct_type.fields[count].name = name;
    forma_type->as.struct_type.fields[count].type = field_type;
    forma_type->as.struct_type.field_count++;
}

ScoriaType* type_create_actio(ScoriaType* return_type, ScoriaType** param_types, int param_count) {
    ScoriaType* t = (ScoriaType*)malloc(sizeof(ScoriaType));
    t->kind = TY_ACTIO;
    t->as.func_type.return_type = return_type;
    t->as.func_type.param_count = param_count;
    t->as.func_type.param_types = NULL;
    if (param_count > 0) {
        t->as.func_type.param_types = (ScoriaType**)malloc(sizeof(ScoriaType*) * param_count);
        memcpy(t->as.func_type.param_types, param_types, sizeof(ScoriaType*) * param_count);
    }
    return t;
}
