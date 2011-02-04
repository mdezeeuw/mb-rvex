#include <stdio.h>
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "fpga-config.h"
#include "fpga-pass-utils.h"

extern const char *fpga_configuration_file_name;

tree fpga_config = NULL_TREE;

bool fpga_config_load (void) {
  FILE *f;
  tree t;
  char name[100];
  int xr_in, xr_out,addr_set,addr_execute;

  if (fpga_configuration_file_name == NULL) {
    printf ("No fpga config file\n");
    return false;
  }
  f = fopen(fpga_configuration_file_name, "rt");
  if (f == NULL) {
    printf ("Could not open fpga config file: %s\n", fpga_configuration_file_name);
    return false; 
  }
  while(!feof(f)) {
    xr_in = xr_out = addr_set = addr_execute = -1;
    fscanf(f, "OP_NAME = %s\n", name);
    fscanf(f, "XRin = %x\n", &xr_in);
    fscanf(f, "XRout = %x\n", &xr_out);
    fscanf(f, "SET_ADDR = %x\n", &addr_set);
    fscanf(f, "EXEC_ADDR = %x\n", &addr_execute);
    fscanf(f, "END_OP\n");

    if(xr_in==-1 || xr_out==-1 || addr_set==-1 || addr_execute==-1) {
        printf(" Bad configuration file format exiting.\n");
        exit(-1);
    };

    fpga_print_debug(10, " Read OP_NAME %s\n", name);
    t = build_tree_list(build_string(4,"name"),build_string(strlen(name),name));
    t = chainon(t, build_tree_list(build_string(8,"xr_in"),build_int_cst(NULL_TREE,xr_in)));
    t = chainon(t, build_tree_list(build_string(8,"xr_out"),build_int_cst(NULL_TREE,xr_out)));
    t = chainon(t, build_tree_list(build_string(8,"addr_set"),build_int_cst(NULL_TREE,addr_set)));
    t = chainon(t, build_tree_list(build_string(12,"addr_execute"),build_int_cst(NULL_TREE,addr_execute)));

    fpga_config = chainon(fpga_config, build_tree_list(NULL_TREE,t));
  }

  fclose(f);
  return true;
}

tree fpga_config_get(const char *n) {
  tree l,c;
  for (l = fpga_config; l; l = TREE_CHAIN (l)) {
    for (c = TREE_VALUE(l); c; c = TREE_CHAIN (c)) {
      if(strcmp(TREE_STRING_POINTER(TREE_PURPOSE(c)),"name") == 0 && strcmp(TREE_STRING_POINTER(TREE_VALUE(c)),n) == 0) {
        return l;
      }
    }
  }
  return NULL_TREE;
}

static int fpga_config_get_generic(const char *n,const char *w) {
  tree l = fpga_config_get(n), c;
  
  for (c = TREE_VALUE(l); c; c = TREE_CHAIN (c)) {
    if(strcmp(TREE_STRING_POINTER(TREE_PURPOSE(c)),w)==0) {
      return TREE_INT_CST_LOW(TREE_VALUE(c));
    }
  }

  return 100;
}

int fpga_config_get_xr_in(const char *n) {
  return fpga_config_get_generic(n,"xr_in");
}

int fpga_config_get_xr_out(const char *n) {
  return fpga_config_get_generic(n,"xr_out");
}

int fpga_config_get_address_set(const char *n) {
  return fpga_config_get_generic(n,"addr_set");
}

int fpga_config_get_address_execute(const char *n) {
  return fpga_config_get_generic(n,"addr_execute");
}
