#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------
// AST 节点内存分配 (幽灵后勤大营)
// ---------------------------------------------------------
AstNode* ast_create_node(Arena* arena, AstNodeKind kind, Token token) {
    AstNode* node = (AstNode*)arena_alloc(arena, sizeof(AstNode));
    memset(node, 0, sizeof(AstNode));
    node->kind = kind;
    node->token = token;
    return node;
}

// ---------------------------------------------------------
// 错误处理与游标控制
// ---------------------------------------------------------
static void error_at(Parser* parser, Token* token, const char* message) {
    if (parser->panic_mode) return;
    parser->panic_mode = true;
    fprintf(stderr, "[linea %d:%d] Erratum", token->line, token->column);
    if (token->kind == TK_EOF) {
        fprintf(stderr, " ad finem");
    } else if (token->kind != TK_UNKNOWN) {
        fprintf(stderr, " ad '%.*s'", token->length, token->start);
    }
    fprintf(stderr, ": %s\n", message);
    parser->had_error = true;
}

static void error(Parser* parser, const char* message) {
    error_at(parser, &parser->current, message);
}

static void advance(Parser* parser) {
    parser->previous = parser->current;
    for (;;) {
        parser->current = lexer_next_token(&parser->lexer);
        if (parser->current.kind != TK_UNKNOWN) break;
        error(parser, "Symbolum ignotum est.");
    }
}

static void consume(Parser* parser, TokenKind type, const char* message) {
    if (parser->current.kind == type) {
        advance(parser);
        return;
    }
    error_at(parser, &parser->current, message);
}

static bool check(Parser* parser, TokenKind type) {
    return parser->current.kind == type;
}

static bool match(Parser* parser, TokenKind type) {
    if (!check(parser, type)) return false;
    advance(parser);
    return true;
}

// ---------------------------------------------------------
// 递归下降解析前置声明
// ---------------------------------------------------------
static AstNode* expression(Parser* parser);
static AstNode* statement(Parser* parser);
static AstNode* declaration(Parser* parser);
static AstNode* parse_type(Parser* parser);
static AstNode* var_declaration(Parser* parser, bool is_const);
static AstNode* func_declaration(Parser* parser);
static AstNode* struct_declaration(Parser* parser);

// ---------------------------------------------------------
// 类型解析 (Type Parsing)
// ---------------------------------------------------------
static AstNode* parse_type(Parser* parser) {
    AstNode* node = ast_create_node(&parser->arena, AST_TYPE, parser->current);
    node->as.type_node.module_prefix = (Token){0};
    node->as.type_node.is_via = false;
    node->as.type_node.is_cohors = false;
    node->as.type_node.is_acies = false;
    node->as.type_node.array_size = NULL;
    node->as.type_node.inner_type = NULL;

    if (match(parser, TK_TY_VIA)) {
        node->as.type_node.is_via = true;
        node->as.type_node.inner_type = parse_type(parser);
        return node;
    } else if (match(parser, TK_TY_COHORS)) {
        node->as.type_node.is_cohors = true;
        node->as.type_node.inner_type = parse_type(parser);
        return node;
    } else if (match(parser, TK_TY_ACIES)) {
        node->as.type_node.is_acies = true;
        consume(parser, TK_LBRACKET, "Post aciem '[' exspectatur.");
        node->as.type_node.array_size = expression(parser);
        consume(parser, TK_RBRACKET, "Post magnitudinem aciei ']' exspectatur.");
        node->as.type_node.inner_type = parse_type(parser);
        return node;
    }

    if (match(parser, TK_TY_I8) || match(parser, TK_TY_I16) || match(parser, TK_TY_I32) || match(parser, TK_TY_I64) ||
        match(parser, TK_TY_P8) || match(parser, TK_TY_P16) || match(parser, TK_TY_P32) || match(parser, TK_TY_P64) ||
        match(parser, TK_TY_F32) || match(parser, TK_TY_F64) ||
        match(parser, TK_TY_LOGICA) || match(parser, TK_TY_LITTERA) || match(parser, TK_TY_TEXTUS) ||
        match(parser, TK_KW_NIHIL) || match(parser, TK_IDENTIFIER)) {
        
        node->as.type_node.base_type = parser->previous;
        
        if (node->as.type_node.base_type.kind == TK_IDENTIFIER && match(parser, TK_DOT)) {
            node->as.type_node.module_prefix = node->as.type_node.base_type;
            consume(parser, TK_IDENTIFIER, "Nomen typi post '.' exspectatur.");
            node->as.type_node.base_type = parser->previous;
        }
        
        // 处理古典词缀组合，例如 minimus (i8) purus -> p8
        if (match(parser, TK_TY_PURUS)) {
            switch (node->as.type_node.base_type.kind) {
                case TK_TY_I8:  node->as.type_node.base_type.kind = TK_TY_P8; break;
                case TK_TY_I16: node->as.type_node.base_type.kind = TK_TY_P16; break;
                case TK_TY_I32: node->as.type_node.base_type.kind = TK_TY_P32; break;
                case TK_TY_I64: node->as.type_node.base_type.kind = TK_TY_P64; break;
                default: 
                    error(parser, "Usus modificationis 'purus' invalidus est.");
                    break;
            }
        } else if (node->as.type_node.base_type.kind == TK_TY_F64 && match(parser, TK_TY_I32)) {
            // fractus (f64) medius (i32) -> f32
            node->as.type_node.base_type.kind = TK_TY_F32;
        }
    } else {
        error(parser, "Nomen typi exspectatur.");
    }

    return node;
}

