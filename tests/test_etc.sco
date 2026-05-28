// 轨道 A: FFI 无类型变长参数 (调用 C 标准库)
actio barbara("msvcrt.dll") printf(fmt: via littera, etc) -> i32;

// 轨道 B: 原生同构变长参数 (底层自动降级为 cohors i32 切片)
actio summa(n: i32, etc numeri: i32) -> i32 {
    sit sum: i32 = n;
    per (sit i: i64 = 0; i < numeri.longitudo; i += 1) {
        sum += numeri[i];
    }
    redde sum;
}

actio princeps() -> i32 {
    scribe("--- Test Native Variadic ---\n");
    sit res: i32 = summa(100, 1, 2, 3, 4, 5);
    scribe("Summa (100 + 1+2+3+4+5) = ", res, "\n\n");

    scribe("--- Test FFI Variadic ---\n");
    sit f: f32 = 3.14159;
    // 注意：C 语言的 printf 需要裸指针 (via littera)，所以我们用 .caput 提取字符串切片的头部指针
    // 并且必须手动在末尾加上 \0 以符合 C 语言的字符串规范
    printf("printf: integer=%d, float=%f, string=%s\n\0".caput, 42, f, "Salve Mundus!\0".caput);

    // Exspectatus exitus (Expected output):
    // --- Test Native Variadic ---
    // Summa (100 + 1+2+3+4+5) = 115
    // 
    // --- Test FFI Variadic ---
    // printf: integer=42, float=3.141590, string=Salve Mundus!

    redde 0;
}
