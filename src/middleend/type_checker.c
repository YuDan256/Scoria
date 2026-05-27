#include "type_checker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// ---------------------------------------------------------
// 错误处理
// ---------------------------------------------------------
static void type_error(TypeChecker* checker, Token token, const char* message) {
    checker->had_error = true;
    fprintf(stderr, "[linea %d:%d] Erratum Typi ad '%.*s': %s\n", 
            token.line, token.column, token.length, token.start, message);
}

// ---------------------------------------------------------
// 前置声明
// ---------------------------------------------------------
static ScoriaType* resolve_type_node(TypeChecker* checker, AstNode* type_node);
static void check_statement(TypeChecker* checker, AstNode* stmt);
static ScoriaType* check_expression(TypeChecker* checker, AstNode* expr, ScoriaType* expected_type);

// ---------------------------------------------------------
// 类型解析 (AST_TYPE -> ScoriaType)
// ---------------------------------------------------------
static ScoriaType* resolve_type_node(TypeChecker* checker, AstNode* type_node) {
    if (!type_node || type_node->kind != AST_TYPE) return type_get_basic(TY_UNKNOWN);

    // 处理修饰符 (递归处理 inner_type)
    if (type_node->as.type_node.is_via) {
        ScoriaType* inner = resolve_type_node(checker, type_node->as.type_node.inner_type);
        return type_get_via(inner);
    } else if (type_node->as.type_node.is_cohors) {
        ScoriaType* inner = resolve_type_node(checker, type_node->as.type_node.inner_type);
        return type_get_cohors(inner);
    } else if (type_node->as.type_node.is_acies) {
        ScoriaType* inner = resolve_type_node(checker, type_node->as.type_node.inner_type);
        uint32_t length = 0;
        // 简化处理：假设数组大小在语法解析阶段已经是一个常量整数节点
        if (type_node->as.type_node.array_size && type_node->as.type_node.array_size->kind == AST_LITERAL_EXPR) {
            Token tok = type_node->as.type_node.array_size->token;
            char buf[32];
            uint32_t len = tok.length < 31 ? tok.length : 31;
            memcpy(buf, tok.start, len);
            buf[len] = '\0';
            length = (uint32_t)strtoul(buf, NULL, 10);
        } else {
            type_error(checker, type_node->token, "Magnitudo aciei constans esse debet.");
        }
        return type_get_acies(inner, length);
    }

    ScoriaType* base_type = NULL;
    Token base_tok = type_node->as.type_node.base_type;

    // 解析基础类型或结构体名
    switch (base_tok.kind) {
        case TK_TY_I8:  base_type = type_get_basic(TY_I8); break;
        case TK_TY_I16: base_type = type_get_basic(TY_I16); break;
        case TK_TY_I32: base_type = type_get_basic(TY_I32); break;
        case TK_TY_I64: base_type = type_get_basic(TY_I64); break;
        case TK_TY_P8:  base_type = type_get_basic(TY_P8); break;
        case TK_TY_P16: base_type = type_get_basic(TY_P16); break;
        case TK_TY_P32: base_type = type_get_basic(TY_P32); break;
        case TK_TY_P64: base_type = type_get_basic(TY_P64); break;
        case TK_TY_F32: base_type = type_get_basic(TY_F32); break;
        case TK_TY_F64: base_type = type_get_basic(TY_F64); break;
        case TK_TY_LOGICA: base_type = type_get_basic(TY_LOGICA); break;
        case TK_TY_LITTERA: base_type = type_get_basic(TY_LITTERA); break;
        case TK_TY_TEXTUS: base_type = type_get_basic(TY_TEXTUS); break;
        case TK_KW_NIHIL: base_type = type_get_basic(TY_NIHIL); break;
        case TK_IDENTIFIER: {
            Symbol* sym = NULL;
            if (type_node->as.type_node.module_prefix.length > 0) {
                Symbol* mod_sym = symtab_lookup(&checker->symtab, type_node->as.type_node.module_prefix);
                if (mod_sym && mod_sym->kind == SYM_MODULE) {
                    sym = symtab_lookup_in_scope(mod_sym->module_scope, base_tok);
                    if (sym && !sym->is_editus) {
                        type_error(checker, base_tok, "Typus privatus est et extra librum adhiberi non potest.");
                        sym = NULL;
                    }
                } else {
                    type_error(checker, type_node->as.type_node.module_prefix, "Liber non inventus est.");
                }
            } else {
                sym = symtab_lookup(&checker->symtab, base_tok);
            }

            if (sym && (sym->kind == SYM_STRUCT || sym->kind == SYM_UNION)) {
                base_type = sym->type;
            } else {
                type_error(checker, base_tok, "Forma vel unio ignota est.");
                base_type = type_get_basic(TY_UNKNOWN);
            }
            break;
        }
        default:
            base_type = type_get_basic(TY_UNKNOWN);
            break;
    }

    return base_type;
}

