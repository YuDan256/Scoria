#include "types.h"
#include <stdlib.h>
#include <string.h>

// 基础类型单例
static ScoriaType basic_types[] = {
    {TY_UNKNOWN}, {TY_NIHIL},
    {TY_I8}, {TY_I16}, {TY_I32}, {TY_I64},
    {TY_P8}, {TY_P16}, {TY_P32}, {TY_P64},
    {TY_F32}, {TY_F64},
    {TY_LOGICA}, {TY_LITTERA},
    {TY_VIA},      // 14 - 占位
    {TY_COHORS},   // 15 - 占位
    {TY_ACIES},    // 16 - 占位
    {TY_FORMA},    // 17 - 占位
    {TY_UNIO},     // 18 - 占位
    {TY_ACTIO},    // 19 - 占位
    {TY_MODULE}    // 20
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
    if (kind >= TY_UNKNOWN && kind <= TY_MODULE) {
        return &basic_types[kind];
    }
    return &basic_types[TY_UNKNOWN];
}

bool type_equals(ScoriaType* a, ScoriaType* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    // 允许 nihil 隐式匹配任何指针、切片或函数类型 (类似 C 语言的 NULL)
    if (a->kind == TY_NIHIL && (b->kind == TY_VIA || b->kind == TY_COHORS || b->kind == TY_ACTIO)) return true;
    if (b->kind == TY_NIHIL && (a->kind == TY_VIA || a->kind == TY_COHORS || a->kind == TY_ACTIO)) return true;

    if (a->kind != b->kind) return false;
    
    switch (a->kind) {
        case TY_VIA:
        case TY_COHORS:
            return type_equals(a->as.inner, b->as.inner);
        case TY_ACIES:
            return a->as.array.length == b->as.array.length && type_equals(a->as.array.inner, b->as.array.inner);
        case TY_FORMA:
        case TY_UNIO:
            // 结构体/联合体通过名称比较
            return a->as.struct_type.name.length == b->as.struct_type.name.length &&
                   memcmp(a->as.struct_type.name.start, b->as.struct_type.name.start, a->as.struct_type.name.length) == 0;
        case TY_ACTIO:
            if (a->as.func_type.param_count != b->as.func_type.param_count) return false;
            if (a->as.func_type.is_variadic != b->as.func_type.is_variadic) return false;
            if (a->as.func_type.is_native_variadic != b->as.func_type.is_native_variadic) return false;
            if (!type_equals(a->as.func_type.return_type, b->as.func_type.return_type)) return false;
            for (int i = 0; i < a->as.func_type.param_count; i++) {
                if (!type_equals(a->as.func_type.param_types[i], b->as.func_type.param_types[i])) return false;
            }
            return true;
        default:
            return true; // 基础类型且 kind 相同
    }
}

static bool type_strict_equals(ScoriaType* a, ScoriaType* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    
    switch (a->kind) {
        case TY_VIA:
        case TY_COHORS:
            return type_strict_equals(a->as.inner, b->as.inner);
        case TY_ACIES:
            return a->as.array.length == b->as.array.length && type_strict_equals(a->as.array.inner, b->as.array.inner);
        case TY_FORMA:
        case TY_UNIO:
            return a->as.struct_type.name.length == b->as.struct_type.name.length &&
                   memcmp(a->as.struct_type.name.start, b->as.struct_type.name.start, a->as.struct_type.name.length) == 0;
        case TY_ACTIO:
            if (a->as.func_type.param_count != b->as.func_type.param_count) return false;
            if (a->as.func_type.is_variadic != b->as.func_type.is_variadic) return false;
            if (a->as.func_type.is_native_variadic != b->as.func_type.is_native_variadic) return false;
            if (!type_strict_equals(a->as.func_type.return_type, b->as.func_type.return_type)) return false;
            for (int i = 0; i < a->as.func_type.param_count; i++) {
                if (!type_strict_equals(a->as.func_type.param_types[i], b->as.func_type.param_types[i])) return false;
            }
            return true;
        default:
            return true;
    }
}

