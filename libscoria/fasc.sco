=======
liber fasc;

// ================= [ Windows API 类型映射 ] =================
imago HANDLE = via nihil;
imago DWORD = p32;
imago BOOL = i32;

// ================= [ 蛮族接口 (FFI) 声明 ] =================
actio barbara("kernel32.dll") CreateFileA(
    lpFileName: via littera,
    dwDesiredAccess: DWORD,
    dwShareMode: DWORD,
    lpSecurityAttributes: via nihil,
    dwCreationDisposition: DWORD,
    dwFlagsAndAttributes: DWORD,
    hTemplateFile: HANDLE
) -> HANDLE;

actio barbara("kernel32.dll") GetFileSizeEx(
    hFile: HANDLE,
    lpFileSize: via i64
) -> BOOL;

actio barbara("kernel32.dll") ReadFile(
    hFile: HANDLE,
    lpBuffer: via nihil,
    nNumberOfBytesToRead: DWORD,
    lpNumberOfBytesRead: via DWORD,
    lpOverlapped: via nihil
) -> BOOL;

actio barbara("kernel32.dll") WriteFile(
    hFile: HANDLE,
    lpBuffer: via nihil,
    nNumberOfBytesToWrite: DWORD,
    lpNumberOfBytesWritten: via DWORD,
    lpOverlapped: via nihil
) -> BOOL;

actio barbara("kernel32.dll") CloseHandle(
    hObject: HANDLE
) -> BOOL;

// ================= [ Windows 常量 ] =================
lex GENERIC_READ: DWORD = 0x80000000;
lex GENERIC_WRITE: DWORD = 0x40000000;
lex FILE_SHARE_READ: DWORD = 1;
lex OPEN_EXISTING: DWORD = 3;
lex CREATE_ALWAYS: DWORD = 2;
lex FILE_ATTRIBUTE_NORMAL: DWORD = 0x80;

// ================= [ Scoria 封装 ] =================

// 读取整个文件，返回一个包含文件内容的切片 (cohors littera)
// 注意：调用者在使用完毕后，需要负责调用 neca(返回的切片.caput) 释放内存
actio edita lege_fasc(iter: textus) -> cohors littera {
    sit invalid_handle: HANDLE = muta(HANDLE, -1);

    // 1. 转换文件名为 C 风格字符串 (追加 \0)
    sit c_filename: via littera = crea(littera, muta(i32, iter.longitudo + 1));
    per (sit i: i64 = 0; i < iter.longitudo; i += 1) {
        tene vade(c_filename, muta(i32, i)) = tene vade(iter.caput, muta(i32, i));
    }
    tene vade(c_filename, muta(i32, iter.longitudo)) = muta(littera, 0);

    // 2. 打开文件
    sit hFile: HANDLE = CreateFileA(
        c_filename,
        GENERIC_READ,
        FILE_SHARE_READ,
        muta(via nihil, 0),
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        muta(via nihil, 0)
    );

    neca(c_filename); // 用完立即释放临时文件名内存

    si (hFile == invalid_handle) {
        redde cohors littera { caput: muta(via littera, 0), longitudo: 0 };
    }

    // 2. 获取文件大小
    sit magnitudo: i64 = 0;
    sit res_size: BOOL = GetFileSizeEx(hFile, locus magnitudo);
    si (res_size == 0) {
        CloseHandle(hFile);
        redde cohors littera { caput: muta(via littera, 0), longitudo: 0 };
    }

    // 3. 分配内存 (严格按照文件大小分配，不追加 \0)
    sit quiddam: via littera = crea(littera, muta(i32, magnitudo));
    si (quiddam == muta(via littera, 0)) {
        CloseHandle(hFile);
        redde cohors littera { caput: muta(via littera, 0), longitudo: 0 };
    }

    // 4. 读取文件内容
    sit bytes_read: DWORD = 0;
    sit res_read: BOOL = ReadFile(
        hFile,
        muta(via nihil, quiddam),
        muta(DWORD, magnitudo),
        locus bytes_read,
        muta(via nihil, 0)
    );

    CloseHandle(hFile);

    si (res_read == 0) {
        neca(quiddam);
        redde cohors littera { caput: muta(via littera, 0), longitudo: 0 };
    }

    // 5. 返回胖指针切片
    redde cohors littera { caput: quiddam, longitudo: magnitudo };
}

// 将切片内容写入文件。如果文件存在则覆盖，不存在则创建。
// 返回写入的字节数，如果失败返回 -1。
actio edita scribe_fasc(iter: textus, content: cohors littera) -> i64 {
    sit invalid_handle: HANDLE = muta(HANDLE, -1);

    // 1. 转换文件名为 C 风格字符串 (追加 \0)
    sit c_filename: via littera = crea(littera, muta(i32, iter.longitudo + 1));
    per (sit i: i64 = 0; i < iter.longitudo; i += 1) {
        tene vade(c_filename, muta(i32, i)) = tene vade(iter.caput, muta(i32, i));
    }
    tene vade(c_filename, muta(i32, iter.longitudo)) = muta(littera, 0);

    // 2. 打开/创建文件 (独占写入，不共享)
    sit hFile: HANDLE = CreateFileA(
        c_filename,
        GENERIC_WRITE,
        0, 
        muta(via nihil, 0),
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        muta(via nihil, 0)
    );

    neca(c_filename); // 用完立即释放临时文件名内存

    si (hFile == invalid_handle) {
        redde -1;
    }

    // 3. 写入文件内容
    sit bytes_written: DWORD = 0;
    sit res_write: BOOL = WriteFile(
        hFile,
        muta(via nihil, content.caput),
        muta(DWORD, content.longitudo),
        locus bytes_written,
        muta(via nihil, 0)
    );

    CloseHandle(hFile);

    si (res_write == 0) {
        redde -1;
    }

    redde muta(i64, bytes_written);
}
