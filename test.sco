liber LegioX;

// 【第一防区】统帅的装甲方阵 (Structura)
forma edita Miles {
    sit id: medius;
    sit hp: medius;
    sit veteranus: logica;
}

// 【第二防区】辅助动作：试除法
actio edita is_sacred(n: medius) -> logica {
    si (n <= 1) {
        redde falsum;
    }
    
    sit i: medius = 2;
    dum (i * i <= n) {
        si (n % i == 0) {
            redde falsum;
        }
        i += 1;
    }
    redde verum;
}

// 【主将降临】纯原生 Win32 入口
actio edita princeps() -> medius {
    scribe("=== Scoria Exercitus (Roman Army Muster) ===\n");

    lex cohort_size: medius = 10;
    
    // 修复：改用 'turma' (小队) 作为普通变量名，避开关键字 'cohors'
    sit turma: via Miles = crea(Miles, cohort_size);

    per (sit i: medius = 0; i < cohort_size; i += 1) {
        // 装甲推移
        sit miles_ptr: via Miles = vade(turma, i);
        
        miles_ptr->id = i + 1;
        miles_ptr->hp = 100 + (i * 5);
        
        si (is_sacred(miles_ptr->id) == verum) {
            miles_ptr->veteranus = verum;
        } aliter {
            miles_ptr->veteranus = falsum;
        }
    }

    per (sit i: medius = 0; i < cohort_size; i += 1) {
        sit m: via Miles = vade(turma, i);
        
        scribe("Miles ID: ");
        scribe(m->id);
        
        si (m->veteranus == verum) {
            scribe(" -> VETERANUS (Sacred)\n");
        } aliter {
            scribe(" -> Tiro (Recruit)\n");
        }
    }

    // 手动处决裸游标
    neca(turma);
    scribe("--> Memoria purificata est (Memory Cleared).\n");

    redde 0;
}