#include "lexer.h"
#include <string.h>
#include <ctype.h>

// ---------------------------------------------------------
// 游标推步基建 (Cursor Mechanics)
// ---------------------------------------------------------
static bool is_at_end(Lexer* lexer) { return *lexer->current == '\0'; }

static char advance(Lexer* lexer) {
    lexer->current++;
    lexer->column++;
    return lexer->current[-1];
}

static char peek(Lexer* lexer) { return *lexer->current; }

static char peek_next(Lexer* lexer) {
    if (is_at_end(lexer)) return '\0';
    return lexer->current[1];
}

static bool match(Lexer* lexer, char expected) {
    if (is_at_end(lexer) || *lexer->current != expected) return false;
    lexer->current++;
    lexer->column++;
    return true;
}

// ---------------------------------------------------------
// Token 铸造炉
// ---------------------------------------------------------
static Token make_token(Lexer* lexer, TokenKind kind) {
    Token token;
    token.kind = kind;
    token.start = lexer->start;
    token.length = (uint32_t)(lexer->current - lexer->start);
    token.line = lexer->line;
    token.column = lexer->column - token.length; // 锚定于起始列
    return token;
}

// ---------------------------------------------------------
// 阵地清除 (跳过空格与注释)
// ---------------------------------------------------------
static void skip_whitespace(Lexer* lexer) {
    for (;;) {
        char c = peek(lexer);
        switch (c) {
        case ' ': case '\r': case '\t':
            advance(lexer);
            break;
        case '\n':
            lexer->line++;
            lexer->column = 0;
            advance(lexer);
            break;
        case '/':
            if (peek_next(lexer) == '/') {
                while (peek(lexer) != '\n' && !is_at_end(lexer)) advance(lexer);
            }
            else {
                return; // 遇到的是除法符 '/'
            }
            break;
        default:
            return;
        }
    }
}

// ---------------------------------------------------------
// 古典拉丁与缩写高速校验树 (O(1) 速度)
// ---------------------------------------------------------
static TokenKind check_keyword(Lexer* lexer, uint32_t start_col, uint32_t length, const char* rest, TokenKind kind) {
    if (lexer->current - lexer->start == (ptrdiff_t)(start_col + length) &&
        memcmp(lexer->start + start_col, rest, length) == 0) {
        return kind;
    }
    return TK_IDENTIFIER;
}