// ---------------------------------------------------------
// 编译期值域安全校验
// ---------------------------------------------------------
static uint64_t parse_roman_numeral(const char* str) {
    uint64_t total = 0;
    uint64_t prev_value = 0;
    int len = (int)strlen(str);
    
    // 从右向左遍历，方便处理减法规则 (如 IV = 5 - 1)
    for (int i = len - 1; i >= 0; i--) {
        uint64_t current_value = 0;
        switch (str[i]) {
            case 'I': case 'i': current_value = 1; break;
            case 'V': case 'v': current_value = 5; break;
            case 'X': case 'x': current_value = 10; break;
            case 'L': case 'l': current_value = 50; break;
            case 'C': case 'c': current_value = 100; break;
            case 'D': case 'd': current_value = 500; break;
            case 'M': case 'm': current_value = 1000; break;
            default: break;
        }
        if (current_value < prev_value) {
            total -= current_value;
        } else {
            total += current_value;
        }
        prev_value = current_value;
    }
    return total;
}

static void parse_and_check_literal(TypeChecker* checker, AstNode* expr, ScoriaType* expected_type) {
    Token token = expr->token;
    char buf[128];
    int len = token.length < 127 ? token.length : 127;
    memcpy(buf, token.start, len);
    buf[len] = '\0';

    if (token.kind == TK_INT_CONST) {
        uint64_t val = 0;
        errno = 0;
        if (len > 2 && buf[0] == '0') {
            if (buf[1] == 'x' || buf[1] == 'X') val = strtoull(buf + 2, NULL, 16);
            else if (buf[1] == 'b' || buf[1] == 'B') val = strtoull(buf + 2, NULL, 2);
            else if (buf[1] == 'o' || buf[1] == 'O') val = strtoull(buf + 2, NULL, 8);
            else if (buf[1] == 'r' || buf[1] == 'R') val = parse_roman_numeral(buf + 2);
            else val = strtoull(buf, NULL, 10);
        } else {
            val = strtoull(buf, NULL, 10);
        }

        expr->as.literal_expr.int_val = (int64_t)val;

        bool overflow = (errno == ERANGE);
        if (!overflow && expected_type) {
            // 注意：对于有符号整数，字面量本身总是正数，负号由外层的 AST_UNARY_EXPR 提供。
            // 因此 i8 的字面量最大允许值为 128 (即 |-128|)。
            switch (expected_type->kind) {
                case TY_I8:  if (val > 128ULL) overflow = true; break;
                case TY_I16: if (val > 32768ULL) overflow = true; break;
                case TY_I32: if (val > 2147483648ULL) overflow = true; break;
                case TY_I64: if (val > 9223372036854775808ULL) overflow = true; break;
                case TY_P8:  if (val > 255ULL) overflow = true; break;
                case TY_P16: if (val > 65535ULL) overflow = true; break;
                case TY_P32: if (val > 4294967295ULL) overflow = true; break;
                default: break;
            }
        }

        if (overflow && expected_type) {
            type_error(checker, token, "Valor constans extra fines typi exspectati est.");
        }
    } else if (token.kind == TK_FLOAT_CONST) {
        if (expected_type && expected_type->kind >= TY_I8 && expected_type->kind <= TY_P64) {
            type_error(checker, token, "Numerus fractus in typum integrum implicite converti non potest.");
            return;
        }
        errno = 0;
        double fval = strtod(buf, NULL);
        expr->as.literal_expr.float_val = fval;
        if (errno == ERANGE && expected_type) {
            type_error(checker, token, "Valor constans extra fines typi exspectati est.");
        }
    }
}

