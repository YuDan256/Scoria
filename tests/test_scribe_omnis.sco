liber test_tria;

// ==========================================
// 1. 数组探测 (测试隐形的 Acies 是否修复)
// ==========================================
actio test_acies() -> nihil {
    scribe("--- 1. Acies (Static Array) ---\n");
    
    // 声明一个长度为 4 的定长数组
    sit arr: acies[4] i32;
    arr[0] = muta(i32, 10);
    arr[1] = muta(i32, 20);
    arr[2] = muta(i32, 30);
    arr[3] = muta(i32, 40);
    
    // 期待您的 AST 能够精准铺开，打出 [10, 20, 30, 40]
    scribe("Acies print: ", arr, "\n\n");
}

// ==========================================
// 2. 指针与解引用探测 (测试 Via 格式化)
// ==========================================
actio test_via() -> nihil {
    scribe("--- 2. Via (Pointer & Deref) ---\n");
    
    sit arca: i64 = muta(i64, 777);
    
    // locus 提取变量原生地理位置
    sit ptr: via i64 = locus arca;
    
    scribe("Valore (Value)       : ", arca, "\n");
    
    // 期待打出正确的 16 进制内存地址 (之前是 h0000000 报错)
    scribe("Via (Raw Address)    : ", ptr, "\n");
    
    // tene 强制沿着地址读取物理内存
    scribe("Tene (Dereferenced)  : ", tene ptr, "\n\n");
}

// ==========================================
// 3. 联合体物理层透视 (终极测试 Unio 直接打印)
// ==========================================
unio Metamorphosis {
    sit i: i64;
    sit f: f64;
}

actio test_unio() -> nihil {
    scribe("--- 3. Unio (Memory Aliasing) ---\n");
    
    sit u: Metamorphosis;
    
    // 技巧：十进制 4611686018427387904 
    // 在 64 位下对应十六进制 0x4000000000000000
    // 这恰好是 IEEE-754 双精度浮点数里完美的 2.0！
    u.i = muta(i64, 4611686018427387904);
    
    scribe("Scripsi (Written) : u.i = 4611686018427387904\n");
    
    // 💥 见证奇迹的时刻！直接打印整个联合体！
    // 期待看到类似: Metamorphosis { i: 4611686018427387904, f: 2.000000 }
    scribe("Unio Omnis (Dump) : ", u, "\n\n");
}

// ==========================================
// 主函数
// ==========================================
actio princeps() -> i32 {
    scribe("===== INITIATIO TEST TRIA =====\n\n");
    
    test_acies();
    test_via();
    test_unio();
    
    scribe("===== PERFECTUM =====\n");
    redde muta(i32, 0);
}