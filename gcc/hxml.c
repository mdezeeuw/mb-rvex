#include <expat.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>

#define _GNU_SOURCE
#include <string.h>

#include "options.h"
#include "hxml.h"

#define BUFFER_SIZE 100

static XML_Parser parser;


// These 2 flags are not updated anymore. That means new error will not be gated by this.
int hxml_show_error = 1;
static int stop_on_error = 1;

static char* data = NULL;
static char empty[] = "";

struct functional_component {
  char *name;
  functional_component_type type;
  struct functional_component *next;
};

struct storage_component {
  char *name;
  struct storage_component *next;
};

struct bus_component {
  char *name;
  char *type;
  int bandwidth;
  
  int addr_bus_width;
  int data_bus_width;
  int read_cycles;
  
  int functional_components_nr;
  struct functional_component **functional_components;
  int storage_components_nr;
  struct storage_component **storage_components;
  
  struct bus_component *next;
};

struct implementation {
  int id;
  int start_input_xr;
  int start_output_xr;
  struct implementation *next;
};

struct component {
  char *name;
  struct functional_component *functional_component;  
  struct implementation *implementations;
  struct component *next;
};

struct operation {
  char *name;
  struct component *components;
  struct operation *next;
};

static struct operation *operations;
static struct functional_component *functional_components;
static struct storage_component *storage_components;
static struct bus_component *bus_components;

// Local variables to fill in with data from xml
// Ignored elements VBUS_COMPONENT
typedef enum { NONE=0, FUNCTIONAL_COMPONENT, STORAGE_COMPONENT, BUS_COMPONENT, VBUS_COMPONENT, FUNCTIONAL_COMPONENTS, STORAGE_COMPONENTS, OPERATIONS, OPERATION, COMPONENT, IMPLEMENTATION, HEADERS, DATA } states;
static states state;

static struct functional_component *current_functional_component;
static struct storage_component *current_storage_component;
static struct bus_component *current_bus_component;

static struct operation *current_operation;
static struct component *current_component;
static struct implementation *current_implementation;

// This variables are used to fill in the bus_component
static int temp_functional_components_nr = 0;
static struct functional_component* temp_functional_components[100];
static int temp_storage_components_nr = 0;
static struct storage_component* temp_storage_components[100];

// Local functions to deal with the data structures

static void functional_component_add(void) {
  if(functional_components==NULL) {
    functional_components = malloc(sizeof(struct functional_component));
    current_functional_component = functional_components;
  } else {
    current_functional_component->next = malloc(sizeof(struct functional_component));
    current_functional_component = current_functional_component->next;
  }
  memset(current_functional_component,0,sizeof(struct functional_component));
}

static void storage_component_add(void) {
  if(storage_components==NULL) {
    storage_components = malloc(sizeof(struct storage_component));
    current_storage_component = storage_components;
  } else {
    current_storage_component->next = malloc(sizeof(struct storage_component));
    current_storage_component = current_storage_component->next;
  }
  memset(current_storage_component,0,sizeof(struct storage_component));
}

static void bus_component_add(void) {
  if(bus_components==NULL) {
    bus_components = malloc(sizeof(struct bus_component));
    current_bus_component = bus_components;
  } else {
    current_bus_component->next = malloc(sizeof(struct bus_component));
    current_bus_component = current_bus_component->next;
  }
  memset(current_bus_component,0,sizeof(struct bus_component));
}


static void operation_add(void) {
  if(operations==NULL) {
    operations = malloc(sizeof(struct operation));
    current_operation = operations;
  } else {
    current_operation->next = malloc(sizeof(struct operation));
    current_operation = current_operation->next;
  }
  memset(current_operation,0,sizeof(struct operation));
}

static void component_add(void) {
  if(current_operation->components==NULL) {
    current_operation->components = malloc(sizeof(struct component));
    current_component = current_operation->components;
  } else {
    current_component->next = malloc(sizeof(struct component));
    current_component = current_component->next;
  }
  memset(current_component,0,sizeof(struct component));
}

static void implementation_add(void) {
  if(current_component->implementations==NULL) {
    current_component->implementations = malloc(sizeof(struct implementation));
    current_implementation = current_component->implementations;
  } else {
    current_implementation->next = malloc(sizeof(struct implementation));
    current_implementation = current_implementation->next;
  }
  memset(current_implementation,0,sizeof(struct implementation));
}

