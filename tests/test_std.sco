liber test_std;

consule liber fasc;
consule liber systema;

actio edita princeps() -> i32 {
    sit fasc_nomen: textus = "test_output.txt";
    sit contentus: textus = "Salve, Scoria!\nHaec est probatio tabularii.\n";

    // 1. 测试写入文件
    scribe("=== Probatio: scribe_fasc ===\n");
    sit bytes_written: i64 = fasc.scribe_fasc(fasc_nomen, contentus);
    si (bytes_written > 0) {
        scribe("Scribe successit. Bytes scripti: ", bytes_written, "\n");
    } aliter {
        scribe("Scribe defecit.\n");
        redde 1;
    }

    // 2. 测试读取文件
    scribe("\n=== Probatio: lege_fasc ===\n");
    sit read_content: cohors littera = fasc.lege_fasc(fasc_nomen);
    si (read_content.longitudo > 0) {
        scribe("Lege successit. Longitudo: ", read_content.longitudo, "\n");
        
        // 释放由 lege_fasc 分配的内存
        neca(read_content.caput);
    } aliter {
        scribe("Lege defecit.\n");
        redde 1;
    }

    // 3. 测试执行系统命令
    scribe("\n=== Probatio: exequere ===\n");
    // 调用 cmd 打印信息
    sit exit_code1: i32 = systema.exequere("cmd.exe /c echo Hello from systema!");
    scribe("Echo exit code: ", exit_code1, "\n");

    // 调用 cmd 删除刚刚创建的测试文件
    sit exit_code2: i32 = systema.exequere("cmd.exe /c del test_output.txt");
    scribe("Del exit code: ", exit_code2, "\n");

    scribe("\nOmnia successerunt!\n");
    redde 0;
}
