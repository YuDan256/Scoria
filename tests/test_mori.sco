liber test_mori;

// 测试 1: mori 满足返回路径检查
// 这个函数声明返回 i32，但在 aliter 分支中没有 redde，只有 mori。
// 如果编译器没有报错，说明返回路径分析正确识别了 mori。
actio test_return_path(x: i32) -> i32 {
    si (x > 0) {
        redde x * 2;
    } aliter {
        scribe("  [!] Ingressus in viam mortis...\n");
        mori; // 触发硬件陷阱，免除 redde 义务
    }
}

actio edita princeps() -> i32 {
    scribe("=== Test mori (Trap) ===\n");
    
    scribe("Test 1: x = 10\n");
    sit res: i32 = test_return_path(10);
    scribe("  Res: ", res, "\n");

    scribe("Test 2: x = -5 (Expect Trap / Crash)\n");
    test_return_path(-5);

    scribe("Hoc non debet imprimi.\n"); // 这行绝对不应该被打印
    redde 0;
}