// ---------------------------------------------------------
// 表达式解析 (Expression Parsing)
// ---------------------------------------------------------
static AstNode* parse_struct_literal_body(Parser* parser, AstNode* type_expr, Token brace) {
    AstNode* node = ast_create_node(&parser->arena, AST_STRUCT_LITERAL, brace);
    node->as.struct_literal.type_expr = type_expr;
    node->as.struct_literal.field_names = NULL;
    node->as.struct_literal.field_values = NULL;
    node->as.struct_literal.field_count = 0;
    int capacity = 0;
    if (!check(parser, TK_RBRACE)) {
        do {
            if (node->as.struct_literal.field_count >= capacity) {
                int old_capacity = capacity;
                capacity = capacity < 4 ? 4 : capacity * 2;
                node->as.struct_literal.field_names = arena_realloc(&parser->arena, node->as.struct_literal.field_names, sizeof(Token) * old_capacity, sizeof(Token) * capacity);
                node->as.struct_literal.field_values = arena_realloc(&parser->arena, node->as.struct_literal.field_values, sizeof(AstNode*) * old_capacity, sizeof(AstNode*) * capacity);
            }
            consume(parser, TK_IDENTIFIER, "Nomen campi exspectatur.");
            node->as.struct_literal.field_names[node->as.struct_literal.field_count] = parser->previous;
            consume(parser, TK_COLON, "Post nomen campi ':' exspectatur.");
            node->as.struct_literal.field_values[node->as.struct_literal.field_count] = expression(parser);
            node->as.struct_literal.field_count++;
        } while (match(parser, TK_COMMA));
    }
    consume(parser, TK_RBRACE, "Post campos formae '}' exspectatur.");
    return node;
}

static AstNode* primary(Parser* parser) {
    if (match(parser, TK_INT_CONST) || match(parser, TK_FLOAT_CONST) ||
        match(parser, TK_BOOL_CONST) || match(parser, TK_STRING_CONST) ||
        match(parser, TK_CHAR_CONST) || match(parser, TK_KW_NULLUS)) {
        return ast_create_node(&parser->arena, AST_LITERAL_EXPR, parser->previous);
    }
    if (match(parser, TK_IDENTIFIER)) {
        return ast_create_node(&parser->arena, AST_IDENT_EXPR, parser->previous);
    }
    if (match(parser, TK_KW_MUTA)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Post 'muta' '(' exspectatur.");
        AstNode* target_type = parse_type(parser);
        consume(parser, TK_COMMA, "Post typum in 'muta' ',' exspectatur.");
        AstNode* value = expression(parser);
        consume(parser, TK_RPAREN, "Post argumenta 'muta' ')' exspectatur.");
        AstNode* node = ast_create_node(&parser->arena, AST_CAST_EXPR, keyword);
        node->as.cast_expr.target_type = target_type;
        node->as.cast_expr.value = value;
        return node;
    }
    if (match(parser, TK_KW_VADE) || match(parser, TK_KW_RECEDE)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Post 'vade/recede' '(' exspectatur.");
        AstNode* pointer = expression(parser);
        consume(parser, TK_COMMA, "Post indicem ',' exspectatur.");
        AstNode* offset = expression(parser);
        consume(parser, TK_RPAREN, "Post argumenta ')' exspectatur.");
        AstNode* node = ast_create_node(&parser->arena, keyword.kind == TK_KW_VADE ? AST_VADE_EXPR : AST_RECEDE_EXPR, keyword);
        node->as.pointer_offset.pointer = pointer;
        node->as.pointer_offset.offset = offset;
        return node;
    }
    if (match(parser, TK_KW_CREA)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Post 'crea' '(' exspectatur.");
        AstNode* type = parse_type(parser);
        AstNode* count = NULL;
        if (match(parser, TK_COMMA)) {
            count = expression(parser);
        }
        consume(parser, TK_RPAREN, "Post argumenta 'crea' ')' exspectatur.");
        AstNode* node = ast_create_node(&parser->arena, AST_CREA_EXPR, keyword);
        node->as.crea_expr.type = type;
        node->as.crea_expr.count = count;
        return node;
    }
    if (match(parser, TK_KW_NECA)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Post 'neca' '(' exspectatur.");
        AstNode* pointer = expression(parser);
        consume(parser, TK_RPAREN, "Post argumenta 'neca' ')' exspectatur.");
        AstNode* node = ast_create_node(&parser->arena, AST_NECA_EXPR, keyword);
        node->as.neca_expr.pointer = pointer;
        return node;
    }
    if (match(parser, TK_KW_SCRIBE)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Post 'scribe' '(' exspectatur.");
        AstNode* node = ast_create_node(&parser->arena, AST_SCRIBE_EXPR, keyword);
        node->as.scribe_expr.args = NULL;
        node->as.scribe_expr.arg_count = 0;
        int capacity = 0;
        if (!check(parser, TK_RPAREN)) {
            do {
                if (node->as.scribe_expr.arg_count >= capacity) {
                    int old_capacity = capacity;
                    capacity = capacity < 4 ? 4 : capacity * 2;
                    node->as.scribe_expr.args = arena_realloc(&parser->arena, node->as.scribe_expr.args, sizeof(AstNode*) * old_capacity, sizeof(AstNode*) * capacity);
                }
                node->as.scribe_expr.args[node->as.scribe_expr.arg_count++] = expression(parser);
            } while (match(parser, TK_COMMA));
        }
        consume(parser, TK_RPAREN, "Post argumenta 'scribe' ')' exspectatur.");
        return node;
    }
    if (match(parser, TK_KW_LEGE)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Post 'lege' '(' exspectatur.");
        AstNode* node = ast_create_node(&parser->arena, AST_LEGE_EXPR, keyword);
        node->as.lege_expr.args = NULL;
        node->as.lege_expr.arg_count = 0;
        int capacity = 0;
        if (!check(parser, TK_RPAREN)) {
            do {
                if (node->as.lege_expr.arg_count >= capacity) {
                    int old_capacity = capacity;
                    capacity = capacity < 4 ? 4 : capacity * 2;
                    node->as.lege_expr.args = arena_realloc(&parser->arena, node->as.lege_expr.args, sizeof(AstNode*) * old_capacity, sizeof(AstNode*) * capacity);
                }
                node->as.lege_expr.args[node->as.lege_expr.arg_count++] = expression(parser);
            } while (match(parser, TK_COMMA));
        }
        consume(parser, TK_RPAREN, "Post argumenta 'lege' ')' exspectatur.");
        return node;
    }
    if (match(parser, TK_LBRACKET)) {
        Token bracket = parser->previous;
        AstNode* node = ast_create_node(&parser->arena, AST_ARRAY_LITERAL, bracket);
        node->as.array_literal.elements = NULL;
        node->as.array_literal.element_count = 0;
        int capacity = 0;
        if (!check(parser, TK_RBRACKET)) {
            do {
                if (node->as.array_literal.element_count >= capacity) {
                    int old_capacity = capacity;
                    capacity = capacity < 4 ? 4 : capacity * 2;
                    node->as.array_literal.elements = arena_realloc(&parser->arena, node->as.array_literal.elements, sizeof(AstNode*) * old_capacity, sizeof(AstNode*) * capacity);
                }
                node->as.array_literal.elements[node->as.array_literal.element_count++] = expression(parser);
            } while (match(parser, TK_COMMA));
        }
        consume(parser, TK_RBRACKET, "Post elementa aciei ']' exspectatur.");
        return node;
    }
    if (match(parser, TK_LBRACE)) {
        return parse_struct_literal_body(parser, NULL, parser->previous);
    }
    if (match(parser, TK_LPAREN)) {
        AstNode* expr = expression(parser);
        consume(parser, TK_RPAREN, "Post expressionem ')' exspectatur.");
        return expr;
    }
    error(parser, "Expressio exspectatur.");
    return NULL;
}

