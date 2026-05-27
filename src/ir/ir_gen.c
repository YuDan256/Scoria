#include "ir_gen.h"
#include "../middleend/symtab.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static SirValue* gen_expression(IrBuilder* builder, AstNode* expr);
static SirValue* gen_lvalue(IrBuilder* builder, AstNode* expr);
static void gen_statement(IrBuilder* builder, AstNode* stmt);

static bool is_simple_assign(AstNode* stmt, AstNode** out_target, AstNode** out_val) {
    if (!stmt) return false;
    if (stmt->kind == AST_BLOCK_STMT && stmt->as.block.stmt_count == 1) {
        stmt = stmt->as.block.statements[0];
    }
    if (stmt->kind == AST_EXPR_STMT && stmt->as.expr_stmt.expr->kind == AST_ASSIGN_EXPR) {
        AstNode* assign = stmt->as.expr_stmt.expr;
        if (assign->as.assign.op.kind == TK_ASSIGN && assign->as.assign.target->kind == AST_IDENT_EXPR) {
            ScoriaType* t = assign->as.assign.target->expr_type;
            // 仅对标量整数/指针进行 CMOV 优化 (浮点数和结构体不支持简单的 CMOV)
            if (!t || t->kind == TY_F32 || t->kind == TY_F64 || type_get_size(t) > 8) return false;
            
            *out_target = assign->as.assign.target;
            *out_val = assign->as.assign.value;
            if ((*out_val)->kind == AST_LITERAL_EXPR || (*out_val)->kind == AST_IDENT_EXPR) {
                return true;
            }
        }
    }
    return false;
}

static bool check_mutated(AstNode* node, Symbol* sym) {
    if (!node) return false;
    switch (node->kind) {
        case AST_ASSIGN_EXPR: {
            AstNode* target = node->as.assign.target;
            if (target->kind == AST_IDENT_EXPR && target->resolved_symbol == sym) return true;
            if (check_mutated(node->as.assign.value, sym)) return true;
            if (check_mutated(target, sym)) return true;
            break;
        }
        case AST_UNARY_EXPR:
            if (node->as.unary.op.kind == TK_KW_LOCUS) {
                AstNode* target = node->as.unary.operand;
                if (target->kind == AST_IDENT_EXPR && target->resolved_symbol == sym) return true;
            }
            if (check_mutated(node->as.unary.operand, sym)) return true;
            break;
        case AST_BLOCK_STMT:
            for (int i=0; i<node->as.block.stmt_count; i++) {
                if (check_mutated(node->as.block.statements[i], sym)) return true;
            }
            break;
        case AST_IF_STMT:
            if (check_mutated(node->as.if_stmt.condition, sym)) return true;
            if (check_mutated(node->as.if_stmt.then_branch, sym)) return true;
            if (check_mutated(node->as.if_stmt.else_branch, sym)) return true;
            break;
        case AST_WHILE_STMT:
            if (check_mutated(node->as.while_stmt.condition, sym)) return true;
            if (check_mutated(node->as.while_stmt.body, sym)) return true;
            break;
        case AST_FOR_STMT:
            if (check_mutated(node->as.for_stmt.initializer, sym)) return true;
            if (check_mutated(node->as.for_stmt.condition, sym)) return true;
            if (check_mutated(node->as.for_stmt.increment, sym)) return true;
            if (check_mutated(node->as.for_stmt.body, sym)) return true;
            break;
        case AST_RETURN_STMT:
            if (check_mutated(node->as.return_stmt.value, sym)) return true;
            break;
        case AST_EXPR_STMT:
            if (check_mutated(node->as.expr_stmt.expr, sym)) return true;
            break;
        case AST_BINARY_EXPR:
            if (check_mutated(node->as.binary.left, sym)) return true;
            if (check_mutated(node->as.binary.right, sym)) return true;
            break;
        case AST_CALL_EXPR:
            if (check_mutated(node->as.call.callee, sym)) return true;
            for (int i=0; i<node->as.call.arg_count; i++) {
                if (check_mutated(node->as.call.args[i], sym)) return true;
            }
            break;
        case AST_INDEX_EXPR:
            if (check_mutated(node->as.index_expr.target, sym)) return true;
            if (check_mutated(node->as.index_expr.index, sym)) return true;
            break;
        case AST_MEMBER_EXPR:
            if (check_mutated(node->as.member_expr.object, sym)) return true;
            break;
        case AST_CAST_EXPR:
            if (check_mutated(node->as.cast_expr.value, sym)) return true;
            break;
        case AST_ARRAY_LITERAL:
            for (int i=0; i<node->as.array_literal.element_count; i++) {
                if (check_mutated(node->as.array_literal.elements[i], sym)) return true;
            }
            break;
        case AST_STRUCT_LITERAL:
            for (int i=0; i<node->as.struct_literal.field_count; i++) {
                if (check_mutated(node->as.struct_literal.field_values[i], sym)) return true;
            }
            break;
        case AST_SWITCH_STMT:
            if (check_mutated(node->as.switch_stmt.condition, sym)) return true;
            for (int i=0; i<node->as.switch_stmt.case_count; i++) {
                for (int j=0; j<node->as.switch_stmt.case_val_counts[i]; j++) {
                    if (check_mutated(node->as.switch_stmt.case_vals[i][j], sym)) return true;
                }
                if (check_mutated(node->as.switch_stmt.case_stmts[i], sym)) return true;
            }
            if (check_mutated(node->as.switch_stmt.default_branch, sym)) return true;
            break;
        case AST_VAR_DECL:
        case AST_CONST_DECL:
            if (check_mutated(node->as.var_decl.initializer, sym)) return true;
            break;
        case AST_SCRIBE_EXPR:
            for (int i=0; i<node->as.scribe_expr.arg_count; i++) {
                if (check_mutated(node->as.scribe_expr.args[i], sym)) return true;
            }
            break;
        case AST_CREA_EXPR:
            if (check_mutated(node->as.crea_expr.count, sym)) return true;
            break;
        case AST_NECA_EXPR:
            if (check_mutated(node->as.neca_expr.pointer, sym)) return true;
            break;
        case AST_VADE_EXPR:
        case AST_RECEDE_EXPR:
            if (check_mutated(node->as.pointer_offset.pointer, sym)) return true;
            if (check_mutated(node->as.pointer_offset.offset, sym)) return true;
            break;
        default: break;
    }
    return false;
}

static SirValue* gen_string_slice(IrBuilder* builder, const char* str, int len) {
    char* str_copy = (char*)arena_alloc(&builder->arena, len + 1);
    memcpy(str_copy, str, len);
    str_copy[len] = '\0';
    SirValue* raw_str_val = ir_const_string(builder, str_copy, len);
    ScoriaType* cohors_type = type_get_cohors(type_get_basic(TY_LITTERA));
    SirValue* slice_ptr = ir_build_alloca(builder, cohors_type, 16);
    ir_build_store(builder, raw_str_val, slice_ptr);
    SirValue* len_offset = ir_const_int(builder, type_get_basic(TY_I32), 1);
    SirValue* len_ptr = ir_build_gep(builder, slice_ptr, len_offset, 8, type_get_via(type_get_basic(TY_I64)));
    SirValue* len_val = ir_const_int(builder, type_get_basic(TY_I64), len);
    ir_build_store(builder, len_val, len_ptr);
    return slice_ptr;
}

static void gen_scribe_call(IrBuilder* builder, SirValue* callee, SirValue* arg) {
    SirValue** args = (SirValue**)arena_alloc(&builder->arena, sizeof(SirValue*) * 1);
    args[0] = arg;
    ir_build_call(builder, callee, args, 1, type_get_basic(TY_NIHIL));
}