static struct functional_component* get_functional_component(char *name) {
  struct functional_component* ret;
  // Search for the corresponding functional_component
  ret = functional_components;
  while(ret!=NULL) {
    if(strcmp(ret->name,name)==0) {
	break;
     }
     ret = ret->next;
  }
  return ret;
}

static struct storage_component* get_storage_component(char *name) {
  struct storage_component* ret;
  // Search for the corresponding storage_component
  ret = storage_components;
  while(ret!=NULL) {
    if(strcmp(ret->name,name)==0) {
	break;
     }
     ret = ret->next;
  }
  return ret;
}


static int convert_to_int(int *v, char *s) {
  char *p;
  *v = strtol(s,&p,0);
	
  if(p==data) {
    return 1;
  }
  
  while(*p!='\0' && isspace(*p)) {
    p++;
  }
  
  if(*p!='\0') {
    return 1;
  }
  
  return 0;
}

static void startElement(void *userData, const char *name, const char **atts) {  
  userData = userData;
  atts = atts;
  
  if(strcmp(name,"FUNCTIONAL_COMPONENT")==0) {
    functional_component_add();
    state = FUNCTIONAL_COMPONENT;
  }
  if(strcmp(name,"STORAGE_COMPONENT")==0) {
    storage_component_add();
    state = STORAGE_COMPONENT;
  }
  if(strcmp(name,"BUS_COMPONENT")==0) {
    bus_component_add();
    state = BUS_COMPONENT;
  }
  if(strcmp(name,"VBUS_COMPONENT")==0) {
    // We do not get any info from those in the compiler so we ignore it
    state = VBUS_COMPONENT;
  }
  if(strcmp(name,"FUNCTIONAL_COMPONENTS")==0) {
    if(state==BUS_COMPONENT) {
      state = FUNCTIONAL_COMPONENTS;
      temp_functional_components_nr = 0;
    } else {
      fprintf(stderr,"hxml: Parse error. Didn't expect FUNCTIONAL_COMPONENTS at line %d.\n",(int)XML_GetCurrentLineNumber(parser));
      if(stop_on_error) {
        exit(2);
      }
    }
  }
  if(strcmp(name,"STORAGE_COMPONENTS")==0) {
    if(state==BUS_COMPONENT) {
      state = STORAGE_COMPONENTS;
      temp_storage_components_nr = 0;
    } else {
      fprintf(stderr,"hxml: Parse error. Didn't expect STORAGE_COMPONENTS at line %d.\n",(int)XML_GetCurrentLineNumber(parser));
      if(stop_on_error) {
        exit(2);
      }
    }
  }

  if(strcmp(name,"OPERATIONS")==0) {
    state = OPERATIONS;
  }

  if(strcmp(name,"OPERATION")==0) {
    if(state == OPERATIONS) {
      operation_add();
      state = OPERATION;
    } else {
      fprintf(stderr,"hxml: Parse error. Didn't expect OPERATION at line %d.\n",(int)XML_GetCurrentLineNumber(parser)); 
      exit(2);
    }
  }
  if(strcmp(name,"COMPONENT")==0) {
    if(state == OPERATION) {
      component_add();
      state = COMPONENT;
    } else {
      fprintf(stderr,"hxml: Parse error. Didn't expect COMPONENT at line %d.\n",(int)XML_GetCurrentLineNumber(parser));
      exit(2);
    }
  }
  if(strcmp(name,"IMPLEMENTATION")==0) {
    if(state == COMPONENT) {
      implementation_add();
      state = IMPLEMENTATION;
    } else {
      fprintf(stderr,"hxml: Parse error. Didn't expect IMPLEMENTATION at line %d.\n",(int)XML_GetCurrentLineNumber(parser));
      exit(2);
    }
  }
 
  if(strcmp(name,"HEADERS")==0) {
    // We do not get any info from those in the compiler so we ignore it
    state = HEADERS;
  }

  if(strcmp(name,"DATA")==0) {
    // We do not get any info from those in the compiler so we ignore it
    state = DATA;
  }
  
  data = NULL;  
}

