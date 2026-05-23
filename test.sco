// 经典的递归斐波那契，极其考察函数调用的底层开销
actio fib(n: medius) -> medius {
    si (n <= 1) {
        redde n;
    }
    redde fib(n - 1) + fib(n - 2);
}

actio princeps() -> medius {
    scribe("Calculando Fib(40)... Exspecta!\n");
    
    // 计算 Fib(40)，这将触发上亿次的函数 Call 和栈帧分配！
    sit res: medius = fib(40);
    
    scribe("Resultatum: ", res, "\n");
    redde 0;
}