static void gen_scribe_value(IrBuilder* builder, SirValue* callee, ScoriaType* type, SirValue* ptr) {
    if (type->kind == TY_FORMA || type->kind == TY_UNIO) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%.*s { ", type->as.struct_type.name.length, type->as.struct_type.name.start);
        gen_scribe_call(builder, callee, gen_string_slice(builder, buf, (int)strlen(buf)));
        
        int byte_offset = 0;
        for (int i = 0; i < type->as.struct_type.field_count; i++) {
            StructField field = type->as.struct_type.fields[i];
            int field_size = type_get_size(field.type);
            int field_align = type->as.struct_type.is_densa ? 1 : (field_size > 8 ? 8 : field_size);
            
            if (type->kind == TY_FORMA) {
                if (!type->as.struct_type.is_densa) {
                    byte_offset = (byte_offset + field_align - 1) & ~(field_align - 1);
                }
            } else {
                byte_offset = 0; // 联合体字段偏移始终为 0
            }
            
            snprintf(buf, sizeof(buf), "%.*s: ", field.name.length, field.name.start);
            gen_scribe_call(builder, callee, gen_string_slice(builder, buf, (int)strlen(buf)));
            
            SirValue* index_val = ir_const_int(builder, type_get_basic(TY_I32), byte_offset);
            SirValue* field_ptr = ir_build_gep(builder, ptr, index_val, 1, type_get_via(field.type));
            
            bool is_string_slice = (field.type->kind == TY_COHORS && field.type->as.inner->kind == TY_LITTERA);
            if (field.type->kind == TY_FORMA || field.type->kind == TY_UNIO || field.type->kind == TY_ACIES || (field.type->kind == TY_COHORS && !is_string_slice)) {
                gen_scribe_value(builder, callee, field.type, field_ptr);
            } else if (field.type->kind == TY_COHORS) {
                gen_scribe_call(builder, callee, field_ptr);
            } else {
                SirValue* field_val = ir_build_load(builder, field_ptr);
                gen_scribe_call(builder, callee, field_val);
            }
            
            if (i < type->as.struct_type.field_count - 1) {
                gen_scribe_call(builder, callee, gen_string_slice(builder, ", ", 2));
            }
            
            if (type->kind == TY_FORMA) {
                byte_offset += field_size;
            }
        }
        
        gen_scribe_call(builder, callee, gen_string_slice(builder, " }", 2));
    } else if (type->kind == TY_ACIES) {
        gen_scribe_call(builder, callee, gen_string_slice(builder, "[", 1));
        int len = type->as.array.length;
        ScoriaType* inner = type->as.array.inner;
        if (len == 0 && ptr && ptr->type && ptr->type->kind == TY_VIA && ptr->type->as.inner->kind == TY_ACIES) {
            len = ptr->type->as.inner->as.array.length;
            inner = ptr->type->as.inner->as.array.inner;
        }
        int inner_size = type_get_size(inner);
        
        for (int i = 0; i < len; i++) {
            SirValue* index_val = ir_const_int(builder, type_get_basic(TY_I32), i);
            SirValue* elem_ptr = ir_build_gep(builder, ptr, index_val, inner_size, type_get_via(inner));
            
            bool is_string_slice = (inner->kind == TY_COHORS && inner->as.inner->kind == TY_LITTERA);
            if (inner->kind == TY_FORMA || inner->kind == TY_UNIO || inner->kind == TY_ACIES || (inner->kind == TY_COHORS && !is_string_slice)) {
                gen_scribe_value(builder, callee, inner, elem_ptr);
            } else if (inner->kind == TY_COHORS) {
                gen_scribe_call(builder, callee, elem_ptr);
            } else {
                SirValue* elem_val = ir_build_load(builder, elem_ptr);
                gen_scribe_call(builder, callee, elem_val);
            }
            
            if (i < len - 1) {
                gen_scribe_call(builder, callee, gen_string_slice(builder, ", ", 2));
            }
        }
        gen_scribe_call(builder, callee, gen_string_slice(builder, "]", 1));
    } else if (type->kind == TY_COHORS) {
        // 动态生成 IR 循环来打印切片内容
        gen_scribe_call(builder, callee, gen_string_slice(builder, "[", 1));
        
        ScoriaType* inner = type->as.inner;
        int inner_size = type_get_size(inner);
        
        // 获取切片长度
        SirValue* len_offset = ir_const_int(builder, type_get_basic(TY_I32), 1);
        SirValue* len_ptr = ir_build_gep(builder, ptr, len_offset, 8, type_get_via(type_get_basic(TY_I64)));
        SirValue* len_val = ir_build_load(builder, len_ptr);
        
        // 获取切片原始指针
        SirValue* zero_offset = ir_const_int(builder, type_get_basic(TY_I32), 0);
        SirValue* raw_ptr_ptr = ir_build_gep(builder, ptr, zero_offset, 1, type_get_via(type_get_via(inner)));
        SirValue* raw_ptr = ir_build_load(builder, raw_ptr_ptr);
        
        // 分配循环计数器 i
        SirValue* i_ptr = ir_build_alloca(builder, type_get_basic(TY_I64), 8);
        ir_build_store(builder, ir_const_int(builder, type_get_basic(TY_I64), 0), i_ptr);
        
        SirBlock* cond_block = ir_builder_create_block(builder, "scribe_slice_cond");
        SirBlock* body_block = ir_builder_create_block(builder, "scribe_slice_body");
        SirBlock* exit_block = ir_builder_create_block(builder, "scribe_slice_exit");
        
        ir_build_jmp(builder, cond_block);
        ir_builder_set_insert_point(builder, cond_block);
        
        SirValue* i_val = ir_build_load(builder, i_ptr);
        SirValue* cmp = ir_build_binary(builder, SIR_ICMP_LT, i_val, len_val);
        ir_build_br(builder, cmp, body_block, exit_block);
        
        ir_builder_set_insert_point(builder, body_block);
        
        // 获取元素指针并打印
        SirValue* elem_ptr = ir_build_gep(builder, raw_ptr, i_val, inner_size, type_get_via(inner));
        
        bool is_string_slice = (inner->kind == TY_COHORS && inner->as.inner->kind == TY_LITTERA);
        if (inner->kind == TY_FORMA || inner->kind == TY_UNIO || inner->kind == TY_ACIES || (inner->kind == TY_COHORS && !is_string_slice)) {
            gen_scribe_value(builder, callee, inner, elem_ptr);
        } else if (inner->kind == TY_COHORS) {
            gen_scribe_call(builder, callee, elem_ptr);
        } else {
            SirValue* elem_val = ir_build_load(builder, elem_ptr);
            gen_scribe_call(builder, callee, elem_val);
        }
        
        // 打印逗号
        SirValue* one = ir_const_int(builder, type_get_basic(TY_I64), 1);
        SirValue* len_minus_one = ir_build_binary(builder, SIR_SUB, len_val, one);
        SirValue* cmp_comma = ir_build_binary(builder, SIR_ICMP_LT, i_val, len_minus_one);
        
        SirBlock* comma_block = ir_builder_create_block(builder, "scribe_slice_comma");
        SirBlock* inc_block = ir_builder_create_block(builder, "scribe_slice_inc");
        
        ir_build_br(builder, cmp_comma, comma_block, inc_block);
        
        ir_builder_set_insert_point(builder, comma_block);
        gen_scribe_call(builder, callee, gen_string_slice(builder, ", ", 2));
        ir_build_jmp(builder, inc_block);
        
        ir_builder_set_insert_point(builder, inc_block);
        SirValue* i_next = ir_build_binary(builder, SIR_ADD, i_val, one);
        ir_build_store(builder, i_next, i_ptr);
        ir_build_jmp(builder, cond_block);
        
        ir_builder_set_insert_point(builder, exit_block);
        gen_scribe_call(builder, callee, gen_string_slice(builder, "]", 1));
    }
}

