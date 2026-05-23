#ifndef SCORIA_TYPES_H
#define SCORIA_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include "../frontend/token.h"

typedef enum {
    TY_UNKNOWN,
    TY_NIHIL,
    TY_I8, TY_I16, TY_I32, TY_I64,
    TY_P8, TY_P16, TY_P32, TY_P64,
    TY_F32, TY_F64,
    TY_LOGICA,
    TY_LITTERA,
    TY_TEXTUS,
    TY_VIA,      // 裸指针
    TY_COHORS,   // 切片
    TY_ACIES,    // 数组
    TY_FORMA,    // 结构体
    TY_ACTIO     // 函数
} TypeKind;

typedef struct ScoriaType ScoriaType;

// 结构体字段
typedef struct {
    Token name;
    ScoriaType* type;
} StructField;

struct ScoriaType {
    TypeKind kind;
    
    union {
        // 用于 TY_VIA, TY_COHORS
        ScoriaType* inner;
        
        // 用于 TY_ACIES
        struct {
            ScoriaType* inner;
            uint32_t length;
        } array;
        
        // 用于 TY_FORMA
        struct {
            Token name;
            StructField* fields;
            int field_count;
            bool is_densa;
        } struct_type;
        
        // 用于 TY_ACTIO
        struct {
            ScoriaType** param_types;
            int param_count;
            ScoriaType* return_type;
        } func_type;
    } as;
};

// 类型系统初始化 (初始化基础类型的单例)
void types_init(void);

// 获取基础类型单例
ScoriaType* type_get_basic(TypeKind kind);

// 获取或创建复合类型 (Type Interning)
ScoriaType* type_get_via(ScoriaType* inner);
ScoriaType* type_get_cohors(ScoriaType* inner);
ScoriaType* type_get_acies(ScoriaType* inner, uint32_t length);

// 创建结构体和函数类型
ScoriaType* type_create_forma(Token name, bool is_densa);
void type_forma_add_field(ScoriaType* forma_type, Token name, ScoriaType* field_type);

ScoriaType* type_create_actio(ScoriaType* return_type, ScoriaType** param_types, int param_count);

// 类型比较 (因为使用了 Interning，大部分情况下可以直接比较指针)
bool type_equals(ScoriaType* a, ScoriaType* b);

// 获取类型在内存中的实际字节大小
int type_get_size(ScoriaType* type);

// 判断类型是否有符号/无符号
bool type_is_signed(ScoriaType* type);
bool type_is_unsigned(ScoriaType* type);

#endif // SCORIA_TYPES_H
