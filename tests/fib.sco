actio fib(n: medius) -> medius {
    si (n <= 1) redde n;
    aliter redde fib(n - 1) + fib(n - 2);
}

actio princeps() -> medius {
    sit n: medius;
    scribe("Da mihi integrum n, quaeso! \n");
    lege(n);
    scribe("Calculando Fib(n)... Exspecta!\n");
    sit res: medius = fib(n);
    scribe("Resultatum: ", res, "\n");
    redde 0;
}