static TokenKind identifier_type(Lexer* lexer) {
    char c = lexer->start[0];
    uint32_t len = (uint32_t)(lexer->current - lexer->start);

    // 【极速缩写映射通道】
    if (len == 2 && c == 'i' && lexer->start[1] == '8') return TK_TY_I8;
    if (len == 3 && memcmp(lexer->start, "i16", 3) == 0) return TK_TY_I16;
    if (len == 3 && memcmp(lexer->start, "i32", 3) == 0) return TK_TY_I32;
    if (len == 3 && memcmp(lexer->start, "i64", 3) == 0) return TK_TY_I64;
    if (len == 3 && memcmp(lexer->start, "img", 3) == 0) return TK_KW_IMAGO;

    if (len == 2 && c == 'p' && lexer->start[1] == '8') return TK_TY_P8;
    if (len == 3 && memcmp(lexer->start, "p16", 3) == 0) return TK_TY_P16;
    if (len == 3 && memcmp(lexer->start, "p32", 3) == 0) return TK_TY_P32;
    if (len == 3 && memcmp(lexer->start, "p64", 3) == 0) return TK_TY_P64;

    if (len == 3 && memcmp(lexer->start, "f32", 3) == 0) return TK_TY_F32;
    if (len == 3 && memcmp(lexer->start, "f64", 3) == 0) return TK_TY_F64;

    if (len == 3 && memcmp(lexer->start, "edt", 3) == 0) return TK_KW_EDITA;
    if (len == 3 && memcmp(lexer->start, "bbr", 3) == 0) return TK_KW_BARBARA;
    if (len == 3 && memcmp(lexer->start, "lgc", 3) == 0) return TK_TY_LOGICA;
    if (len == 3 && memcmp(lexer->start, "ltr", 3) == 0) return TK_TY_LITTERA;
    if (len == 3 && memcmp(lexer->start, "txt", 3) == 0) return TK_TY_TEXTUS;
    if (len == 3 && memcmp(lexer->start, "frm", 3) == 0) return TK_TY_FORMA;
    if (len == 3 && memcmp(lexer->start, "dns", 3) == 0) return TK_TY_DENSA;
    if (len == 3 && memcmp(lexer->start, "crs", 3) == 0) return TK_TY_COHORS;
    if (len == 3 && memcmp(lexer->start, "act", 3) == 0) return TK_KW_ACTIO;
    if (len == 3 && memcmp(lexer->start, "rdd", 3) == 0) return TK_KW_REDDE;
    if (len == 3 && memcmp(lexer->start, "nhl", 3) == 0) return TK_KW_NIHIL;
    if (len == 3 && memcmp(lexer->start, "rmp", 3) == 0) return TK_KW_RUMPE;
    if (len == 3 && memcmp(lexer->start, "prg", 3) == 0) return TK_KW_PERGE;
    if (len == 3 && memcmp(lexer->start, "csl", 3) == 0) return TK_KW_CONSULE;
    if (len == 3 && memcmp(lexer->start, "xcp", 3) == 0) return TK_KW_EXCERPE;
    if (len == 3 && memcmp(lexer->start, "alt", 3) == 0) return TK_KW_ALITER;
    if (len == 3 && memcmp(lexer->start, "scb", 3) == 0) return TK_KW_SCRIBE;
    if (len == 3 && memcmp(lexer->start, "leg", 3) == 0) return TK_KW_LEGE;
    if (len == 3 && memcmp(lexer->start, "rcd", 3) == 0) return TK_KW_RECEDE;

    // 【古典宗卷长体映射网络】
    switch (c) {
    case 'a':
        if (len == 5) {
            if (memcmp(lexer->start + 1, "ctio", 4) == 0) return TK_KW_ACTIO;
            if (memcmp(lexer->start + 1, "cies", 4) == 0) return TK_TY_ACIES;
        }
        if (len == 6) return check_keyword(lexer, 1, 5, "liter", TK_KW_ALITER);
        break;
    case 'b':
        if (len == 6) return check_keyword(lexer, 1, 5, "revis", TK_TY_I16);
        if (len == 7) return check_keyword(lexer, 1, 6, "arbara", TK_KW_BARBARA);
        break;
    case 'c':
        if (len == 7) return check_keyword(lexer, 1, 6, "onsule", TK_KW_CONSULE);
        if (len == 6) return check_keyword(lexer, 1, 5, "ohors", TK_TY_COHORS);
        if (len == 5) return check_keyword(lexer, 1, 4, "asus", TK_KW_CASUS);
        if (len == 4) return check_keyword(lexer, 1, 3, "rea", TK_KW_CREA);
        break;
    case 'd':
        if (len == 2) return check_keyword(lexer, 1, 1, "e", TK_KW_DE);
        if (len == 3) return check_keyword(lexer, 1, 2, "um", TK_KW_DUM);
        if (len == 5) return check_keyword(lexer, 1, 4, "ensa", TK_TY_DENSA);
        break;
    case 'e':
        if (len == 7) return check_keyword(lexer, 1, 6, "xcerpe", TK_KW_EXCERPE);
        if (len == 5) {
            if (memcmp(lexer->start + 1, "dita", 4) == 0) return TK_KW_EDITA;
            if (memcmp(lexer->start + 1, "lige", 4) == 0) return TK_KW_ELIGE;
        }
        if (len == 3) return check_keyword(lexer, 1, 2, "tc", TK_KW_ETC);
        break;
    case 'f':
        if (len == 6 && lexer->start[1] == 'a') return check_keyword(lexer, 2, 4, "lsum", TK_BOOL_CONST);
        if (len == 5 && lexer->start[1] == 'o') return check_keyword(lexer, 2, 3, "rma", TK_TY_FORMA);
        if (len == 7 && lexer->start[1] == 'r') return check_keyword(lexer, 2, 5, "actus", TK_TY_F64);
        break;
    case 'i':
        if (len == 7) return check_keyword(lexer, 1, 6, "nteger", TK_TY_I64);
        if (len == 5) return check_keyword(lexer, 1, 4, "mago", TK_KW_IMAGO);
        break;
    case 'l':
        if (len == 5 && lexer->start[1] == 'i') return check_keyword(lexer, 2, 3, "ber", TK_KW_LIBER);
        if (len == 6 && lexer->start[1] == 'o') return check_keyword(lexer, 2, 4, "gica", TK_TY_LOGICA);
        if (len == 7 && lexer->start[1] == 'i') return check_keyword(lexer, 2, 5, "ttera", TK_TY_LITTERA);
        if (len == 5 && lexer->start[1] == 'o') return check_keyword(lexer, 2, 3, "cus", TK_KW_LOCUS);
        if (len == 4 && lexer->start[1] == 'e') return check_keyword(lexer, 2, 2, "ge", TK_KW_LEGE);
        if (len == 3 && lexer->start[1] == 'e') return check_keyword(lexer, 2, 1, "x", TK_KW_LEX);
        break;
    case 'm':
        if (len == 4) return check_keyword(lexer, 1, 3, "uta", TK_KW_MUTA);
        if (len == 6) return check_keyword(lexer, 1, 5, "edius", TK_TY_I32);
        if (len == 7) return check_keyword(lexer, 1, 6, "inimus", TK_TY_I8);
        break;
    case 'n':
        if (len == 4) return check_keyword(lexer, 1, 3, "eca", TK_KW_NECA);
        if (len == 5) return check_keyword(lexer, 1, 4, "ihil", TK_KW_NIHIL);
        break;
    case 'p':
        if (len == 3) return check_keyword(lexer, 1, 2, "er", TK_KW_PER);
        if (len == 5) {
            if (memcmp(lexer->start + 1, "erge", 4) == 0) return TK_KW_PERGE;
            if (memcmp(lexer->start + 1, "urus", 4) == 0) return TK_TY_PURUS;
        }
        break;
    case 'r':
        if (len == 5) {
            if (memcmp(lexer->start + 1, "edde", 4) == 0) return TK_KW_REDDE;
            if (memcmp(lexer->start + 1, "umpe", 4) == 0) return TK_KW_RUMPE;
        }
        if (len == 6) return check_keyword(lexer, 1, 5, "ecede", TK_KW_RECEDE);
        break;
    case 's':
        if (len == 2) return check_keyword(lexer, 1, 1, "i", TK_KW_SI);
        if (len == 3) return check_keyword(lexer, 1, 2, "it", TK_KW_SIT);
        if (len == 4) return check_keyword(lexer, 1, 3, "ali", TK_KW_SALI);
        if (len == 6) return check_keyword(lexer, 1, 5, "cribe", TK_KW_SCRIBE);
        break;
    case 't':
        if (len == 4) return check_keyword(lexer, 1, 3, "ene", TK_KW_TENE);
        if (len == 6) return check_keyword(lexer, 1, 5, "extus", TK_TY_TEXTUS);
        break;
    case 'u':
        if (len == 4) return check_keyword(lexer, 1, 3, "nio", TK_KW_UNIO);
        break;
    case 'v':
        if (len == 5) return check_keyword(lexer, 1, 4, "erum", TK_BOOL_CONST);
        if (len == 4) return check_keyword(lexer, 1, 3, "ade", TK_KW_VADE);
        if (len == 3) return check_keyword(lexer, 1, 2, "ia", TK_TY_VIA);
        break;
    }
    return TK_IDENTIFIER;
}