static AstNode* postfix(Parser* parser) {
    AstNode* expr = primary(parser);
    while (true) {
        if (match(parser, TK_LPAREN)) {
            Token paren = parser->previous;
            AstNode* node = ast_create_node(&parser->arena, AST_CALL_EXPR, paren);
            node->as.call.callee = expr;
            node->as.call.args = NULL;
            node->as.call.arg_count = 0;
            int capacity = 0;
            if (!check(parser, TK_RPAREN)) {
                do {
                    if (node->as.call.arg_count >= capacity) {
                        int old_capacity = capacity;
                        capacity = capacity < 4 ? 4 : capacity * 2;
                        node->as.call.args = arena_realloc(&parser->arena, node->as.call.args, sizeof(AstNode*) * old_capacity, sizeof(AstNode*) * capacity);
                    }
                    node->as.call.args[node->as.call.arg_count++] = expression(parser);
                } while (match(parser, TK_COMMA));
            }
            consume(parser, TK_RPAREN, "Post argumenta ')' exspectatur.");
            expr = node;
        } else if (match(parser, TK_LBRACKET)) {
            Token bracket = parser->previous;
            AstNode* index = expression(parser);
            consume(parser, TK_RBRACKET, "Post indicem ']' exspectatur.");
            AstNode* node = ast_create_node(&parser->arena, AST_INDEX_EXPR, bracket);
            node->as.index_expr.target = expr;
            node->as.index_expr.index = index;
            expr = node;
        } else if (match(parser, TK_DOT)) {
            Token dot = parser->previous;
            if (!match(parser, TK_IDENTIFIER)) {
                error(parser, "Post '.' nomen proprietatis exspectatur.");
            }
            Token name = parser->previous;
            AstNode* node = ast_create_node(&parser->arena, AST_MEMBER_EXPR, dot);
            node->as.member_expr.object = expr;
            node->as.member_expr.property = name;
            node->as.member_expr.is_pointer = false;
            expr = node;
        } else if (match(parser, TK_ARROW)) {
            Token arrow = parser->previous;
            if (!match(parser, TK_IDENTIFIER)) {
                error(parser, "Post '->' nomen proprietatis exspectatur.");
            }
            Token name = parser->previous;
            AstNode* node = ast_create_node(&parser->arena, AST_MEMBER_EXPR, arrow);
            node->as.member_expr.object = expr;
            node->as.member_expr.property = name;
            node->as.member_expr.is_pointer = true;
            expr = node;
        } else if (match(parser, TK_LBRACE)) {
            expr = parse_struct_literal_body(parser, expr, parser->previous);
        } else {
            break;
        }
    }
    return expr;
}

static AstNode* unary(Parser* parser) {
    if (match(parser, TK_MINUS) || match(parser, TK_LOGIC_NOT) || match(parser, TK_TILDE) ||
        match(parser, TK_KW_LOCUS) || match(parser, TK_KW_TENE)) {
        Token op = parser->previous;
        AstNode* operand = unary(parser);
        AstNode* node = ast_create_node(&parser->arena, AST_UNARY_EXPR, op);
        node->as.unary.op = op;
        node->as.unary.operand = operand;
        return node;
    }
    return postfix(parser);
}

static AstNode* factor(Parser* parser) {
    AstNode* expr = unary(parser);
    while (match(parser, TK_STAR) || match(parser, TK_SLASH) || match(parser, TK_MOD)) {
        Token op = parser->previous;
        AstNode* right = unary(parser);
        AstNode* node = ast_create_node(&parser->arena, AST_BINARY_EXPR, op);
        node->as.binary.left = expr;
        node->as.binary.op = op;
        node->as.binary.right = right;
        expr = node;
    }
    return expr;
}

static AstNode* term(Parser* parser) {
    AstNode* expr = factor(parser);
    while (match(parser, TK_PLUS) || match(parser, TK_MINUS)) {
        Token op = parser->previous;
        AstNode* right = factor(parser);
        AstNode* node = ast_create_node(&parser->arena, AST_BINARY_EXPR, op);
        node->as.binary.left = expr;
        node->as.binary.op = op;
        node->as.binary.right = right;
        expr = node;
    }
    return expr;
}

static AstNode* shift(Parser* parser) {
    AstNode* expr = term(parser);
    while (match(parser, TK_SHL) || match(parser, TK_SHR)) {
        Token op = parser->previous;
        AstNode* right = term(parser);
        AstNode* node = ast_create_node(&parser->arena, AST_BINARY_EXPR, op);
        node->as.binary.left = expr;
        node->as.binary.op = op;
        node->as.binary.right = right;
        expr = node;
    }
    return expr;
}

static AstNode* bitwise_and(Parser* parser) {
    AstNode* expr = shift(parser);
    while (match(parser, TK_AMP)) {
        Token op = parser->previous;
        AstNode* right = shift(parser);
        AstNode* node = ast_create_node(&parser->arena, AST_BINARY_EXPR, op);
        node->as.binary.left = expr;
        node->as.binary.op = op;
        node->as.binary.right = right;
        expr = node;
    }
    return expr;
}

