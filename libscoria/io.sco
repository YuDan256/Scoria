=======
liber io;

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

actio barbara("kernel32.dll") CloseHandle(
    hObject: HANDLE
) -> BOOL;

// ================= [ Windows 常量 ] =================
lex GENERIC_READ: DWORD = 0x80000000;
lex FILE_SHARE_READ: DWORD = 1;
lex OPEN_EXISTING: DWORD = 3;
lex FILE_ATTRIBUTE_NORMAL: DWORD = 0x80;

// ================= [ Scoria 封装 ] =================

// 读取整个文件，返回一个包含文件内容的切片 (cohors littera)
// 注意：调用者在使用完毕后，需要负责调用 neca(返回的切片.caput) 释放内存
actio edita lege_fasciculum(iter: textus) -> cohors littera {
    sit invalid_handle: HANDLE = muta(HANDLE, -1);

    // 1. 打开文件 (textus 底层是切片，使用 .caput 获取裸指针)
    sit hFile: HANDLE = CreateFileA(
        iter.caput,
        GENERIC_READ,
        FILE_SHARE_READ,
        muta(via nihil, 0),
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        muta(via nihil, 0)
    );

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

    // 3. 分配内存 (多分配 1 字节用于 '\0' 结尾，方便后续与 C ABI 交互)
    sit quiddam: via littera = crea(littera, muta(i32, magnitudo + 1));
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

    // 5. 设置字符串结尾 '\0'
    sit end_ptr: via littera = vade(quiddam, muta(i32, magnitudo));
    tene end_ptr = muta(littera, 0);

    // 6. 返回胖指针切片
    redde cohors littera { caput: quiddam, longitudo: magnitudo };
}