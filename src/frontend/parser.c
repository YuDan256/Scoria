#include "parser.h"
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------
// AST 节点内存分配 (幽灵后勤大营)
// ---------------------------------------------------------
AstNode* ast_create_node(Arena* arena, AstNodeKind kind, Token token) {
    AstNode* node = (AstNode*)arena_alloc(arena, sizeof(AstNode));
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
        error(parser, "Symbolum ignotum.");
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

// ---------------------------------------------------------
// 类型解析 (Type Parsing)
// ---------------------------------------------------------
static AstNode* parse_type(Parser* parser) {
    AstNode* node = ast_create_node(&parser->arena, AST_TYPE, parser->current);
    node->as.type_node.is_via = false;
    node->as.type_node.is_cohors = false;
    node->as.type_node.is_acies = false;
    node->as.type_node.array_size = NULL;

    if (match(parser, TK_TY_VIA)) {
        node->as.type_node.is_via = true;
    } else if (match(parser, TK_TY_COHORS)) {
        node->as.type_node.is_cohors = true;
    } else if (match(parser, TK_TY_ACIES)) {
        node->as.type_node.is_acies = true;
        consume(parser, TK_LBRACKET, "Exspecta '[' post acies.");
        node->as.type_node.array_size = expression(parser);
        consume(parser, TK_RBRACKET, "Exspecta ']' post magnitudinem aciei.");
    }

    if (match(parser, TK_TY_I8) || match(parser, TK_TY_I16) || match(parser, TK_TY_I32) || match(parser, TK_TY_I64) ||
        match(parser, TK_TY_P8) || match(parser, TK_TY_P16) || match(parser, TK_TY_P32) || match(parser, TK_TY_P64) ||
        match(parser, TK_TY_F32) || match(parser, TK_TY_F64) ||
        match(parser, TK_TY_LOGICA) || match(parser, TK_TY_LITTERA) || match(parser, TK_TY_TEXTUS) ||
        match(parser, TK_IDENTIFIER)) {
        
        node->as.type_node.base_type = parser->previous;
        
        // 处理古典词缀组合，例如 minimus (i8) purus -> p8
        if (match(parser, TK_TY_PURUS)) {
            switch (node->as.type_node.base_type.kind) {
                case TK_TY_I8:  node->as.type_node.base_type.kind = TK_TY_P8; break;
                case TK_TY_I16: node->as.type_node.base_type.kind = TK_TY_P16; break;
                case TK_TY_I32: node->as.type_node.base_type.kind = TK_TY_P32; break;
                case TK_TY_I64: node->as.type_node.base_type.kind = TK_TY_P64; break;
                default: error(parser, "Usus invalidus modificationis 'purus'.");
            }
        } else if (node->as.type_node.base_type.kind == TK_TY_F64 && match(parser, TK_TY_I32)) {
            // fractus (f64) medius (i32) -> f32
            node->as.type_node.base_type.kind = TK_TY_F32;
        }
    } else {
        error(parser, "Exspecta nomen typi.");
    }

    return node;
}

// ---------------------------------------------------------
// 表达式解析 (Expression Parsing)
// ---------------------------------------------------------
static AstNode* primary(Parser* parser) {
    if (match(parser, TK_INT_CONST) || match(parser, TK_FLOAT_CONST) ||
        match(parser, TK_BOOL_CONST) || match(parser, TK_STRING_CONST) ||
        match(parser, TK_CHAR_CONST)) {
        return ast_create_node(&parser->arena, AST_LITERAL_EXPR, parser->previous);
    }
    if (match(parser, TK_IDENTIFIER)) {
        return ast_create_node(&parser->arena, AST_IDENT_EXPR, parser->previous);
    }
    if (match(parser, TK_KW_MUTA)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Exspecta '(' post muta.");
        AstNode* target_type = parse_type(parser);
        consume(parser, TK_COMMA, "Exspecta ',' post typum in muta.");
        AstNode* value = expression(parser);
        consume(parser, TK_RPAREN, "Exspecta ')' post argumenta muta.");
        AstNode* node = ast_create_node(&parser->arena, AST_CAST_EXPR, keyword);
        node->as.cast_expr.target_type = target_type;
        node->as.cast_expr.value = value;
        return node;
    }
    if (match(parser, TK_KW_VADE) || match(parser, TK_KW_RECEDE)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Exspecta '(' post vade/recede.");
        AstNode* pointer = expression(parser);
        consume(parser, TK_COMMA, "Exspecta ',' post indicem.");
        AstNode* offset = expression(parser);
        consume(parser, TK_RPAREN, "Exspecta ')' post argumenta.");
        AstNode* node = ast_create_node(&parser->arena, keyword.kind == TK_KW_VADE ? AST_VADE_EXPR : AST_RECEDE_EXPR, keyword);
        node->as.pointer_offset.pointer = pointer;
        node->as.pointer_offset.offset = offset;
        return node;
    }
    if (match(parser, TK_KW_CREA)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Exspecta '(' post crea.");
        AstNode* type = parse_type(parser);
        AstNode* count = NULL;
        if (match(parser, TK_COMMA)) {
            count = expression(parser);
        }
        consume(parser, TK_RPAREN, "Exspecta ')' post argumenta crea.");
        AstNode* node = ast_create_node(&parser->arena, AST_CREA_EXPR, keyword);
        node->as.crea_expr.type = type;
        node->as.crea_expr.count = count;
        return node;
    }
    if (match(parser, TK_KW_NECA)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Exspecta '(' post neca.");
        AstNode* pointer = expression(parser);
        consume(parser, TK_RPAREN, "Exspecta ')' post argumenta neca.");
        AstNode* node = ast_create_node(&parser->arena, AST_NECA_EXPR, keyword);
        node->as.neca_expr.pointer = pointer;
        return node;
    }
    if (match(parser, TK_KW_SCRIBE)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Exspecta '(' post scribe.");
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
        consume(parser, TK_RPAREN, "Exspecta ')' post argumenta scribe.");
        return node;
    }
    if (match(parser, TK_LPAREN)) {
        AstNode* expr = expression(parser);
        consume(parser, TK_RPAREN, "Exspecta ')' post expressionem.");
        return expr;
    }
    error(parser, "Exspecta expressionem.");
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
            consume(parser, TK_RPAREN, "Exspecta ')' post argumenta.");
            expr = node;
        } else if (match(parser, TK_LBRACKET)) {
            Token bracket = parser->previous;
            AstNode* index = expression(parser);
            consume(parser, TK_RBRACKET, "Exspecta ']' post indicem.");
            AstNode* node = ast_create_node(&parser->arena, AST_INDEX_EXPR, bracket);
            node->as.index_expr.target = expr;
            node->as.index_expr.index = index;
            expr = node;
        } else if (match(parser, TK_DOT)) {
            Token dot = parser->previous;
            consume(parser, TK_IDENTIFIER, "Exspecta nomen proprietatis post '.'.");
            Token name = parser->previous;
            AstNode* node = ast_create_node(&parser->arena, AST_MEMBER_EXPR, dot);
            node->as.member_expr.object = expr;
            node->as.member_expr.property = name;
            node->as.member_expr.is_pointer = false;
            expr = node;
        } else if (match(parser, TK_ARROW)) {
            Token arrow = parser->previous;
            consume(parser, TK_IDENTIFIER, "Exspecta nomen proprietatis post '->'.");
            Token name = parser->previous;
            AstNode* node = ast_create_node(&parser->arena, AST_MEMBER_EXPR, arrow);
            node->as.member_expr.object = expr;
            node->as.member_expr.property = name;
            node->as.member_expr.is_pointer = true;
            expr = node;
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
            node->as.assign.target = expr;
            node->as.assign.value = value;
            return node;
        }
        error_at(parser, &equals, "Scopus assignationis invalidus.");
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
    consume(parser, TK_SEMI, "Exspecta ';' post expressionem.");
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
        node->as.block.statements[node->as.block.stmt_count++] = declaration(parser);
    }
    consume(parser, TK_RBRACE, "Exspecta '}' post clausulam.");
    return node;
}