static AstNode* bitwise_xor(Parser* parser) {
    AstNode* expr = bitwise_and(parser);
    while (match(parser, TK_CARET)) {
        Token op = parser->previous;
        AstNode* right = bitwise_and(parser);
        AstNode* node = ast_create_node(&parser->arena, AST_BINARY_EXPR, op);
        node->as.binary.left = expr;
        node->as.binary.op = op;
        node->as.binary.right = right;
        expr = node;
    }
    return expr;
}

static AstNode* bitwise_or(Parser* parser) {
    AstNode* expr = bitwise_xor(parser);
    while (match(parser, TK_PIPE)) {
        Token op = parser->previous;
        AstNode* right = bitwise_xor(parser);
        AstNode* node = ast_create_node(&parser->arena, AST_BINARY_EXPR, op);
        node->as.binary.left = expr;
        node->as.binary.op = op;
        node->as.binary.right = right;
        expr = node;
    }
    return expr;
}

static AstNode* comparison(Parser* parser) {
    AstNode* expr = bitwise_or(parser);
    while (match(parser, TK_GT) || match(parser, TK_GTE) || match(parser, TK_LT) || match(parser, TK_LTE)) {
        Token op = parser->previous;
        AstNode* right = bitwise_or(parser);
        AstNode* node = ast_create_node(&parser->arena, AST_BINARY_EXPR, op);
        node->as.binary.left = expr;
        node->as.binary.op = op;
        node->as.binary.right = right;
        expr = node;
    }
    return expr;
}

static AstNode* equality(Parser* parser) {
    AstNode* expr = comparison(parser);
    while (match(parser, TK_EQ) || match(parser, TK_NEQ)) {
        Token op = parser->previous;
        AstNode* right = comparison(parser);
        AstNode* node = ast_create_node(&parser->arena, AST_BINARY_EXPR, op);
        node->as.binary.left = expr;
        node->as.binary.op = op;
        node->as.binary.right = right;
        expr = node;
    }
    return expr;
}

static AstNode* logic_and(Parser* parser) {
    AstNode* expr = equality(parser);
    while (match(parser, TK_LOGIC_AND)) {
        Token op = parser->previous;
        AstNode* right = equality(parser);
        AstNode* node = ast_create_node(&parser->arena, AST_BINARY_EXPR, op);
        node->as.binary.left = expr;
        node->as.binary.op = op;
        node->as.binary.right = right;
        expr = node;
    }
    return expr;
}

static AstNode* logic_or(Parser* parser) {
    AstNode* expr = logic_and(parser);
    while (match(parser, TK_LOGIC_OR)) {
        Token op = parser->previous;
        AstNode* right = logic_and(parser);
        AstNode* node = ast_create_node(&parser->arena, AST_BINARY_EXPR, op);
        node->as.binary.left = expr;
        node->as.binary.op = op;
        node->as.binary.right = right;
        expr = node;
    }
    return expr;
}

static AstNode* assignment(Parser* parser) {
    AstNode* expr = logic_or(parser);
    if (match(parser, TK_ASSIGN) || match(parser, TK_PLUS_ASSIGN) || match(parser, TK_MINUS_ASSIGN) ||
        match(parser, TK_STAR_ASSIGN) || match(parser, TK_SLASH_ASSIGN) || match(parser, TK_MOD_ASSIGN) ||
        match(parser, TK_AMP_ASSIGN) || match(parser, TK_PIPE_ASSIGN) || match(parser, TK_CARET_ASSIGN) ||
        match(parser, TK_SHL_ASSIGN) || match(parser, TK_SHR_ASSIGN)) {
        Token equals = parser->previous;
        AstNode* value = assignment(parser);
        if (expr && (expr->kind == AST_IDENT_EXPR || expr->kind == AST_MEMBER_EXPR || expr->kind == AST_INDEX_EXPR || expr->kind == AST_UNARY_EXPR)) {
            AstNode* node = ast_create_node(&parser->arena, AST_ASSIGN_EXPR, equals);
            node->as.assign.op = equals;
            node->as.assign.target = expr;
            node->as.assign.value = value;
            return node;
        }
        error_at(parser, &equals, "Scopus assignationis invalidus est.");
    }
    return expr;
}

static AstNode* expression(Parser* parser) {
    return assignment(parser);
}

// ---------------------------------------------------------
// 语句解析 (Statement Parsing)
// ---------------------------------------------------------
static AstNode* expression_statement(Parser* parser) {
    AstNode* expr = expression(parser);
    if (expr && expr->kind == AST_IDENT_EXPR && match(parser, TK_COLON)) {
        AstNode* node = ast_create_node(&parser->arena, AST_LABEL_STMT, expr->token);
        node->as.label_stmt.name = expr->token;
        return node;
    }
    consume(parser, TK_SEMI, "Post expressionem ';' exspectatur.");
    AstNode* node = ast_create_node(&parser->arena, AST_EXPR_STMT, expr ? expr->token : parser->previous);
    node->as.expr_stmt.expr = expr;
    return node;
}

static AstNode* block_statement(Parser* parser) {
    AstNode* node = ast_create_node(&parser->arena, AST_BLOCK_STMT, parser->previous);
    node->as.block.statements = NULL;
    node->as.block.stmt_count = 0;
    int capacity = 0;

    while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
        if (node->as.block.stmt_count >= capacity) {
            int old_capacity = capacity;
            capacity = capacity < 8 ? 8 : capacity * 2;
            node->as.block.statements = arena_realloc(&parser->arena, node->as.block.statements, sizeof(AstNode*) * old_capacity, sizeof(AstNode*) * capacity);
        }
        const char* start_cursor = parser->current.start;
        AstNode* decl = declaration(parser);
        if (decl) {
            node->as.block.statements[node->as.block.stmt_count++] = decl;
        }
        // 如果游标没有前进，强制推进以避免死循环
        if (parser->current.start == start_cursor) {
            advance(parser);
        }
    }
    consume(parser, TK_RBRACE, "Post clausulam '}' exspectatur.");
    return node;
}

