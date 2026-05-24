liber main_multi;

// 测试多文件引入：精确摘录 (xcp = excerpe)
de math_lib xcp Vector, add_vec;

// 测试 FFI 调用：声明 msvcrt.dll 中的 C 标准库函数 (bbr = barbara, ltr = littera)
act bbr("msvcrt.dll") puts(str: via ltr) -> medius;
act bbr("msvcrt.dll") strlen(str: via ltr) -> integer;

act princeps() -> medius {
    // 1. 调用 FFI 函数
    // 直接使用 .locus 提取胖指针的裸游标，完美兼容 C ABI
    sit msg1: textus = "--- Test FFI & Multi-file ---\0";
    puts(msg1.locus);
    
    sit msg2: textus = "Scoria\0";
    sit len: integer = strlen(msg2.locus);
    scb("Longitudo verbi 'Scoria' est: ", len, "\n");
    
    // 也可以直接获取胖指针的长度
    scb("Longitudo ex textus: ", msg2.longitudo, "\n");

    // 2. 使用外部模块的结构体和函数
    sit v1: Vector;
    v1.x = 10;
    v1.y = 20;

    sit v2: Vector;
    v2.x = 5;
    v2.y = 15;

    sit v3: Vector;
    // locus 取地址
    add_vec(locus v1, locus v2, locus v3);

    scb("Vector Resultatum: x=", v3.x, ", y=", v3.y, "\n");

    sit msg3: textus = "--- Successus! ---\0";
    puts(msg3.locus);

    rdd 0; // rdd = redde
}