// ---------------------------------------------------------
// 表达式类型检查 (Type Checking Pass - Expressions)
// ---------------------------------------------------------
static ScoriaType* check_expression(TypeChecker* checker, AstNode* expr, ScoriaType* expected_type) {
    if (!expr) return type_get_basic(TY_UNKNOWN);

    ScoriaType* type = type_get_basic(TY_UNKNOWN);

    switch (expr->kind) {
        case AST_LITERAL_EXPR:
            switch (expr->token.kind) {
                case TK_INT_CONST:
                case TK_FLOAT_CONST: {
                    parse_and_check_literal(checker, expr, expected_type);
                    if (expected_type && expected_type->kind >= TY_I8 && expected_type->kind <= TY_F64) {
                        type = expected_type;
                    } else {
                        type = (expr->token.kind == TK_INT_CONST) ? type_get_basic(TY_I32) : type_get_basic(TY_F64);
                    }
                    break;
                }
                case TK_BOOL_CONST:   type = type_get_basic(TY_LOGICA); break;
                case TK_CHAR_CONST:   type = type_get_basic(TY_LITTERA); break;
                case TK_STRING_CONST: type = type_get_basic(TY_TEXTUS); break;
                case TK_KW_NIHIL:     type = type_get_basic(TY_NIHIL); break;
                default: break;
            }
            break;

        case AST_ARRAY_LITERAL: {
            if (expr->as.array_literal.element_count == 0) {
                if (expected_type && expected_type->kind == TY_ACIES) {
                    type = expected_type;
                } else {
                    type_error(checker, expr->token, "Acies vacua typum inferre non potest.");
                    type = type_get_acies(type_get_basic(TY_UNKNOWN), 0);
                }
            } else {
                ScoriaType* elem_expected = NULL;
                if (expected_type && expected_type->kind == TY_ACIES) {
                    elem_expected = expected_type->as.array.inner;
                } else if (expected_type && expected_type->kind == TY_COHORS) {
                    elem_expected = expected_type->as.inner;
                }

                ScoriaType* elem_type = check_expression(checker, expr->as.array_literal.elements[0], elem_expected);
                for (int i = 1; i < expr->as.array_literal.element_count; i++) {
                    ScoriaType* t = check_expression(checker, expr->as.array_literal.elements[i], elem_expected ? elem_expected : elem_type);
                    if (!type_equals(elem_type, t)) {
                        type_error(checker, expr->as.array_literal.elements[i]->token, "Typi elementorum in acie non congruunt.");
                    }
                }
                type = type_get_acies(elem_type, expr->as.array_literal.element_count);
            }
            break;
        }

        case AST_STRUCT_LITERAL: {
            ScoriaType* struct_type = check_expression(checker, expr->as.struct_literal.type_expr, NULL);
            
            if (struct_type->kind != TY_FORMA && struct_type->kind != TY_UNIO) {
                type_error(checker, expr->as.struct_literal.type_expr->token, "Symbolum non est forma vel unio.");
            } else {
                if (struct_type->kind == TY_UNIO && expr->as.struct_literal.field_count > 1) {
                    type_error(checker, expr->token, "Unio unum tantum campum initializare potest.");
                }

                // 检查字段
                for (int i = 0; i < expr->as.struct_literal.field_count; i++) {
                    Token field_name = expr->as.struct_literal.field_names[i];
                    ScoriaType* expected_field_type = NULL;
                    
                    bool found = false;
                    for (int j = 0; j < struct_type->as.struct_type.field_count; j++) {
                        StructField f = struct_type->as.struct_type.fields[j];
                        if (f.name.length == field_name.length && memcmp(f.name.start, field_name.start, f.name.length) == 0) {
                            found = true;
                            expected_field_type = f.type;
                            break;
                        }
                    }
                    
                    ScoriaType* val_type = check_expression(checker, expr->as.struct_literal.field_values[i], expected_field_type);
                    
                    if (!found) {
                        type_error(checker, field_name, "Campus in forma vel unione non inventus est.");
                    } else if (!type_equals(expected_field_type, val_type)) {
                        type_error(checker, field_name, "Typus valoris cum typo campi non congruit.");
                    }
                }
            }
            type = struct_type;
            break;
        }

        case AST_IDENT_EXPR: {
            Symbol* sym = symtab_lookup(&checker->symtab, expr->token);
            if (!sym) {
                type_error(checker, expr->token, "Symbolum definitum non est.");
            } else {
                expr->resolved_symbol = sym;
                type = sym->type;
            }
            break;
        }

        case AST_ASSIGN_EXPR: {
            ScoriaType* target_type = check_expression(checker, expr->as.assign.target, NULL);
            ScoriaType* value_type = check_expression(checker, expr->as.assign.value, target_type);
            
            if (!type_equals(target_type, value_type)) {
                type_error(checker, expr->token, "In assignatione typi non congruunt (conversio implicita nulla est).");
            }
            type = target_type;
            break;
        }

        case AST_BINARY_EXPR: {
            ScoriaType* left_type = check_expression(checker, expr->as.binary.left, expected_type);
            ScoriaType* right_type = check_expression(checker, expr->as.binary.right, left_type);

            if (!type_equals(left_type, right_type)) {
                type_error(checker, expr->token, "In expressione binaria typi operandorum non congruunt.");
            }

            switch (expr->as.binary.op.kind) {
                case TK_PLUS: case TK_MINUS: case TK_STAR: case TK_SLASH: case TK_MOD:
                case TK_SHL: case TK_SHR: case TK_AMP: case TK_PIPE: case TK_CARET:
                    type = left_type; // 算术和位运算返回原类型
                    break;
                case TK_EQ: case TK_NEQ: case TK_LT: case TK_LTE: case TK_GT: case TK_GTE:
                case TK_LOGIC_AND: case TK_LOGIC_OR:
                    type = type_get_basic(TY_LOGICA); // 比较和逻辑运算返回 logica
                    break;
                default:
                    break;
            }
            break;
        }

        case AST_UNARY_EXPR: {
            ScoriaType* operand_expected = NULL;
            if (expr->as.unary.op.kind == TK_KW_LOCUS && expected_type && expected_type->kind == TY_VIA) {
                operand_expected = expected_type->as.inner;
            } else if (expr->as.unary.op.kind == TK_KW_TENE && expected_type) {
                operand_expected = type_get_via(expected_type);
            } else if (expr->as.unary.op.kind == TK_LOGIC_NOT) {
                operand_expected = type_get_basic(TY_LOGICA);
            } else {
                operand_expected = expected_type;
            }
            ScoriaType* operand_type = check_expression(checker, expr->as.unary.operand, operand_expected);
            
            if (expr->as.unary.op.kind == TK_KW_LOCUS) {
                type = type_get_via(operand_type);
            } else if (expr->as.unary.op.kind == TK_KW_TENE) {
                if (operand_type->kind == TY_VIA) {
                    type = operand_type->as.inner;
                } else {
                    type_error(checker, expr->token, "'tene' ad 'via' solum applicari potest.");
                }
            } else if (expr->as.unary.op.kind == TK_LOGIC_NOT) {
                if (operand_type->kind != TY_LOGICA) {
                    type_error(checker, expr->token, "Operandus pro '!' logicus esse debet.");
                }
                type = type_get_basic(TY_LOGICA);
            } else {
                type = operand_type;
            }
            break;
        }

        case AST_CAST_EXPR: {
            type = resolve_type_node(checker, expr->as.cast_expr.target_type);
            check_expression(checker, expr->as.cast_expr.value, type);
            break;
        }

        case AST_CALL_EXPR: {
            ScoriaType* callee_type = check_expression(checker, expr->as.call.callee, NULL);
            if (callee_type->kind != TY_ACTIO) {
                type_error(checker, expr->token, "Actiones solae vocari possunt.");
                break;
            }

            if (callee_type->as.func_type.param_count != expr->as.call.arg_count) {
                type_error(checker, expr->token, "Numerus argumentorum non congruit.");
            } else {
                for (int i = 0; i < expr->as.call.arg_count; i++) {
                    ScoriaType* expected_arg_type = callee_type->as.func_type.param_types[i];
                    ScoriaType* arg_type = check_expression(checker, expr->as.call.args[i], expected_arg_type);
                    if (!type_equals(expected_arg_type, arg_type)) {
                        type_error(checker, expr->as.call.args[i]->token, "Typus argumenti non congruit.");
                    }
                }
            }
            type = callee_type->as.func_type.return_type;
            break;
        }

        case AST_MEMBER_EXPR: {
            ScoriaType* obj_type = check_expression(checker, expr->as.member_expr.object, NULL);

            // 模块命名空间访问 (如 math_lib.Vector)
            if (obj_type->kind == TY_MODULE) {
                Symbol* mod_sym = expr->as.member_expr.object->resolved_symbol;
                Symbol* prop_sym = symtab_lookup_in_scope(mod_sym->module_scope, expr->as.member_expr.property);
                if (!prop_sym) {
                    type_error(checker, expr->as.member_expr.property, "Symbolum in libro non inventum est.");
                } else if (!prop_sym->is_editus) {
                    type_error(checker, expr->as.member_expr.property, "Symbolum privatum est et extra librum adhiberi non potest.");
                } else {
                    expr->resolved_symbol = prop_sym; // 将 AST_MEMBER_EXPR 直接重定向到目标符号
                    type = prop_sym->type;
                }
                break;
            }

            if (expr->as.member_expr.is_pointer) {
                if (obj_type->kind != TY_VIA || (obj_type->as.inner->kind != TY_FORMA && obj_type->as.inner->kind != TY_UNIO && obj_type->as.inner->kind != TY_COHORS)) {
                    type_error(checker, expr->token, "Operator '->' ad 'via forma', 'via unio' vel 'via cohors' solum applicari potest.");
                    break;
                }
                obj_type = obj_type->as.inner;
            } else {
                if (obj_type->kind != TY_FORMA && obj_type->kind != TY_UNIO && obj_type->kind != TY_COHORS) {
                    type_error(checker, expr->token, "Operator '.' ad 'forma', 'unio', 'cohors' vel 'liber' solum applicari potest.");
                    break;
                }
            }

            if (obj_type->kind == TY_COHORS) {
                if (expr->as.member_expr.property.length == 5 && strncmp(expr->as.member_expr.property.start, "locus", 5) == 0) {
                    type = type_get_via(obj_type->as.inner);
                } else if (expr->as.member_expr.property.length == 9 && strncmp(expr->as.member_expr.property.start, "longitudo", 9) == 0) {
                    type = type_get_basic(TY_I64);
                } else {
                    type_error(checker, expr->as.member_expr.property, "In cohorte proprietas ignota est (solum 'locus' et 'longitudo' licent).");
                }
                break;
            }

            bool found = false;
            for (int i = 0; i < obj_type->as.struct_type.field_count; i++) {
                StructField field = obj_type->as.struct_type.fields[i];
                if (field.name.length == expr->as.member_expr.property.length &&
                    memcmp(field.name.start, expr->as.member_expr.property.start, field.name.length) == 0) {
                    type = field.type;
                    found = true;
                    break;
                }
            }
            if (!found) {
                type_error(checker, expr->as.member_expr.property, "In forma proprietas ignota est.");
            }
            break;
        }

        case AST_CREA_EXPR: {
            ScoriaType* target_type = resolve_type_node(checker, expr->as.crea_expr.type);
            if (expr->as.crea_expr.count) {
                check_expression(checker, expr->as.crea_expr.count, type_get_basic(TY_I64));
            }
            type = type_get_via(target_type);
            break;
        }

        case AST_NECA_EXPR: {
            ScoriaType* ptr_type = check_expression(checker, expr->as.neca_expr.pointer, NULL);
            if (ptr_type->kind != TY_VIA) {
                type_error(checker, expr->token, "'neca' ad 'via' solum applicari potest.");
            }
            type = type_get_basic(TY_NIHIL);
            break;
        }

        case AST_INDEX_EXPR: {
            ScoriaType* target_type = check_expression(checker, expr->as.index_expr.target, NULL);
            ScoriaType* index_type = check_expression(checker, expr->as.index_expr.index, type_get_basic(TY_I64));
            
            if (target_type->kind == TY_ACIES) {
                type = target_type->as.array.inner;
            } else if (target_type->kind == TY_COHORS || target_type->kind == TY_VIA) {
                type = target_type->as.inner;
            } else {
                type_error(checker, expr->token, "Index ad aciem, cohortem vel viam solum applicari potest.");
            }

            // 简单校验索引是否为整数类型 (i8~i64, p8~p64)
            if (index_type->kind < TY_I8 || index_type->kind > TY_P64) {
                type_error(checker, expr->token, "Index integer esse debet.");
            }
            break;
        }

        case AST_VADE_EXPR:
        case AST_RECEDE_EXPR: {
            ScoriaType* ptr_type = check_expression(checker, expr->as.pointer_offset.pointer, NULL);
            ScoriaType* offset_type = check_expression(checker, expr->as.pointer_offset.offset, type_get_basic(TY_I64));

            if (ptr_type->kind != TY_VIA && ptr_type->kind != TY_COHORS) {
                type_error(checker, expr->token, "'vade/recede' ad 'via' vel 'cohors' solum applicari potest.");
            }
            if (offset_type->kind < TY_I8 || offset_type->kind > TY_P64) {
                type_error(checker, expr->token, "Offset integer esse debet.");
            }
            type = ptr_type;
            break;
        }

        case AST_SCRIBE_EXPR: {
            for (int i = 0; i < expr->as.scribe_expr.arg_count; i++) {
                check_expression(checker, expr->as.scribe_expr.args[i], NULL);
            }
            type = type_get_basic(TY_NIHIL);
            break;
        }

        default:
            break;
    }

    expr->expr_type = type;
    return type;
}