static AstNode* statement(Parser* parser) {
    if (match(parser, TK_KW_SI)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Post 'si' '(' exspectatur.");
        AstNode* condition = expression(parser);
        consume(parser, TK_RPAREN, "Post condicionem ')' exspectatur.");
        
        AstNode* then_branch = statement(parser);
        
        AstNode* else_branch = NULL;
        if (match(parser, TK_KW_ALITER)) {
            else_branch = statement(parser);
        }
        
        AstNode* node = ast_create_node(&parser->arena, AST_IF_STMT, keyword);
        node->as.if_stmt.condition = condition;
        node->as.if_stmt.then_branch = then_branch;
        node->as.if_stmt.else_branch = else_branch;
        return node;
    }
    if (match(parser, TK_KW_ELIGE)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Post 'elige' '(' exspectatur.");
        AstNode* condition = expression(parser);
        consume(parser, TK_RPAREN, "Post condicionem ')' exspectatur.");
        consume(parser, TK_LBRACE, "Ante corpus 'elige' '{' exspectatur.");

        AstNode*** case_vals = NULL;
        int* case_val_counts = NULL;
        AstNode** case_stmts = NULL;
        int case_count = 0;
        int capacity = 0;
        AstNode* default_branch = NULL;

        while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
            if (match(parser, TK_KW_CASUS)) {
                AstNode** vals = NULL;
                int val_count = 0;
                int val_cap = 0;
                do {
                    if (val_count >= val_cap) {
                        int old_cap = val_cap;
                        val_cap = val_cap < 4 ? 4 : val_cap * 2;
                        vals = arena_realloc(&parser->arena, vals, sizeof(AstNode*) * old_cap, sizeof(AstNode*) * val_cap);
                    }
                    vals[val_count++] = expression(parser);
                } while (match(parser, TK_COMMA));

                consume(parser, TK_COLON, "Post valorem casus ':' exspectatur.");

                AstNode* body = ast_create_node(&parser->arena, AST_BLOCK_STMT, parser->previous);
                body->as.block.statements = NULL;
                body->as.block.stmt_count = 0;
                int stmt_cap = 0;
                while (!check(parser, TK_KW_CASUS) && !check(parser, TK_KW_ALITER) && !check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
                    if (body->as.block.stmt_count >= stmt_cap) {
                        int old_cap = stmt_cap;
                        stmt_cap = stmt_cap < 4 ? 4 : stmt_cap * 2;
                        body->as.block.statements = arena_realloc(&parser->arena, body->as.block.statements, sizeof(AstNode*) * old_cap, sizeof(AstNode*) * stmt_cap);
                    }
                    const char* start_cursor = parser->current.start;
                    AstNode* decl = declaration(parser);
                    if (decl) {
                        body->as.block.statements[body->as.block.stmt_count++] = decl;
                    }
                    if (parser->current.start == start_cursor) {
                        advance(parser);
                    }
                }

                if (case_count >= capacity) {
                    int old_cap = capacity;
                    capacity = capacity < 4 ? 4 : capacity * 2;
                    case_vals = arena_realloc(&parser->arena, case_vals, sizeof(AstNode**) * old_cap, sizeof(AstNode**) * capacity);
                    case_val_counts = arena_realloc(&parser->arena, case_val_counts, sizeof(int) * old_cap, sizeof(int) * capacity);
                    case_stmts = arena_realloc(&parser->arena, case_stmts, sizeof(AstNode*) * old_cap, sizeof(AstNode*) * capacity);
                }
                case_vals[case_count] = vals;
                case_val_counts[case_count] = val_count;
                case_stmts[case_count] = body;
                case_count++;
            } else if (match(parser, TK_KW_ALITER)) {
                consume(parser, TK_COLON, "Post 'aliter' ':' exspectatur.");
                AstNode* body = ast_create_node(&parser->arena, AST_BLOCK_STMT, parser->previous);
                body->as.block.statements = NULL;
                body->as.block.stmt_count = 0;
                int stmt_cap = 0;
                while (!check(parser, TK_KW_CASUS) && !check(parser, TK_KW_ALITER) && !check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
                    if (body->as.block.stmt_count >= stmt_cap) {
                        int old_cap = stmt_cap;
                        stmt_cap = stmt_cap < 4 ? 4 : stmt_cap * 2;
                        body->as.block.statements = arena_realloc(&parser->arena, body->as.block.statements, sizeof(AstNode*) * old_cap, sizeof(AstNode*) * stmt_cap);
                    }
                    const char* start_cursor = parser->current.start;
                    AstNode* decl = declaration(parser);
                    if (decl) {
                        body->as.block.statements[body->as.block.stmt_count++] = decl;
                    }
                    if (parser->current.start == start_cursor) {
                        advance(parser);
                    }
                }
                default_branch = body;
            } else {
                error(parser, "In 'elige' 'casus' vel 'aliter' exspectatur.");
                advance(parser);
            }
        }
        consume(parser, TK_RBRACE, "Post corpus 'elige' '}' exspectatur.");

        AstNode* node = ast_create_node(&parser->arena, AST_SWITCH_STMT, keyword);
        node->as.switch_stmt.condition = condition;
        node->as.switch_stmt.case_vals = case_vals;
        node->as.switch_stmt.case_val_counts = case_val_counts;
        node->as.switch_stmt.case_stmts = case_stmts;
        node->as.switch_stmt.case_count = case_count;
        node->as.switch_stmt.default_branch = default_branch;
        return node;
    }
    if (match(parser, TK_KW_DUM)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Post 'dum' '(' exspectatur.");
        AstNode* condition = expression(parser);
        consume(parser, TK_RPAREN, "Post condicionem ')' exspectatur.");
        
        AstNode* body = statement(parser);
        
        AstNode* node = ast_create_node(&parser->arena, AST_WHILE_STMT, keyword);
        node->as.while_stmt.condition = condition;
        node->as.while_stmt.body = body;
        return node;
    }
    if (match(parser, TK_KW_PER)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Post 'per' '(' exspectatur.");
        
        AstNode* initializer = NULL;
        if (match(parser, TK_SEMI)) {
            // 空初始化
        } else if (match(parser, TK_KW_SIT)) {
            initializer = var_declaration(parser, false);
        } else {
            initializer = expression_statement(parser);
        }
        
        AstNode* condition = NULL;
        if (!check(parser, TK_SEMI)) {
            condition = expression(parser);
        }
        consume(parser, TK_SEMI, "Post condicionem 'per' ';' exspectatur.");
        
        AstNode* increment = NULL;
        if (!check(parser, TK_RPAREN)) {
            increment = expression(parser);
        }
        consume(parser, TK_RPAREN, "Post clausulas 'per' ')' exspectatur.");
        
        AstNode* body = statement(parser);
        
        AstNode* node = ast_create_node(&parser->arena, AST_FOR_STMT, keyword);
        node->as.for_stmt.initializer = initializer;
        node->as.for_stmt.condition = condition;
        node->as.for_stmt.increment = increment;
        node->as.for_stmt.body = body;
        return node;
    }
    if (match(parser, TK_KW_RUMPE)) {
        Token keyword = parser->previous;
        consume(parser, TK_SEMI, "Post 'rumpe' ';' exspectatur.");
        return ast_create_node(&parser->arena, AST_BREAK_STMT, keyword);
    }
    if (match(parser, TK_KW_PERGE)) {
        Token keyword = parser->previous;
        consume(parser, TK_SEMI, "Post 'perge' ';' exspectatur.");
        return ast_create_node(&parser->arena, AST_CONTINUE_STMT, keyword);
    }
    if (match(parser, TK_KW_MORERE)) {
        Token keyword = parser->previous;
        consume(parser, TK_SEMI, "Post 'morere' ';' exspectatur.");
        return ast_create_node(&parser->arena, AST_TRAP_STMT, keyword);
    }
    if (match(parser, TK_KW_SALI)) {
        Token keyword = parser->previous;
        consume(parser, TK_IDENTIFIER, "Nomen tituli exspectatur.");
        Token label_name = parser->previous;
        consume(parser, TK_SEMI, "Post 'sali' ';' exspectatur.");
        AstNode* node = ast_create_node(&parser->arena, AST_GOTO_STMT, keyword);
        node->as.goto_stmt.label_name = label_name;
        return node;
    }
    if (match(parser, TK_KW_REDDE)) {
        Token keyword = parser->previous;
        AstNode* value = NULL;
        if (!check(parser, TK_SEMI)) {
            value = expression(parser);
        }
        consume(parser, TK_SEMI, "Post valorem redditum ';' exspectatur.");
        AstNode* node = ast_create_node(&parser->arena, AST_RETURN_STMT, keyword);
        node->as.return_stmt.value = value;
        return node;
    }
    if (match(parser, TK_LBRACE)) {
        return block_statement(parser);
    }
    if (match(parser, TK_SEMI)) {
        // 容忍孤立的分号 (空语句)
        AstNode* node = ast_create_node(&parser->arena, AST_EXPR_STMT, parser->previous);
        node->as.expr_stmt.expr = NULL;
        return node;
    }
    return expression_statement(parser);
}