// 获取左值 (L-value) 的内存地址指针
static SirValue* gen_lvalue(IrBuilder* builder, AstNode* expr) {
    if (!expr) return NULL;
    switch (expr->kind) {
        case AST_IDENT_EXPR: {
            Symbol* sym = expr->resolved_symbol;
            if (sym) {
                while (sym->alias_target) sym = sym->alias_target;
                if (sym->type && sym->type->kind == TY_ACTIO) {
                    SirValue* val = (SirValue*)arena_alloc(&builder->arena, sizeof(SirValue));
                    val->kind = SIR_VAL_GLOBAL;
                    val->type = type_get_via(sym->type);
                    char* name = (char*)arena_alloc(&builder->arena, sym->name.length + 1);
                    strncpy(name, sym->name.start, sym->name.length);
                    name[sym->name.length] = '\0';
                    val->as.global_name = name;
                    return val;
                }
                if (sym->ir_val) return sym->ir_val;
            }
            break;
        }
        case AST_UNARY_EXPR: {
            if (expr->as.unary.op.kind == TK_KW_TENE) {
                // tene p (解引用) 的左值就是指针 p 本身的值
                return gen_expression(builder, expr->as.unary.operand);
            }
            break;
        }
        case AST_INDEX_EXPR: {
            SirValue* ptr = NULL;
            ScoriaType* target_type = expr->as.index_expr.target->expr_type;
            if (target_type && target_type->kind == TY_ACIES) {
                // 数组退化为指针：直接取数组的左值地址，不进行 Load
                ptr = gen_lvalue(builder, expr->as.index_expr.target);
            } else if (target_type && target_type->kind == TY_COHORS) {
                // 切片：先取切片的左值地址，然后 GEP 取出内部的游标 (前 8 字节)
                SirValue* slice_ptr = gen_lvalue(builder, expr->as.index_expr.target);
                SirValue* zero_offset = ir_const_int(builder, type_get_basic(TY_I32), 0);
                SirValue* raw_ptr_ptr = ir_build_gep(builder, slice_ptr, zero_offset, 1, type_get_via(type_get_via(expr->expr_type)));
                ptr = ir_build_load(builder, raw_ptr_ptr);
            } else {
                // 裸指针：正常求值得到指针本身
                ptr = gen_expression(builder, expr->as.index_expr.target);
            }
            SirValue* index = gen_expression(builder, expr->as.index_expr.index);
            int element_size = type_get_size(expr->expr_type);
            return ir_build_gep(builder, ptr, index, element_size, type_get_via(expr->expr_type));
        }
        case AST_ARRAY_LITERAL:
        case AST_STRUCT_LITERAL:
            return gen_expression(builder, expr);
        case AST_MEMBER_EXPR: {
            if (expr->resolved_symbol) {
                // 这是一个跨模块的符号访问 (如 math_lib.Vector)，直接返回目标符号的左值
                Symbol* sym = expr->resolved_symbol;
                while (sym->alias_target) sym = sym->alias_target;
                if (sym->type && sym->type->kind == TY_ACTIO) {
                    SirValue* val = (SirValue*)arena_alloc(&builder->arena, sizeof(SirValue));
                    val->kind = SIR_VAL_GLOBAL;
                    val->type = type_get_via(sym->type);
                    char* name = (char*)arena_alloc(&builder->arena, sym->name.length + 1);
                    strncpy(name, sym->name.start, sym->name.length);
                    name[sym->name.length] = '\0';
                    val->as.global_name = name;
                    return val;
                }
                if (sym->ir_val) return sym->ir_val;
                return NULL;
            }

            ScoriaType* obj_type = expr->as.member_expr.object->expr_type;
            Symbol* obj_sym = expr->as.member_expr.object->resolved_symbol;
            if ((obj_type && obj_type->kind == TY_MODULE) || 
                (obj_sym && obj_sym->type && obj_sym->type->kind == TY_MODULE)) {
                SirValue* val = (SirValue*)arena_alloc(&builder->arena, sizeof(SirValue));
                val->kind = SIR_VAL_GLOBAL;
                val->type = type_get_via(expr->expr_type);
                char* name = (char*)arena_alloc(&builder->arena, expr->as.member_expr.property.length + 1);
                strncpy(name, expr->as.member_expr.property.start, expr->as.member_expr.property.length);
                name[expr->as.member_expr.property.length] = '\0';
                val->as.global_name = name;
                return val;
            }

            SirValue* obj_ptr = NULL;
            if (expr->as.member_expr.is_pointer) {
                // p->field: p 已经是指针，直接求值
                obj_ptr = gen_expression(builder, expr->as.member_expr.object);
            } else {
                // obj.field: obj 是值，需要取其左值地址
                obj_ptr = gen_lvalue(builder, expr->as.member_expr.object);
            }
            
            int byte_offset = 0;
            if (obj_type && obj_type->kind == TY_VIA) obj_type = obj_type->as.inner;
            
            if (obj_type && obj_type->kind == TY_COHORS) {
                if (expr->as.member_expr.property.length == 9 && strncmp(expr->as.member_expr.property.start, "longitudo", 9) == 0) {
                    byte_offset = 8;
                } else if (expr->as.member_expr.property.length == 5 && strncmp(expr->as.member_expr.property.start, "locus", 5) == 0) {
                    byte_offset = 0;
                }
            } else if (obj_type && obj_type->kind == TY_FORMA) {
                for (int i = 0; i < obj_type->as.struct_type.field_count; i++) {
                    StructField field = obj_type->as.struct_type.fields[i];
                    int field_size = type_get_size(field.type);
                    int field_align = obj_type->as.struct_type.is_densa ? 1 : (field_size > 8 ? 8 : field_size);
                    
                    if (!obj_type->as.struct_type.is_densa) {
                        byte_offset = (byte_offset + field_align - 1) & ~(field_align - 1);
                    }
                    
                    if (field.name.length == expr->as.member_expr.property.length &&
                        memcmp(field.name.start, expr->as.member_expr.property.start, field.name.length) == 0) {
                        break;
                    }
                    byte_offset += field_size;
                }
            } else if (obj_type && obj_type->kind == TY_UNIO) {
                byte_offset = 0; // 联合体所有字段偏移均为 0
            }
            SirValue* index_val = ir_const_int(builder, type_get_basic(TY_I32), byte_offset);
            return ir_build_gep(builder, obj_ptr, index_val, 1, type_get_via(expr->expr_type));
        }
        default: break;
    }
    return NULL;
}