static AstNode* statement(Parser* parser) {
    if (match(parser, TK_KW_SI)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Exspecta '(' post 'si'.");
        AstNode* condition = expression(parser);
        consume(parser, TK_RPAREN, "Exspecta ')' post condicionem.");
        
        consume(parser, TK_LBRACE, "Exspecta '{' ante corpus 'si'.");
        AstNode* then_branch = block_statement(parser);
        
        AstNode* else_branch = NULL;
        if (match(parser, TK_KW_ALITER)) {
            if (check(parser, TK_KW_SI)) {
                // aliter si
                else_branch = statement(parser);
            } else {
                consume(parser, TK_LBRACE, "Exspecta '{' ante corpus 'aliter'.");
                else_branch = block_statement(parser);
            }
        }
        
        AstNode* node = ast_create_node(&parser->arena, AST_IF_STMT, keyword);
        node->as.if_stmt.condition = condition;
        node->as.if_stmt.then_branch = then_branch;
        node->as.if_stmt.else_branch = else_branch;
        return node;
    }
    if (match(parser, TK_KW_DUM)) {
        Token keyword = parser->previous;
        consume(parser, TK_LPAREN, "Exspecta '(' post 'dum'.");
        AstNode* condition = expression(parser);
        consume(parser, TK_RPAREN, "Exspecta ')' post condicionem.");
        
        consume(parser, TK_LBRACE, "Exspecta '{' ante corpus 'dum'.");
        AstNode* body = block_statement(parser);
        
        AstNode* node = ast_create_node(&parser->arena, AST_WHILE_STMT, keyword);
        node->as.while_stmt.condition = condition;
        node->as.while_stmt.body = body;
        return node;
    }
    if (match(parser, TK_KW_REDDE)) {
        Token keyword = parser->previous;
        AstNode* value = NULL;
        if (!check(parser, TK_SEMI)) {
            value = expression(parser);
        }
        consume(parser, TK_SEMI, "Exspecta ';' post valorem redditum.");
        AstNode* node = ast_create_node(&parser->arena, AST_RETURN_STMT, keyword);
        node->as.return_stmt.value = value;
        return node;
    }
    if (match(parser, TK_LBRACE)) {
        return block_statement(parser);
    }
    return expression_statement(parser);
}