// ---------------------------------------------------------
// 声明解析 (Declaration Parsing)
// ---------------------------------------------------------
static AstNode* var_declaration(Parser* parser, bool is_const) {
    Token keyword = parser->previous;
    bool is_edita = match(parser, TK_KW_EDITA);
    consume(parser, TK_IDENTIFIER, "Nomen variabilis exspectatur.");
    Token name = parser->previous;

    AstNode* type = NULL;
    if (match(parser, TK_COLON)) {
        type = parse_type(parser);
    }

    AstNode* initializer = NULL;
    if (match(parser, TK_ASSIGN)) {
        initializer = expression(parser);
    } else if (is_const) {
        error(parser, "Pro 'lex' '=' et valor exspectantur.");
    }

    if (!type && !initializer) {
        error(parser, "Pro inferentia typus vel valor exspectatur.");
    }

    consume(parser, TK_SEMI, "Post declarationem variabilis ';' exspectatur.");

    AstNode* node = ast_create_node(&parser->arena, is_const ? AST_CONST_DECL : AST_VAR_DECL, keyword);
    node->as.var_decl.name = name;
    node->as.var_decl.type = type;
    node->as.var_decl.initializer = initializer;
    node->as.var_decl.is_editus = is_edita;
    return node;
}

static AstNode* func_declaration(Parser* parser) {
    Token keyword = parser->previous;
    bool is_edita = false;
    bool is_barbara = false;
    Token dll_name = {0};
    
    while (true) {
        if (match(parser, TK_KW_EDITA)) {
            is_edita = true;
        } else if (match(parser, TK_KW_BARBARA)) {
            is_barbara = true;
            if (match(parser, TK_LPAREN)) {
                consume(parser, TK_STRING_CONST, "Nomen bibliothecae (DLL) exspectatur.");
                dll_name = parser->previous;
                consume(parser, TK_RPAREN, "Post nomen bibliothecae ')' exspectatur.");
            }
        } else {
            break;
        }
    }
    consume(parser, TK_IDENTIFIER, "Nomen actionis exspectatur.");
    Token name = parser->previous;

    consume(parser, TK_LPAREN, "Post nomen actionis '(' exspectatur.");
    
    AstNode** params = NULL;
    int param_count = 0;
    int capacity = 0;
    bool is_variadic = false;
    bool is_native_variadic = false;
    
    if (!check(parser, TK_RPAREN)) {
        do {
            if (is_variadic) {
                error(parser, "Parametrum varians 'etc' ultimum esse debet.");
                break;
            }
            
            if (match(parser, TK_KW_ETC)) {
                is_variadic = true;
                if (check(parser, TK_RPAREN)) {
                    is_native_variadic = false;
                    break; // FFI 无类型变长参数，直接结束参数解析
                } else {
                    is_native_variadic = true; // 原生变长参数，继续解析参数名和类型
                }
            }

            if (param_count >= capacity) {
                int old_capacity = capacity;
                capacity = capacity < 4 ? 4 : capacity * 2;
                params = arena_realloc(&parser->arena, params, sizeof(AstNode*) * old_capacity, sizeof(AstNode*) * capacity);
            }
            consume(parser, TK_IDENTIFIER, "Nomen parametri exspectatur.");
            Token param_name = parser->previous;
            consume(parser, TK_COLON, "Post nomen parametri ':' exspectatur.");
            AstNode* param_type = parse_type(parser);
            
            AstNode* param_node = ast_create_node(&parser->arena, AST_VAR_DECL, param_name);
            param_node->as.var_decl.name = param_name;
            param_node->as.var_decl.type = param_type;
            param_node->as.var_decl.initializer = NULL;
            
            params[param_count++] = param_node;
        } while (match(parser, TK_COMMA));
    }
    consume(parser, TK_RPAREN, "Post parametros ')' exspectatur.");

    AstNode* return_type = NULL;
    if (match(parser, TK_ARROW)) {
        return_type = parse_type(parser);
    }

    AstNode* body = NULL;
    if (match(parser, TK_SEMI)) {
        // 外部函数声明，无函数体
    } else {
        consume(parser, TK_LBRACE, "Ante corpus actionis '{' exspectatur.");
        body = block_statement(parser);
    }

    AstNode* node = ast_create_node(&parser->arena, AST_FUNC_DECL, keyword);
    node->as.func_decl.name = name;
    node->as.func_decl.return_type = return_type;
    node->as.func_decl.params = params;
    node->as.func_decl.param_count = param_count;
    node->as.func_decl.body = body;
    node->as.func_decl.is_editus = is_edita;
    node->as.func_decl.is_barbarus = is_barbara;
    node->as.func_decl.dll_name = dll_name;
    node->as.func_decl.is_variadic = is_variadic;
    node->as.func_decl.is_native_variadic = is_native_variadic;
    return node;
}

