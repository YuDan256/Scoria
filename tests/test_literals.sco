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

    p3 = { x: 999, y: 888 };
    scribe("p3 (post assignationem): ", p3.x, ", ", p3.y, "\n");

    // 1. 基础类型的赋值推导
    sit a: i8 = 10;
    a = 127; // 正数最大为 127
    sit b: i8 = -128; // 负数最小为 -128，均能完美通过校验

    // 2. 数组的赋值推导
    sit arr: acies[3] i8 = [1, 2, 3];
    arr = [10, 20, 30]; // 数组字面量及其内部元素都会根据 arr 的类型自动推导为 acies[3] i8 和 i8

    scribe("a: ", a, "\n");
    scribe("b: ", b, "\n");
    scribe("arr: ", arr[0], ", ", arr[1], ", ", arr[2], "\n");

    redde 0;
}