static void endElement(void *userData, const char *name) {
  userData = userData; 
  
  if(data==NULL) data = empty;
  
  if(strcmp(name,"FUNCTIONAL_COMPONENT")==0) {
    state = NONE;
  }
  if(strcmp(name,"STORAGE_COMPONENT")==0) {
    state = NONE;
  }
  if(strcmp(name,"BUS_COMPONENT")==0) {
    state = NONE;
  }
  if(strcmp(name,"VBUS_COMPONENT")==0) {
    state = NONE;
  }
  
  if(strcmp(name,"ADDR_BUS_WIDTH")==0) {
    switch(state) {
      case BUS_COMPONENT:
        if(current_bus_component!=NULL && convert_to_int(&current_bus_component->addr_bus_width,data)!=0) {
          if(hxml_show_error) {
            fprintf(stderr,"hxml: Error parsing ADDR_BUS_WIDTH '%s'.Should be integers.\n",data);
          }
          if(stop_on_error) {
            exit(2);
          }
        }
	break;
      default:
        if(hxml_show_error) {
          fprintf(stderr,"hxml: Parse error. Didn't expect ADDR_BUS_WIDTH at line %d.\n",(int)XML_GetCurrentLineNumber(parser));
        }
        exit(2);
        break;
    }
  }

  if(strcmp(name,"DATA_BUS_WIDTH")==0) {
    switch(state) {
      case BUS_COMPONENT:
        if(current_bus_component!=NULL && convert_to_int(&current_bus_component->data_bus_width,data)!=0) {
          if(hxml_show_error) {
            fprintf(stderr,"hxml: Error parsing DATA_BUS_WIDTH '%s'.Should be integers.\n",data);
          }
          if(stop_on_error) {
            exit(2);
          }
        }
        break;
      default:
        if(hxml_show_error) {
          fprintf(stderr,"hxml: Parse error. Didn't expect DATA_BUS_WIDTH at line %d.\n",(int)XML_GetCurrentLineNumber(parser));
        }
        exit(2);
        break;
    }
  }
  
  if(strcmp(name,"READ_CYCLES")==0) {
    switch(state) {
      case BUS_COMPONENT:
        if(current_bus_component!=NULL && convert_to_int(&current_bus_component->read_cycles,data)!=0) {
          if(hxml_show_error) {
            fprintf(stderr,"hxml: Error parsing READ_CYCLES '%s'.Should be integers.\n",data);
          }
          if(stop_on_error) {
            exit(2);
          }
        }
	break;
      default:
        if(hxml_show_error) {
          fprintf(stderr,"hxml: Parse error. Didn't expect READ_CYCLES at line %d.\n",(int)XML_GetCurrentLineNumber(parser));
        }
        exit(2);
        break;
    }
  }
  
  if(strcmp(name,"FUNCTIONAL_COMPONENTS")==0) {
    state = BUS_COMPONENT;
    
    current_bus_component->functional_components = malloc(sizeof(struct functional_component*)*temp_functional_components_nr);
    memcpy(current_bus_component->functional_components, temp_functional_components,sizeof(struct functional_component*)*temp_functional_components_nr);
    current_bus_component->functional_components_nr = temp_functional_components_nr;
    temp_functional_components_nr=0;
  }
  if(strcmp(name,"STORAGE_COMPONENTS")==0) {
    state = BUS_COMPONENT;

    current_bus_component->storage_components = malloc(sizeof(struct storage_component*)*temp_storage_components_nr);
    memcpy(current_bus_component->storage_components, temp_storage_components,sizeof(struct storage_component*)*temp_storage_components_nr);
    current_bus_component->storage_components_nr = temp_storage_components_nr;
    temp_storage_components_nr=0;
  }

  if(strcmp(name,"NAME")==0) {
    switch(state) {
      case FUNCTIONAL_COMPONENT:
        current_functional_component->name = data;
	break;
      case STORAGE_COMPONENT:
        current_storage_component->name = data;
	break;
      case BUS_COMPONENT:
        current_bus_component->name = data;
	break;
      case VBUS_COMPONENT:
	break;
      case FUNCTIONAL_COMPONENTS:
        temp_functional_components[temp_functional_components_nr++] = get_functional_component(data);
	if(temp_functional_components[temp_functional_components_nr-1]==NULL) {
          if(hxml_show_error) {
            fprintf(stderr,"hxml: Error: BUS_COMPONENTS, invalid FUNCTIONAL_COMPONENTS with NAME %s at line %d.\n",data,(int)XML_GetCurrentLineNumber(parser));
          }
	  temp_functional_components_nr--;
          if(stop_on_error) {
            exit(2);
          }
	}
        break;
      case STORAGE_COMPONENTS:
        temp_storage_components[temp_storage_components_nr++] = get_storage_component(data);
	if(temp_storage_components[temp_storage_components_nr-1]==NULL) {
          if(hxml_show_error) {
            fprintf(stderr,"hxml: Error: BUS_COMPONENTS, invalid STORAGE_COMPONENTS with NAME %s at line %d.\n",data,(int)XML_GetCurrentLineNumber(parser));
          }
	  temp_storage_components_nr--;
          if(stop_on_error) {
            exit(2);
          }
	}
        break;
      case OPERATION: 
        current_operation->name = data;
	break;
      case COMPONENT: 
        current_component->name = data;
	
	// Search for the corresponding functional_component
	current_component->functional_component = get_functional_component(current_component->name);

	if(current_component->functional_component ==NULL) {
          if(hxml_show_error) {
            fprintf(stderr,"hxml: Error: component without valid functional component %s at line %d.\n",current_component->name,(int)XML_GetCurrentLineNumber(parser));
          }
          if(stop_on_error) {
            exit(2);
          }
	}	
	break;
      case HEADERS:
        break;
      case DATA:
        break;
      default:
        if(hxml_show_error) {
          fprintf(stderr,"hxml: Error parsing : NAME element in unknown context at line %d\n",(int)XML_GetCurrentLineNumber(parser));
        }
        break;
    }
  };
  if(strcmp(name,"OPERATION")==0) {
    state = OPERATIONS;
  }
  if(strcmp(name,"COMPONENT")==0) {
    state = OPERATION;
  }
  if(strcmp(name,"IMPLEMENTATION")==0) {
    state = COMPONENT;
  }
  if(strcmp(name,"HEADERS")==0) {
    state = FUNCTIONAL_COMPONENT;
  }

  if(strcmp(name,"ID")==0) {
    switch(state) {
      case IMPLEMENTATION:
        if(current_implementation!=NULL && convert_to_int(&current_implementation->id,data)!=0) {
          if(hxml_show_error) {
            fprintf(stderr,"hxml: Error parsing id '%s'. ID-s should be integers\n",data);
          }
          if(stop_on_error) {
            exit(2);
          }
        }
	break;
      default:
        if(hxml_show_error) {
          fprintf(stderr,"hxml: Parse error. Didn't expect ID at line %d.\n",(int)XML_GetCurrentLineNumber(parser));
        }
        exit(2);
        break;
    }
  };
  
  if(strcmp(name,"TYPE")==0) {
    switch(state) {
      case FUNCTIONAL_COMPONENT:
        if(strcmp(data,"GPP")==0) {
          current_functional_component->type=GPP;
	} else if(strcmp(data,"FPGA")==0) {
          current_functional_component->type=FPGA;
	} else if(strcmp(data,"DSP")==0) {
          current_functional_component->type=DSP;
	} else {
          if(hxml_show_error) {
            fprintf(stderr,"hxml: Error reading functional_component type '%s' at line\n",data,(int)XML_GetCurrentLineNumber(parser));

          }
          if(stop_on_error) {
            exit(2);
          }
        }
        break;
      case STORAGE_COMPONENT:
        break;
      case BUS_COMPONENT:
        break;
      default:
        if(hxml_show_error) {
          fprintf(stderr,"hxml: Error parsing : TYPE element in unknown context (%d)\n",state);
        }
        break;
    }
  }
  
  if(strcmp(name,"START_INPUT_XR")==0) {
    if(current_implementation!=NULL && convert_to_int(&current_implementation->start_input_xr,data)!=0) {
      if(hxml_show_error) {
        fprintf(stderr,"hxml: Error parsing start input xr '%s'. START_INPUT_XR should be integers\n",data);
      }
      if(stop_on_error) {
        exit(2);
      }
    }  
  }
  
  if(strcmp(name,"START_OUTPUT_XR")==0) {
    if(current_implementation!=NULL && convert_to_int(&current_implementation->start_output_xr,data)!=0) {
      if(hxml_show_error) {
        fprintf(stderr,"hxml: Error parsing start output xr '%s'. START_OUTPUT_XR should be integers\n",data);
      }
      if(stop_on_error) {
        exit(2);
      }
    }
  }
  
  data = NULL;
}

