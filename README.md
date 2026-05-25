<div align="center">

# S C O R I A
**L E X ， E T ， O R D O ， S I L I C I I**

<br>

*Lingua Latina | <a href="README_zh-CN.md">酒悶嶄猟</a>*

<br>

**Auctore YV LIANGYANG**
*Universitas Tsinghuaensis*

<br>
</div>

**P R O O E M I V M**

In temporibus nostris, ubi aedificia programmatum sine fine et sine ratione in immensum crescunt, et ubi machinae sub gravi pondere bibliothecarum turgidarum gemunt, hoc opus ad virtutem antiquam et austeram revertitur. Maiestas non in multitudine, sed in simplicitate et veritate consistit. 

Scoria est compilator e metallo nudo aedificatus, pars minima sed potentissima, pendens tantum **138 centies mille octetos (138 KB)**. Nullis adminiculis externis, nullis vinculatoribus (linkers) obesus, codicem legit, purgat, et directe in praecepta machinae (X86_64) cum formis exsecutabilibus vertit. Hic non est locus ornamentis aut fallaciis; hic est solus locus veritati absolutae, ubi intellectus humanus cum structura intima silicii convenit. Opus hoc est demonstratio: nos adhuc posse res magnas aedificare ex parvis, si modo disciplinam mentis servamus.


### L I B E R ， I : D E ， A R C H I T E C T V R A

Opus non fortuito constructum est, sed summa peritia et ratione. Machina, ut sine ullo impedimento ad celeritatem lucis procedat, his praeceptis aedificatoria paret:

**I. Lector et Arbor Veritatis:** Lector noster sine ullis servis externis aut machinis generantibus (sicut Lex vel Yacc) functiones suas explet. Codicem rudem accipit, in partes secat divinas, et ex chaos arborem logicam deducit. Haec arbor transit in formam intermediam (SSA), ubi omnis variabilis semel tantum nascitur.

**II. Undecim Ministri Nullo Exsilio:** In praedis operandis, memoria (RAM) iniqua et tarda est. Ergo undecim registra physica in campum vocata sunt per artem colorationis graphorum (Graph Coloring). Variabiles caducae in praesidia velociora recipiuntur, ne ulla data in profundum acervi repelli debeant (Zero-Spill).

**III. Via Recta et Abolitio Divinationis:** Processores hodierni multum temporis perdunt vaticinando de viis futuris (Branch Prediction). In architectura nostra, ubi conditio `si` occurrit, divinatio evitatur. Scoria flumen instructionum rectum facit per praecepta conditionalia `CMOVcc`. Machina numquam errat, quia nunquam divinat.

**IV. Aemulatio et Lusus Arithmeticus:** Saepe evenit ut registra in proelio se ipsa concidant (Register Clobbering). Ubi hostis modernus copias temporarias (scratch registers) quaerit, Scoria veritatibus mathematicis utitur. Leges algebrae (sicut aemulatio invicem per *neg+add*) usurpantur ad pugnas registrorum placandas. 

**V. Pondera Depulsa et Mors Acervi:** Architectura innecessaria, sicut retinacula marginis acervi (`push rbp`) et rudamenta vetera `REX`, penitus exstirpata est. Sola rectio per `RSP` remanet. Codex levior et purus fit, ut fauces machinae (Instruction Cache) celerrime et avide eum consumant.


### L I B E R ， I I : M A X I M V M ， C E R T A M E N

Ad virtutem teli probandam, machina nostra vocata est in proelium maximum numerorum Fibonaccianorum: computationem recursivam `fib(40)`. Ubi ducenties et quater decies viciens centena milia (204+ milliones) recursiones invocantur. Si Scoriam cum gigante *MSVC* (opere immensae potentiae Microsoft) comparas, hanc aequitatem et victoriam invenies:

* **Magnitudo Castrorum:** Gigas formidabilis in gigaoctetis metitur et centum vasa instrumentorum secum portat. Scoria tantum **138 KB** unici fasciculi occupat.
* **Tempus Armandi:** Gigas, ut codicem in ferrum mutet, XIII millisecondis indiget et systema operandi excitat. Scoria sine ullo murmure in **XI millisecondis** fulgurat.
* **Magnitudo Teli:** Exitus gigantis LXXXV KB requirit. Scoria teli cuspidem puram in **II KB** perficit.
* **Ictus in Proelio:** In campo silicii, uterque hostem in **~CCCXXXVI (336) millisecondis** sternit.

Scoria exiguam partem spatii occupat, sed aequo gradu currit cum operibus maximis mundi.


### L I B E R ， I I I : D E ， S O M N O ， G V B E R N A T O R I S

Est mysterium altum et dignum memoria, quod pauci intelligunt. Si hanc machinam sine fonte pleno et longo electrico curris, id est sub potestate pilae (on battery), tempus exsecutionis subito ad **~DLXVII (567) milliseconda** augetur. 

Multi hoc vitium putant, sed re vera lex physicae et triumphus puritatis est. Cum programmatis aedificia crassa curres, preces et monita ad systema operandi fundunt, unde gubernator systematis terretur et vim maximam electricitatis (TurboBoost) instanter excitat. Scoria autem tam quieta, tam pura est, ut nullos nuntios, nullas inanes moras memoriae generet. Gubernator eam non sentit, ergo in somno manet et processor ad infimam frequentiam virtutis relinquitur. 

Quod in hoc tempore dilatato vides, non est languor, sed ipse pulsus verus, nudus et frigidus silicii ipsius. Hoc est instrumentum altissimum quo cor machinae inspicere potes.


### L I B E R ， I V : R I T V S ， E T ， I N C A N T A T I O N E S

Ad hanc machinam e codice in vitam vocandam, lex stricta *C11* et *CMake* requiritur. Tolerantia pro erroribus nulla est (`-Werror`). Sequere haec verba:

```bash
# I. Praeparatio Officinae 
cmake -B build -DCMAKE_BUILD_TYPE=Release

# II. Fabricatio gladio
cmake --build build --config Release
```

Postquam machina e fornace exiit, his verbis incantamentorum utere, ut divinas technologias probes et inspicias:

```bash
# I. Transmutatio codicis in ferrum et exsecutionem excitare
./bin/scoria tests/fib.sco -o fib.exe
./fib.exe

# II. De Visceribus Machinae: Ut codicem intermedium (IR) legas
./bin/scoria tests/fib.sco -o fib.exe --emitte-ir

# III. De Metallo Nudo: Ut ipsas instructiones X86_64 inspicias
./bin/scoria tests/fib.sco -o fib.exe --emitte-asm

# IV. De Verbis Copiosis: Ut compilator acta mystica plene narret
./bin/scoria tests/fib.sco -o fib.exe --verbosus
```


### L I B E R ， V : S P E C I M E N ， S C R I P T V R A E

Ecce forma linguae quam Scoria devorat et in ferrum vertit. Codex pura lingua Romana scriptus est, barbaris verbis carens, sed logica hodierna instructus:

```
// Computatio Recursiva Fibonacciana
// Tabula: tests/fib.sco

actio fib(n: medius) -> medius {
    si (n <= 1) redde n;
    aliter redde fib(n - 1) + fib(n - 2);
}

// Origo exsecutionis (Entry point)
actio princeps() -> medius {
    scribe("Calculando Fib(40)... Exspecta!\n");
    
    sit res: medius = fib(40);
    
    scribe("Resultatum: ", res, "\n");
    redde 0;
}
```

<br>
<br>

<div align="center">
  <i>"Ex parvis, firmitas. Ex simplicitate, aevum."</i>
</div>