// ---------------------------------------------------------
// 语句类型检查 (Type Checking Pass - Statements)
// ---------------------------------------------------------
static void check_statement(TypeChecker* checker, AstNode* stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
        case AST_BLOCK_STMT:
            symtab_enter_scope(&checker->symtab);
            for (int i = 0; i < stmt->as.block.stmt_count; i++) {
                check_statement(checker, stmt->as.block.statements[i]);
            }
            symtab_leave_scope(&checker->symtab);
            break;

        case AST_EXPR_STMT:
            check_expression(checker, stmt->as.expr_stmt.expr, NULL);
            break;

        case AST_VAR_DECL:
        case AST_CONST_DECL: {
            ScoriaType* declared_type = NULL;
            if (stmt->as.var_decl.type) {
                declared_type = resolve_type_node(checker, stmt->as.var_decl.type);
            }

            ScoriaType* init_type = NULL;
            if (stmt->as.var_decl.initializer) {
                init_type = check_expression(checker, stmt->as.var_decl.initializer, declared_type);
            }

            ScoriaType* final_type = declared_type;
            if (declared_type && init_type) {
                if (!type_equals(declared_type, init_type)) {
                    type_error(checker, stmt->token, "Typus valoris initialis cum typo declarato non congruit.");
                }
            } else if (!declared_type && init_type) {
                final_type = init_type;
            } else if (!declared_type && !init_type) {
                type_error(checker, stmt->token, "Typum declarare vel valorem initialem praebere necesse est.");
                final_type = type_get_basic(TY_UNKNOWN);
            }

            SymbolKind sym_kind = (stmt->kind == AST_CONST_DECL) ? SYM_CONST : SYM_VAR;
            if (checker->current_function_return_type != NULL) {
                if (!symtab_define(&checker->symtab, stmt->as.var_decl.name, sym_kind, final_type, stmt, stmt->as.var_decl.is_editus)) {
                    type_error(checker, stmt->as.var_decl.name, "In hoc scuto nomen iam definitum est.");
                }
            }
            break;
        }

        case AST_RETURN_STMT: {
            ScoriaType* return_value_type = type_get_basic(TY_NIHIL);
            if (stmt->as.return_stmt.value) {
                return_value_type = check_expression(checker, stmt->as.return_stmt.value, checker->current_function_return_type);
            }

            if (checker->current_function_return_type) {
                if (!type_equals(checker->current_function_return_type, return_value_type)) {
                    type_error(checker, stmt->token, "Typus redditus cum declaratione actionis non congruit.");
                }
            } else {
                type_error(checker, stmt->token, "'redde' extra actionem adhiberi non potest.");
            }
            break;
        }

        case AST_IF_STMT: {
            ScoriaType* cond_type = check_expression(checker, stmt->as.if_stmt.condition, type_get_basic(TY_LOGICA));
            if (cond_type->kind != TY_LOGICA) {
                type_error(checker, stmt->token, "Condicio in 'si' logica esse debet.");
            }
            check_statement(checker, stmt->as.if_stmt.then_branch);
            if (stmt->as.if_stmt.else_branch) {
                check_statement(checker, stmt->as.if_stmt.else_branch);
            }
            break;
        }

        case AST_WHILE_STMT: {
            ScoriaType* cond_type = check_expression(checker, stmt->as.while_stmt.condition, type_get_basic(TY_LOGICA));
            if (cond_type->kind != TY_LOGICA) {
                type_error(checker, stmt->token, "Condicio in 'dum' logica esse debet.");
            }
            checker->loop_depth++;
            check_statement(checker, stmt->as.while_stmt.body);
            checker->loop_depth--;
            break;
        }

        case AST_FOR_STMT: {
            symtab_enter_scope(&checker->symtab);
            if (stmt->as.for_stmt.initializer) {
                check_statement(checker, stmt->as.for_stmt.initializer);
            }
            if (stmt->as.for_stmt.condition) {
                ScoriaType* cond_type = check_expression(checker, stmt->as.for_stmt.condition, type_get_basic(TY_LOGICA));
                if (cond_type->kind != TY_LOGICA) {
                    type_error(checker, stmt->token, "Condicio in 'per' logica esse debet.");
                }
            }
            if (stmt->as.for_stmt.increment) {
                check_expression(checker, stmt->as.for_stmt.increment, NULL);
            }
            
            checker->loop_depth++;
            check_statement(checker, stmt->as.for_stmt.body);
            checker->loop_depth--;
            
            symtab_leave_scope(&checker->symtab);
            break;
        }

        case AST_BREAK_STMT:
            if (checker->loop_depth == 0) {
                type_error(checker, stmt->token, "'rumpe' intra cyclum solum adhiberi potest.");
            }
            break;

        case AST_CONTINUE_STMT:
            if (checker->loop_depth == 0) {
                type_error(checker, stmt->token, "'perge' intra cyclum solum adhiberi potest.");
            }
            break;

        case AST_GOTO_STMT:
        case AST_LABEL_STMT:
            break;

        case AST_SWITCH_STMT: {
            ScoriaType* cond_type = check_expression(checker, stmt->as.switch_stmt.condition, NULL);
            for (int i = 0; i < stmt->as.switch_stmt.case_count; i++) {
                for (int j = 0; j < stmt->as.switch_stmt.case_val_counts[i]; j++) {
                    ScoriaType* case_type = check_expression(checker, stmt->as.switch_stmt.case_vals[i][j], cond_type);
                    if (!type_equals(cond_type, case_type)) {
                        type_error(checker, stmt->as.switch_stmt.case_vals[i][j]->token, "Typus casus cum condicione non congruit.");
                    }
                }
                checker->loop_depth++; // 允许 rumpe
                check_statement(checker, stmt->as.switch_stmt.case_stmts[i]);
                checker->loop_depth--;
            }
            if (stmt->as.switch_stmt.default_branch) {
                checker->loop_depth++;
                check_statement(checker, stmt->as.switch_stmt.default_branch);
                checker->loop_depth--;
            }
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------
// 收集全局声明 (Declaration Pass)
// ---------------------------------------------------------
static void collect_declarations(TypeChecker* checker, AstNode* program) {
    if (program->kind != AST_PROGRAM) return;

    // 阶段 1：仅注册结构体名称，解决前向引用问题
    for (int i = 0; i < program->as.program.decl_count; i++) {
        AstNode* decl = program->as.program.declarations[i];
        if (decl->kind == AST_STRUCT_DECL) {
            ScoriaType* forma_type = type_create_forma(decl->as.struct_decl.name, decl->as.struct_decl.is_densa);
            if (!symtab_define(&checker->symtab, decl->as.struct_decl.name, SYM_STRUCT, forma_type, decl, decl->as.struct_decl.is_editus)) {
                type_error(checker, decl->as.struct_decl.name, "Nomen formae iam definitum est.");
            }
        } else if (decl->kind == AST_UNION_DECL) {
            ScoriaType* unio_type = type_create_unio(decl->as.struct_decl.name);
            if (!symtab_define(&checker->symtab, decl->as.struct_decl.name, SYM_UNION, unio_type, decl, decl->as.struct_decl.is_editus)) {
                type_error(checker, decl->as.struct_decl.name, "Nomen unionis iam definitum est.");
            }
        }
    }

    // 阶段 2：解析结构体字段、函数签名和全局变量
    for (int i = 0; i < program->as.program.decl_count; i++) {
        AstNode* decl = program->as.program.declarations[i];
        
        if (decl->kind == AST_STRUCT_DECL || decl->kind == AST_UNION_DECL) {
            Symbol* sym = symtab_lookup(&checker->symtab, decl->as.struct_decl.name);
            if (sym && (sym->type->kind == TY_FORMA || sym->type->kind == TY_UNIO)) {
                ScoriaType* comp_type = sym->type;
                for (int j = 0; j < decl->as.struct_decl.field_count; j++) {
                    AstNode* field = decl->as.struct_decl.fields[j];
                    ScoriaType* field_type = resolve_type_node(checker, field->as.var_decl.type);
                    type_forma_add_field(comp_type, field->as.var_decl.name, field_type);
                }
            }
        } 
        else if (decl->kind == AST_FUNC_DECL) {
            ScoriaType* return_type = type_get_basic(TY_NIHIL);
            if (decl->as.func_decl.return_type) {
                return_type = resolve_type_node(checker, decl->as.func_decl.return_type);
            }

            int param_count = decl->as.func_decl.param_count;
            ScoriaType** param_types = NULL;
            if (param_count > 0) {
                param_types = (ScoriaType**)malloc(sizeof(ScoriaType*) * param_count);
                if (!param_types) {
                    fprintf(stderr, "Memoria non sufficit.\n");
                    exit(1);
                }
                for (int j = 0; j < param_count; j++) {
                    param_types[j] = resolve_type_node(checker, decl->as.func_decl.params[j]->as.var_decl.type);
                }
            }

            ScoriaType* actio_type = type_create_actio(return_type, param_types, param_count);
            if (param_types) free(param_types);

            if (!symtab_define(&checker->symtab, decl->as.func_decl.name, SYM_FUNC, actio_type, decl, decl->as.func_decl.is_editus)) {
                type_error(checker, decl->as.func_decl.name, "Nomen actionis iam definitum est.");
            }
        }
        else if (decl->kind == AST_VAR_DECL || decl->kind == AST_CONST_DECL) {
            ScoriaType* var_type = NULL;
            if (decl->as.var_decl.type) {
                var_type = resolve_type_node(checker, decl->as.var_decl.type);
            }
            SymbolKind sym_kind = (decl->kind == AST_CONST_DECL) ? SYM_CONST : SYM_VAR;
            if (!symtab_define(&checker->symtab, decl->as.var_decl.name, sym_kind, var_type, decl, decl->as.var_decl.is_editus)) {
                type_error(checker, decl->as.var_decl.name, "Nomen iam definitum est.");
            }
        }
    }
}

// ---------------------------------------------------------
// 核心 API
// ---------------------------------------------------------
void type_checker_init(TypeChecker* checker) {
    symtab_init(&checker->symtab);
    checker->had_error = false;
    checker->current_function_return_type = NULL;
    checker->loop_depth = 0;
}

void type_checker_free(TypeChecker* checker) {
    symtab_free(&checker->symtab);
}

bool type_checker_run(TypeChecker* checker, AstNode** programs, int count) {
    // 第一遍：注册所有模块 (Module Registration Pass)
    for (int i = 0; i < count; i++) {
        AstNode* prog = programs[i];
        if (prog->kind != AST_PROGRAM) continue;
        
        Token mod_name = {0};
        for (int j = 0; j < prog->as.program.decl_count; j++) {
            if (prog->as.program.declarations[j]->kind == AST_MODULE_DECL) {
                mod_name = prog->as.program.declarations[j]->as.module_decl.name;
                break;
            }
        }
        
        if (mod_name.length == 0) {
            // 可选的 liber 声明：如果没有，则作为独立匿名模块
            char* anon_name = (char*)malloc(32);
            if (!anon_name) {
                fprintf(stderr, "Memoria non sufficit.\n");
                exit(1);
            }
            snprintf(anon_name, 32, "@anon_%d", i);
            mod_name.start = anon_name;
            mod_name.length = (uint32_t)strlen(anon_name);
            mod_name.kind = TK_IDENTIFIER;
        }
        
        checker->symtab.current_scope = checker->symtab.universe_scope;
        symtab_enter_scope(&checker->symtab); // 创建模块的全局作用域
        Scope* mod_scope = checker->symtab.current_scope;
        checker->symtab.current_scope = checker->symtab.universe_scope;
        
        if (symtab_define(&checker->symtab, mod_name, SYM_MODULE, type_get_basic(TY_MODULE), NULL, true)) {
            Symbol* mod_sym = symtab_lookup_current(&checker->symtab, mod_name);
            mod_sym->module_scope = mod_scope;
            prog->resolved_symbol = mod_sym;
        } else {
            type_error(checker, mod_name, "Nomen libri iam definitum est.");
        }
    }
    
    if (checker->had_error) return false;

    // 第二遍：收集各模块内的全局声明 (Declaration Pass)
    for (int i = 0; i < count; i++) {
        AstNode* prog = programs[i];
        if (!prog->resolved_symbol) continue;
        checker->symtab.current_scope = prog->resolved_symbol->module_scope;
        collect_declarations(checker, prog);
    }
    
    if (checker->had_error) return false;

    // 第三遍：处理模块导入 (Import Pass)
    for (int i = 0; i < count; i++) {
        AstNode* prog = programs[i];
        if (!prog->resolved_symbol) continue;
        checker->symtab.current_scope = prog->resolved_symbol->module_scope;
        
        for (int j = 0; j < prog->as.program.decl_count; j++) {
            AstNode* decl = prog->as.program.declarations[j];
            if (decl->kind == AST_IMPORT_DECL) {
                Symbol* target_mod = symtab_lookup_in_scope(checker->symtab.universe_scope, decl->as.import_decl.module_name);
                if (!target_mod) {
                    type_error(checker, decl->as.import_decl.module_name, "Liber importatus non inventus est.");
                    continue;
                }
                
                if (decl->as.import_decl.item_count == 0) {
                    // consule liber X; (引入整个模块命名空间)
                    symtab_define(&checker->symtab, target_mod->name, SYM_MODULE, type_get_basic(TY_MODULE), NULL, false);
                    Symbol* alias = symtab_lookup_current(&checker->symtab, target_mod->name);
                    alias->module_scope = target_mod->module_scope;
                } else {
                    // de X xcp Y, Z; (精确摘录)
                    for (int k = 0; k < decl->as.import_decl.item_count; k++) {
                        Token item_name = decl->as.import_decl.items[k];
                        Symbol* item_sym = symtab_lookup_in_scope(target_mod->module_scope, item_name);
                        if (!item_sym) {
                            type_error(checker, item_name, "Symbolum in libro non inventum est.");
                        } else if (!item_sym->is_editus) {
                            type_error(checker, item_name, "Symbolum privatum est et importari non potest.");
                        } else {
                            // 创建别名符号
                            symtab_insert_alias(&checker->symtab, item_name, item_sym);
                        }
                    }
                }
            }
        }
    }
    
    if (checker->had_error) return false;

    // 第四遍：深入检查函数体 (Type Checking Pass)
    for (int i = 0; i < count; i++) {
        AstNode* prog = programs[i];
        if (!prog->resolved_symbol) continue;
        checker->symtab.current_scope = prog->resolved_symbol->module_scope;
        
        for (int j = 0; j < prog->as.program.decl_count; j++) {
            AstNode* decl = prog->as.program.declarations[j];
            if (decl->kind == AST_FUNC_DECL) {
                Symbol* sym = symtab_lookup_current(&checker->symtab, decl->as.func_decl.name);
                if (sym && sym->type->kind == TY_ACTIO) {
                    checker->current_function_return_type = sym->type->as.func_type.return_type;
                    symtab_enter_scope(&checker->symtab);
                    
                    for (int k = 0; k < decl->as.func_decl.param_count; k++) {
                        AstNode* param = decl->as.func_decl.params[k];
                        ScoriaType* param_type = resolve_type_node(checker, param->as.var_decl.type);
                        symtab_define(&checker->symtab, param->as.var_decl.name, SYM_VAR, param_type, param, false);
                    }
                    
                    if (decl->as.func_decl.body) {
                        check_statement(checker, decl->as.func_decl.body);
                    }
                    
                    symtab_leave_scope(&checker->symtab);
                    checker->current_function_return_type = NULL;
                }
            }
        }
    }

    return !checker->had_error;
}