// ---------------------------------------------------------
// 块状数据切片网
// ---------------------------------------------------------
static Token identifier(Lexer* lexer) {
    while (isalpha(peek(lexer)) || isdigit(peek(lexer)) || peek(lexer) == '_') advance(lexer);
    return make_token(lexer, identifier_type(lexer));
}

// 硬核十六进制、八进制、二进制、罗马基数前缀防线
static Token number(Lexer* lexer) {
    if (lexer->start[0] == '0') {
        char prefix = peek(lexer);

        if (prefix == 'x' || prefix == 'X') {
            advance(lexer);
            while (isxdigit(peek(lexer))) advance(lexer);
            return make_token(lexer, TK_INT_CONST);
        }
        if (prefix == 'b' || prefix == 'B') {
            advance(lexer);
            while (peek(lexer) == '0' || peek(lexer) == '1') advance(lexer);
            return make_token(lexer, TK_INT_CONST);
        }
        if (prefix == 'o' || prefix == 'O') {
            advance(lexer);
            while (peek(lexer) >= '0' && peek(lexer) <= '7') advance(lexer);
            return make_token(lexer, TK_INT_CONST);
        }
        if (prefix == 'r' || prefix == 'R') {
            advance(lexer);
            while (peek(lexer) != '\0' && strchr("IVXLCDMivxlcdm", peek(lexer))) advance(lexer);
            return make_token(lexer, TK_INT_CONST);
        }
    }

    while (isdigit(peek(lexer))) advance(lexer);

    if (peek(lexer) == '.' && isdigit(peek_next(lexer))) {
        advance(lexer);
        while (isdigit(peek(lexer))) advance(lexer);
        return make_token(lexer, TK_FLOAT_CONST);
    }

    return make_token(lexer, TK_INT_CONST);
}

