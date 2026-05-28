liber test_data_section;

// 引入外部 C 标准库函数用于打印测试结果
actio barbara("msvcrt.dll") printf(fmt: via littera, etc) -> i32;

// ==========================================
// 以下全局变量/常量应当在编译期被求值并直接刻入 .data 段
// ==========================================

lex global_int: i32 = -42;
lex global_float: f64 = 3.14159;
lex global_bool: logica = verum;

forma Point {
    sit x: i32;
    sit y: i32;
}

lex global_struct: Point = Point { x: 10, y: 20 };
lex global_array: acies[3] i32 = [100, 200, 300];

actio edita princeps() -> i32 {
    printf("=== Test .data section ===\n\0".caput);

    printf("global_int: %d\n\0".caput, global_int);
    printf("global_float: %f\n\0".caput, global_float);

    si (global_bool) {
        printf("global_bool: verum\n\0".caput);
    } aliter {
        printf("global_bool: falsum\n\0".caput);
    }

    printf("global_struct: x=%d, y=%d\n\0".caput, global_struct.x, global_struct.y);
    printf("global_array: [%d, %d, %d]\n\0".caput, global_array[0], global_array[1], global_array[2]);

    redde 0;
}
