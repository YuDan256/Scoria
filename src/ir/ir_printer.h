#ifndef SCORIA_IR_PRINTER_H
#define SCORIA_IR_PRINTER_H

#include "sir.h"
#include <stdio.h>

// 将 SIR 模块以人类可读的文本格式打印到输出流
void sir_print_module(FILE* out, SirModule* module);

#endif // SCORIA_IR_PRINTER_H