static void charData (void *userData, const XML_Char *s, int len) {  
  userData = userData;
  if(data==NULL) {
    data = strndup(s,len);
  } else {
    char *d = malloc(strlen(data)+len+1);
    d[0]='\0';
    strcat(d,data);
    strncat(d,s,len);
    free(data);
    data = d;
  }
}


int hxml_read_xml() {
   const char *infile = hartes_configuration_file_name;
   static int initialized = 0;
   
   char buf[BUFFER_SIZE];
   FILE * in;
   int done;

   if(initialized) {
     return 0;
   }

   initialized = 1;
 
   if (!infile) {
      in = stdin;
   } else if (!(in = fopen (infile, "r"))) {
      printf ("Unable to open input file %s\n", infile);
      exit (2);
   }
 
   parser = XML_ParserCreate(NULL);

   XML_SetElementHandler(parser, startElement, endElement);
   XML_SetCharacterDataHandler(parser, charData);

   done = 0;

   do {
      size_t len = fread(buf, 1, sizeof(buf), in);
      done = len < sizeof(buf);
      if (!XML_Parse(parser, buf, len, done)) {
         fprintf(stderr,
            "%s at line %d\n",
            XML_ErrorString(XML_GetErrorCode(parser)),
            (int)XML_GetCurrentLineNumber(parser));
         return 1;
      }
   } while (!done);
   XML_ParserFree(parser);

   if (in != stdin) fclose (in);


   return 0;
}

