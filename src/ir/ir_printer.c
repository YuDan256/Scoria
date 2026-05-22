#include "ir_printer.h"
#include <string.h>

static void print_type(FILE* out, ScoriaType* type) {
    if (!type) { fprintf(out, "ignotus"); return; }
    switch (type->kind) {
        case TY_I8: fprintf(out, "i8"); break;
        case TY_I16: fprintf(out, "i16"); break;
        case TY_I32: fprintf(out, "i32"); break;
        case TY_I64: fprintf(out, "i64"); break;
        case TY_P8: fprintf(out, "p8"); break;
        case TY_P16: fprintf(out, "p16"); break;
        case TY_P32: fprintf(out, "p32"); break;
        case TY_P64: fprintf(out, "p64"); break;
        case TY_F32: fprintf(out, "f32"); break;
        case TY_F64: fprintf(out, "f64"); break;
        case TY_LOGICA: fprintf(out, "logica"); break;
        case TY_LITTERA: fprintf(out, "littera"); break;
        case TY_TEXTUS: fprintf(out, "textus"); break;
        case TY_NIHIL: fprintf(out, "nihil"); break;
        case TY_VIA:
            fprintf(out, "via ");
            print_type(out, type->as.inner);
            break;
        case TY_COHORS:
            fprintf(out, "cohors ");
            print_type(out, type->as.inner);
            break;
        case TY_ACIES:
            fprintf(out, "acies[%u] ", type->as.array.length);
            print_type(out, type->as.array.inner);
            break;
        case TY_FORMA:
            fprintf(out, "forma %.*s", type->as.struct_type.name.length, type->as.struct_type.name.start);
            break;
        case TY_ACTIO:
            fprintf(out, "actio");
            break;
        default: fprintf(out, "typus"); break;
    }
}

static void print_value(FILE* out, SirValue* val) {
    if (!val) { fprintf(out, "null"); return; }
    switch (val->kind) {
        case SIR_VAL_VREG: 
            fprintf(out, "%%%u", val->as.vreg); 
            break;
        case SIR_VAL_CONST_INT: 
            fprintf(out, "%lld", (long long)val->as.int_val); 
            break;
        case SIR_VAL_CONST_FLOAT: 
            fprintf(out, "%f", val->as.float_val); 
            break;
        case SIR_VAL_CONST_BOOL: 
            fprintf(out, "%s", val->as.bool_val ? "verum" : "falsum"); 
            break;
        case SIR_VAL_CONST_STRING: 
            fprintf(out, "\"%s\"", val->as.string_val); 
            break;
        case SIR_VAL_GLOBAL: 
            fprintf(out, "@%s", val->as.global_name); 
            break;
        case SIR_VAL_BLOCK: 
            fprintf(out, "label %%%s_%u", val->as.block->name, val->as.block->id); 
            break;
    }
}

static void print_inst(FILE* out, SirInst* inst) {
    fprintf(out, "  ");
    if (inst->dest) {
        print_value(out, inst->dest);
        fprintf(out, " = ");
    }

    switch (inst->opcode) {
        case SIR_ADD: fprintf(out, "add "); break;
        case SIR_SUB: fprintf(out, "sub "); break;
        case SIR_MUL: fprintf(out, "mul "); break;
        case SIR_DIV: fprintf(out, "div "); break;
        case SIR_MOD: fprintf(out, "mod "); break;
        case SIR_AND: fprintf(out, "and "); break;
        case SIR_OR:  fprintf(out, "or "); break;
        case SIR_XOR: fprintf(out, "xor "); break;
        case SIR_SHL: fprintf(out, "shl "); break;
        case SIR_SHR: fprintf(out, "shr "); break;
        case SIR_ICMP_EQ: fprintf(out, "icmp eq "); break;
        case SIR_ICMP_NE: fprintf(out, "icmp ne "); break;
        case SIR_ICMP_LT: fprintf(out, "icmp lt "); break;
        case SIR_ICMP_LE: fprintf(out, "icmp le "); break;
        case SIR_ICMP_GT: fprintf(out, "icmp gt "); break;
        case SIR_ICMP_GE: fprintf(out, "icmp ge "); break;
        case SIR_ALLOCA: fprintf(out, "alloca "); break;
        case SIR_LOAD: fprintf(out, "load "); break;
        case SIR_STORE: fprintf(out, "store "); break;
        case SIR_GEP: fprintf(out, "gep "); break;
        case SIR_JMP: fprintf(out, "jmp "); break;
        case SIR_BR: fprintf(out, "br "); break;
        case SIR_CALL: fprintf(out, "call "); break;
        case SIR_RET: fprintf(out, "ret "); break;
        case SIR_CAST: fprintf(out, "cast "); break;
        case SIR_GET_PARAM: fprintf(out, "get_param "); break;
        default: fprintf(out, "unknown_op "); break;
    }

    if (inst->dest && inst->dest->type) {
        print_type(out, inst->dest->type);
        fprintf(out, " ");
    } else if (inst->opcode == SIR_STORE && inst->operands[0] && inst->operands[0]->type) {
        print_type(out, inst->operands[0]->type);
        fprintf(out, " ");
    }

    for (int i = 0; i < inst->num_operands; i++) {
        if (i > 0) fprintf(out, ", ");
        print_value(out, inst->operands[i]);
    }
    fprintf(out, "\n");
}

static void print_block(FILE* out, SirBlock* block) {
    fprintf(out, "%s_%u:\n", block->name, block->id);
    SirInst* inst = block->first_inst;
    while (inst) {
        print_inst(out, inst);
        inst = inst->next;
    }
}

static void print_function(FILE* out, SirFunction* func) {
    fprintf(out, "actio @%s(", func->name);
    fprintf(out, ") -> ");
    print_type(out, func->type->as.func_type.return_type);
    fprintf(out, " {\n");

    SirBlock* block = func->first_block;
    while (block) {
        print_block(out, block);
        block = block->next;
    }
    fprintf(out, "}\n\n");
}

void sir_print_module(FILE* out, SirModule* module) {
    if (!module) return;
    fprintf(out, "; Module: %s\n\n", module->name);

    SirFunction* func = module->first_func;
    while (func) {
        print_function(out, func);
        func = func->next;
    }
}
