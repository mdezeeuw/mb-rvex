#ifndef _FPGA_CONFIG_H
#define _FPGA_CONFIG_H

bool fpga_config_load(void);
tree fpga_config_get(const char *n);
int  fpga_config_get_address_set(const char *n);
int  fpga_config_get_address_execute(const char *n);
int fpga_config_get_xr_out(const char *n);
int fpga_config_get_xr_in(const char *n);
char const* output_fpga_call(FILE *f,tree attr);

#endif