void hxml_debug_xml(void) {
  current_operation = operations;
  while(current_operation!=NULL) {
    printf("operation name=%s\n",current_operation->name);
    
    current_component = current_operation->components;
    while(current_component!=NULL) {
      printf("  component name=%s\n",current_component->name);      
      current_implementation = current_component->implementations;
      while(current_implementation!=NULL) {
        printf("    implementation id=%d\n",current_implementation->id);
        current_implementation = current_implementation->next;
      } 
      current_component = current_component->next;
    }
    current_operation = current_operation->next;
  }
  
  current_functional_component = functional_components;
  while(current_functional_component!=NULL) {
    printf("functional_component name=%s type=%d\n",current_functional_component->name,current_functional_component->type);
    current_functional_component = current_functional_component->next;
  }
  
  current_storage_component = storage_components;
  while(current_storage_component!=NULL) {
    printf("storage_component name=%s \n",current_storage_component->name);
    current_storage_component = current_storage_component->next;
  }
  
  current_bus_component = bus_components;
  while(current_bus_component!=NULL) {
    int i;
    printf("bus_component name=%s \n",current_bus_component->name);
    
    for(i=0;i<current_bus_component->functional_components_nr;i++) {
      printf("  functional_component name=%s type=%d\n",current_bus_component->functional_components[i]->name,current_bus_component->functional_components[i]->type);
    }
    for(i=0;i<current_bus_component->storage_components_nr;i++) {
      printf("  storage_component name=%s \n",current_bus_component->storage_components[i]->name);
    }
    
    current_bus_component = current_bus_component->next;
  }
}

static struct functional_component* get_functional_component_from_implementation(int id) {
  current_operation = operations;
  while(current_operation!=NULL) {
    current_component = current_operation->components;
    while(current_component!=NULL) {
      current_implementation = current_component->implementations;
      while(current_implementation!=NULL) {
        if(current_implementation->id==id) {
	  return current_component->functional_component;
	}
        current_implementation = current_implementation->next;
      }      
      
      current_component = current_component->next;
    }    
    current_operation = current_operation->next;
  }
  return NULL;
}

functional_component_type hxml_get_functional_component_type(int id) {
  struct functional_component* f = get_functional_component_from_implementation(id);
  if(f!=NULL) {
    return f->type;
  }
  fprintf(stderr,"hxml:get_functional_component_type: Error couldn't find implementation with id %d .\n",id);
  if(stop_on_error) {
    exit(2);
  }
  
  return GPP;
}

static struct implementation* get_implementation(int id) {
  current_operation = operations;
  while(current_operation!=NULL) {
    current_component = current_operation->components;
    while(current_component!=NULL) {
      current_implementation = current_component->implementations;
      while(current_implementation!=NULL) {
        if(current_implementation->id==id) {
	  return current_implementation;
	}
        current_implementation = current_implementation->next;
      }      
      
      current_component = current_component->next;
    }    
    current_operation = current_operation->next;
  }
  
  return NULL;
}

// This function will be used for searching the buses used
// by the FPGA component
static struct bus_component* get_bus(int id, memory_name_type m) {
  // We need the functional component for this id
  struct functional_component* f = get_functional_component_from_implementation(id);

