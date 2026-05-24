liber omnis_test;

// 1. 外部函数 (FFI) 测试
actio barbara("msvcrt.dll") puts(str: via littera) -> medius;

// 2. 常量与字面量测试 (支持 16进制, 2进制, 8进制, 罗马数字)
lex MAX_VAL: medius = 0rC;       // 罗马数字 100
lex HEX_VAL: medius = 0xFF;      // 16进制 255
lex BIN_VAL: medius = 0b1010;    // 2进制 10
lex OCT_VAL: medius = 0o77;      // 8进制 63

// 3. 结构体与联合体测试
forma Punctum {
    sit x: medius;
    sit y: medius;
}

forma densa PunctumDensum {
    sit a: p8;
    sit b: integer;
}

unio Numerus {
    sit i: integer;
    sit f: fractus;
}

// 4. 控制流测试 (si, elige, dum, per, sali)
actio test_fluxus() -> nihil {
    sit x: medius = 10;
    
    // 条件分支
    si (x > 5) {
        scribe("x est maior quam 5\n");
    } aliter si (x == 5) {
        scribe("x est 5\n");
    } aliter {
        scribe("x est minor quam 5\n");
    }

    // 多路分支 (Switch)
    elige (x) {
        casus 1, 2, 3:
            scribe("x est 1, 2, vel 3\n");
        casus 10:
            scribe("x est 10 (Victoria!)\n");
        aliter:
            scribe("x est ignotus\n");
    }

    // dum 循环与 rumpe/perge
    sit i: medius = 0;
    dum (i < 5) {
        si (i == 2) {
            i += 1;
            perge; // continue
        }
        si (i == 4) {
            rumpe; // break
        }
        i += 1;
    }

    // per 循环
    per (sit j: medius = 0; j < 3; j += 1) {
        scribe("per cyclus: ", j, "\n");
    }

    // 无条件跳转 (Goto)
    sali finis;
    scribe("Hoc numquam scribetur!\n");
finis:
    scribe("Sali successit!\n");
}

// 5. 内存与指针测试
actio test_memoria() -> nihil {
    // 动态分配
    sit ptr: via medius = crea(medius, 5);
    si (ptr == nhl) {
        scribe("Allocatio defecit!\n");
        redde;
    }
    
    // 解引用写入
    tene ptr = 42;
    
    // 指针偏移
    sit p2: via medius = vade(ptr, 1);
    tene p2 = 100;
    
    // 取址与强制类型转换
    sit val: medius = tene ptr;
    sit p_val: via medius = locus val;
    sit p_byte: via p8 = muta(via p8, p_val);
    
    // 释放内存
    neca(ptr);
    scribe("Memoria liberata est.\n");
}

// 6. 数据类型测试
actio test_typi() -> nihil {
    sit a: i8 = muta(i8, -1);
    sit b: p32 = muta(p32, 0xFFFFFFFF);
    sit c: logica = verum;
    sit d: littera = 'A';
    sit e: textus = "Salve, Mundus!";
    
    // 数组
    sit arr: acies[3] medius;
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    
    scribe("Typi testati sunt.\n");
}

// 主函数
actio princeps() -> medius {
    scribe("--- Incipio Test Omnis ---\n");
    
    test_fluxus();
    test_memoria();
    test_typi();
    
    scribe("--- Omnia Successerunt! ---\n");
    redde 0;
}