static ScoriaType* intern_type(ScoriaType* new_type) {
    for (TypeNode* node = interned_types; node != NULL; node = node->next) {
        if (type_strict_equals(node->type, new_type)) {
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

ScoriaType* type_create_unio(Token name) {
    ScoriaType* t = (ScoriaType*)malloc(sizeof(ScoriaType));
    t->kind = TY_UNIO;
    t->as.struct_type.name = name;
    t->as.struct_type.fields = NULL;
    t->as.struct_type.field_count = 0;
    t->as.struct_type.is_densa = false;
    return t; // 联合体通过名字区分，不放入 intern 池
}

void type_forma_add_field(ScoriaType* forma_type, Token name, ScoriaType* field_type) {
    if (forma_type->kind != TY_FORMA && forma_type->kind != TY_UNIO) return;
    
    int count = forma_type->as.struct_type.field_count;
    forma_type->as.struct_type.fields = realloc(forma_type->as.struct_type.fields, sizeof(StructField) * (count + 1));
    forma_type->as.struct_type.fields[count].name = name;
    forma_type->as.struct_type.fields[count].type = field_type;
    forma_type->as.struct_type.field_count++;
}

int type_get_size(ScoriaType* type) {
    if (!type) return 0;
    switch (type->kind) {
        case TY_NIHIL: case TY_UNKNOWN: return 0;
        case TY_I8: case TY_P8: case TY_LITTERA: case TY_LOGICA: return 1;
        case TY_I16: case TY_P16: return 2;
        case TY_I32: case TY_P32: case TY_F32: return 4;
        case TY_I64: case TY_P64: case TY_F64: case TY_VIA: return 8;
        case TY_COHORS: return 16;
        case TY_ACIES: return type->as.array.length * type_get_size(type->as.array.inner);
        case TY_FORMA: {
            int size = 0;
            int max_align = 1;
            for (int i = 0; i < type->as.struct_type.field_count; i++) {
                int field_size = type_get_size(type->as.struct_type.fields[i].type);
                int field_align = type->as.struct_type.is_densa ? 1 : (field_size > 8 ? 8 : field_size);
                if (field_align > max_align) max_align = field_align;
                if (!type->as.struct_type.is_densa) {
                    size = (size + field_align - 1) & ~(field_align - 1);
                }
                size += field_size;
            }
            if (!type->as.struct_type.is_densa) {
                size = (size + max_align - 1) & ~(max_align - 1);
            }
            return size;
        }
        case TY_UNIO: {
            int max_size = 0;
            int max_align = 1;
            for (int i = 0; i < type->as.struct_type.field_count; i++) {
                int field_size = type_get_size(type->as.struct_type.fields[i].type);
                int field_align = field_size > 8 ? 8 : field_size;
                if (field_align > max_align) max_align = field_align;
                if (field_size > max_size) max_size = field_size;
            }
            return (max_size + max_align - 1) & ~(max_align - 1);
        }
        default: return 8;
    }
}

bool type_is_signed(ScoriaType* type) {
    if (!type) return false;
    return type->kind == TY_I8 || type->kind == TY_I16 || type->kind == TY_I32 || type->kind == TY_I64;
}

bool type_is_unsigned(ScoriaType* type) {
    if (!type) return false;
    return type->kind == TY_P8 || type->kind == TY_P16 || type->kind == TY_P32 || type->kind == TY_P64 || type->kind == TY_LITTERA || type->kind == TY_LOGICA;
}

ScoriaType* type_create_actio(ScoriaType* return_type, ScoriaType** param_types, int param_count, bool is_variadic, bool is_native_variadic) {
    ScoriaType* t = (ScoriaType*)malloc(sizeof(ScoriaType));
    t->kind = TY_ACTIO;
    t->as.func_type.return_type = return_type;
    t->as.func_type.param_count = param_count;
    t->as.func_type.is_variadic = is_variadic;
    t->as.func_type.is_native_variadic = is_native_variadic;
    t->as.func_type.param_types = NULL;
    if (param_count > 0) {
        t->as.func_type.param_types = (ScoriaType**)malloc(sizeof(ScoriaType*) * param_count);
        memcpy(t->as.func_type.param_types, param_types, sizeof(ScoriaType*) * param_count);
    }
    return t;
}
