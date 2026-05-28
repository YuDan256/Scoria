actio princeps() -> i32 {
    scribe("--- Test lege (Input Macro) ---\n");

    scribe("Scribe unum numerum integrum (i32): ");
    sit a: i32 = 0;
    lege(a);
    scribe("Legisti: ", a, "\n\n");

    scribe("Scribe unum numerum fluitantem (f64): ");
    sit b: f64 = 0.0;
    lege(b);
    scribe("Legisti: ", b, "\n\n");

    scribe("Scribe unam litteram (char): ");
    sit c: littera = ' ';
    lege(c);
    scribe("Legisti: ", c, "\n\n");

    scribe("Scribe unam logicam (1/0 vel v/f): ");
    sit d: logica = falsum;
    lege(d);
    scribe("Legisti: ", d, "\n\n");

    scribe("--- Test finis ---\n");
    redde 0;
}
