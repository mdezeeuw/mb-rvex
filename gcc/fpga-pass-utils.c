#include <stdio.h>
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "rtl.h"
#include "output.h"

#include "fpga-pass-utils.h"
#include "fpga-config.h"

extern int fpga_pass_debug_level;

int fpga_print_debug(char level, const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  
  if (fpga_pass_debug_level >= level)
    vprintf (msg, args);
  
  va_end(args);
  return 0; 
}

char const* output_fpga_call(FILE *asm_out_file,tree attr) {
	rtx lb = gen_label_rtx();
	output_asm_insn("sync",NULL);
	output_asm_insn("nop",NULL);
	output_asm_insn("nop",NULL);
	output_asm_insn("nop",NULL);
	output_asm_insn("bl %l0",&lb);
	output_asm_label(lb);
	asm_fprintf(asm_out_file,":\n");
	asm_fprintf(asm_out_file,"\t.long %llu # fpga_execute\n", TREE_INT_CST(TREE_VALUE(attr)).low);
	output_asm_insn("nop",NULL);
	return "";
}
