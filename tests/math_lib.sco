liber math_lib;

// 导出一个结构体 (frm = forma, edt = edita)
frm edt Vector {
    sit x: medius;
    sit y: medius;
}

// 导出一个函数 (act = actio, nhl = nihil)
act edt add_vec(a: via Vector, b: via Vector, out: via Vector) -> nhl {
    // 使用 -> 进行指针成员访问
    out->x = a->x + b->x;
    out->y = a->y + b->y;
}