static AstNode* union_declaration(Parser* parser) {
    Token keyword = parser->previous;
    bool is_edita = match(parser, TK_KW_EDITA);
    
    consume(parser, TK_IDENTIFIER, "Nomen unionis exspectatur.");
    Token name = parser->previous;
    
    consume(parser, TK_LBRACE, "Ante corpus unionis '{' exspectatur.");
    
    AstNode** fields = NULL;
    int field_count = 0;
    int capacity = 0;
    
    while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
        if (match(parser, TK_KW_SIT)) {
            if (field_count >= capacity) {
                int old_capacity = capacity;
                capacity = capacity < 8 ? 8 : capacity * 2;
                fields = arena_realloc(&parser->arena, fields, sizeof(AstNode*) * old_capacity, sizeof(AstNode*) * capacity);
            }
            fields[field_count++] = var_declaration(parser, false);
        } else {
            error(parser, "Pro declaratione campi unionis 'sit' exspectatur.");
            advance(parser);
        }
    }
    
    consume(parser, TK_RBRACE, "Post corpus unionis '}' exspectatur.");
    
    AstNode* node = ast_create_node(&parser->arena, AST_UNION_DECL, keyword);
    node->as.struct_decl.name = name;
    node->as.struct_decl.is_editus = is_edita;
    node->as.struct_decl.is_densa = false;
    node->as.struct_decl.fields = fields;
    node->as.struct_decl.field_count = field_count;
    return node;
}

static AstNode* enum_declaration(Parser* parser) {
    Token keyword = parser->previous;
    bool is_edita = match(parser, TK_KW_EDITA);
    
    consume(parser, TK_IDENTIFIER, "Nomen ordinis exspectatur.");
    Token name = parser->previous;
    
    consume(parser, TK_LBRACE, "Ante corpus ordinis '{' exspectatur.");
    
    Token* variant_names = NULL;
    AstNode** variant_values = NULL;
    int variant_count = 0;
    int capacity = 0;
    
    if (!check(parser, TK_RBRACE)) {
        do {
            if (variant_count >= capacity) {
                int old_capacity = capacity;
                capacity = capacity < 8 ? 8 : capacity * 2;
                variant_names = arena_realloc(&parser->arena, variant_names, sizeof(Token) * old_capacity, sizeof(Token) * capacity);
                variant_values = arena_realloc(&parser->arena, variant_values, sizeof(AstNode*) * old_capacity, sizeof(AstNode*) * capacity);
            }
            
            consume(parser, TK_IDENTIFIER, "Nomen variantis exspectatur.");
            variant_names[variant_count] = parser->previous;
            
            AstNode* value = NULL;
            if (match(parser, TK_ASSIGN)) {
                value = expression(parser);
            }
            variant_values[variant_count] = value;
            variant_count++;
            
        } while (match(parser, TK_COMMA));
    }
    
    consume(parser, TK_RBRACE, "Post corpus ordinis '}' exspectatur.");
    
    AstNode* node = ast_create_node(&parser->arena, AST_ENUM_DECL, keyword);
    node->as.enum_decl.name = name;
    node->as.enum_decl.is_editus = is_edita;
    node->as.enum_decl.variant_names = variant_names;
    node->as.enum_decl.variant_values = variant_values;
    node->as.enum_decl.variant_count = variant_count;
    return node;
}

static AstNode* type_alias_declaration(Parser* parser) {
    Token keyword = parser->previous;
    bool is_edita = match(parser, TK_KW_EDITA);
    
    consume(parser, TK_IDENTIFIER, "Nomen imaginis exspectatur.");
    Token name = parser->previous;
    
    consume(parser, TK_ASSIGN, "Post nomen imaginis '=' exspectatur.");
    
    AstNode* target_type = parse_type(parser);
    
    consume(parser, TK_SEMI, "Post declarationem imaginis ';' exspectatur.");
    
    AstNode* node = ast_create_node(&parser->arena, AST_TYPE_ALIAS_DECL, keyword);
    node->as.type_alias_decl.name = name;
    node->as.type_alias_decl.target_type = target_type;
    node->as.type_alias_decl.is_editus = is_edita;
    return node;
}