// ---------------------------------------------------------
// 声明解析 (Declaration Parsing)
// ---------------------------------------------------------
static AstNode* var_declaration(Parser* parser, bool is_editus, bool is_const) {
    Token keyword = parser->previous;
    consume(parser, TK_IDENTIFIER, "Exspecta nomen variabilis.");
    Token name = parser->previous;

    AstNode* type = NULL;
    if (match(parser, TK_COLON)) {
        type = parse_type(parser);
    }

    AstNode* initializer = NULL;
    if (match(parser, TK_ASSIGN)) {
        initializer = expression(parser);
    }
    consume(parser, TK_SEMI, "Exspecta ';' post declarationem variabilis.");

    AstNode* node = ast_create_node(&parser->arena, is_const ? AST_CONST_DECL : AST_VAR_DECL, keyword);
    node->as.var_decl.name = name;
    node->as.var_decl.type = type;
    node->as.var_decl.initializer = initializer;
    node->as.var_decl.is_editus = is_editus;
    return node;
}

static AstNode* func_declaration(Parser* parser, bool is_editus, bool is_barbarus) {
    Token keyword = parser->previous;
    consume(parser, TK_IDENTIFIER, "Exspecta nomen actionis.");
    Token name = parser->previous;

    consume(parser, TK_LPAREN, "Exspecta '(' post nomen actionis.");
    
    AstNode** params = NULL;
    int param_count = 0;
    int capacity = 0;
    
    if (!check(parser, TK_RPAREN)) {
        do {
            if (param_count >= capacity) {
                int old_capacity = capacity;
                capacity = capacity < 4 ? 4 : capacity * 2;
                params = arena_realloc(&parser->arena, params, sizeof(AstNode*) * old_capacity, sizeof(AstNode*) * capacity);
            }
            consume(parser, TK_IDENTIFIER, "Exspecta nomen parametri.");
            Token param_name = parser->previous;
            consume(parser, TK_COLON, "Exspecta ':' post nomen parametri.");
            AstNode* param_type = parse_type(parser);
            
            AstNode* param_node = ast_create_node(&parser->arena, AST_VAR_DECL, param_name);
            param_node->as.var_decl.name = param_name;
            param_node->as.var_decl.type = param_type;
            param_node->as.var_decl.initializer = NULL;
            
            params[param_count++] = param_node;
        } while (match(parser, TK_COMMA));
    }
    consume(parser, TK_RPAREN, "Exspecta ')' post parametros.");

    AstNode* return_type = NULL;
    if (match(parser, TK_ARROW)) {
        return_type = parse_type(parser);
    }

    AstNode* body = NULL;
    if (match(parser, TK_SEMI)) {
        // 外部函数声明，无函数体
    } else {
        consume(parser, TK_LBRACE, "Exspecta '{' ante corpus actionis.");
        body = block_statement(parser);
    }

    AstNode* node = ast_create_node(&parser->arena, AST_FUNC_DECL, keyword);
    node->as.func_decl.name = name;
    node->as.func_decl.return_type = return_type;
    node->as.func_decl.params = params;
    node->as.func_decl.param_count = param_count;
    node->as.func_decl.body = body;
    node->as.func_decl.is_editus = is_editus;
    node->as.func_decl.is_barbarus = is_barbarus;
    return node;
}

