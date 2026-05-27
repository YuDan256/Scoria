forma Punctum {
    sit x: i32;
    sit y: i32;
}

actio princeps() -> i32 {
    // 1. 完整的经典写法
    sit p1: Punctum = Punctum { x: 1, y: 2 };

    // 2. 省略左侧类型 (依赖右侧显式类型)
    sit p2 = Punctum { x: 10, y: 20 };

    // 3. 省略右侧类型 (依赖左侧期待类型，匿名结构体)
    sit p3: Punctum = { x: 100, y: 200 };

    scribe("p1: ", p1.x, ", ", p1.y, "\n");
    scribe("p2: ", p2.x, ", ", p2.y, "\n");
    scribe("p3: ", p3.x, ", ", p3.y, "\n");

    redde 0;
}