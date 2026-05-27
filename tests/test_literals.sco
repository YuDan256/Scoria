actio princeps() -> i32 {
    // 1. 测试字面量自适应推导与多种进制解析
    // 这里的字面量会根据左侧的期待类型自动推导，并在编译期进行值域校验
    sit a: i8 = 120;          // 十进制
    sit b: p16 = 0xFFFF;      // 十六进制 (65535)
    sit c: i32 = 0b101010;    // 二进制 (42)
    sit d: i64 = 0o777;       // 八进制 (511)
    sit e: i32 = 0rXIV;       // 罗马数字 (14)

    scribe("--- Numeri ---\n");
    scribe("a (i8, dec): ", a, "\n");
    scribe("b (p16, hex): ", b, "\n");
    scribe("c (i32, bin): ", c, "\n");
    scribe("d (i64, oct): ", d, "\n");
    scribe("e (i32, rom): ", e, "\n\n");

    // 2. 测试 C 语言风格的转义字符
    sit str_normal: textus = "Linea prima\nLinea secunda\tTabulata\n";
    sit str_hex: textus = "Hex: \x4A\x4B\x4C\n"; // JKL
    sit str_oct: textus = "Oct: \115\116\117\n"; // MNO
    sit str_ctrl: textus = "Sonus\a, Recessus\b, \?, \\, \', \"\n";

    sit ch_hex: littera = '\x41'; // A
    sit ch_oct: littera = '\102'; // B
    sit ch_nl: littera = '\n';

    scribe("--- Textus et Litterae ---\n");
    scribe(str_normal);
    scribe(str_hex);
    scribe(str_oct);
    scribe(str_ctrl);
    scribe("Litterae: ", ch_hex, ch_oct, ch_nl);

    redde 0;
}
