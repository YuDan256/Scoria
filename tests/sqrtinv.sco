actio Q_rsqrt(numerus: f32) -> f32 {
    sit threehalfs: f32 = 1.5;
    sit x2: f32 = numerus * 0.5;
    sit y: f32 = numerus;
    sit i: p32 = 0; 
    i = tene muta(via p32, locus y);
    i = 0x5F3759DF - (i >> 1);
    y = tene muta(via f32, locus i);
    y = y * (threehalfs - (x2 * y * y));
    redde y;
}

actio princeps() -> i32 {
    scribe("=== Probatio: Q_rsqrt ===\n");
    sit f: f32;
    scribe("Da mihi numerum fractum f, quaeso: ");
    lege(f);
    sit res: f32 = Q_rsqrt(f);
    scribe("Q_rsqrt(f) = ", res, "\n");
    redde 0;
}