static SirValue* gen_expression(IrBuilder* builder, AstNode* expr) {
    if (!expr) return NULL;

    switch (expr->kind) {
        case AST_LITERAL_EXPR: {
            if (expr->token.kind == TK_INT_CONST) {
                return ir_const_int(builder, expr->expr_type, expr->as.literal_expr.int_val);
            } else if (expr->token.kind == TK_FLOAT_CONST) {
                return ir_const_float(builder, expr->expr_type, expr->as.literal_expr.float_val);
            } else if (expr->token.kind == TK_BOOL_CONST) {
                bool val = (expr->token.length == 5 && strncmp(expr->token.start, "verum", 5) == 0);
                return ir_const_bool(builder, val);
            } else if (expr->token.kind == TK_STRING_CONST) {
                // 提取字符串内容 (去掉首尾引号) 并处理转义字符
                char* str = (char*)arena_alloc(&builder->arena, expr->token.length - 1);
                int j = 0;
                for (uint32_t i = 1; i < expr->token.length - 1; i++) {
                    if (expr->token.start[i] == '\\' && i + 1 < expr->token.length - 1) {
                        char next_c = expr->token.start[i+1];
                        if (next_c == 'n') { str[j++] = '\n'; i++; continue; }
                        if (next_c == 't') { str[j++] = '\t'; i++; continue; }
                        if (next_c == 'r') { str[j++] = '\r'; i++; continue; }
                        if (next_c == 'a') { str[j++] = '\a'; i++; continue; }
                        if (next_c == 'b') { str[j++] = '\b'; i++; continue; }
                        if (next_c == 'f') { str[j++] = '\f'; i++; continue; }
                        if (next_c == 'v') { str[j++] = '\v'; i++; continue; }
                        if (next_c == '"') { str[j++] = '\"'; i++; continue; }
                        if (next_c == '\'') { str[j++] = '\''; i++; continue; }
                        if (next_c == '\\') { str[j++] = '\\'; i++; continue; }
                        if (next_c == '?') { str[j++] = '\?'; i++; continue; }
                        if (next_c == 'x') {
                            i += 2;
                            int hex_val = 0;
                            int hex_len = 0;
                            while (i < expr->token.length - 1 && hex_len < 2) {
                                char hc = expr->token.start[i];
                                if (hc >= '0' && hc <= '9') hex_val = hex_val * 16 + (hc - '0');
                                else if (hc >= 'a' && hc <= 'f') hex_val = hex_val * 16 + (hc - 'a' + 10);
                                else if (hc >= 'A' && hc <= 'F') hex_val = hex_val * 16 + (hc - 'A' + 10);
                                else break;
                                i++;
                                hex_len++;
                            }
                            str[j++] = (char)hex_val;
                            i--; // 抵消 for 循环的 i++
                            continue;
                        }
                        if (next_c >= '0' && next_c <= '7') {
                            i++;
                            int oct_val = 0;
                            int oct_len = 0;
                            while (i < expr->token.length - 1 && oct_len < 3) {
                                char oc = expr->token.start[i];
                                if (oc >= '0' && oc <= '7') {
                                    oct_val = oct_val * 8 + (oc - '0');
                                    i++;
                                    oct_len++;
                                } else {
                                    break;
                                }
                            }
                            str[j++] = (char)oct_val;
                            i--; // 抵消 for 循环的 i++
                            continue;
                        }
                    }
                    str[j++] = expr->token.start[i];
                }
                str[j] = '\0'; // 仅为编译器内部 C 字符串打印安全保留，不计入 Scoria 物理长度
                
                // 纯净的 Scoria 字符串：绝不偷偷追加 \0，长度就是实际字符数
                SirValue* raw_str_val = ir_const_string(builder, str, j);
                ScoriaType* cohors_type = type_get_cohors(type_get_basic(TY_LITTERA));
                SirValue* slice_ptr = ir_build_alloca(builder, cohors_type, 16);
                
                ir_build_store(builder, raw_str_val, slice_ptr);
                
                SirValue* len_offset = ir_const_int(builder, type_get_basic(TY_I32), 1);
                SirValue* len_ptr = ir_build_gep(builder, slice_ptr, len_offset, 8, type_get_via(type_get_basic(TY_I64)));
                SirValue* len_val = ir_const_int(builder, type_get_basic(TY_I64), j);
                ir_build_store(builder, len_val, len_ptr);
                
                return slice_ptr;
            } else if (expr->token.kind == TK_CHAR_CONST) {
                char c = 0;
                if (expr->token.length >= 3) {
                    if (expr->token.start[1] == '\\') {
                        char next_c = expr->token.start[2];
                        switch (next_c) {
                            case 'n': c = '\n'; break;
                            case 't': c = '\t'; break;
                            case 'r': c = '\r'; break;
                            case 'a': c = '\a'; break;
                            case 'b': c = '\b'; break;
                            case 'f': c = '\f'; break;
                            case 'v': c = '\v'; break;
                            case '\\': c = '\\'; break;
                            case '\'': c = '\''; break;
                            case '\"': c = '\"'; break;
                            case '?': c = '\?'; break;
                            case 'x': {
                                int hex_val = 0;
                                for (uint32_t k = 3; k < expr->token.length - 1 && k < 5; k++) {
                                    char hc = expr->token.start[k];
                                    if (hc >= '0' && hc <= '9') hex_val = hex_val * 16 + (hc - '0');
                                    else if (hc >= 'a' && hc <= 'f') hex_val = hex_val * 16 + (hc - 'a' + 10);
                                    else if (hc >= 'A' && hc <= 'F') hex_val = hex_val * 16 + (hc - 'A' + 10);
                                    else break;
                                }
                                c = (char)hex_val;
                                break;
                            }
                            default:
                                if (next_c >= '0' && next_c <= '7') {
                                    int oct_val = 0;
                                    for (uint32_t k = 2; k < expr->token.length - 1 && k < 5; k++) {
                                        char oc = expr->token.start[k];
                                        if (oc >= '0' && oc <= '7') oct_val = oct_val * 8 + (oc - '0');
                                        else break;
                                    }
                                    c = (char)oct_val;
                                } else {
                                    c = next_c;
                                }
                                break;
                        }
                    } else {
                        c = expr->token.start[1];
                    }
                }
                return ir_const_int(builder, expr->expr_type, (int64_t)c);
            } else if (expr->token.kind == TK_KW_NIHIL) {
                return ir_const_int(builder, expr->expr_type, 0);
            }
            break;
        }
        case AST_ARRAY_LITERAL: {
            ScoriaType* arr_type = expr->expr_type;
            int arr_size = type_get_size(arr_type);
            SirValue* arr_ptr = ir_build_alloca(builder, arr_type, arr_size);
            
            ScoriaType* elem_type = arr_type->as.array.inner;
            int elem_size = type_get_size(elem_type);
            
            for (int i = 0; i < expr->as.array_literal.element_count; i++) {
                SirValue* elem_val = gen_expression(builder, expr->as.array_literal.elements[i]);
                SirValue* index_val = ir_const_int(builder, type_get_basic(TY_I32), i);
                SirValue* elem_ptr = ir_build_gep(builder, arr_ptr, index_val, elem_size, type_get_via(elem_type));
                
                if (elem_type->kind == TY_FORMA || elem_type->kind == TY_UNIO || elem_type->kind == TY_ACIES || elem_type->kind == TY_COHORS) {
                    ir_build_memcpy(builder, elem_ptr, elem_val, elem_size);
                } else {
                    ir_build_store(builder, elem_val, elem_ptr);
                }
            }
            return arr_ptr;
        }
        case AST_STRUCT_LITERAL: {
            ScoriaType* struct_type = expr->expr_type;
            int struct_size = type_get_size(struct_type);
            SirValue* struct_ptr = ir_build_alloca(builder, struct_type, struct_size);
            
            for (int i = 0; i < expr->as.struct_literal.field_count; i++) {
                Token field_name = expr->as.struct_literal.field_names[i];
                SirValue* field_val = gen_expression(builder, expr->as.struct_literal.field_values[i]);
                
                int byte_offset = 0;
                ScoriaType* field_type = NULL;
                
                if (struct_type->kind == TY_FORMA) {
                    for (int j = 0; j < struct_type->as.struct_type.field_count; j++) {
                        StructField f = struct_type->as.struct_type.fields[j];
                        int f_size = type_get_size(f.type);
                        int f_align = struct_type->as.struct_type.is_densa ? 1 : (f_size > 8 ? 8 : f_size);
                        
                        if (!struct_type->as.struct_type.is_densa) {
                            byte_offset = (byte_offset + f_align - 1) & ~(f_align - 1);
                        }
                        
                        if (f.name.length == field_name.length && memcmp(f.name.start, field_name.start, f.name.length) == 0) {
                            field_type = f.type;
                            break;
                        }
                        byte_offset += f_size;
                    }
                } else if (struct_type->kind == TY_UNIO) {
                    byte_offset = 0;
                    for (int j = 0; j < struct_type->as.struct_type.field_count; j++) {
                        StructField f = struct_type->as.struct_type.fields[j];
                        if (f.name.length == field_name.length && memcmp(f.name.start, field_name.start, f.name.length) == 0) {
                            field_type = f.type;
                            break;
                        }
                    }
                }
                
                if (field_type) {
                    SirValue* index_val = ir_const_int(builder, type_get_basic(TY_I32), byte_offset);
                    SirValue* field_ptr = ir_build_gep(builder, struct_ptr, index_val, 1, type_get_via(field_type));
                    
                    if (field_type->kind == TY_FORMA || field_type->kind == TY_UNIO || field_type->kind == TY_ACIES || field_type->kind == TY_COHORS) {
                        ir_build_memcpy(builder, field_ptr, field_val, type_get_size(field_type));
                    } else {
                        ir_build_store(builder, field_val, field_ptr);
                    }
                }
            }
            return struct_ptr;
        }
        case AST_IDENT_EXPR: {
            Symbol* sym = expr->resolved_symbol;
            if (sym) {
                if (sym->type && sym->type->kind == TY_ACTIO) {
                    // 获取函数指针
                    SirValue* val = (SirValue*)arena_alloc(&builder->arena, sizeof(SirValue));
                    val->kind = SIR_VAL_GLOBAL;
                    val->type = type_get_via(sym->type);
                    char* name = (char*)arena_alloc(&builder->arena, sym->name.length + 1);
                    strncpy(name, sym->name.start, sym->name.length);
                    name[sym->name.length] = '\0';
                    val->as.global_name = name;
                    return val;
                } else if (sym->ir_val) {
                    // 结构体、数组和切片作为右值时，退化为指针
                    if (sym->type && (sym->type->kind == TY_FORMA || sym->type->kind == TY_UNIO || sym->type->kind == TY_ACIES || sym->type->kind == TY_COHORS)) {
                        return sym->ir_val;
                    }
                    // 如果 ir_val 已经是直接值 (如被优化的参数)，直接返回
                    if (sym->ir_val->type && type_equals(sym->ir_val->type, sym->type)) {
                        return sym->ir_val;
                    }
                    // 局部变量或全局变量的 ir_val 是指针，使用时需要 load 出来
                    return ir_build_load(builder, sym->ir_val);
                }
            }
            break;
        }
        case AST_ASSIGN_EXPR: {
            ScoriaType* type = expr->expr_type ? expr->expr_type : expr->as.assign.target->expr_type;
            if (type && (type->kind == TY_FORMA || type->kind == TY_UNIO || type->kind == TY_ACIES || type->kind == TY_COHORS)) {
                // 结构体、联合体、数组和切片赋值：使用 memcpy 拷贝内存块
                SirValue* dst_ptr = gen_lvalue(builder, expr->as.assign.target);
                SirValue* src_ptr = gen_expression(builder, expr->as.assign.value); // 右值已退化为指针
                if (dst_ptr && src_ptr) {
                    ir_build_memcpy(builder, dst_ptr, src_ptr, type_get_size(type));
                }
                return NULL;
            } else {
                // 1. 仅对左值求值一次，获取其内存地址
                SirValue* lval = gen_lvalue(builder, expr->as.assign.target);
                // 2. 对右值求值
                SirValue* val = gen_expression(builder, expr->as.assign.value);
                
                // 3. 如果是复合赋值 (+=, -= 等)，执行 Load -> Op
                if (expr->as.assign.op.kind != TK_ASSIGN && lval) {
                    SirValue* current_val = ir_build_load(builder, lval);
                    SirOpcode op = SIR_ADD;
                    bool is_float = (type && (type->kind == TY_F32 || type->kind == TY_F64));
                    switch (expr->as.assign.op.kind) {
                        case TK_PLUS_ASSIGN:  op = is_float ? SIR_FADD : SIR_ADD; break;
                        case TK_MINUS_ASSIGN: op = is_float ? SIR_FSUB : SIR_SUB; break;
                        case TK_STAR_ASSIGN:  op = is_float ? SIR_FMUL : SIR_MUL; break;
                        case TK_SLASH_ASSIGN: op = is_float ? SIR_FDIV : SIR_DIV; break;
                        case TK_MOD_ASSIGN:   op = SIR_MOD; break;
                        case TK_AMP_ASSIGN:   op = SIR_AND; break;
                        case TK_PIPE_ASSIGN:  op = SIR_OR; break;
                        case TK_CARET_ASSIGN: op = SIR_XOR; break;
                        case TK_SHL_ASSIGN:   op = SIR_SHL; break;
                        case TK_SHR_ASSIGN:   op = SIR_SHR; break;
                        default: break;
                    }
                    val = ir_build_binary(builder, op, current_val, val);
                }
                
                // 4. Store 回内存
                if (lval) {
                    ir_build_store(builder, val, lval);
                }
                return val;
            }
        }
        case AST_BINARY_EXPR: {
            if (expr->as.binary.op.kind == TK_LOGIC_AND || expr->as.binary.op.kind == TK_LOGIC_OR) {
                // 短路求值 (Short-circuit Evaluation)
                SirBlock* right_block = ir_builder_create_block(builder, "logic.dextra");
                SirBlock* merge_block = ir_builder_create_block(builder, "logic.exitus");
                
                SirValue* left = gen_expression(builder, expr->as.binary.left);
                SirValue* result_ptr = ir_build_alloca(builder, type_get_basic(TY_LOGICA), 1);
                ir_build_store(builder, left, result_ptr);
                
                if (expr->as.binary.op.kind == TK_LOGIC_AND) {
                    ir_build_br(builder, left, right_block, merge_block);
                } else {
                    ir_build_br(builder, left, merge_block, right_block);
                }
                
                ir_builder_set_insert_point(builder, right_block);
                SirValue* right = gen_expression(builder, expr->as.binary.right);
                ir_build_store(builder, right, result_ptr);
                ir_build_jmp(builder, merge_block);
                
                ir_builder_set_insert_point(builder, merge_block);
                return ir_build_load(builder, result_ptr);
            }

            SirValue* left = gen_expression(builder, expr->as.binary.left);
            SirValue* right = gen_expression(builder, expr->as.binary.right);
            if (!left || !right) return NULL;
            SirOpcode op = SIR_ADD;
            bool is_float = (left->type && (left->type->kind == TY_F32 || left->type->kind == TY_F64));
            switch (expr->as.binary.op.kind) {
                case TK_PLUS:  op = is_float ? SIR_FADD : SIR_ADD; break;
                case TK_MINUS: op = is_float ? SIR_FSUB : SIR_SUB; break;
                case TK_STAR:  op = is_float ? SIR_FMUL : SIR_MUL; break;
                case TK_SLASH: op = is_float ? SIR_FDIV : SIR_DIV; break;
                case TK_MOD:   op = SIR_MOD; break;
                case TK_SHL:   op = SIR_SHL; break;
                case TK_SHR:   op = SIR_SHR; break;
                case TK_AMP:   op = SIR_AND; break;
                case TK_PIPE:  op = SIR_OR; break;
                case TK_CARET: op = SIR_XOR; break;
                case TK_EQ:    op = is_float ? SIR_FCMP_EQ : SIR_ICMP_EQ; break;
                case TK_NEQ:   op = is_float ? SIR_FCMP_NE : SIR_ICMP_NE; break;
                case TK_LT:    op = is_float ? SIR_FCMP_LT : SIR_ICMP_LT; break;
                case TK_LTE:   op = is_float ? SIR_FCMP_LE : SIR_ICMP_LE; break;
                case TK_GT:    op = is_float ? SIR_FCMP_GT : SIR_ICMP_GT; break;
                case TK_GTE:   op = is_float ? SIR_FCMP_GE : SIR_ICMP_GE; break;
                default: break;
            }
            return ir_build_binary(builder, op, left, right);
        }
        case AST_UNARY_EXPR: {
            if (expr->as.unary.op.kind == TK_KW_LOCUS) {
                return gen_lvalue(builder, expr->as.unary.operand);
            } else if (expr->as.unary.op.kind == TK_KW_TENE) {
                SirValue* ptr = gen_expression(builder, expr->as.unary.operand);
                ScoriaType* target_type = expr->expr_type;
                if (target_type && (target_type->kind == TY_FORMA || target_type->kind == TY_UNIO || target_type->kind == TY_ACIES || target_type->kind == TY_COHORS)) {
                    return ptr; // 结构体/联合体/数组/切片退化为指针
                }
                return ir_build_load(builder, ptr);
            } else if (expr->as.unary.op.kind == TK_LOGIC_NOT) {
                SirValue* operand = gen_expression(builder, expr->as.unary.operand);
                SirValue* true_val = ir_const_bool(builder, true);
                return ir_build_binary(builder, SIR_XOR, operand, true_val); // !x 等价于 x XOR true
            } else if (expr->as.unary.op.kind == TK_MINUS) {
                SirValue* operand = gen_expression(builder, expr->as.unary.operand);
                if (!operand) return NULL;
                bool is_float = (operand->type && (operand->type->kind == TY_F32 || operand->type->kind == TY_F64));
                SirValue* zero = is_float ? ir_const_float(builder, operand->type, 0.0) : ir_const_int(builder, operand->type, 0);
                return ir_build_binary(builder, is_float ? SIR_FSUB : SIR_SUB, zero, operand); // -x 等价于 0 - x
            } else if (expr->as.unary.op.kind == TK_TILDE) {
                SirValue* operand = gen_expression(builder, expr->as.unary.operand);
                SirValue* minus_one = ir_const_int(builder, operand->type, -1);
                return ir_build_binary(builder, SIR_XOR, operand, minus_one); // ~x 等价于 x ^ -1
            }
            break;
        }
        case AST_CALL_EXPR: {
            SirValue* callee = NULL;
            Symbol* callee_sym = NULL;
            
            if (expr->as.call.callee->kind == AST_IDENT_EXPR || expr->as.call.callee->kind == AST_MEMBER_EXPR) {
                callee_sym = expr->as.call.callee->resolved_symbol;
                while (callee_sym && callee_sym->alias_target) callee_sym = callee_sym->alias_target;
            }
            
            if (callee_sym && !callee_sym->ir_val) {
                // 全局函数调用 (包括跨模块的命名空间调用)
                callee = (SirValue*)arena_alloc(&builder->arena, sizeof(SirValue));
                callee->kind = SIR_VAL_GLOBAL;
                callee->type = type_get_via(callee_sym->type);
                char* name = (char*)arena_alloc(&builder->arena, callee_sym->name.length + 1);
                strncpy(name, callee_sym->name.start, callee_sym->name.length);
                name[callee_sym->name.length] = '\0';
                callee->as.global_name = name;
            } else {
                // 函数指针调用 (通过表达式求值获取指针)
                callee = gen_expression(builder, expr->as.call.callee);
            }
            
            int arg_count = expr->as.call.arg_count;
            bool hidden_ret = type_get_size(expr->expr_type) > 8;
            int total_args = hidden_ret ? arg_count + 1 : arg_count;
            
            SirValue** args = NULL;
            SirValue* ret_ptr = NULL;
            if (total_args > 0) {
                args = (SirValue**)arena_alloc(&builder->arena, sizeof(SirValue*) * total_args);
                int arg_idx = 0;
                if (hidden_ret) {
                    ret_ptr = ir_build_alloca(builder, expr->expr_type, type_get_size(expr->expr_type));
                    args[arg_idx++] = ret_ptr;
                }
                for (int i = 0; i < arg_count; i++) {
                    args[arg_idx++] = gen_expression(builder, expr->as.call.args[i]);
                }
            }
            
            SirValue* call_res = ir_build_call(builder, callee, args, total_args, hidden_ret ? type_get_via(expr->expr_type) : expr->expr_type);
            return hidden_ret ? ret_ptr : call_res;
        }
        case AST_CAST_EXPR: {
            ScoriaType* target_type = expr->expr_type;
            ScoriaType* src_type = expr->as.cast_expr.value->expr_type;
            
            if (!target_type || !src_type) return NULL;
            
            if (type_equals(src_type, target_type)) {
                return gen_expression(builder, expr->as.cast_expr.value);
            }
            
            if (src_type && target_type && src_type->kind == TY_ACIES && target_type->kind == TY_COHORS) {
                // 数组转切片 (胖指针构造)
                SirValue* slice_ptr = ir_build_alloca(builder, target_type, 16);
                SirValue* arr_ptr = gen_lvalue(builder, expr->as.cast_expr.value);
                ir_build_store(builder, arr_ptr, slice_ptr); // 写入游标 (偏移 0)
                
                SirValue* len_offset = ir_const_int(builder, type_get_basic(TY_I32), 1);
                SirValue* len_ptr = ir_build_gep(builder, slice_ptr, len_offset, 8, type_get_via(type_get_basic(TY_I64)));
                SirValue* len_val = ir_const_int(builder, type_get_basic(TY_I64), src_type->as.array.length);
                ir_build_store(builder, len_val, len_ptr); // 写入长度 (偏移 8)
                
                return slice_ptr;
            }
            
            SirValue* val = gen_expression(builder, expr->as.cast_expr.value);
            return ir_build_cast(builder, val, target_type);
        }
        case AST_VADE_EXPR:
        case AST_RECEDE_EXPR: {
            SirValue* ptr = gen_expression(builder, expr->as.pointer_offset.pointer);
            SirValue* offset = gen_expression(builder, expr->as.pointer_offset.offset);
            if (expr->kind == AST_RECEDE_EXPR) {
                SirValue* zero = ir_const_int(builder, offset->type, 0);
                offset = ir_build_binary(builder, SIR_SUB, zero, offset);
            }
            ScoriaType* element_type = expr->expr_type;
            if (element_type && element_type->kind == TY_VIA) element_type = element_type->as.inner;
            int element_size = type_get_size(element_type);
            return ir_build_gep(builder, ptr, offset, element_size, expr->expr_type);
        }
        case AST_CREA_EXPR: {
            SirValue* callee = (SirValue*)arena_alloc(&builder->arena, sizeof(SirValue));
            callee->kind = SIR_VAL_GLOBAL;
            callee->type = type_get_basic(TY_ACTIO);
            callee->as.global_name = "crea";
            
            // 动态计算分配类型的实际大小
            ScoriaType* allocated_type = expr->expr_type;
            bool is_slice = false;
            if (allocated_type) {
                if (allocated_type->kind == TY_VIA) {
                    allocated_type = allocated_type->as.inner;
                } else if (allocated_type->kind == TY_COHORS) {
                    allocated_type = allocated_type->as.inner;
                    is_slice = true;
                }
            }
            int element_size = type_get_size(allocated_type);
            
            SirValue* count_val;
            if (expr->as.crea_expr.count) {
                count_val = gen_expression(builder, expr->as.crea_expr.count);
            } else {
                count_val = ir_const_int(builder, type_get_basic(TY_I64), 1);
            }
            
            if (!count_val) return NULL;
            
            if (type_get_size(count_val->type) < 8) {
                count_val = ir_build_cast(builder, count_val, type_get_basic(TY_I64));
            }
            
            SirValue* size_val = ir_const_int(builder, type_get_basic(TY_I64), element_size);
            SirValue* total_size = ir_build_binary(builder, SIR_MUL, count_val, size_val);
            
            SirValue** args = (SirValue**)arena_alloc(&builder->arena, sizeof(SirValue*) * 1);
            args[0] = total_size;
            
            SirValue* raw_ptr = ir_build_call(builder, callee, args, 1, type_get_via(allocated_type));
            
            if (is_slice) {
                SirValue* slice_ptr = ir_build_alloca(builder, expr->expr_type, 16);
                ir_build_store(builder, raw_ptr, slice_ptr); // 写入游标 (偏移 0)
                
                SirValue* len_offset = ir_const_int(builder, type_get_basic(TY_I32), 1);
                SirValue* len_ptr = ir_build_gep(builder, slice_ptr, len_offset, 8, type_get_via(type_get_basic(TY_I64)));
                ir_build_store(builder, count_val, len_ptr); // 写入长度 (偏移 8)
                
                return slice_ptr;
            }
            
            return raw_ptr;
        }
        case AST_NECA_EXPR: {
            SirValue* callee = (SirValue*)arena_alloc(&builder->arena, sizeof(SirValue));
            callee->kind = SIR_VAL_GLOBAL;
            callee->type = type_get_basic(TY_ACTIO);
            callee->as.global_name = "neca";
            
            SirValue* ptr_val = gen_expression(builder, expr->as.neca_expr.pointer);
            if (expr->as.neca_expr.pointer->expr_type && expr->as.neca_expr.pointer->expr_type->kind == TY_COHORS) {
                // 如果是切片，提取内部的游标 (前 8 字节)
                SirValue* zero_offset = ir_const_int(builder, type_get_basic(TY_I32), 0);
                SirValue* raw_ptr_ptr = ir_build_gep(builder, ptr_val, zero_offset, 1, type_get_via(type_get_via(type_get_basic(TY_I8))));
                ptr_val = ir_build_load(builder, raw_ptr_ptr);
            }
            
            SirValue** args = (SirValue**)arena_alloc(&builder->arena, sizeof(SirValue*) * 1);
            args[0] = ptr_val;
            
            return ir_build_call(builder, callee, args, 1, type_get_basic(TY_NIHIL));
        }
        case AST_INDEX_EXPR:
        case AST_MEMBER_EXPR: {
            if (expr->kind == AST_MEMBER_EXPR && expr->resolved_symbol) {
                // 跨模块符号访问作为右值
                Symbol* sym = expr->resolved_symbol;
                while (sym->alias_target) sym = sym->alias_target;
                if (sym->type && sym->type->kind == TY_ACTIO) {
                    return gen_lvalue(builder, expr); // 函数指针
                } else if (sym->ir_val) {
                    if (sym->type && (sym->type->kind == TY_FORMA || sym->type->kind == TY_UNIO || sym->type->kind == TY_ACIES || sym->type->kind == TY_COHORS)) {
                        return sym->ir_val;
                    }
                    return ir_build_load(builder, sym->ir_val);
                }
                return NULL;
            }

            // 数组索引和成员访问作为右值时，先取其左值地址
            SirValue* lval = gen_lvalue(builder, expr);
            if (lval) {
                // 结构体、数组、切片和函数退化为指针
                if (expr->expr_type && (expr->expr_type->kind == TY_FORMA || expr->expr_type->kind == TY_ACIES || expr->expr_type->kind == TY_COHORS || expr->expr_type->kind == TY_ACTIO)) {
                    return lval;
                }
                return ir_build_load(builder, lval);
            }
            return NULL;
        }
        case AST_SCRIBE_EXPR: {
            // 将 scribe(a, b, c) 拆分为多个独立的单参数 scribe 调用
            SirValue* callee = (SirValue*)arena_alloc(&builder->arena, sizeof(SirValue));
            callee->kind = SIR_VAL_GLOBAL;
            callee->type = type_get_basic(TY_ACTIO);
            callee->as.global_name = "scribe";
            
            int arg_count = expr->as.scribe_expr.arg_count;
            if (arg_count == 0) return NULL;
            
            SirValue* last_val = NULL;
            for (int i = 0; i < arg_count; i++) {
                AstNode* arg_expr = expr->as.scribe_expr.args[i];
                ScoriaType* arg_type = arg_expr->expr_type;
                SirValue* val = gen_expression(builder, arg_expr);
                
                bool is_string_slice = (arg_type && arg_type->kind == TY_COHORS && arg_type->as.inner->kind == TY_LITTERA);
                if (arg_type && (arg_type->kind == TY_FORMA || arg_type->kind == TY_UNIO || arg_type->kind == TY_ACIES || (arg_type->kind == TY_COHORS && !is_string_slice))) {
                    gen_scribe_value(builder, callee, arg_type, val);
                    last_val = NULL;
                } else {
                    SirValue** args = (SirValue**)arena_alloc(&builder->arena, sizeof(SirValue*) * 1);
                    args[0] = val;
                    last_val = ir_build_call(builder, callee, args, 1, type_get_basic(TY_NIHIL));
                }
            }
            return last_val;
        }
        default: break;
    }
    return NULL;
}

