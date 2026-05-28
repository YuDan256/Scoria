ordo TokenKind {
    TK_EOF,
    TK_IDENTIFIER,
    TK_INT_CONST = 10,
    TK_PLUS
}

actio princeps() -> i32 {
    sit a: TokenKind = TokenKind.TK_EOF;
    sit b: TokenKind = TokenKind.TK_PLUS;

    scribe("TK_EOF = ", a, "\n");
    scribe("TK_PLUS = ", b, "\n");

    redde 0;
}