static AstNode* struct_declaration(Parser* parser) {
    Token keyword = parser->previous;
    bool is_densa = false;
    bool is_edita = false;
    while (true) {
        if (match(parser, TK_TY_DENSA)) is_densa = true;
        else if (match(parser, TK_KW_EDITA)) is_edita = true;
        else break;
    }
    
    consume(parser, TK_IDENTIFIER, "Nomen formae exspectatur.");
    Token name = parser->previous;
    
    consume(parser, TK_LBRACE, "Ante corpus formae '{' exspectatur.");
    
    AstNode** fields = NULL;
    int field_count = 0;
    int capacity = 0;
    
    while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF)) {
        if (match(parser, TK_KW_SIT)) {
            if (field_count >= capacity) {
                int old_capacity = capacity;
                capacity = capacity < 8 ? 8 : capacity * 2;
                fields = arena_realloc(&parser->arena, fields, sizeof(AstNode*) * old_capacity, sizeof(AstNode*) * capacity);
            }
            fields[field_count++] = var_declaration(parser, false);
        } else {
            error(parser, "Pro declaratione campi formae 'sit' exspectatur.");
            advance(parser);
        }
    }
    
    consume(parser, TK_RBRACE, "Post corpus formae '}' exspectatur.");
    
    AstNode* node = ast_create_node(&parser->arena, AST_STRUCT_DECL, keyword);
    node->as.struct_decl.name = name;
    node->as.struct_decl.is_editus = is_edita;
    node->as.struct_decl.is_densa = is_densa;
    node->as.struct_decl.fields = fields;
    node->as.struct_decl.field_count = field_count;
    return node;
}

// 错误恢复同步
static void synchronize(Parser* parser) {
    while (parser->current.kind != TK_EOF) {
        if (parser->previous.kind == TK_SEMI) {
            parser->panic_mode = false;
            return;
        }
        switch (parser->current.kind) {
            case TK_KW_ACTIO:
            case TK_KW_SIT:
            case TK_KW_LEX:
            case TK_TY_FORMA:
            case TK_KW_UNIO:
            case TK_KW_LIBER:
            case TK_KW_CONSULE:
            case TK_KW_SI:
            case TK_KW_DUM:
            case TK_KW_PER:
            case TK_KW_REDDE:
            case TK_KW_SALI:
                parser->panic_mode = false;
                return;
            default:
                ; // 继续寻找同步点
        }
        advance(parser);
    }
    parser->panic_mode = false;
}

static AstNode* declaration(Parser* parser) {
    AstNode* decl = NULL;
    
    if (match(parser, TK_KW_LIBER)) {
        Token keyword = parser->previous;
        consume(parser, TK_IDENTIFIER, "Nomen libri exspectatur.");
        Token name = parser->previous;
        consume(parser, TK_SEMI, "Post declarationem libri ';' exspectatur.");
        decl = ast_create_node(&parser->arena, AST_MODULE_DECL, keyword);
        decl->as.module_decl.name = name;
    } else if (match(parser, TK_KW_CONSULE)) {
        Token keyword = parser->previous;
        consume(parser, TK_KW_LIBER, "Post 'consule' 'liber' exspectatur.");
        consume(parser, TK_IDENTIFIER, "Nomen libri exspectatur.");
        Token name = parser->previous;
        consume(parser, TK_SEMI, "Post declarationem importatam ';' exspectatur.");
        decl = ast_create_node(&parser->arena, AST_IMPORT_DECL, keyword);
        decl->as.import_decl.module_name = name;
        decl->as.import_decl.items = NULL;
        decl->as.import_decl.item_count = 0;
    } else if (match(parser, TK_KW_DE)) {
        Token keyword = parser->previous;
        consume(parser, TK_IDENTIFIER, "Post 'de' nomen libri exspectatur.");
        Token name = parser->previous;
        consume(parser, TK_KW_EXCERPE, "Post nomen libri 'excerpe' exspectatur.");
        
        Token* items = NULL;
        int item_count = 0;
        int capacity = 0;
        
        do {
            if (item_count >= capacity) {
                int old_capacity = capacity;
                capacity = capacity < 4 ? 4 : capacity * 2;
                items = arena_realloc(&parser->arena, items, sizeof(Token) * old_capacity, sizeof(Token) * capacity);
            }
            consume(parser, TK_IDENTIFIER, "Nomen elementi ad importandum exspectatur.");
            items[item_count++] = parser->previous;
        } while (match(parser, TK_COMMA));
        
        consume(parser, TK_SEMI, "Post declarationem importatam ';' exspectatur.");
        decl = ast_create_node(&parser->arena, AST_IMPORT_DECL, keyword);
        decl->as.import_decl.module_name = name;
        decl->as.import_decl.items = items;
        decl->as.import_decl.item_count = item_count;
    } else {
        if (match(parser, TK_KW_ACTIO)) {
            decl = func_declaration(parser);
        } else if (match(parser, TK_KW_SIT)) {
            decl = var_declaration(parser, false);
        } else if (match(parser, TK_KW_LEX)) {
            decl = var_declaration(parser, true);
        } else if (match(parser, TK_TY_FORMA)) {
            decl = struct_declaration(parser);
        } else if (match(parser, TK_KW_UNIO)) {
            decl = union_declaration(parser);
        } else if (match(parser, TK_KW_IMAGO)) {
            decl = type_alias_declaration(parser);
        } else if (match(parser, TK_KW_ORDO)) {
            decl = enum_declaration(parser);
        } else {
            decl = statement(parser);
        }
    }

    if (parser->panic_mode) synchronize(parser);
    return decl;
}

// ---------------------------------------------------------
// API 核心
// ---------------------------------------------------------
void parser_init(Parser* parser, const char* source) {
    lexer_init(&parser->lexer, source);
    parser->had_error = false;
    parser->panic_mode = false;
    // 拨付 64MB 幽灵后勤大营
    arena_init(&parser->arena, 64 * 1024 * 1024);
    advance(parser);
}

void parser_free(Parser* parser) {
    arena_free(&parser->arena);
}

AstNode* parse_program(Parser* parser) {
    AstNode* node = ast_create_node(&parser->arena, AST_PROGRAM, parser->current);
    node->as.program.declarations = NULL;
    node->as.program.decl_count = 0;
    int capacity = 0;

    while (!match(parser, TK_EOF)) {
        if (node->as.program.decl_count >= capacity) {
            int old_capacity = capacity;
            capacity = capacity < 8 ? 8 : capacity * 2;
            node->as.program.declarations = arena_realloc(&parser->arena, node->as.program.declarations, sizeof(AstNode*) * old_capacity, sizeof(AstNode*) * capacity);
        }
        const char* start_cursor = parser->current.start;
        AstNode* decl = declaration(parser);
        if (decl) {
            node->as.program.declarations[node->as.program.decl_count++] = decl;
        }
        // 如果游标没有前进，强制推进以避免死循环
        if (parser->current.start == start_cursor) {
            advance(parser);
        }
    }
    return node;
}