static Token string_const(Lexer* lexer) {
    while (!is_at_end(lexer)) {
        if (peek(lexer) == '"') break;
        if (peek(lexer) == '\n') {
            lexer->line++;
            lexer->column = 0;
        } else if (peek(lexer) == '\\' && peek_next(lexer) != '\0') {
            advance(lexer); // 跳过转义符
        }
        advance(lexer);
    }
    if (!is_at_end(lexer)) advance(lexer);
    return make_token(lexer, TK_STRING_CONST);
}

static Token char_const(Lexer* lexer) {
    while (!is_at_end(lexer)) {
        if (peek(lexer) == '\'') break;
        if (peek(lexer) == '\n') {
            lexer->line++;
            lexer->column = 0;
        } else if (peek(lexer) == '\\' && peek_next(lexer) != '\0') {
            advance(lexer);
        }
        advance(lexer);
    }
    if (!is_at_end(lexer)) advance(lexer);
    return make_token(lexer, TK_CHAR_CONST);
}

// ---------------------------------------------------------
// API 核心
// ---------------------------------------------------------
void lexer_init(Lexer* lexer, const char* source) {
    lexer->start = source;
    lexer->current = source;
    lexer->line = 1;
    lexer->column = 1;
}

Token lexer_next_token(Lexer* lexer) {
    skip_whitespace(lexer);
    lexer->start = lexer->current;

    if (is_at_end(lexer)) return make_token(lexer, TK_EOF);

    char c = advance(lexer);

    if (isalpha(c)) return identifier(lexer);
    if (isdigit(c)) return number(lexer);

    // 深层推导操作符探测网 (Lookahead 最深 3 层)
    switch (c) {
    case '(': return make_token(lexer, TK_LPAREN);
    case ')': return make_token(lexer, TK_RPAREN);
    case '{': return make_token(lexer, TK_LBRACE);
    case '}': return make_token(lexer, TK_RBRACE);
    case '[': return make_token(lexer, TK_LBRACKET);
    case ']': return make_token(lexer, TK_RBRACKET);
    case ';': return make_token(lexer, TK_SEMI);
    case ',': return make_token(lexer, TK_COMMA);
    case ':': return make_token(lexer, TK_COLON);
    case '.': return make_token(lexer, TK_DOT);
    case '~': return make_token(lexer, TK_TILDE);

    case '+': return make_token(lexer, match(lexer, '=') ? TK_PLUS_ASSIGN : TK_PLUS);
    case '*': return make_token(lexer, match(lexer, '=') ? TK_STAR_ASSIGN : TK_STAR);
    case '/': return make_token(lexer, match(lexer, '=') ? TK_SLASH_ASSIGN : TK_SLASH);
    case '%': return make_token(lexer, match(lexer, '=') ? TK_MOD_ASSIGN : TK_MOD);
    case '^': return make_token(lexer, match(lexer, '=') ? TK_CARET_ASSIGN : TK_CARET);

    case '-':
        if (match(lexer, '>')) return make_token(lexer, TK_ARROW);
        if (match(lexer, '=')) return make_token(lexer, TK_MINUS_ASSIGN);
        return make_token(lexer, TK_MINUS);

    case '!':
        return make_token(lexer, match(lexer, '=') ? TK_NEQ : TK_LOGIC_NOT);

    case '=':
        return make_token(lexer, match(lexer, '=') ? TK_EQ : TK_ASSIGN);

    case '&':
        if (match(lexer, '&')) return make_token(lexer, TK_LOGIC_AND);
        if (match(lexer, '=')) return make_token(lexer, TK_AMP_ASSIGN);
        return make_token(lexer, TK_AMP);

    case '|':
        if (match(lexer, '|')) return make_token(lexer, TK_LOGIC_OR);
        if (match(lexer, '=')) return make_token(lexer, TK_PIPE_ASSIGN);
        return make_token(lexer, TK_PIPE);

    case '<':
        if (match(lexer, '=')) return make_token(lexer, TK_LTE);
        if (match(lexer, '<')) return make_token(lexer, match(lexer, '=') ? TK_SHL_ASSIGN : TK_SHL);
        return make_token(lexer, TK_LT);

    case '>':
        if (match(lexer, '=')) return make_token(lexer, TK_GTE);
        if (match(lexer, '>')) return make_token(lexer, match(lexer, '=') ? TK_SHR_ASSIGN : TK_SHR);
        return make_token(lexer, TK_GT);

    case '"': return string_const(lexer);
    case '\'': return char_const(lexer);
    }

    return make_token(lexer, TK_UNKNOWN);
}