static AstNode* struct_declaration(Parser* parser, bool is_editus) {
    Token keyword = parser->previous;
    bool is_densa = false;
    if (match(parser, TK_TY_DENSA)) {
        is_densa = true;
    }
    
    consume(parser, TK_IDENTIFIER, "Exspecta nomen formae.");
    Token name = parser->previous;
    
    consume(parser, TK_LBRACE, "Exspecta '{' ante corpus formae.");
    
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
            fields[field_count++] = var_declaration(parser, false, false);
        } else {
            error(parser, "Exspecta 'sit' pro declaratione campi formae.");
            advance(parser);
        }
    }
    
    consume(parser, TK_RBRACE, "Exspecta '}' post corpus formae.");
    
    AstNode* node = ast_create_node(&parser->arena, AST_STRUCT_DECL, keyword);
    node->as.struct_decl.name = name;
    node->as.struct_decl.is_editus = is_editus;
    node->as.struct_decl.is_densa = is_densa;
    node->as.struct_decl.fields = fields;
    node->as.struct_decl.field_count = field_count;
    return node;
}

// 错误恢复同步
static void synchronize(Parser* parser) {
    parser->panic_mode = false;
    while (parser->current.kind != TK_EOF) {
        if (parser->previous.kind == TK_SEMI) return;
        switch (parser->current.kind) {
            case TK_KW_ACTIO:
            case TK_KW_SIT:
            case TK_KW_SI:
            case TK_KW_DUM:
            case TK_KW_REDDE:
                return;
            default:
                ; // 继续寻找同步点
        }
        advance(parser);
    }
}

static AstNode* declaration(Parser* parser) {
    AstNode* decl = NULL;
    
    if (match(parser, TK_KW_LIBER)) {
        Token keyword = parser->previous;
        consume(parser, TK_IDENTIFIER, "Exspecta nomen libri.");
        Token name = parser->previous;
        consume(parser, TK_SEMI, "Exspecta ';' post declarationem libri.");
        decl = ast_create_node(&parser->arena, AST_MODULE_DECL, keyword);
        decl->as.module_decl.name = name;
    } else if (match(parser, TK_KW_CONSULE)) {
        Token keyword = parser->previous;
        consume(parser, TK_KW_LIBER, "Exspecta 'liber' post 'consule'.");
        consume(parser, TK_IDENTIFIER, "Exspecta nomen libri.");
        Token name = parser->previous;
        consume(parser, TK_SEMI, "Exspecta ';' post declarationem importatam.");
        decl = ast_create_node(&parser->arena, AST_IMPORT_DECL, keyword);
        decl->as.import_decl.module_name = name;
        decl->as.import_decl.items = NULL;
        decl->as.import_decl.item_count = 0;
    } else if (match(parser, TK_KW_DE)) {
        Token keyword = parser->previous;
        consume(parser, TK_IDENTIFIER, "Exspecta nomen libri post 'de'.");
        Token name = parser->previous;
        consume(parser, TK_KW_EXCERPE, "Exspecta 'excerpe' post nomen libri.");
        
        Token* items = NULL;
        int item_count = 0;
        int capacity = 0;
        
        do {
            if (item_count >= capacity) {
                int old_capacity = capacity;
                capacity = capacity < 4 ? 4 : capacity * 2;
                items = arena_realloc(&parser->arena, items, sizeof(Token) * old_capacity, sizeof(Token) * capacity);
            }
            consume(parser, TK_IDENTIFIER, "Exspecta nomen elementi ad importandum.");
            items[item_count++] = parser->previous;
        } while (match(parser, TK_COMMA));
        
        consume(parser, TK_SEMI, "Exspecta ';' post declarationem importatam.");
        decl = ast_create_node(&parser->arena, AST_IMPORT_DECL, keyword);
        decl->as.import_decl.module_name = name;
        decl->as.import_decl.items = items;
        decl->as.import_decl.item_count = item_count;
    } else {
        bool is_editus = match(parser, TK_KW_EDITUS);
        bool is_barbarus = match(parser, TK_KW_BARBARUS);
        
        if (match(parser, TK_KW_ACTIO)) {
            decl = func_declaration(parser, is_editus, is_barbarus);
        } else if (match(parser, TK_KW_SIT)) {
            decl = var_declaration(parser, is_editus, false);
        } else if (match(parser, TK_KW_LEX)) {
            decl = var_declaration(parser, is_editus, true);
        } else if (match(parser, TK_TY_FORMA)) {
            decl = struct_declaration(parser, is_editus);
        } else {
            if (is_editus || is_barbarus) {
                error(parser, "Usus invalidus modificationis 'editus' vel 'barbarus'.");
            }
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
        node->as.program.declarations[node->as.program.decl_count++] = declaration(parser);
    }
    return node;
}
