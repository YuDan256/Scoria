liber main;

lex edt MAX_MILITUM: medius = 100; 

actio princeps() -> medius {

    sit via_ad_castra: via p8 = crea(p8, muta(integer, MAX_MILITUM));
    
    // 修正1：严谨的指针相等比较，将虚无转型为 p8 指针
    si (via_ad_castra == muta(via p8, nhl)) {
        scribe("Ruina: Allocatio memoriae defecit!");
        redde -1; 
    }
    
    // 修正2：严谨的初始化赋值，字面量默认是 medius，必须强制转型为 p32
    sit i: p32 = muta(p32, 0);
    
    dum (i < muta(p32, MAX_MILITUM)) {
        // 修正3：向 p8 (1字节) 写入时，明确将内容截断为 p8
        tene via_ad_castra = muta(p8, 0xFF);  
        
        via_ad_castra = vade(via_ad_castra, 1);     
        
        // 修正4：将字面量 1 muta 为 p32 才能与 i 相加
        i += muta(p32, 1);
    }
    
    via_ad_castra = recede(via_ad_castra, MAX_MILITUM);
    neca(via_ad_castra);
    
    scribe("Victoria!");
    rdd 0; 
}