static void gen_statement(IrBuilder* builder, AstNode* stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
        case AST_BLOCK_STMT:
            for (int i = 0; i < stmt->as.block.stmt_count; i++) {
                gen_statement(builder, stmt->as.block.statements[i]);
            }
            break;
        case AST_EXPR_STMT:
            gen_expression(builder, stmt->as.expr_stmt.expr);
            break;
        case AST_VAR_DECL:
        case AST_CONST_DECL: {
            Symbol* sym = stmt->resolved_symbol;
            if (sym) {
                int type_size = type_get_size(sym->type);
                bool is_scalar = (sym->type->kind >= TY_I8 && sym->type->kind <= TY_LOGICA);
                
                AstNode* initializer = stmt->as.var_decl.initializer;
                SirValue* init_val = NULL;
                if (initializer) {
                    init_val = gen_expression(builder, initializer);
                } else if (is_scalar) {
                    init_val = ir_const_int(builder, sym->type, 0);
                }
                
                // 轻量级 Mem2Reg：如果标量变量从未被修改或取址，直接绑定为虚拟寄存器
                if (is_scalar && builder->current_func_body && !check_mutated(builder->current_func_body, sym)) {
                    sym->ir_val = init_val;
                } else {
                    // 1. 在当前函数的栈帧上分配内存 (Alloca)
                    sym->ir_val = ir_build_alloca(builder, sym->type, type_size);
                    
                    // 2. 如果有初始值，生成 Store 或 Memcpy 指令写入栈内存
                    if (init_val) {
                        if (sym->type->kind == TY_FORMA || sym->type->kind == TY_UNIO || sym->type->kind == TY_ACIES || sym->type->kind == TY_COHORS) {
                            ir_build_memcpy(builder, sym->ir_val, init_val, type_size);
                        } else {
                            ir_build_store(builder, init_val, sym->ir_val);
                        }
                    }
                }
            }
            break;
        }
        case AST_RETURN_STMT: {
            SirValue* ret_val = NULL;
            if (stmt->as.return_stmt.value) {
                ret_val = gen_expression(builder, stmt->as.return_stmt.value);
                if (builder->current_hidden_ret_ptr) {
                    ir_build_memcpy(builder, builder->current_hidden_ret_ptr, ret_val, type_get_size(stmt->as.return_stmt.value->expr_type));
                    ret_val = builder->current_hidden_ret_ptr;
                }
            }
            ir_build_ret(builder, ret_val);
            break;
        }
        case AST_IF_STMT: {
            AstNode* then_target = NULL;
            AstNode* then_val = NULL;
            AstNode* else_target = NULL;
            AstNode* else_val = NULL;

            // 模式匹配：如果 if 和 else 都是对同一个变量的简单赋值，降维为 SELECT
            if (stmt->as.if_stmt.else_branch &&
                is_simple_assign(stmt->as.if_stmt.then_branch, &then_target, &then_val) &&
                is_simple_assign(stmt->as.if_stmt.else_branch, &else_target, &else_val) &&
                then_target->resolved_symbol == else_target->resolved_symbol &&
                then_target->resolved_symbol != NULL) {
                
                SirValue* cond = gen_expression(builder, stmt->as.if_stmt.condition);
                SirValue* t_val = gen_expression(builder, then_val);
                SirValue* f_val = gen_expression(builder, else_val);
                
                SirValue* select_val = ir_build_select(builder, cond, t_val, f_val);
                SirValue* lval = gen_lvalue(builder, then_target);
                if (lval) {
                    ir_build_store(builder, select_val, lval);
                }
                break;
            }

            SirValue* cond = gen_expression(builder, stmt->as.if_stmt.condition);
            
            if (cond && cond->kind == SIR_VAL_CONST_BOOL) {
                if (cond->as.bool_val) {
                    gen_statement(builder, stmt->as.if_stmt.then_branch);
                } else if (stmt->as.if_stmt.else_branch) {
                    gen_statement(builder, stmt->as.if_stmt.else_branch);
                }
                break;
            }

            SirBlock* then_block = ir_builder_create_block(builder, "si.verum");
            SirBlock* else_block = stmt->as.if_stmt.else_branch ? ir_builder_create_block(builder, "si.falsum") : NULL;
            SirBlock* merge_block = ir_builder_create_block(builder, "si.exitus");

            ir_build_br(builder, cond, then_block, else_block ? else_block : merge_block);

            ir_builder_set_insert_point(builder, then_block);
            gen_statement(builder, stmt->as.if_stmt.then_branch);
            ir_build_jmp(builder, merge_block);

            if (else_block) {
                ir_builder_set_insert_point(builder, else_block);
                gen_statement(builder, stmt->as.if_stmt.else_branch);
                ir_build_jmp(builder, merge_block);
            }

            ir_builder_set_insert_point(builder, merge_block);
            break;
        }
        case AST_WHILE_STMT: {
            SirBlock* body_block = ir_builder_create_block(builder, "dum.corpus");
            SirBlock* cond_block = ir_builder_create_block(builder, "dum.cond");
            SirBlock* exit_block = ir_builder_create_block(builder, "dum.exitus");

            ir_build_jmp(builder, cond_block);

            // 保存外层循环上下文
            SirBlock* prev_cond = builder->current_loop_cond;
            SirBlock* prev_exit = builder->current_loop_exit;
            builder->current_loop_cond = cond_block;
            builder->current_loop_exit = exit_block;

            ir_builder_set_insert_point(builder, body_block);
            gen_statement(builder, stmt->as.while_stmt.body);
            ir_build_jmp(builder, cond_block);

            ir_builder_set_insert_point(builder, cond_block);
            SirValue* cond = gen_expression(builder, stmt->as.while_stmt.condition);
            
            if (cond && cond->kind == SIR_VAL_CONST_BOOL && !cond->as.bool_val) {
                ir_build_jmp(builder, exit_block);
            } else {
                ir_build_br(builder, cond, body_block, exit_block);
            }

            // 恢复外层循环上下文
            builder->current_loop_cond = prev_cond;
            builder->current_loop_exit = prev_exit;

            ir_builder_set_insert_point(builder, exit_block);
            break;
        }
        case AST_FOR_STMT: {
            if (stmt->as.for_stmt.initializer) {
                gen_statement(builder, stmt->as.for_stmt.initializer);
            }

            SirBlock* body_block = ir_builder_create_block(builder, "per.corpus");
            SirBlock* inc_block = ir_builder_create_block(builder, "per.inc");
            SirBlock* cond_block = ir_builder_create_block(builder, "per.cond");
            SirBlock* exit_block = ir_builder_create_block(builder, "per.exitus");

            ir_build_jmp(builder, cond_block);

            SirBlock* prev_cond = builder->current_loop_cond;
            SirBlock* prev_exit = builder->current_loop_exit;
            builder->current_loop_cond = inc_block; // perge (continue) 跳转到步进块
            builder->current_loop_exit = exit_block;

            ir_builder_set_insert_point(builder, body_block);
            gen_statement(builder, stmt->as.for_stmt.body);
            ir_build_jmp(builder, inc_block);

            ir_builder_set_insert_point(builder, inc_block);
            if (stmt->as.for_stmt.increment) {
                gen_expression(builder, stmt->as.for_stmt.increment);
            }
            ir_build_jmp(builder, cond_block);

            ir_builder_set_insert_point(builder, cond_block);
            if (stmt->as.for_stmt.condition) {
                SirValue* cond = gen_expression(builder, stmt->as.for_stmt.condition);
                if (cond && cond->kind == SIR_VAL_CONST_BOOL && !cond->as.bool_val) {
                    ir_build_jmp(builder, exit_block);
                } else {
                    ir_build_br(builder, cond, body_block, exit_block);
                }
            } else {
                ir_build_jmp(builder, body_block);
            }

            builder->current_loop_cond = prev_cond;
            builder->current_loop_exit = prev_exit;

            ir_builder_set_insert_point(builder, exit_block);
            break;
        }
        case AST_BREAK_STMT: {
            if (builder->current_loop_exit) {
                ir_build_jmp(builder, builder->current_loop_exit);
            }
            break;
        }
        case AST_CONTINUE_STMT: {
            if (builder->current_loop_cond) {
                ir_build_jmp(builder, builder->current_loop_cond);
            }
            break;
        }
        case AST_GOTO_STMT: {
            SirBlock* target_block = ir_builder_get_or_create_label_block(builder, stmt->as.goto_stmt.label_name.start, stmt->as.goto_stmt.label_name.length);
            ir_build_jmp(builder, target_block);
            
            SirBlock* next_block = ir_builder_create_block(builder, "post_sali");
            ir_builder_set_insert_point(builder, next_block);
            break;
        }
        case AST_LABEL_STMT: {
            SirBlock* label_block = ir_builder_get_or_create_label_block(builder, stmt->as.label_stmt.name.start, stmt->as.label_stmt.name.length);
            
            if (builder->current_block && (!builder->current_block->last_inst || 
                (builder->current_block->last_inst->opcode != SIR_JMP && 
                 builder->current_block->last_inst->opcode != SIR_BR && 
                 builder->current_block->last_inst->opcode != SIR_RET))) {
                ir_build_jmp(builder, label_block);
            }
            
            ir_builder_set_insert_point(builder, label_block);
            break;
        }
        case AST_SWITCH_STMT: {
            SirValue* cond = gen_expression(builder, stmt->as.switch_stmt.condition);

            int total_case_vals = 0;
            for (int i = 0; i < stmt->as.switch_stmt.case_count; i++) {
                total_case_vals += stmt->as.switch_stmt.case_val_counts[i];
            }

            SirValue** flat_case_vals = NULL;
            SirBlock** flat_case_blocks = NULL;
            if (total_case_vals > 0) {
                flat_case_vals = (SirValue**)arena_alloc(&builder->arena, sizeof(SirValue*) * total_case_vals);
                flat_case_blocks = (SirBlock**)arena_alloc(&builder->arena, sizeof(SirBlock*) * total_case_vals);
            }

            SirBlock* default_block = ir_builder_create_block(builder, "elige.aliter");
            SirBlock* exit_block = ir_builder_create_block(builder, "elige.exitus");

            SirBlock** branch_blocks = (SirBlock**)arena_alloc(&builder->arena, sizeof(SirBlock*) * stmt->as.switch_stmt.case_count);

            int flat_idx = 0;
            for (int i = 0; i < stmt->as.switch_stmt.case_count; i++) {
                branch_blocks[i] = ir_builder_create_block(builder, "elige.casus");
                for (int j = 0; j < stmt->as.switch_stmt.case_val_counts[i]; j++) {
                    flat_case_vals[flat_idx] = gen_expression(builder, stmt->as.switch_stmt.case_vals[i][j]);
                    flat_case_blocks[flat_idx] = branch_blocks[i];
                    flat_idx++;
                }
            }

            ir_build_switch(builder, cond, default_block, flat_case_vals, flat_case_blocks, total_case_vals);

            SirBlock* prev_exit = builder->current_loop_exit;
            builder->current_loop_exit = exit_block; // 允许 rumpe 跳出 switch

            for (int i = 0; i < stmt->as.switch_stmt.case_count; i++) {
                ir_builder_set_insert_point(builder, branch_blocks[i]);
                gen_statement(builder, stmt->as.switch_stmt.case_stmts[i]);
                // 默认不贯穿：如果块末尾没有跳转，自动跳到出口
                if (builder->current_block && (!builder->current_block->last_inst ||
                    (builder->current_block->last_inst->opcode != SIR_JMP &&
                     builder->current_block->last_inst->opcode != SIR_BR &&
                     builder->current_block->last_inst->opcode != SIR_RET))) {
                    ir_build_jmp(builder, exit_block);
                }
            }

            ir_builder_set_insert_point(builder, default_block);
            if (stmt->as.switch_stmt.default_branch) {
                gen_statement(builder, stmt->as.switch_stmt.default_branch);
            }
            if (builder->current_block && (!builder->current_block->last_inst ||
                (builder->current_block->last_inst->opcode != SIR_JMP &&
                 builder->current_block->last_inst->opcode != SIR_BR &&
                 builder->current_block->last_inst->opcode != SIR_RET))) {
                ir_build_jmp(builder, exit_block);
            }

            builder->current_loop_exit = prev_exit;
            ir_builder_set_insert_point(builder, exit_block);
            break;
        }
        default: break;
    }
}

