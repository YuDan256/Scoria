liber systema;

// ================= [ Windows API 类型映射 ] =================
imago HANDLE = via nihil;
imago DWORD = p32;
imago BOOL = i32;

// ================= [ 蛮族接口 (FFI) 声明 ] =================
actio barbara("kernel32.dll") CreateProcessA(
    lpApplicationName: via nihil,
    lpCommandLine: via littera,
    lpProcessAttributes: via nihil,
    lpThreadAttributes: via nihil,
    bInheritHandles: BOOL,
    dwCreationFlags: DWORD,
    lpEnvironment: via nihil,
    lpCurrentDirectory: via nihil,
    lpStartupInfo: via littera,
    lpProcessInformation: via littera
) -> BOOL;

actio barbara("kernel32.dll") WaitForSingleObject(
    hHandle: HANDLE,
    dwMilliseconds: DWORD
) -> DWORD;

actio barbara("kernel32.dll") GetExitCodeProcess(
    hProcess: HANDLE,
    lpExitCode: via DWORD
) -> BOOL;

actio barbara("kernel32.dll") CloseHandle(
    hObject: HANDLE
) -> BOOL;

// ================= [ Windows 常量 ] =================
lex INFINITE: DWORD = 0xFFFFFFFF;

// ================= [ Scoria 封装 ] =================

// 执行系统命令 (例如调用汇编器将 .asm 编译为 .exe)
// 返回命令的退出状态码
actio edita exequere(cmd: textus) -> i32 {
    // 1. 转换命令为 C 风格字符串 (追加 \0)
    // CreateProcessA 要求命令行字符串必须是可写的，crea 分配在堆上正好满足要求
    sit c_cmd: via littera = crea(littera, muta(i32, cmd.longitudo + 1));
    per (sit i: i64 = 0; i < cmd.longitudo; i += 1) {
        tene vade(c_cmd, muta(i32, i)) = tene vade(cmd.caput, muta(i32, i));
    }
    tene vade(c_cmd, muta(i32, cmd.longitudo)) = muta(littera, 0);

    // 2. 准备 CreateProcessA 的参数 (STARTUPINFOA 和 PROCESS_INFORMATION)
    // 64位下 STARTUPINFOA 大小为 104 字节
    sit s_info: via littera = crea(littera, muta(i32, 104));
    per (sit i: i64 = 0; i < 104; i += 1) {
        tene vade(s_info, muta(i32, i)) = muta(littera, 0);
    }
    tene muta(via DWORD, s_info) = 104; // s_info.cb = sizeof(STARTUPINFOA)

    // 64位下 PROCESS_INFORMATION 大小为 24 字节
    sit pi: via littera = crea(littera, muta(i32, 24));

    // 3. 调用 CreateProcessA
    sit res: BOOL = CreateProcessA(
        muta(via nihil, 0),
        c_cmd,
        muta(via nihil, 0),
        muta(via nihil, 0),
        0,
        0,
        muta(via nihil, 0),
        muta(via nihil, 0),
        s_info,
        pi
    );

    sit exit_code: DWORD = 0;

    si (res != 0) {
        // 获取 pi.hProcess (偏移 0) 和 pi.hThread (偏移 8)
        sit hProcess: HANDLE = tene muta(via HANDLE, pi);
        sit hThread: HANDLE = tene muta(via HANDLE, vade(pi, muta(i32, 8)));

        // 等待进程结束
        WaitForSingleObject(tene hProcess, INFINITE);

        // 获取退出码
        GetExitCodeProcess(tene hProcess, locus exit_code);

        // 关闭句柄，防止句柄泄漏
        CloseHandle(tene hProcess);
        CloseHandle(tene hThread);
    } aliter {
        exit_code = muta(DWORD, -1);
    }

    // 释放内存
    neca(c_cmd);
    neca(s_info);
    neca(pi);

    redde muta(i32, exit_code);
}