  // We search for the buses connecting f. They need to be 2 (one 
  // to normal memory and one to xreg
  struct bus_component * normal = NULL;
  struct bus_component * xreg = NULL;
  
  if(f==NULL) return NULL;
  
  current_bus_component = bus_components;
  while(current_bus_component!=NULL) {
    int i;
    int found = 0;
    
    for(i=0;i<current_bus_component->functional_components_nr;i++) {
      if(strcmp(current_bus_component->functional_components[i]->name,f->name)==0) {
        found = 1;
	break;
      }
    }

    if(found && normal!=NULL && xreg !=NULL) {
      fprintf(stderr,"hxml: found multiple buses connected to %s (only a normal and a xreg bus should be connected to the fpga for dwarv to be able to use it) .\n",f->name);
      if(stop_on_error) {
        exit(2);
      }
    }

    if(found) {
      for(i=0;i<current_bus_component->storage_components_nr;i++) {
        if(strcmp(current_bus_component->storage_components[i]->name,"XREG")==0) {
	  xreg = current_bus_component;
	  break;
	}
      }
      
      // We didn't find any storage named xreg, this must be the 'normal' one
      if(i==current_bus_component->storage_components_nr) {
        normal = current_bus_component;
      }
    }

    current_bus_component = current_bus_component->next;
  }
  
  switch(m) {
    case OTHER:
      return normal;
    case XREG:
      return xreg;
  }
  
  return NULL;
}

int hxml_get_start_input_xr(int id) {
  struct implementation *impl = get_implementation(id);
  if(impl!=NULL) {
    return impl->start_input_xr;
  } else {
    fprintf(stderr,"hxml:get_start_input_xr: Error couldn't find implementation with id %d .\n",id);
    if(stop_on_error) {
      exit(2);
    }
    return 0;
  }
}

int hxml_get_start_output_xr(int id) {
  struct implementation *impl = get_implementation(id);
  if(impl!=NULL) {
    return impl->start_output_xr;
  } else {
    fprintf(stderr,"hxml:get_start_output_xr: Error couldn't find implementation with id %d .\n",id);
    if(stop_on_error) {
      exit(2);
    }
    return 0;
  }
}

int hxml_get_addr_bus_width(int id,memory_name_type m) {
  struct bus_component* b = get_bus(id,m);
  
  if(b!=NULL) {
    return b->addr_bus_width;
  } else {
    fprintf(stderr,"hxml:get_addr_bus_width Error couldn't find bus for implementation %d.\n",id);
    if(stop_on_error) {
      exit(2);
    }
    /* These are the default value for the Hartes board. Added this
       so that if there is a big problem with XML, still DWARV can 
       get some valid data. (ofc will not work if we have multiple
       boards but this is not the case yet). */
    switch(m) {
	case XREG:
	    return 10;
	case OTHER:
	    return 18;
	default:
	    return 32;
    }
  }
}

int hxml_get_data_bus_width(int id,memory_name_type m) {
  struct bus_component* b = get_bus(id,m);
  
  if(b!=NULL) {
    return b->data_bus_width;
  } else {
    fprintf(stderr,"hxml:get_addr_bus_width Error couldn't find bus for implementation %d.\n",id);
    if(stop_on_error) {
      exit(2);
    }
    /* These are the default value for the Hartes board. Added this
       so that if there is a big problem with XML, still DWARV can 
       get some valid data. (ofc will not work if we have multiple
       boards but this is not the case yet). */
    switch(m) {
	case XREG:
	    return 4;
	case OTHER:
	    return 4;
	default:
	    return 8;
    }
  }
}

int hxml_get_read_cycles(int id,memory_name_type m) {
  struct bus_component* b = get_bus(id,m);

  if(b!=NULL) {
    return b->read_cycles;
  } else {
    fprintf(stderr,"hxml:get_addr_bus_width Error couldn't find bus for implementation %d.\n",id);
    if(stop_on_error) {
      exit(2);
    }
    /* These are the default value for the Hartes board. Added this
       so that if there is a big problem with XML, still DWARV can 
       get some valid data. (ofc will not work if we have multiple
       boards but this is not the case yet). */
    switch(m) {
	case XREG:
	    return 3;
	case OTHER:
	    return 5;
	default:
	    return 2;
    }
  }
}
