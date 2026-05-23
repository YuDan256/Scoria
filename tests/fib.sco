actio fib(n: medius) -> medius {
    si (n <= 1) redde n;
    redde fib(n - 1) + fib(n - 2);
}

actio princeps() -> medius {
    scribe("Calculando Fib(40)... Exspecta!\n");
    sit res: medius = fib(40);
    scribe("Resultatum: ", res, "\n");
    redde 0;
}