void ir_gen_generate(IrBuilder* builder, AstNode** programs, int count, int opt_level) {
    // 查找或创建全局初始化函数 __scoria_init
    SirFunction* init_func = NULL;
    for (SirFunction* f = builder->module->first_func; f; f = f->next) {
        if (strcmp(f->name, "__scoria_init") == 0) {
            init_func = f;
            break;
        }
    }
    
    SirBlock* current_init_block = NULL;
    if (!init_func) {
        ScoriaType* init_func_type = type_create_actio(type_get_basic(TY_NIHIL), NULL, 0);
        init_func = ir_builder_create_function(builder, "__scoria_init", init_func_type);
        current_init_block = ir_builder_create_block(builder, "ingressus");
    } else {
        current_init_block = init_func->last_block;
        // 移除之前的 ret 指令，以便继续追加初始化代码
        if (current_init_block->last_inst && current_init_block->last_inst->opcode == SIR_RET) {
            SirInst* ret_inst = current_init_block->last_inst;
            current_init_block->last_inst = ret_inst->prev;
            if (current_init_block->last_inst) {
                current_init_block->last_inst->next = NULL;
            } else {
                current_init_block->first_inst = NULL;
            }
        }
    }

    // 遍历所有模块生成 IR
    for (int p = 0; p < count; p++) {
        AstNode* program = programs[p];
        if (!program || program->kind != AST_PROGRAM) continue;

        for (int i = 0; i < program->as.program.decl_count; i++) {
            AstNode* decl = program->as.program.declarations[i];
            
            if (decl->kind == AST_VAR_DECL || decl->kind == AST_CONST_DECL) {
            Symbol* sym = decl->resolved_symbol;
            if (sym) {
                int size = type_get_size(sym->type);
                SirGlobalVar* gvar = ir_builder_create_global(builder, sym->name.start, sym->name.length, sym->type, size);
                
                SirValue* val = (SirValue*)arena_alloc(&builder->arena, sizeof(SirValue));
                val->kind = SIR_VAL_GLOBAL;
                val->type = type_get_via(sym->type);
                val->as.global_name = gvar->name;
                sym->ir_val = val;

                AstNode* initializer = decl->as.var_decl.initializer;
                if (initializer) {
                    SirFunction* prev_func = builder->current_func;
                    SirBlock* prev_block = builder->current_block;
                    
                    builder->current_func = init_func;
                    ir_builder_set_insert_point(builder, current_init_block);
                    
                    SirValue* init_val = gen_expression(builder, initializer);
                    if (sym->type->kind == TY_FORMA || sym->type->kind == TY_UNIO || sym->type->kind == TY_ACIES || sym->type->kind == TY_COHORS) {
                        ir_build_memcpy(builder, sym->ir_val, init_val, size);
                    } else {
                        ir_build_store(builder, init_val, sym->ir_val);
                    }
                    
                    current_init_block = builder->current_block;
                    
                    builder->current_func = prev_func;
                    ir_builder_set_insert_point(builder, prev_block);
                }
            }
        } else if (decl->kind == AST_FUNC_DECL) {
            Symbol* sym = decl->resolved_symbol;
            if (!sym || sym->type->kind != TY_ACTIO) continue;

            if (!decl->as.func_decl.body) {
                // 外部函数声明 (barbara)
                ir_builder_add_extern(builder, decl->as.func_decl.name.start, decl->as.func_decl.name.length, 
                                      decl->as.func_decl.dll_name.start, decl->as.func_decl.dll_name.length);
                continue;
            }

            // 1. 提取函数名并创建 SIR 函数
            char* func_name = (char*)arena_alloc(&builder->arena, decl->as.func_decl.name.length + 1);
            memcpy(func_name, decl->as.func_decl.name.start, decl->as.func_decl.name.length);
            func_name[decl->as.func_decl.name.length] = '\0';

            ir_builder_create_function(builder, func_name, sym->type);
            
            // 2. 创建入口基本块 (Entry Block)
            SirBlock* entry_block = ir_builder_create_block(builder, "ingressus"); // 拉丁语 entry
            ir_builder_set_insert_point(builder, entry_block);

            builder->current_hidden_ret_ptr = NULL;
            bool hidden_ret = type_get_size(sym->type->as.func_type.return_type) > 8;
            int param_offset = hidden_ret ? 1 : 0;
            
            // 3. 集中获取所有参数 (避免后续指令破坏传参寄存器 RCX/RDX/R8/R9)
            int total_params = decl->as.func_decl.param_count + (hidden_ret ? 1 : 0);
            SirValue** arg_vals = (SirValue**)arena_alloc(&builder->arena, sizeof(SirValue*) * (total_params > 0 ? total_params : 1));
            
            if (hidden_ret) {
                arg_vals[0] = ir_get_param(builder, 0, type_get_via(sym->type->as.func_type.return_type));
                builder->current_hidden_ret_ptr = arg_vals[0];
            }
            for (int j = 0; j < decl->as.func_decl.param_count; j++) {
                Symbol* param_sym = decl->as.func_decl.params[j]->resolved_symbol;
                if (param_sym) {
                    ScoriaType* ptype = param_sym->type;
                    if (type_get_size(ptype) > 8) ptype = type_get_via(ptype);
                    arg_vals[j + param_offset] = ir_get_param(builder, j + param_offset, ptype);
                }
            }

            // 4. 为参数分配栈空间并存储传入的值 (或直接使用寄存器)
            for (int j = 0; j < decl->as.func_decl.param_count; j++) {
                AstNode* param_node = decl->as.func_decl.params[j];
                Symbol* param_sym = param_node->resolved_symbol;
                if (param_sym) {
                    int param_size = type_get_size(param_sym->type);
                    bool is_scalar = (param_sym->type->kind >= TY_I8 && param_sym->type->kind <= TY_LOGICA);
                    
                    if (is_scalar && !check_mutated(decl->as.func_decl.body, param_sym)) {
                        // 极速优化：参数未被修改且为标量，直接使用寄存器，消除 Alloca 和 Load/Store
                        param_sym->ir_val = arg_vals[j + param_offset];
                    } else {
                        param_sym->ir_val = ir_build_alloca(builder, param_sym->type, param_size);
                        if (param_size > 8) {
                            ir_build_memcpy(builder, param_sym->ir_val, arg_vals[j + param_offset], param_size);
                        } else {
                            ir_build_store(builder, arg_vals[j + param_offset], param_sym->ir_val);
                        }
                    }
                }
            }

            // 4. 递归生成函数体指令
            if (decl->as.func_decl.body) {
                builder->current_func_body = decl->as.func_decl.body;
                gen_statement(builder, decl->as.func_decl.body);
                builder->current_func_body = NULL;
            }
            
            // 5. 安全兜底：如果基本块最后没有返回指令，自动补全
            if (builder->current_block && (!builder->current_block->last_inst || builder->current_block->last_inst->opcode != SIR_RET)) {
                if (sym->type->as.func_type.return_type->kind == TY_NIHIL) {
                    ir_build_ret(builder, NULL);
                } else {
                    // 对于非 nihil 返回类型，补全默认的 0 返回值以防止控制流崩溃
                    SirValue* zero_val = ir_const_int(builder, sym->type->as.func_type.return_type, 0);
                    ir_build_ret(builder, zero_val);
                }
            }
        }
    }
    } // end for programs

    // 结束 __scoria_init 函数
    SirFunction* prev_func = builder->current_func;
    SirBlock* prev_block = builder->current_block;
    builder->current_func = init_func;
    ir_builder_set_insert_point(builder, current_init_block);
    ir_build_ret(builder, NULL);
    builder->current_func = prev_func;
    ir_builder_set_insert_point(builder, prev_block);

    // 执行 IR 级优化
    ir_optimize_module(builder, opt_level);
}
