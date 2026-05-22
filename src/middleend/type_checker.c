#include "type_checker.h"
#include <stdio.h>
#include <stdlib.h>

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
static ScoriaType* check_expression(TypeChecker* checker, AstNode* expr);

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
            // TODO: 完善常量表达式求值，目前暂存为 0
            length = 0; 
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
            Symbol* sym = symtab_lookup(&checker->symtab, base_tok);
            if (sym && sym->kind == SYM_STRUCT) {
                base_type = sym->type;
            } else {
                type_error(checker, base_tok, "Forma ignota est.");
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
// 表达式类型检查 (Type Checking Pass - Expressions)
// ---------------------------------------------------------
static ScoriaType* check_expression(TypeChecker* checker, AstNode* expr) {
    if (!expr) return type_get_basic(TY_UNKNOWN);

    ScoriaType* type = type_get_basic(TY_UNKNOWN);

    switch (expr->kind) {
        case AST_LITERAL_EXPR:
            switch (expr->token.kind) {
                case TK_INT_CONST:    type = type_get_basic(TY_I32); break;
                case TK_FLOAT_CONST:  type = type_get_basic(TY_F64); break;
                case TK_BOOL_CONST:   type = type_get_basic(TY_LOGICA); break;
                case TK_CHAR_CONST:   type = type_get_basic(TY_LITTERA); break;
                case TK_STRING_CONST: type = type_get_basic(TY_TEXTUS); break;
                case TK_KW_NIHIL:     type = type_get_basic(TY_NIHIL); break;
                default: break;
            }
            break;

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
            ScoriaType* target_type = check_expression(checker, expr->as.assign.target);
            ScoriaType* value_type = check_expression(checker, expr->as.assign.value);
            
            if (!type_equals(target_type, value_type)) {
                type_error(checker, expr->token, "In assignatione typi non congruunt (conversio implicita nulla est).");
            }
            type = target_type;
            break;
        }

        case AST_BINARY_EXPR: {
            ScoriaType* left_type = check_expression(checker, expr->as.binary.left);
            ScoriaType* right_type = check_expression(checker, expr->as.binary.right);

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
            ScoriaType* operand_type = check_expression(checker, expr->as.unary.operand);
            
            if (operand_type->kind == TY_UNKNOWN) {
                break; // 防止级联报错
            }

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
            check_expression(checker, expr->as.cast_expr.value);
            break;
        }

        case AST_CALL_EXPR: {
            ScoriaType* callee_type = check_expression(checker, expr->as.call.callee);
            if (callee_type->kind == TY_UNKNOWN) {
                break; // 防止级联报错
            }
            if (callee_type->kind != TY_ACTIO) {
                type_error(checker, expr->token, "Actiones solae vocari possunt.");
                break;
            }

            if (callee_type->as.func_type.param_count != expr->as.call.arg_count) {
                type_error(checker, expr->token, "Numerus argumentorum non congruit.");
            } else {
                for (int i = 0; i < expr->as.call.arg_count; i++) {
                    ScoriaType* arg_type = check_expression(checker, expr->as.call.args[i]);
                    if (!type_equals(callee_type->as.func_type.param_types[i], arg_type)) {
                        type_error(checker, expr->as.call.args[i]->token, "Typus argumenti non congruit.");
                    }
                }
            }
            type = callee_type->as.func_type.return_type;
            break;
        }

        case AST_MEMBER_EXPR: {
            ScoriaType* obj_type = check_expression(checker, expr->as.member_expr.object);
            
            if (obj_type->kind == TY_UNKNOWN) {
                break; // 防止级联报错
            }

            if (expr->as.member_expr.is_pointer) {
                if (obj_type->kind != TY_VIA || obj_type->as.inner->kind != TY_FORMA) {
                    type_error(checker, expr->token, "Operator '->' ad 'via forma' solum applicari potest.");
                    break;
                }
                obj_type = obj_type->as.inner;
            } else {
                if (obj_type->kind != TY_FORMA) {
                    type_error(checker, expr->token, "Operator '.' ad 'forma' solum applicari potest.");
                    break;
                }
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
                check_expression(checker, expr->as.crea_expr.count);
            }
            type = type_get_via(target_type);
            break;
        }

        case AST_NECA_EXPR: {
            ScoriaType* ptr_type = check_expression(checker, expr->as.neca_expr.pointer);
            if (ptr_type->kind != TY_UNKNOWN && ptr_type->kind != TY_VIA) {
                type_error(checker, expr->token, "'neca' ad 'via' solum applicari potest.");
            }
            type = type_get_basic(TY_NIHIL);
            break;
        }

        case AST_INDEX_EXPR: {
            ScoriaType* target_type = check_expression(checker, expr->as.index_expr.target);
            ScoriaType* index_type = check_expression(checker, expr->as.index_expr.index);
            
            if (target_type->kind == TY_UNKNOWN) {
                // skip
            } else if (target_type->kind == TY_ACIES) {
                type = target_type->as.array.inner;
            } else if (target_type->kind == TY_COHORS || target_type->kind == TY_VIA) {
                type = target_type->as.inner;
            } else {
                type_error(checker, expr->token, "Index ad aciem, cohortem vel viam solum applicari potest.");
            }

            // 简单校验索引是否为整数类型 (i8~i64, p8~p64)
            if (index_type->kind != TY_UNKNOWN && (index_type->kind < TY_I8 || index_type->kind > TY_P64)) {
                type_error(checker, expr->token, "Index integer esse debet.");
            }
            break;
        }

        case AST_VADE_EXPR:
        case AST_RECEDE_EXPR: {
            ScoriaType* ptr_type = check_expression(checker, expr->as.pointer_offset.pointer);
            ScoriaType* offset_type = check_expression(checker, expr->as.pointer_offset.offset);

            if (ptr_type->kind != TY_UNKNOWN && ptr_type->kind != TY_VIA && ptr_type->kind != TY_COHORS) {
                type_error(checker, expr->token, "'vade/recede' ad 'via' vel 'cohors' solum applicari potest.");
            }
            if (offset_type->kind != TY_UNKNOWN && (offset_type->kind < TY_I8 || offset_type->kind > TY_P64)) {
                type_error(checker, expr->token, "Offset integer esse debet.");
            }
            type = ptr_type;
            break;
        }

        case AST_SCRIBE_EXPR: {
            for (int i = 0; i < expr->as.scribe_expr.arg_count; i++) {
                check_expression(checker, expr->as.scribe_expr.args[i]);
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
            check_expression(checker, stmt->as.expr_stmt.expr);
            break;

        case AST_VAR_DECL:
        case AST_CONST_DECL: {
            ScoriaType* declared_type = NULL;
            if (stmt->as.var_decl.type) {
                declared_type = resolve_type_node(checker, stmt->as.var_decl.type);
            }

            ScoriaType* init_type = NULL;
            if (stmt->as.var_decl.initializer) {
                init_type = check_expression(checker, stmt->as.var_decl.initializer);
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
                return_value_type = check_expression(checker, stmt->as.return_stmt.value);
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
            ScoriaType* cond_type = check_expression(checker, stmt->as.if_stmt.condition);
            if (cond_type->kind != TY_UNKNOWN && cond_type->kind != TY_LOGICA) {
                type_error(checker, stmt->token, "Condicio in 'si' logica esse debet.");
            }
            check_statement(checker, stmt->as.if_stmt.then_branch);
            if (stmt->as.if_stmt.else_branch) {
                check_statement(checker, stmt->as.if_stmt.else_branch);
            }
            break;
        }

        case AST_WHILE_STMT: {
            ScoriaType* cond_type = check_expression(checker, stmt->as.while_stmt.condition);
            if (cond_type->kind != TY_UNKNOWN && cond_type->kind != TY_LOGICA) {
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
                ScoriaType* cond_type = check_expression(checker, stmt->as.for_stmt.condition);
                if (cond_type->kind != TY_UNKNOWN && cond_type->kind != TY_LOGICA) {
                    type_error(checker, stmt->token, "Condicio in 'per' logica esse debet.");
                }
            }
            if (stmt->as.for_stmt.increment) {
                check_expression(checker, stmt->as.for_stmt.increment);
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

        default:
            break;
    }
}

// ---------------------------------------------------------
// 第一遍：收集全局声明 (Declaration Pass)
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
        }
    }

    // 阶段 2：解析结构体字段、函数签名和全局变量
    for (int i = 0; i < program->as.program.decl_count; i++) {
        AstNode* decl = program->as.program.declarations[i];
        
        if (decl->kind == AST_STRUCT_DECL) {
            Symbol* sym = symtab_lookup(&checker->symtab, decl->as.struct_decl.name);
            if (sym && sym->type->kind == TY_FORMA) {
                ScoriaType* forma_type = sym->type;
                for (int j = 0; j < decl->as.struct_decl.field_count; j++) {
                    AstNode* field = decl->as.struct_decl.fields[j];
                    ScoriaType* field_type = resolve_type_node(checker, field->as.var_decl.type);
                    type_forma_add_field(forma_type, field->as.var_decl.name, field_type);
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

bool type_checker_run(TypeChecker* checker, AstNode* program) {
    // 第一遍：收集全局声明
    collect_declarations(checker, program);
    
    if (checker->had_error) return false;

    // 第二遍：深入检查函数体 (Type Checking Pass)
    if (program->kind == AST_PROGRAM) {
        for (int i = 0; i < program->as.program.decl_count; i++) {
            AstNode* decl = program->as.program.declarations[i];
            
            if (decl->kind == AST_FUNC_DECL) {
                Symbol* sym = symtab_lookup(&checker->symtab, decl->as.func_decl.name);
                if (sym && sym->type->kind == TY_ACTIO) {
                    checker->current_function_return_type = sym->type->as.func_type.return_type;
                    
                    symtab_enter_scope(&checker->symtab);
                    
                    // 将参数注册到函数作用域
                    for (int j = 0; j < decl->as.func_decl.param_count; j++) {
                        AstNode* param = decl->as.func_decl.params[j];
                        ScoriaType* param_type = resolve_type_node(checker, param->as.var_decl.type);
                        symtab_define(&checker->symtab, param->as.var_decl.name, SYM_VAR, param_type, param, false);
                    }
                    
                    // 检查函数体
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
