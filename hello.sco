actio princeps() -> i32 {
    // 分配 10 个 i32 的内存
    sit ptr: via i32 = crea(i32, 10);

    // 指针偏移 5 个元素
    sit p2: via i32 = vade(ptr, 5);

    // 解引用并赋值 (正确写法)
    tene p2 = 42;
    scribe(tene p2);
    // 释放内存
    neca(ptr);

    redde 0;
}