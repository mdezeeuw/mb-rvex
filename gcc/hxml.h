#ifndef HXML_H
#define HXML_H

extern int hxml_show_error;

typedef enum {GPP,FPGA,DSP} functional_component_type;

typedef enum {XREG, OTHER} memory_name_type;

int hxml_read_xml();
void hxml_debug_xml(void);

functional_component_type hxml_get_functional_component_type(int id);
int hxml_get_start_input_xr(int id);
int hxml_get_start_output_xr(int id);

int hxml_get_addr_bus_width(int id,memory_name_type m);
int hxml_get_data_bus_width(int id,memory_name_type m);
int hxml_get_read_cycles(int id,memory_name_type m);

#endif
