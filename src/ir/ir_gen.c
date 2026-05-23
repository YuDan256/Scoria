#include "ir_gen.h"
#include "../middleend/symtab.h"
#include <stdlib.h>
#include <string.h>

static SirValue* gen_expression(IrBuilder* builder, AstNode* expr);
static SirValue* gen_lvalue(IrBuilder* builder, AstNode* expr);
static void gen_statement(IrBuilder* builder, AstNode* stmt);

// 计算类型在内存中的实际字节大小 (支持 C ABI 对齐与 densa 紧凑布局)
static int get_type_size(ScoriaType* type) {
    if (!type) return 0;
    switch (type->kind) {
        case TY_I8: case TY_P8: case TY_LITTERA: case TY_LOGICA: return 1;
        case TY_I16: case TY_P16: return 2;
        case TY_I32: case TY_P32: case TY_F32: return 4;
        case TY_I64: case TY_P64: case TY_F64: case TY_VIA: return 8;
        case TY_COHORS: return 16;
        case TY_ACIES: return type->as.array.length * get_type_size(type->as.array.inner);
        case TY_FORMA: {
            int size = 0;
            int max_align = 1;
            for (int i = 0; i < type->as.struct_type.field_count; i++) {
                int field_size = get_type_size(type->as.struct_type.fields[i].type);
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
        default: return 8;
    }
}

// 获取左值 (L-value) 的内存地址指针
static SirValue* gen_lvalue(IrBuilder* builder, AstNode* expr) {
    if (!expr) return NULL;
    switch (expr->kind) {
        case AST_IDENT_EXPR: {
            Symbol* sym = expr->resolved_symbol;
            if (sym && sym->ir_val) return sym->ir_val;
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
            if (target_type->kind == TY_ACIES) {
                // 数组退化为指针：直接取数组的左值地址，不进行 Load
                ptr = gen_lvalue(builder, expr->as.index_expr.target);
            } else {
                // 切片或裸指针：正常求值得到指针本身
                ptr = gen_expression(builder, expr->as.index_expr.target);
            }
            SirValue* index = gen_expression(builder, expr->as.index_expr.index);
            int element_size = get_type_size(expr->expr_type);
            return ir_build_gep(builder, ptr, index, element_size, type_get_via(expr->expr_type));
        }
        case AST_MEMBER_EXPR: {
            SirValue* obj_ptr = NULL;
            if (expr->as.member_expr.is_pointer) {
                // p->field: p 已经是指针，直接求值
                obj_ptr = gen_expression(builder, expr->as.member_expr.object);
            } else {
                // obj.field: obj 是值，需要取其左值地址
                obj_ptr = gen_lvalue(builder, expr->as.member_expr.object);
            }
            
            int byte_offset = 0;
            ScoriaType* obj_type = expr->as.member_expr.object->expr_type;
            if (obj_type->kind == TY_VIA) obj_type = obj_type->as.inner;
            
            for (int i = 0; i < obj_type->as.struct_type.field_count; i++) {
                StructField field = obj_type->as.struct_type.fields[i];
                int field_size = get_type_size(field.type);
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
                int64_t val = strtoll(expr->token.start, NULL, 10);
                return ir_const_int(builder, expr->expr_type, val);
            } else if (expr->token.kind == TK_FLOAT_CONST) {
                double val = strtod(expr->token.start, NULL);
                return ir_const_float(builder, expr->expr_type, val);
            } else if (expr->token.kind == TK_BOOL_CONST) {
                bool val = (expr->token.length == 5 && strncmp(expr->token.start, "verum", 5) == 0);
                return ir_const_bool(builder, val);
            } else if (expr->token.kind == TK_STRING_CONST) {
                // 提取字符串内容 (去掉首尾引号) 并处理转义字符
                char* str = (char*)arena_alloc(&builder->arena, expr->token.length - 1);
                int j = 0;
                for (uint32_t i = 1; i < expr->token.length - 1; i++) {
                    if (expr->token.start[i] == '\\' && i + 1 < expr->token.length - 1) {
                        if (expr->token.start[i+1] == 'n') { str[j++] = '\n'; i++; continue; }
                        if (expr->token.start[i+1] == 't') { str[j++] = '\t'; i++; continue; }
                        if (expr->token.start[i+1] == '\\') { str[j++] = '\\'; i++; continue; }
                    }
                    str[j++] = expr->token.start[i];
                }
                str[j] = '\0';
                return ir_const_string(builder, str);
            }
            // TODO: 处理字符字面量
            break;
        }
        case AST_IDENT_EXPR: {
            Symbol* sym = expr->resolved_symbol;
            if (sym && sym->ir_val) {
                // 局部变量的 ir_val 是 alloca 分配的栈指针，使用时需要 load 出来
                return ir_build_load(builder, sym->ir_val);
            }
            break;
        }
        case AST_ASSIGN_EXPR: {
            SirValue* val = gen_expression(builder, expr->as.assign.value);
            SirValue* lval = gen_lvalue(builder, expr->as.assign.target);
            if (lval) {
                ir_build_store(builder, val, lval);
            }
            return val;
        }
        case AST_BINARY_EXPR: {
            SirValue* left = gen_expression(builder, expr->as.binary.left);
            SirValue* right = gen_expression(builder, expr->as.binary.right);
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
                case TK_AMP:
                case TK_LOGIC_AND: op = SIR_AND; break;
                case TK_PIPE:
                case TK_LOGIC_OR:  op = SIR_OR; break;
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
                // locus x: 如果 x 是局部变量，它的 ir_val 就是栈指针，直接返回
                if (expr->as.unary.operand->kind == AST_IDENT_EXPR) {
                    Symbol* sym = expr->as.unary.operand->resolved_symbol;
                    if (sym && sym->ir_val) return sym->ir_val;
                }
            } else if (expr->as.unary.op.kind == TK_KW_TENE) {
                // tene p: 生成 LOAD 指令
                SirValue* ptr = gen_expression(builder, expr->as.unary.operand);
                return ir_build_load(builder, ptr);
            } else if (expr->as.unary.op.kind == TK_LOGIC_NOT) {
                SirValue* operand = gen_expression(builder, expr->as.unary.operand);
                SirValue* true_val = ir_const_bool(builder, true);
                return ir_build_binary(builder, SIR_XOR, operand, true_val); // !x 等价于 x XOR true
            } else if (expr->as.unary.op.kind == TK_MINUS) {
                SirValue* operand = gen_expression(builder, expr->as.unary.operand);
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
            if (expr->as.call.callee->kind == AST_IDENT_EXPR && (!expr->as.call.callee->resolved_symbol || !expr->as.call.callee->resolved_symbol->ir_val)) {
                // 全局函数调用
                Symbol* sym = expr->as.call.callee->resolved_symbol;
                if (sym) {
                    callee = (SirValue*)arena_alloc(&builder->arena, sizeof(SirValue));
                    callee->kind = SIR_VAL_GLOBAL;
                    callee->type = sym->type;
                    char* name = (char*)arena_alloc(&builder->arena, sym->name.length + 1);
                    strncpy(name, sym->name.start, sym->name.length);
                    name[sym->name.length] = '\0';
                    callee->as.global_name = name;
                }
            } else {
                // 函数指针调用 (通过表达式求值获取指针)
                callee = gen_expression(builder, expr->as.call.callee);
            }
            
            int arg_count = expr->as.call.arg_count;
            SirValue** args = NULL;
            if (arg_count > 0) {
                args = (SirValue**)arena_alloc(&builder->arena, sizeof(SirValue*) * arg_count);
                for (int i = 0; i < arg_count; i++) {
                    args[i] = gen_expression(builder, expr->as.call.args[i]);
                }
            }
            
            return ir_build_call(builder, callee, args, arg_count, expr->expr_type);
        }
        case AST_CAST_EXPR: {
            SirValue* val = gen_expression(builder, expr->as.cast_expr.value);
            return ir_build_cast(builder, val, expr->expr_type);
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
            if (element_type->kind == TY_VIA) element_type = element_type->as.inner;
            int element_size = get_type_size(element_type);
            return ir_build_gep(builder, ptr, offset, element_size, expr->expr_type);
        }
        case AST_CREA_EXPR: {
            SirValue* callee = (SirValue*)arena_alloc(&builder->arena, sizeof(SirValue));
            callee->kind = SIR_VAL_GLOBAL;
            callee->type = type_get_basic(TY_ACTIO);
            callee->as.global_name = "crea";
            
            // 动态计算分配类型的实际大小
            ScoriaType* allocated_type = expr->expr_type;
            if (allocated_type->kind == TY_VIA) {
                allocated_type = allocated_type->as.inner;
            }
            int element_size = get_type_size(allocated_type);
            
            SirValue* count_val;
            if (expr->as.crea_expr.count) {
                count_val = gen_expression(builder, expr->as.crea_expr.count);
            } else {
                count_val = ir_const_int(builder, type_get_basic(TY_I32), 1);
            }
            SirValue* size_val = ir_const_int(builder, type_get_basic(TY_I32), element_size);
            SirValue* total_size = ir_build_binary(builder, SIR_MUL, count_val, size_val);
            
            SirValue** args = (SirValue**)arena_alloc(&builder->arena, sizeof(SirValue*) * 1);
            args[0] = total_size;
            
            return ir_build_call(builder, callee, args, 1, expr->expr_type);
        }
        case AST_NECA_EXPR: {
            SirValue* callee = (SirValue*)arena_alloc(&builder->arena, sizeof(SirValue));
            callee->kind = SIR_VAL_GLOBAL;
            callee->type = type_get_basic(TY_ACTIO);
            callee->as.global_name = "neca";
            
            SirValue** args = (SirValue**)arena_alloc(&builder->arena, sizeof(SirValue*) * 1);
            args[0] = gen_expression(builder, expr->as.neca_expr.pointer);
            
            return ir_build_call(builder, callee, args, 1, type_get_basic(TY_NIHIL));
        }
        case AST_INDEX_EXPR:
        case AST_MEMBER_EXPR: {
            // 数组索引和成员访问作为右值时，先取其左值地址，再 Load
            SirValue* lval = gen_lvalue(builder, expr);
            if (lval) return ir_build_load(builder, lval);
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
                SirValue** args = (SirValue**)arena_alloc(&builder->arena, sizeof(SirValue*) * 1);
                args[0] = gen_expression(builder, expr->as.scribe_expr.args[i]);
                last_val = ir_build_call(builder, callee, args, 1, type_get_basic(TY_NIHIL));
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
        case AST_VAR_DECL: {
            Symbol* sym = stmt->resolved_symbol;
            if (sym) {
                // 1. 在当前函数的栈帧上分配内存 (Alloca)
                int type_size = get_type_size(sym->type);
                sym->ir_val = ir_build_alloca(builder, sym->type, type_size);
                
                // 2. 如果有初始值，生成 Store 指令写入栈内存
                if (stmt->as.var_decl.initializer) {
                    SirValue* init_val = gen_expression(builder, stmt->as.var_decl.initializer);
                    ir_build_store(builder, init_val, sym->ir_val);
                }
            }
            break;
        }
        case AST_RETURN_STMT: {
            SirValue* ret_val = NULL;
            if (stmt->as.return_stmt.value) {
                ret_val = gen_expression(builder, stmt->as.return_stmt.value);
            }
            ir_build_ret(builder, ret_val);
            break;
        }
        case AST_IF_STMT: {
            SirValue* cond = gen_expression(builder, stmt->as.if_stmt.condition);
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
            SirBlock* cond_block = ir_builder_create_block(builder, "dum.cond");
            SirBlock* body_block = ir_builder_create_block(builder, "dum.corpus");
            SirBlock* exit_block = ir_builder_create_block(builder, "dum.exitus");

            ir_build_jmp(builder, cond_block);
            ir_builder_set_insert_point(builder, cond_block);
            SirValue* cond = gen_expression(builder, stmt->as.while_stmt.condition);
            ir_build_br(builder, cond, body_block, exit_block);

            // 保存外层循环上下文
            SirBlock* prev_cond = builder->current_loop_cond;
            SirBlock* prev_exit = builder->current_loop_exit;
            builder->current_loop_cond = cond_block;
            builder->current_loop_exit = exit_block;

            ir_builder_set_insert_point(builder, body_block);
            gen_statement(builder, stmt->as.while_stmt.body);
            ir_build_jmp(builder, cond_block);

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

            SirBlock* cond_block = ir_builder_create_block(builder, "per.cond");
            SirBlock* body_block = ir_builder_create_block(builder, "per.corpus");
            SirBlock* inc_block = ir_builder_create_block(builder, "per.inc");
            SirBlock* exit_block = ir_builder_create_block(builder, "per.exitus");

            ir_build_jmp(builder, cond_block);
            ir_builder_set_insert_point(builder, cond_block);
            
            if (stmt->as.for_stmt.condition) {
                SirValue* cond = gen_expression(builder, stmt->as.for_stmt.condition);
                ir_build_br(builder, cond, body_block, exit_block);
            } else {
                ir_build_jmp(builder, body_block);
            }

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
        default: break;
    }
}

void ir_gen_generate(IrBuilder* builder, AstNode* program) {
    if (!program || program->kind != AST_PROGRAM) return;

    for (int i = 0; i < program->as.program.decl_count; i++) {
        AstNode* decl = program->as.program.declarations[i];
        
        if (decl->kind == AST_FUNC_DECL) {
            Symbol* sym = decl->resolved_symbol;
            if (!sym || sym->type->kind != TY_ACTIO) continue;

            // 1. 提取函数名并创建 SIR 函数
            char* func_name = (char*)arena_alloc(&builder->arena, decl->as.func_decl.name.length + 1);
            memcpy(func_name, decl->as.func_decl.name.start, decl->as.func_decl.name.length);
            func_name[decl->as.func_decl.name.length] = '\0';

            ir_builder_create_function(builder, func_name, sym->type);
            
            // 2. 创建入口基本块 (Entry Block)
            SirBlock* entry_block = ir_builder_create_block(builder, "ingressus"); // 拉丁语 entry
            ir_builder_set_insert_point(builder, entry_block);

            // 3. 处理参数：为参数分配栈空间并存储传入的值
            for (int j = 0; j < decl->as.func_decl.param_count; j++) {
                AstNode* param_node = decl->as.func_decl.params[j];
                Symbol* param_sym = param_node->resolved_symbol;
                if (param_sym) {
                    int param_size = get_type_size(param_sym->type);
                    param_sym->ir_val = ir_build_alloca(builder, param_sym->type, param_size);
                    SirValue* arg_val = ir_get_param(builder, j, param_sym->type);
                    ir_build_store(builder, arg_val, param_sym->ir_val);
                }
            }

            // 4. 递归生成函数体指令
            if (decl->as.func_decl.body) {
                gen_statement(builder, decl->as.func_decl.body);
            }
            
            // 5. 安全兜底：如果基本块最后没有返回指令，自动补全 (针对 nihil 返回类型)
            if (!entry_block->last_inst || entry_block->last_inst->opcode != SIR_RET) {
                if (sym->type->as.func_type.return_type->kind == TY_NIHIL) {
                    ir_build_ret(builder, NULL);
                }
            }
        }
    }
}
