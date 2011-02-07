#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "target.h"
#include "cgraph.h"
#include "ipa-prop.h"
#include "tree-flow.h"
#include "tree-pass.h"
#include "flags.h"
#include "timevar.h"
#include "diagnostic.h"
#include "real.h"
#include "hxml.h"
#include "tm_p.h"
#include <stdlib.h>
extern int have_error = 0;
#define NOWAIT_FUNC 0x10000
#define POINTER_ALIAS 0x00010000UL
/*
  Probably a good example pass would be : pass_ipa_cp (this is interprocedural)
  (this is in ipa-cp.c)
*/
/* it's an offset to a dynamic reserved buffer */
#define DSP_ALLOC_POINTERS_AREA 0
static unsigned int do_diopsis_replace_calls_tree_execute(void);
static unsigned int do_diopsis_replace_calls_tree(tree r,tree_stmt_iterator *tsi_parent);
static void diopsis_replace_call_tree_tsi (tree_stmt_iterator *tsi);
static void expand_parameters(tree call,tree_stmt_iterator* it,unsigned read_only_parm,int io);
/*static void diopsis_expand_calls (struct cgraph_node *mt);*/
static int is_internal_memory;
static int pointer_int_arg_advance;
static int float_long_arg_advance;
static int dsp_internal_memory_address;
typedef struct _mem_alloc {
  tree expr;
  int map_addr;
} mem_alloc_t;

static mem_alloc_t dsp_allocated_buffers[128];
static int parm_long_mask;
static int trx_bytes,out_bytes;
static int is_wo=0;
static int is_const=0;
static unsigned func_flags=0;
static unsigned write_only_parm=0;
static int pointer_op,int_pointer;
static tree pragma_on_call=NULL;
static tree pragma_on_call_magic=NULL;

static char* func_type(unsigned typ){
  if(typ&NOWAIT_FUNC) return "NOWAIT";
  return "";
}

enum {
  /** DSP device */
  DSPDEV,
  /** GPP device */
  GPPDEV,
  /** FPGA device */
  FPGADEV,

  NDEVS
};


#define IO_PROLOGUE 1
#define IO_EPILOGUE 2

enum {
  DSP_PARM_INT,
  DSP_PARM_LONG,
  DSP_PARM_FLOAT,
  DSP_PARM_CFLOAT,
  DSP_PARM_VFLOAT,
  DSP_PARM_CLONG,
  DSP_PARM_VLONG,
  DSP_ALLOCATE_POINTER,
  DSP_POINTER_INT,
  DSP_POINTER_FLOAT,
  DSP_POINTER_DWORD,

  DSP_PARM_P_INT,
  DSP_PARM_P_LONG,
  DSP_PARM_P_FLOAT,
  DSP_PARM_P_CFLOAT,
  DSP_PARM_P_VFLOAT,
  DSP_PARM_P_CLONG,
  DSP_PARM_P_VLONG,

  DSP_INTERNAL_POINTER,
  DSP_ARRAY,


  

  DSP_PARM_LIMIT
};


#define POINTER_ALIAS 0x00010000UL
#define POINTER_READONLY 0x00020000UL
#define POINTER_WRITEONLY 0x00040000UL

static char* pointer_print_type(int flags){
  static char type[256];
  strcpy(type,"\"RW\"");
  if(flags&POINTER_READONLY)
    strcpy(type," \"RO\"");

  if(flags&POINTER_WRITEONLY)
    strcpy(type," \"WO\"");
  return type;
}
static int check_if_allocated(tree t, int parm){
  
}
extern int diopsis_pass_debug_level;
int diopsis_print_debug(char level, const char *msg, ...) {
  va_list args;
  va_start(args, msg);
   
  if (diopsis_pass_debug_level >= level)
    vfprintf (stderr,msg, args);
   
  va_end(args);
  return 0; 
}


static int trace_pointer(tree r,tree_stmt_iterator* it,tree *v){
  tree_stmt_iterator tsi;
  int cnt=0;
  *v = NULL;
  for (tsi = *it; !tsi_end_p (tsi); tsi_end_p(tsi)?0:(tsi_prev(&tsi),0) ) {
    tree st=tsi_stmt (tsi);
    if(TREE_CODE(st) == GIMPLE_MODIFY_STMT){
      if(GIMPLE_STMT_OPERAND (st, 0) == r){
	tree op = GIMPLE_STMT_OPERAND (st, 1);
	/*	fprintf(stderr,"====== found!! =====\n",cnt++);*/
	switch(TREE_CODE(op)){
	case NOP_EXPR:{
	  tree innerop = TREE_OPERAND (op,0);
	  if (TREE_CODE (innerop) == ADDR_EXPR){
	     innerop = TREE_OPERAND (innerop, 0);

	     if(TREE_CODE(innerop)==VAR_DECL){
	       tree typ=TREE_TYPE(innerop);

	       if(TREE_CODE(typ)== ARRAY_TYPE){
		 int size=TREE_INT_CST_LOW (TYPE_SIZE_UNIT(typ))/4;    
		 /*		 fprintf(stderr,"array of %d\n",size);*/
		 *v = innerop;
		 return size;
	       }
	       
	     }
	     if(TREE_CODE(innerop)==ARRAY_REF){
	       int off,arr_size;
	       tree vect = TREE_TYPE(TREE_OPERAND(innerop,0));
	       off = TREE_INT_CST_LOW (TREE_OPERAND(innerop,1));
	       arr_size = TREE_INT_CST_LOW (TYPE_SIZE_UNIT(vect))/4;
	       fprintf(stderr,"array of %d off %d\n",arr_size,off);
	       *v = innerop;
	       return (arr_size-off);
	     }
	     debug_tree(op);
	     return 0;
	     /*	     if(TREE_CODE(innerop)==ARRAY_REF){
	       	tree vect=TREE_TYPE(TREE_OPERAND(op,0));
		int offset = TREE_INT_CST_LOW (TREE_OPERAND(op,1));
		int arr_size = TREE_INT_CST_LOW (TYPE_SIZE_UNIT(vect))/4;
	       fprintf(stderr,"====== array =====\n",cnt++);
	       }*/
	  }

	}
	default:
	  /*	  
		  fprintf(stderr,"====== not supported =====\n",cnt++);
		  debug_tree(op);
	  */
	  return 0;
	}
	/*debug_tree( st);*/
	/*debug_tree(op);*/
	return 0;
      }
    }

  }
  return 0;
}
static int diopsis_warn(tree expr, const char *msg, ...){
  const char *file_name=EXPR_FILENAME(expr);
  int line=EXPR_LINENO(expr);
  va_list args;
  va_start(args, msg);
  fprintf (stderr,"%s:%d: warning: ",file_name,line);
  vfprintf (stderr,msg, args);
  va_end(args);
  /*  if (diopsis_pass_debug_level >= level){
      debug_tree(expr);
      }*/

  return 0; 
}

static int diopsis_error(tree expr, const char *msg, ...){
  char *file_name=EXPR_FILENAME(expr);
  int line=EXPR_LINENO(expr);
  va_list args;
  va_start(args, msg);
  fprintf (stderr,"%s:%d: error: ",file_name,line);
  vfprintf (stderr,msg, args);
  va_end(args);
   have_error = 1;
   exit(1);
  /*
    debug_tree(expr);
  */
  
  return 0; 
}
static int check_omogenous(tree field,int code){
  int cnt=0;
  for (; field; field = TREE_CHAIN (field)){

    if(TREE_CODE(TREE_TYPE(field))!=code){
      return 0;
    }
    cnt++;
  }
  return cnt;
}
static tree create_external_var_raw (tree type, const char *prefix)
{
  tree tmp_var;
  tree new_type;

  /* Make the type of the variable writable.  */
  new_type = build_type_variant (type, 0, 0);
  TYPE_ATTRIBUTES (new_type) = TYPE_ATTRIBUTES (type);

  tmp_var = build_decl (VAR_DECL, prefix ? get_identifier (ASTRDUP(prefix)) : NULL,
			type);

  /* The variable was declared by the compiler.  */
  DECL_ARTIFICIAL (tmp_var) = 1;
  /* And we don't want debug info for it.  */
  DECL_IGNORED_P (tmp_var) = 1;

  TREE_READONLY (tmp_var) = 1;

  DECL_EXTERNAL (tmp_var) = 1;
  TREE_STATIC (tmp_var) = 0;
  TREE_USED (tmp_var) = 1;

  return tmp_var;
}

static unsigned int do_diopsis_replace_calls_tree(tree r,tree_stmt_iterator *tsi_parent){
  tree_stmt_iterator tsi;
  switch(TREE_CODE(r)) {
  case TRY_FINALLY_EXPR:
  case TRY_CATCH_EXPR:
    do_diopsis_replace_calls_tree(TREE_OPERAND (r, 0), NULL);
    do_diopsis_replace_calls_tree(TREE_OPERAND (r, 1), NULL);
    break;
  case CATCH_EXPR:
    do_diopsis_replace_calls_tree(CATCH_BODY (r), NULL);
    break;
  case GIMPLE_MODIFY_STMT:
    diopsis_replace_call_tree_tsi(tsi_parent);

    break;
  case BIND_EXPR:
    do_diopsis_replace_calls_tree(BIND_EXPR_BODY(r),NULL);
    break;
  case STATEMENT_LIST:
    tsi = tsi_start (r);

    while(!tsi_end_p (tsi)){
      tree_stmt_iterator* tsp=&tsi ;
      tree st = tsi_stmt (tsi);
      tree call = get_call_expr_in (st);
      tree decl;

      if (call && (decl = get_callee_fndecl (call))){
	/*	debug_tree(call);*/
	if( strcmp(IDENTIFIER_POINTER(DECL_NAME(decl)), "__builtin_molen_dummy")==0){
	  tree typ;
	  unsigned hattr_id;
	  tree arg=CALL_EXPR_ARG (call, 0);
	  char* compname;
	  /*	  fprintf(stderr,"- \"%s\"\n",TREE_STRING_POINTER(arg));*/

	  hartes_call_hw_get_params(TREE_STRING_POINTER(arg),&hattr_id,&compname,0,0);
	  /*	  fprintf(stderr,"- PRAGMA ON CALL %s, id %d comp \"%s\"\n",TREE_STRING_POINTER(arg),hattr_id,compname);*/
	  pragma_on_call = arg;
	  if(compname&&(microblaze_check_dsp(compname))){
	    pragma_on_call_magic=CALL_EXPR_ARG (call, 0);
	    tsi_delink(&tsi);
	  }
	}
      }
      if(!tsi_end_p (tsi))
	do_diopsis_replace_calls_tree(st,&tsi);
      if(!tsi_end_p (tsi))
	tsi_next(&tsi);
    }
    /*    for (tsi = tsi_start (r); !tsi_end_p (tsi); tsi_end_p(tsi)?0:(tsi_next(&tsi),0) ) {

	  }*/
    break;
  case OMP_PARALLEL:
    do_diopsis_replace_calls_tree(OMP_PARALLEL_BODY (r),NULL);
    break;	    
  case OMP_SECTIONS:
    for (tsi = tsi_start (OMP_SECTIONS_BODY (r)); !tsi_end_p (tsi);  tsi_next (&tsi)) {
      tree st = tsi_stmt (tsi);
      if(TREE_CODE (st) == OMP_SECTION) {
	do_diopsis_replace_calls_tree(OMP_SECTION_BODY (st),NULL);
      }
    }
    break;
  case COND_EXPR:
    do_diopsis_replace_calls_tree(COND_EXPR_THEN(r),NULL);
    do_diopsis_replace_calls_tree(COND_EXPR_ELSE(r),NULL);
    break;
  case CALL_EXPR:
    diopsis_replace_call_tree_tsi(tsi_parent);

    break;
  default:
    ;
  }
}
static tree_stmt_iterator* salvato=NULL;
static void diopsis_replace_call_tree_tsi (tree_stmt_iterator *tsi){
  basic_block bb;
  tree stmt = tsi_stmt (*tsi);
  /*      debug_tree(stmt);*/
  tree_stmt_iterator *prev_func,*save_it;
  if (stmt){
    /*	debug_tree(stmt);*/
    /* check if is a call, if yes return statement*/
    tree call = get_call_expr_in (stmt);
    tree decl, attr;

    if (call && (decl = get_callee_fndecl (call))){

      int cnt=0;
      char*compname=0;
      int hattr_id=0;
      /*      tree call_prev=0,decl_prev=0;*/
      func_flags=0;
      /*      debug_tree(call);*/
      if(pragma_on_call_magic){
	tree targ;
	char* arg[100];
	int argn;

	/*	fprintf(stderr," PRAGMA ON CALL :%s id %d comp\n",TREE_STRING_POINTER(pragma_on_call_magic),hattr_id);*/

	hartes_call_hw_get_params(TREE_STRING_POINTER(pragma_on_call_magic),&hattr_id,&compname,arg,&argn);
	
	if(argn){
	  /*	  fprintf(stderr," PRAGMA ARGUMENT :%s narg %d\n",arg[0],argn);*/
	  if(!strcmp(arg[0],"nowait") && (!remwait)){
	    func_flags|=NOWAIT_FUNC;
	  }
	}
	pragma_on_call_magic=NULL;
	//	tsi_delink(salvato);
	//	salvato=NULL;
	diopsis_print_debug(1,"* pragma map %s on call %s \n", compname,IDENTIFIER_POINTER (DECL_NAME (decl)));
      }

      /*if(hattr && (!strcmp(hattr->compname,"DSP") || !strcmp(hattr->compname,"dsp"))){
	compname = hattr->compname;
	hattr_id = hattr->id;
	diopsis_print_debug(1,"* pragma on call %s %d\n",compname,hattr_id );
	remove_hfunc_attrs();
	}*/
      if (compname || (attr = lookup_attribute ("call_hw", DECL_ATTRIBUTES (decl)))){
	int diopsis_id=hattr_id;
	int cnt=0;
	unsigned read_only_parm=0;
	tree return_builtin=NULL;
	if(attr){

	  tree param=TREE_VALUE(attr);
	  char parameters[256];
	  char* arg[100];
	  int argn;
	  strncpy(parameters,TREE_STRING_POINTER(param),256);
	  hartes_call_hw_get_params(parameters,&diopsis_id,&compname,arg,&argn);

	  if(argn){
	    if(!strcmp(arg[0],"nowait")&& (!remwait)){
	      func_flags|=NOWAIT_FUNC;
	    }
	  }
	}


	write_only_parm = 0;
	if(!dspforce){
	  
	  if(hartes_configuration_xml==1) {
	    if (fpga_configuration_file_name == NULL) {
	      hxml_read_xml("hartes.xml");
	    } else {
	      hxml_read_xml(hartes_configuration_file_name);
	    }
	  }
	  if((hxml_get_functional_component_type(diopsis_id)!=DSP)) {
	    return;
	  }
	}
	pointer_int_arg_advance = 0;
	float_long_arg_advance = 0;
	dsp_internal_memory_address=DSP_ALLOC_POINTERS_AREA;
	/*	    debug_tree(stmt);*/
	parm_long_mask =0;
	diopsis_print_debug(1,"DSP analyzing %s 0x%x\n", IDENTIFIER_POINTER (DECL_NAME (decl)),diopsis_id);
	
	pointer_op=0;
	int_pointer=0;
	switch(TREE_CODE (TREE_TYPE(call))){
	case INTEGER_TYPE:{
	  tree rt=TREE_TYPE(call);
	  char *typname=IDENTIFIER_POINTER(DECL_NAME(TYPE_NAME(rt)));
	  /*
	    debug_tree(rt);
	    if(typname)
	    printf("TYPE NAME %s\n",typname);
	  */
	  if((rt == long_unsigned_type_node) || (rt == long_integer_type_node) || (typname && !strcmp(typname,"add_t"))){
	    /* return long */
	    float_long_arg_advance+=2;
	    diopsis_print_debug(2,"returns long\n");
	    return_builtin = built_in_decls[BUILT_IN_DSP_RETL];
	  } else {
	    pointer_int_arg_advance++;
	    diopsis_print_debug(2,"returns integer\n");
	    /*		diopsis_print_debug(2,"returns in ARF\n");*/
	    return_builtin = built_in_decls[BUILT_IN_DSP_RETI];
	  }
	}
	  break;
	case REAL_TYPE:
	  diopsis_print_debug(2,"returns real\n");
	  return_builtin = built_in_decls[BUILT_IN_DSP_RETF];
	  float_long_arg_advance+=2;
	  break;
	case COMPLEX_TYPE:
	case VECTOR_TYPE:
	  diopsis_print_debug(2,"returns complex in RF\n");
	  float_long_arg_advance+=2;
	  /*	      debug_tree(TREE_TYPE(call));*/
	  if(TREE_CODE(TREE_TYPE(TREE_TYPE(call)))==REAL_TYPE){
	    return_builtin = built_in_decls[BUILT_IN_DSP_RETDF];
	  } else {
	    return_builtin = built_in_decls[BUILT_IN_DSP_RETDL];
	  }
	  break;
	case POINTER_TYPE:
	  diopsis_warn(call, "\"%s\" remote DSP call returns pointer \n",IDENTIFIER_POINTER(DECL_NAME(decl)));
	case ENUMERAL_TYPE:
	  diopsis_print_debug(2,"returns pointer or enumeral\n");
	  pointer_int_arg_advance++;
	  return_builtin = built_in_decls[BUILT_IN_DSP_RETI];
	  break;
	case VOID_TYPE:
	  break;

	default:
	  
	  diopsis_error(stmt,"\"%s\" remote DSP unsupported return type\n",IDENTIFIER_POINTER(DECL_NAME(decl)));
	  gcc_unreachable();
	  
	}
	
	/*	    diopsis_print_debug(2,"DECLARATION\n");
		    debug_tree(decl);
		    diopsis_print_debug(2,"CALL\n");
		    debug_tree(call);
		    return;
	*/
	{
	  const_tree parmlist;
	      
	  parmlist = TYPE_ARG_TYPES (TREE_TYPE (decl));
	  /*	      init_const_call_expr_arg_iterator (call, &iter);*/
	  diopsis_print_debug(2,"======================.\n ");
	  for (; parmlist; parmlist = TREE_CHAIN (parmlist)){
	    /*		diopsis_print_debug(2,"decl ");*/
	    tree parm=TREE_VALUE (parmlist);
		
	    /*	
		diopsis_print_debug(2,"arg %d-->\n ",cnt);
		debug_tree(parm);
		debug_tree(TYPE_NAME(parm));
		debug_tree( DECL_ORIGINAL_TYPE(TYPE_NAME(parm)));
	    */
	    if(TYPE_NAME(parm)&& (TREE_CODE(TYPE_NAME(parm))==TYPE_DECL)&& DECL_ORIGINAL_TYPE (TYPE_NAME(parm))){
	      /*		 
				 debug_tree(parm);
				 debug_tree(TYPE_NAME(parm)); 
	      */
	      parm = DECL_ORIGINAL_TYPE (TYPE_NAME(parm));
	    }
	    /*debug_tree(TREE_TYPE(parm));*/
	    if((parm == long_unsigned_type_node) || (parm == long_integer_type_node)){
	      parm_long_mask|=1<<cnt;
	      diopsis_print_debug(2,"parameter %d is long\n",cnt);
	      /*		  debug_tree(parm);*/
	    }
	    if(TREE_CODE(parm)==POINTER_TYPE){
	      if(TREE_READONLY(TREE_TYPE(parm))){
		/*		    diopsis_print_debug(2,"parameter %d read only\n ",cnt);*/
		read_only_parm|=1<<cnt; 
	      }
	      if(TREE_THIS_VOLATILE(TREE_TYPE(parm))){
		 write_only_parm|=1<<cnt; 
	      }
	    }
	    /*		debug_tree(TREE_VALUE (parmlist));*/
	    cnt++;
	  }
	  diopsis_print_debug(2,"======================\n ");
	}
	{
	  tree execute=NULL;
	  tree dsp_return=NULL;
	  char func_mangled[512];
	  tree dsp_addr;
	  int ret_fun=(TREE_CODE (stmt) == GIMPLE_MODIFY_STMT);
	  const char *func_name=IDENTIFIER_POINTER ( DECL_NAME(decl));
	      
	  sprintf(func_mangled,"%s__magic_text__",func_name);
	  microblaze_include_dspfunc(func_name);
	  dsp_addr=create_external_var_raw(integer_type_node,func_mangled);
	  diopsis_print_debug(2,"mangled name %s, RPC UID 0x%x(%d) %s\n ",func_mangled,hartes_rpc_uid,hartes_rpc_uid,func_type(func_flags));
	  /*	      debug_tree(stmt);*/

	  /* generate API calls */
	  if(dspstat){

	    const char *file_name= EXPR_FILENAME(stmt);
	    int lineno     = EXPR_LINENO(stmt);
	    /*		execute= build_call_expr (built_in_decls[BUILT_IN_HARTES_STATS_BEGIN], 4,build_string(strlen(func_name)+1,func_name),build_string(strlen(file_name)+1,file_name),build_int_cst (integer_type_node, EXPR_LINENO(stmt)),build_int_cst (integer_type_node, 0));*/
	    execute= build_call_expr (built_in_decls[BUILT_IN_HARTES_STATS_BEGIN], 4,build_string_literal(strlen(func_name)+1,func_name),build_string_literal(strlen(file_name)+1,file_name),build_int_cst (integer_type_node, EXPR_LINENO(stmt)),build_int_cst (integer_type_node, DSPDEV|func_flags));
	    tsi_link_before (tsi, execute, TSI_SAME_STMT);
	    diopsis_print_debug(2,"*generate statistics for %s file %s line: %d\n",func_name,file_name,lineno);		
	  }
	  execute= build_call_expr (built_in_decls[BUILT_IN_HARTES_RPC], 2,build_int_cst (integer_type_node,hartes_rpc_uid|func_flags),build_int_cst (integer_type_node, DSPDEV));
	  tsi_link_before (tsi, execute, TSI_SAME_STMT);
	  /* PROLOGUE */
	  expand_parameters(call,tsi,read_only_parm,IO_PROLOGUE);

	  if(pointer_op||int_pointer){
	    /* are there pointers or intergers*/
	    execute= build_call_expr (built_in_decls[BUILT_IN_DSP_PRE_EXEC], 0);
	    tsi_link_before (tsi, execute, TSI_SAME_STMT);
	  }
	  if(dspstat && trx_bytes){
	    execute= build_call_expr (built_in_decls[BUILT_IN_HARTES_STATS_PARMIN], 1, build_int_cst (integer_type_node, trx_bytes));
		
	    tsi_link_before (tsi, execute, TSI_SAME_STMT);

	  }
	  if(func_flags&NOWAIT_FUNC){
	     execute= build_call_expr (built_in_decls[BUILT_IN_DSP_EXEC_NOWAIT], 1, build_fold_addr_expr(dsp_addr)/*build_int_cst (integer_type_node, diopsis_id)*/);
	  } else {
	    execute= build_call_expr (built_in_decls[BUILT_IN_DSP_EXEC], 1, build_fold_addr_expr(dsp_addr)/*build_int_cst (integer_type_node, diopsis_id)*/);
	  }
	    tsi_link_before (tsi, execute, TSI_SAME_STMT);	


	  if(dspstat){
	    execute= build_call_expr (built_in_decls[BUILT_IN_HARTES_STATS_EXEC], 0);
	    tsi_link_before (tsi, execute, TSI_SAME_STMT);
	  }

	  if(func_flags&NOWAIT_FUNC){
	    if (TREE_CODE (stmt) == CALL_EXPR) {
	      tsi_delink (tsi);
	      if(!tsi_end_p(*tsi)) {
		tsi_prev (tsi);
	      }
	    } else {
	      diopsis_error(stmt,"\"%s\" NOWAIT remote DSP cannot return values\n",IDENTIFIER_POINTER(DECL_NAME(decl)));
	    }
	    return;
	  }
	  /*	      if(pointer_op){*/
	  /* are there pointers or intergers*/
	  execute= build_call_expr (built_in_decls[BUILT_IN_HARTES_RPC_END], 2,build_int_cst (integer_type_node,hartes_rpc_uid++),build_int_cst (integer_type_node, DSPDEV));
	  tsi_link_before (tsi, execute, TSI_SAME_STMT);
	  /*	      }*/
	      
	  if(dspstat && out_bytes){
	    execute= build_call_expr (built_in_decls[BUILT_IN_HARTES_STATS_PARMOUT], 1, build_int_cst (integer_type_node, out_bytes));
	    tsi_link_before (tsi, execute, TSI_SAME_STMT);
	  }


	     
	  
	  /*
	    diopsis_print_debug(2,"=== return -> \n");
	    debug_tree(stmt);
	  */
		
	  if (TREE_CODE (stmt) == CALL_EXPR) {
	    tsi_delink (tsi);
	    if(!tsi_end_p(*tsi)) {
	      tsi_prev (tsi);
	    }
	  } else {
	    /* generate call to retrieve returns */
	    tree retfun=NULL;
	    tree fold=NULL;
	    tree cnv= NULL;
	    dsp_return = create_tmp_var(TREE_TYPE(call), "__dsp_return");

	    gcc_assert(return_builtin);
	    fold = build_fold_addr_expr(dsp_return);
	    //	    fold = build_fold_addr_expr_with_type(dsp_return,TREE_TYPE(call));
	    retfun=build_call_expr (return_builtin,1,fold);
	    
	    //diopsis_print_debug(2,"return %s\n",IDENTIFIER_POINTER(DECL_NAME(TYPE_NAME(TREE_TYPE(dsp_return)))));
	    tsi_link_before (tsi, retfun, TSI_SAME_STMT);
		
	    if (TREE_CODE (stmt) == GIMPLE_MODIFY_STMT){
		  
	      GIMPLE_STMT_OPERAND (stmt, 1) = dsp_return;
	    } else {
	      GIMPLE_STMT_OPERAND (stmt, 0) = dsp_return;
	    }
	  }
	}
      } else {
	/*	    debug_tree(stmt);*/
	/* local GPP */
	if(gppstat && (pragma_on_call==NULL)){
	  tree execute=NULL;
	  const char *file_name=EXPR_FILENAME(stmt);
	  const char *func_name=IDENTIFIER_POINTER ( DECL_NAME(decl));
	  int lineno=EXPR_LINENO(stmt);

	  if(func_name && file_name && !strstr(func_name,"__builtin")){
	    diopsis_print_debug(2,"* generate GPP statistics for %s file %s line: %d\n",func_name,file_name,lineno);		
	    execute= build_call_expr (built_in_decls[BUILT_IN_HARTES_STATS_BEGIN], 4,build_string_literal(strlen(func_name)+1,func_name),build_string_literal(strlen(file_name)+1,file_name),build_int_cst (integer_type_node, EXPR_LINENO(stmt)),build_int_cst (integer_type_node, GPPDEV)); 
	    tsi_link_before (tsi, execute, TSI_SAME_STMT);

	    execute= build_call_expr (built_in_decls[BUILT_IN_HARTES_STATS_EXEC], 0);
	    tsi_link_after (tsi, execute, TSI_SAME_STMT);
	  }
	}
      }
      {
	const char *func_name=IDENTIFIER_POINTER ( DECL_NAME(decl));
	if(strcmp(func_name,"__builtin_molen_dummy")){
	  pragma_on_call=NULL;
	}
      }
    }
  }
}



static int check_pointer(int parm,tree pointer){
  while(parm--){
    if((pointer!=NULL)&&(dsp_allocated_buffers[parm].expr == pointer)){
      return parm;
    }
  }
  return -1;
}
static void insert_diopsis_api(tree call,tree pointer,tree_stmt_iterator* it,int parm,int dsp_parm_type,int len,int io){
  tree move=NULL;
  int flags=0;
  flags |= (is_const)?POINTER_READONLY:0;
  flags |= (is_wo)?POINTER_WRITEONLY:0;

  /* if no pointer, there is no parameter copy back */
  if(((dsp_parm_type>=DSP_PARM_INT) && (dsp_parm_type<=DSP_INTERNAL_POINTER) )&& (io==IO_EPILOGUE)) return;
  diopsis_print_debug(2,"===magic arg %d (%s):====\n",parm+1,((io==IO_EPILOGUE)?"epilogue":"prologue"));
  /*
  if(((dsp_parm_type>=DSP_PARM_P_INT) && (dsp_parm_type<=DSP_PARM_P_VLONG) )){
    int argument = check_pointer(parm,pointer);
    if(argument>=0){
      trx_bytes+=4;
      move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_INT], 2, build_int_cst (integer_type_node, pointer_int_arg_advance), build_int_cst (integer_type_node,dsp_allocated_buffers[argument].map_addr));
      if((io==IO_EPILOGUE)) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);

      diopsis_print_debug(2,"\tdsp pointer re-allocation of %d (pointer_int_arg %d, internal dsp off 0x%x, len %d)\n",argument+1,pointer_int_arg_advance,dsp_allocated_buffers[argument].map_addr,len);

      pointer_int_arg_advance++;
      return;
    }
  }

*/
  dsp_allocated_buffers[parm].expr=pointer;
  dsp_allocated_buffers[parm].map_addr=-2;     
  if(((dsp_parm_type>=DSP_POINTER_INT) && (dsp_parm_type<DSP_INTERNAL_POINTER) )){
    int add = check_pointer(parm,pointer);
    if(add>=0){
      flags|=(POINTER_ALIAS | add);
      /*      debug_tree(pointer);*/
      diopsis_print_debug(2,"\t* arg %d is a replicated pointer of argument %d, possible optimization\n",parm+1,add);
    }
  }
    
 
  switch(dsp_parm_type){
  case DSP_ALLOCATE_POINTER:{
    trx_bytes+=4;

    move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_INT], 2, build_int_cst (integer_type_node, pointer_int_arg_advance), build_int_cst (integer_type_node,dsp_internal_memory_address));
    if((io==IO_EPILOGUE))
      tsi_link_after (it, move, TSI_SAME_STMT);
    diopsis_print_debug(2,"\tdsp pointer allocate (GPP STACK ALLOCATION pointer_int_arg %d, len %d)\n",pointer_int_arg_advance,len);
    dsp_allocated_buffers[parm].expr=pointer;
    dsp_allocated_buffers[parm].map_addr=dsp_internal_memory_address;
    dsp_internal_memory_address+=len;
    pointer_int_arg_advance++;
    break;
  }
  case DSP_INTERNAL_POINTER:{
    tree add=CALL_EXPR_ARG (call, parm);
    /*    tree data_addr_base=create_external_var_raw(integer_type_node,"__mdata_start");*/
    tree add_t=build2 (BIT_AND_EXPR,integer_type_node,add, build_int_cst (integer_type_node,0x1ffff));
    tree magic_add= build2 (RSHIFT_EXPR,integer_type_node,add_t, build_int_cst (integer_type_node,2));
    /*    debug_tree(magic_add);*/
    int_pointer++;
    move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_INT], 2, build_int_cst (integer_type_node, pointer_int_arg_advance), magic_add);

    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);

    diopsis_print_debug(2,"\tdsp internal pointer (pointer_int_arg %d)\n",pointer_int_arg_advance);
    trx_bytes+=4;
    pointer_int_arg_advance++;
    break;
  }
  case DSP_PARM_INT:{
    int_pointer++;
    move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_INT], 2, build_int_cst (integer_type_node, pointer_int_arg_advance), CALL_EXPR_ARG (call, parm));
    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);
    diopsis_print_debug(2,"\tint (pointer_int_arg %d)\n",pointer_int_arg_advance);
    pointer_int_arg_advance++;
    trx_bytes+=4;
    break;
  }
  case  DSP_POINTER_INT:{
    move =  build_call_expr (built_in_decls[BUILT_IN_DSP_POINTER_INT], 3, build_int_cst (integer_type_node, pointer_int_arg_advance), build_int_cst (integer_type_node, flags), CALL_EXPR_ARG (call, parm));
    tsi_link_before(it,move,TSI_SAME_STMT);

    diopsis_print_debug(2,"\t%s int* [external/internal] (pointer_int_arg %d)\n",pointer_print_type(flags),pointer_int_arg_advance);
    pointer_int_arg_advance++;
    trx_bytes+=4;
    pointer_op++;
    break;
  }
    
  case DSP_POINTER_FLOAT:{
    move =  build_call_expr (built_in_decls[BUILT_IN_DSP_POINTER_FLOAT], 3, build_int_cst (integer_type_node, pointer_int_arg_advance), build_int_cst (integer_type_node, flags), CALL_EXPR_ARG (call, parm));
    tsi_link_before(it,move,TSI_SAME_STMT);

    diopsis_print_debug(2,"\t%s float* [external/internal] (pointer_int_arg %d)\n",pointer_print_type(flags),pointer_int_arg_advance);
    pointer_int_arg_advance++;
    trx_bytes+=4;
    out_bytes+=(is_const)?0:4;
    pointer_op++;
    break;
  }
  case DSP_POINTER_DWORD:{
    move =  build_call_expr (built_in_decls[BUILT_IN_DSP_POINTER_DWORD], 3, build_int_cst (integer_type_node, pointer_int_arg_advance), build_int_cst (integer_type_node, flags), CALL_EXPR_ARG (call, parm));
    tsi_link_before(it,move,TSI_SAME_STMT);

    diopsis_print_debug(2,"\t%s tuknown* [external/internal] (pointer_int_arg %d)\n",pointer_print_type(flags),pointer_int_arg_advance);
    pointer_int_arg_advance++;
    trx_bytes+=4;
    out_bytes+=(is_const)?0:4;
    pointer_op++;
    break;
  }
  case DSP_PARM_LONG:{
    move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_LONG], 2, build_int_cst (integer_type_node, float_long_arg_advance), CALL_EXPR_ARG (call, parm));
    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);
    diopsis_print_debug(2,"\tlong (float_long_arg %d)\n",float_long_arg_advance);
    float_long_arg_advance+=2;
    trx_bytes+=4;
    break;
  }
  case DSP_PARM_FLOAT:{
    move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_FLOAT], 2, build_int_cst (integer_type_node, float_long_arg_advance), CALL_EXPR_ARG (call, parm));
    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);
    diopsis_print_debug(2,"\tfloat (float_long_arg %d)\n",float_long_arg_advance);
    float_long_arg_advance+=2;
    trx_bytes+=4;
    break;
  }

  case DSP_PARM_CFLOAT:{
    move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_CFLOAT], 2, build_int_cst (integer_type_node, float_long_arg_advance), CALL_EXPR_ARG (call, parm));
    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);
    diopsis_print_debug(2,"\tcfloat (float_long_arg %d-%d)\n",float_long_arg_advance,float_long_arg_advance+1);
    float_long_arg_advance+=2;
    trx_bytes+=8;
    break;
  }

  case DSP_PARM_VFLOAT:{
    move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_VFLOAT], 2, build_int_cst (integer_type_node, float_long_arg_advance), CALL_EXPR_ARG (call, parm));
    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);
    diopsis_print_debug(2,"\tvfloat (float_long_arg %d-%d)\n",float_long_arg_advance,float_long_arg_advance+1);
    float_long_arg_advance+=2;
    trx_bytes+=8;
    break;
  }

  case DSP_PARM_CLONG:{
    move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_CLONG], 2, build_int_cst (integer_type_node, float_long_arg_advance), CALL_EXPR_ARG (call, parm));
    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);
    diopsis_print_debug(2,"\tclong (float_long_arg %d-%d)\n",float_long_arg_advance,float_long_arg_advance+1);
    float_long_arg_advance+=2;
    trx_bytes+=8;
    break;
  }
  case DSP_PARM_VLONG:{
    move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_VLONG], 2, build_int_cst (integer_type_node, float_long_arg_advance), CALL_EXPR_ARG (call, parm));
    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);
    diopsis_print_debug(2,"\tvlong (float_long_arg %d-%d)\n",float_long_arg_advance,float_long_arg_advance+1);
    float_long_arg_advance+=2;
    trx_bytes+=8;
    break;
  }
#if 0
  case DSP_ARRAY:{
    trx_bytes+=4;
    trx_bytes+=len;
    out_bytes+=(is_const)?0:4;
    if(io==IO_PROLOGUE){

      move =  build_call_expr (built_in_decls[BUILT_IN_DSP_ARRAY_WRITE], 4, build_int_cst (integer_type_node, pointer_int_arg_advance), build_int_cst (integer_type_node, dsp_internal_memory_address),CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len));
      
      dsp_allocated_buffers[parm].expr=pointer;
      dsp_allocated_buffers[parm].map_addr=dsp_internal_memory_address;
      dsp_internal_memory_address+=len;

    } else {
      move =  build_call_expr (built_in_decls[BUILT_IN_DSP_ARRAY_READ], 3, build_int_cst (integer_type_node, dsp_allocated_buffers[parm]), CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len));
    }
    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);
    pointer_int_arg_advance++;
    diopsis_print_debug(2,"\tarray [%d]\n",len);
    break;
  }
#endif
  case DSP_PARM_P_INT:{
    trx_bytes+=4;
    trx_bytes+=4*len;
    out_bytes+=(is_const)?0:4;
    pointer_op++;
    if(io==IO_PROLOGUE){
      move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_WRITE_INT], 4, build_int_cst (integer_type_node, pointer_int_arg_advance), build_int_cst (integer_type_node, flags),CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len));
      diopsis_print_debug(2,"\tint* [in %d] (pointer_int_arg %d, STACK ALLOCATION)\n",len,pointer_int_arg_advance);
      
      dsp_allocated_buffers[parm].expr=pointer;
      dsp_allocated_buffers[parm].map_addr=dsp_internal_memory_address;
      dsp_internal_memory_address+=len;

    } /*else {
	move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_READ_INT], 3, build_int_cst (integer_type_node, dsp_allocated_buffers[parm]), CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len<<2));
	diopsis_print_debug(2,"\tint* [out %d] (internal dsp off 0x%x)\n",len,dsp_allocated_buffers[parm]);
	}*/
    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);
      
    
    pointer_int_arg_advance++;

    break;
  }

  case DSP_PARM_P_FLOAT:{
    trx_bytes+=4;
    trx_bytes+=4*len;
    out_bytes+=(is_const)?0:4;
    pointer_op++;
    if(io==IO_PROLOGUE){

      move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_WRITE_FLOAT], 4, build_int_cst (integer_type_node, pointer_int_arg_advance), build_int_cst (integer_type_node, flags), CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len));
      diopsis_print_debug(2,"\tfloat* [in %d] (pointer_int_arg %d, GPP STACK ALLOCATION)\n",len,pointer_int_arg_advance); 

      dsp_allocated_buffers[parm].expr=pointer;
      dsp_allocated_buffers[parm].map_addr=dsp_internal_memory_address;
      dsp_internal_memory_address+=len;
    } /*
	else {
	move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_READ_FLOAT], 3, build_int_cst (integer_type_node, dsp_allocated_buffers[parm]), CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len<<2));
	diopsis_print_debug(2,"\tfloat* [out %d] (internal dsp off 0x%x)\n",len,dsp_allocated_buffers[parm]);
	} */
    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);
      
    /*    debug_tree(move);*/
    pointer_int_arg_advance++;

    break;
  }

  case DSP_PARM_P_LONG:{
    trx_bytes+=4;
    trx_bytes+=4*len;
    out_bytes+=(is_const)?0:4;
    pointer_op++;
    if(io==IO_PROLOGUE){

      move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_WRITE_LONG], 4, build_int_cst (integer_type_node, pointer_int_arg_advance), build_int_cst (integer_type_node, flags), CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len));
      diopsis_print_debug(2,"\tlong* [in %d] (pointer_int_arg %d, GPP STACK ALLOCATION)\n",len,pointer_int_arg_advance);
     
      dsp_allocated_buffers[parm].expr=pointer;
      dsp_allocated_buffers[parm].map_addr=dsp_internal_memory_address;
      dsp_internal_memory_address+=len;
    } /*else {
	move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_READ_LONG], 3, build_int_cst (integer_type_node, dsp_allocated_buffers[parm]), CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len<<2));
	diopsis_print_debug(2,"\tlong* [out %d] (internal dsp off 0x%x)\n",len,dsp_allocated_buffers[parm]);
	}*/
    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);
      
    pointer_int_arg_advance++;
    diopsis_print_debug(2,"\tlong* [%d]\n",len);
    break;
  }
  case DSP_PARM_P_CFLOAT:{
    trx_bytes+=4;
    trx_bytes+=4*len;
    out_bytes+=(is_const)?0:4;
    pointer_op++;

    if(io==IO_PROLOGUE){

      move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_WRITE_CFLOAT], 4, build_int_cst (integer_type_node, pointer_int_arg_advance), build_int_cst (integer_type_node, flags), CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len));
      diopsis_print_debug(2,"\tcfloat* [in %d] (pointer_int_arg %d, GPP STACK ALLOCATION)\n",len,pointer_int_arg_advance);
     
      dsp_allocated_buffers[parm].expr=pointer;
      dsp_allocated_buffers[parm].map_addr=dsp_internal_memory_address;
      dsp_internal_memory_address+=len;
    } /*else {
	move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_READ_CFLOAT], 3, build_int_cst (integer_type_node, dsp_allocated_buffers[parm]), CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len<<2));
	diopsis_print_debug(2,"\tcfloat* [out %d] (internal dsp off 0x%x)\n",len,dsp_allocated_buffers[parm]);
	}*/
    if(io==IO_EPILOGUE) {
      tsi_link_after(it,move,TSI_SAME_STMT); 
    } else {
      tsi_link_before(it,move,TSI_SAME_STMT);
    }
      
    pointer_int_arg_advance++;

    break;
  }
  case DSP_PARM_P_VFLOAT:{
    trx_bytes+=4;
    trx_bytes+=4*len;
    out_bytes+=(is_const)?0:4;
    pointer_op++;
    if(io==IO_PROLOGUE){

      move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_WRITE_VFLOAT], 4, build_int_cst (integer_type_node, pointer_int_arg_advance), build_int_cst (integer_type_node, flags), CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len));
      diopsis_print_debug(2,"\tvfloat* [in %d] (pointer_int_arg %d, GPP STACK ALLOCATION)\n",len,pointer_int_arg_advance);
    
      dsp_allocated_buffers[parm].expr=pointer;
      dsp_allocated_buffers[parm].map_addr=dsp_internal_memory_address;
      dsp_internal_memory_address+=len;
    } 
    /*  else {
	move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_READ_VFLOAT], 3, build_int_cst (integer_type_node, dsp_allocated_buffers[parm]), CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len<<2));
	diopsis_print_debug(2,"\tvfloat* [out %d] (internal dsp off 0x%x)\n",len,dsp_allocated_buffers[parm]);
	}*/
    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);
      
    pointer_int_arg_advance++;

    break;
  }
  case DSP_PARM_P_CLONG:{
    trx_bytes+=4;
    trx_bytes+=4*len;
    out_bytes+=(is_const)?0:4;
    pointer_op++;
    if(io==IO_PROLOGUE){

      move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_WRITE_CLONG], 4, build_int_cst (integer_type_node, pointer_int_arg_advance), build_int_cst (integer_type_node, flags), CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len));
      diopsis_print_debug(2,"\tclong* [in %d] (pointer_int_arg %d, GPP STACK ALLOCATION)\n",len,pointer_int_arg_advance);
     
      dsp_allocated_buffers[parm].expr=pointer;
      dsp_allocated_buffers[parm].map_addr=dsp_internal_memory_address;
      dsp_internal_memory_address+=len;
    } /*
	else {
	move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_READ_CLONG], 3, build_int_cst (integer_type_node, dsp_allocated_buffers[parm]), CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len<<2));
	diopsis_print_debug(2,"\tclong* [out %d] (internal dsp off 0x%x)\n",len,dsp_allocated_buffers[parm]);
	}*/
    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);
     
    pointer_int_arg_advance++;

    break;
  }
  case DSP_PARM_P_VLONG:{
    trx_bytes+=4;
    trx_bytes+=4*len;
    pointer_op++;
    out_bytes+=(is_const)?0:4;
    if(io==IO_PROLOGUE){

      move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_WRITE_VLONG], 4, build_int_cst (integer_type_node, pointer_int_arg_advance), build_int_cst (integer_type_node, flags),CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len));
      diopsis_print_debug(2,"\tvlong* [in %d] (pointer_int_arg %d, GPP STACK ALLOCATION)\n",len,pointer_int_arg_advance);
      
      dsp_allocated_buffers[parm].expr=pointer;
      dsp_allocated_buffers[parm].map_addr=dsp_internal_memory_address;
      dsp_internal_memory_address+=len; 
    } /* 
	 else {
	 move =  build_call_expr (built_in_decls[BUILT_IN_DSP_PARM_READ_VLONG], 3, build_int_cst (integer_type_node, dsp_allocated_buffers[parm]), CALL_EXPR_ARG (call, parm),build_int_cst (integer_type_node,len<<2));
	 diopsis_print_debug(2,"\tvlong* [out %d] (internal dsp off 0x%x)\n",len,dsp_allocated_buffers[parm]);
	 }*/
    if(io==IO_EPILOGUE) tsi_link_after(it,move,TSI_SAME_STMT); else tsi_link_before(it,move,TSI_SAME_STMT);
     
    pointer_int_arg_advance++;

    break;
  }
  default:
    gcc_unreachable();
  }
  diopsis_print_debug(2,"=== ===\n");

}

static void expand_parameters(tree call,tree_stmt_iterator* it,unsigned read_only_parm,int io){
  call_expr_arg_iterator iter;
  tree arg;
  int parm=0;
  tree decl=get_callee_fndecl (call);
  trx_bytes=0;
  out_bytes=0;
  FOR_EACH_CALL_EXPR_ARG (arg, iter, call){
    tree stmt_type=TREE_TYPE(arg);
    int dsp_type_func=DSP_PARM_LIMIT;    
    tree inner_type=NULL;

    is_wo = write_only_parm &(1<<parm);
    is_const = read_only_parm&(1<<parm);
    switch(TREE_CODE(arg)){
    case ADDR_EXPR:{
      tree op=TREE_OPERAND(arg,0);
      tree op_type=TREE_TYPE(op);
      int arr_size=0;
      int offset=0;
      tree attr;
      tree vect;
      /* skip pointer type */
      inner_type=TREE_TYPE(stmt_type);

      if( (attr=lookup_attribute("section",DECL_ATTRIBUTES(op)))){
	const char *name =  TREE_STRING_POINTER(TREE_VALUE(TREE_VALUE(attr)));
	if(strstr(name,"dsp_")){
	  is_internal_memory=(1<<parm);    
	  diopsis_print_debug(2,"* attribute %s\n",name);
	  /* pass the pointer directly without copies*/
	  if(io==IO_PROLOGUE){

	    insert_diopsis_api(call,NULL,it,parm,DSP_INTERNAL_POINTER,0,IO_PROLOGUE);	   
	  }
	  parm++;
	  continue;
	}


      }
      
      /*      debug_tree(arg);*/

      /*diopsis_print_debug(2,"type:");
	debug_tree(stmt_type);
	diopsis_print_debug(2,"type/type:");
	debug_tree(TREE_TYPE(stmt_type));
	diopsis_print_debug(2,"op:");
	debug_tree(op);
      */
      switch(TREE_CODE(inner_type)){
      case VECTOR_TYPE:
	/*	is_volatile = TREE_THIS_VOLATILE(inner_type);*/
	inner_type = TREE_TYPE(inner_type);
	if(TREE_CODE(inner_type)==REAL_TYPE){
	  dsp_type_func= DSP_PARM_P_VFLOAT;
	} else if(TREE_CODE(inner_type)==INTEGER_TYPE){
	  dsp_type_func= DSP_PARM_P_VLONG;
	} else {
	  diopsis_error(call,"arg %d vector type not handled\n",parm+1);
	  debug_tree(inner_type);
	  gcc_unreachable ();
	}
	break;
      case RECORD_TYPE:{
	tree f=TYPE_FIELDS (inner_type);
	int fields=0;
	int treecode;
	switch(TREE_CODE(TREE_TYPE(f))){
	case REAL_TYPE:
	  dsp_type_func= DSP_PARM_P_FLOAT;
	  treecode = REAL_TYPE;
	  break;
	case INTEGER_TYPE:
	  treecode = INTEGER_TYPE;
	  dsp_type_func= DSP_PARM_P_INT;
	  break;
	default:
	  debug_tree(f);
	  diopsis_error(call,"arg %d pointer to record type NOT HANDLED\n",parm+1);
	  gcc_unreachable ();
	}
	if((fields=check_omogenous(f,treecode))==0){
	  debug_tree(f);
	  diopsis_error(call,"arg %d record does not contain omegenous types\n",parm+1);
	  gcc_unreachable ();
	}
	diopsis_print_debug(2,"arg %d is a record with %d omogenous items\n",parm+1,fields);
      }
	break;
      case COMPLEX_TYPE:
	/*	is_volatile = TREE_THIS_VOLATILE(inner_type);*/
	inner_type = TREE_TYPE(inner_type);
	if(TREE_CODE(inner_type)==REAL_TYPE){
	  dsp_type_func= DSP_PARM_P_CFLOAT;

	} else if(TREE_CODE(inner_type)==INTEGER_TYPE){

	  dsp_type_func= DSP_PARM_P_CLONG;
	} else {
	  debug_tree(inner_type);
	  diopsis_error(call,"arg %d complex type not handled\n",parm+1);
	  gcc_unreachable ();
	}
	break;
	
      case REAL_TYPE:
	/*	is_volatile = TREE_THIS_VOLATILE(inner_type);*/
	dsp_type_func= DSP_PARM_P_FLOAT;
	break;

      case INTEGER_TYPE:
	/*	is_volatile = TREE_THIS_VOLATILE(inner_type);*/
	dsp_type_func= DSP_PARM_P_INT;
	break;


      default:
	debug_tree(inner_type);
	diopsis_error(call,"arg %d pointer type not handled\n",parm+1);
	gcc_unreachable();
      } /* inner type*/

      vect = op_type;
      if(TREE_CODE(op) == ARRAY_REF){
	vect=TREE_TYPE(TREE_OPERAND(op,0));
	offset = TREE_INT_CST_LOW (TREE_OPERAND(op,1));
	arr_size = TREE_INT_CST_LOW (TYPE_SIZE_UNIT(vect))/4;
	diopsis_print_debug(2,"\"%s\" arg %d is an array reference of an array of %d elements starting from %d\n",IDENTIFIER_POINTER(DECL_NAME(decl)),parm+1,arr_size,offset);
      } else if(TREE_CODE(op_type)==ARRAY_TYPE){
	  arr_size = TREE_INT_CST_LOW (TYPE_SIZE_UNIT(vect))/4;    
      } else if(TREE_CODE(op) == VAR_DECL){
	//	debug_tree(op);
	//	vect=TREE_TYPE(TREE_OPERAND(op,0));
	arr_size = TREE_INT_CST_LOW (TYPE_SIZE_UNIT(vect))/4;    
	diopsis_print_debug(2,"\"%s\" arg %d is a reference of a variable of size %d \n",IDENTIFIER_POINTER(DECL_NAME(decl)),parm+1,arr_size);
      } else {
	debug_tree(arg);
	diopsis_error(call,"\"%s\" remote DSP call,  cannot handle arg %d\n",IDENTIFIER_POINTER(DECL_NAME(decl)),parm+1);
	return;
      }
      
      if(arr_size>16000){
	diopsis_error(call,"\"%s\" arg %d, too big array (%d)\n",IDENTIFIER_POINTER(DECL_NAME(decl)),parm+1,arr_size);
      }
      arr_size = TREE_INT_CST_LOW (TYPE_SIZE_UNIT(vect))/4 -offset;    
      if(read_only_parm&(1<<parm)){

	/*	diopsis_print_debug(2,"READ ONLY VECTOR ");*/
	if(io==IO_PROLOGUE){
	  insert_diopsis_api(call,op,it,parm,dsp_type_func,arr_size,io);
	}
	
      } else if(is_wo){
	/*	diopsis_print_debug(2,"WRITE ONLY VECTOR ");*/
	if(io==IO_EPILOGUE){
	  insert_diopsis_api(call,op,it,parm,dsp_type_func,arr_size,io);	  
	} else {
	  insert_diopsis_api(call,op,it,parm,DSP_ALLOCATE_POINTER,arr_size,io);	  
	}
      } else {
	insert_diopsis_api(call,op,it,parm,dsp_type_func,arr_size,io);
      }
      /*      diopsis_print_debug(2,"array [%d]\n",arr_size);*/
      /*		  debug_tree(TYPE_SIZE(op_type));*/
      
      break;
    }
    case INTEGER_CST:{
      int c_int;
      c_int = TREE_INT_CST_LOW(arg);
      if(parm_long_mask&(1<<parm)){
	dsp_type_func= DSP_PARM_LONG;
      } else {
	if(abs(c_int)>0xffffU){
	  diopsis_error(call,"arg %d integer constant \"0x%x\" to big to be passed to DSP as 'int' try as long\n",parm+1,c_int);

	  gcc_unreachable (); 
	}
	dsp_type_func= DSP_PARM_INT;
      }
      

      insert_diopsis_api(call,NULL,it,parm,dsp_type_func,0,io);
      /*      diopsis_print_debug(2,"* integer constant 0x%x\n",c_int);*/

      break;
    }
    case REAL_CST:{
      char string[100];
      REAL_VALUE_TYPE d=TREE_REAL_CST(arg);
      real_to_decimal (string, &d, sizeof (string), 0, 1);
      /*      diopsis_print_debug(2,"* is real constant %s\n",string);*/
      dsp_type_func= DSP_PARM_FLOAT;
      insert_diopsis_api(call,NULL,it,parm,dsp_type_func,0,io);
      break;
    }
    case VAR_DECL:
    case PARM_DECL:
    case SSA_NAME:
      switch(TREE_CODE(stmt_type)){
      case INTEGER_TYPE:
	/*	debug_tree(stmt_type);*/
	/*	if((stmt_type == long_unsigned_type_node) || (stmt_type == long_integer_type_node)){ 
		dsp_type_func= DSP_PARM_LONG;
		} else {
		dsp_type_func= DSP_PARM_INT;
		}*/
	if(parm_long_mask&(1<<parm)){
	  dsp_type_func= DSP_PARM_LONG;
	} else {
	  dsp_type_func= DSP_PARM_INT;
	}

	insert_diopsis_api(call,NULL,it,parm,dsp_type_func,0,io);
	break;
      case REAL_TYPE:
	dsp_type_func= DSP_PARM_FLOAT;
	insert_diopsis_api(call,NULL,it,parm,dsp_type_func,0,io);
	break;
      case COMPLEX_TYPE:
	inner_type = TREE_TYPE(stmt_type);
	if(TREE_CODE(inner_type)==REAL_TYPE){
	  dsp_type_func= DSP_PARM_CFLOAT;
	} else if(TREE_CODE(inner_type)==INTEGER_TYPE){
	  dsp_type_func= DSP_PARM_CLONG;
	} else {
	  diopsis_error(arg,"arg %d complex type not handled\n",parm+1);

	  gcc_unreachable ();
	}
	insert_diopsis_api(call,NULL,it,parm,dsp_type_func,0,io);
	break;
      case VECTOR_TYPE:
	inner_type = TREE_TYPE(stmt_type);
	if(TREE_CODE(inner_type)==REAL_TYPE){
	  dsp_type_func= DSP_PARM_VFLOAT;
	} else if(TREE_CODE(inner_type)==INTEGER_TYPE){
	  dsp_type_func= DSP_PARM_VLONG;
	} else {
	  diopsis_error(arg,"arg %d vector type not handled\n",parm+1);
	  
	  gcc_unreachable ();
	}
	insert_diopsis_api(call,NULL,it,parm,dsp_type_func,0,io);
	break;
      case POINTER_TYPE:
	inner_type = TREE_TYPE(stmt_type);
	/*	debug_tree(stmt_type);*/
	switch(TREE_CODE(inner_type))
	  {

	  case COMPLEX_TYPE:
	  case VECTOR_TYPE:{
	    int len=0;
	    tree vectret;
	    if(TREE_CODE(TREE_TYPE(inner_type)) == REAL_TYPE){
	      dsp_type_func= DSP_POINTER_FLOAT;
	    } else {
	      dsp_type_func= DSP_POINTER_INT;
	    }
	    len=trace_pointer(arg,it,&vectret);
	    if(len>0){
	      insert_diopsis_api(call,vectret,it,parm,(dsp_type_func==DSP_POINTER_FLOAT)?DSP_PARM_P_FLOAT:DSP_PARM_P_INT,len,io);
	      parm++;
	      continue;
	    }
	    diopsis_warn(call, "\"%s\" remote DSP call may fail if pointer (vector/complex type) arg %d is NOT allocated in the physical memory (allocated with hmalloc or hmalloc_f)\n",IDENTIFIER_POINTER(DECL_NAME(decl)),parm+1);
	    
	    break;
	  }
	  case INTEGER_TYPE:
	    dsp_type_func= DSP_POINTER_INT;
	    
	    break;
	  case REAL_TYPE:{
	    int len=0;
	    tree vectret;
	    dsp_type_func= DSP_POINTER_FLOAT;
	    len = trace_pointer(arg,it,&vectret);
	    if(len>0){
	      insert_diopsis_api(call,vectret,it,parm,(dsp_type_func==DSP_POINTER_FLOAT)?DSP_PARM_P_FLOAT:DSP_PARM_P_INT,len,io);
	      parm++;
	      continue;
	    }
	    
	    diopsis_warn(call, "\"%s\" remote DSP call may fail if float pointer (real type) arg %d is NOT allocated in the physical memory (allocated with hmalloc or hmalloc_f)\n",IDENTIFIER_POINTER(DECL_NAME(decl)),parm+1);

	    break;
	  }
	  default:
	    dsp_type_func= DSP_POINTER_DWORD;
	    diopsis_warn (call, "in remote DSP call \"%s\" arg type %d is not supported, remote call will fail if pointer is NOT allocated in the DSP internal memory\n",IDENTIFIER_POINTER(DECL_NAME(decl)),parm+1);
	  }
	/*	debug_tree(arg);*/
	insert_diopsis_api(call,arg,it,parm,dsp_type_func,0,io);
	break;
	
      default:
	diopsis_error(arg,"arg %d type not handled\n",parm+1);

	debug_tree(arg);
	gcc_unreachable();
	break;
      }

      break;
    default:
      diopsis_error(arg,"arg %d type not handled\n",parm+1);
      debug_tree(arg);
      gcc_unreachable();
      return;
    }

    parm++;
  }
  
}
/********************* OMP *******************/

static int dsp_parallel=0;
static void diopsis_omp_sections_opt_1(tree r, tree_stmt_iterator insert_pos);

static void diopsis_omp_sections_opt(tree r) {

  
  tree_stmt_iterator tsi;
  int cnt=0;
  if(TREE_CODE(r)==BIND_EXPR) {
    r = BIND_EXPR_BODY(r);
  }
  for (tsi = tsi_start (r); !tsi_end_p (tsi);) {
    tree st = tsi_stmt (tsi);
	
    /*	  fprintf(stderr,"=================== %d =============\n",cnt++)
	  debug_tree(st);
	  fprintf(stderr,"=====================================\n");
    */
	
    if(TREE_CODE (st) == OMP_SECTIONS) {
      diopsis_print_debug(2,"* hArtes parallel (sections) optimization at %d\n",EXPR_LINENO(st));
      diopsis_omp_sections_opt_1(st, tsi);
      tsi_delink(&tsi);
      continue;
    } else if(TREE_CODE (st) == OMP_PARALLEL) {
      dsp_parallel=0;
      diopsis_print_debug(2,"* hArtes parallel (parallel) optimization at %d\n",EXPR_LINENO(st));
      diopsis_omp_sections_opt(OMP_PARALLEL_BODY(st));
      tsi_link_before(&tsi,OMP_PARALLEL_BODY(st),TSI_SAME_STMT);
      tsi_delink(&tsi);  
      continue;
      
    } else if(TREE_CODE (st) == COND_EXPR) {      
      diopsis_omp_sections_opt(COND_EXPR_THEN(st));
      diopsis_omp_sections_opt(COND_EXPR_ELSE(st));

    }
    tsi_next (&tsi);
  }
}



static void diopsis_omp_sections_opt_1(tree r, tree_stmt_iterator insert_pos) {
  tree_stmt_iterator tsi,endsections,post_exec_it;

  for (tsi = tsi_start (OMP_SECTIONS_BODY (r)); !tsi_end_p (tsi);  tsi_next (&tsi)) {
    tree st;
    st = tsi_stmt (tsi);
    
    if(TREE_CODE (st) == OMP_SECTION) {
      diopsis_omp_sections_opt(OMP_SECTION_BODY (st));
    }
    
  }
  endsections = insert_pos;
  tsi_next (&endsections);
  // Now we know there are no nested sections
  for (tsi = tsi_start (OMP_SECTIONS_BODY (r)); !tsi_end_p (tsi);  tsi_next (&tsi)) {
    tree st;
    st = tsi_stmt (tsi);
    if(TREE_CODE (st) == OMP_SECTION) {
      int sect_stm_num = 0;
      int cnt;
      int last_break = -1;
      tree t1,tm;
      tree_stmt_iterator tsi_lb;
      int isdsp=0;
      t1 = built_in_decls[BUILT_IN_HARTES_RPC_END];
      tm = built_in_decls[BUILT_IN_FPGA_BREAK];

      /*	    fprintf(stderr,"+++++++++++++++++++++++++++++++++++++++++ \n");

      
       */
      for (tsi_lb = tsi_start (OMP_SECTION_BODY (st)); !tsi_end_p (tsi_lb);  tsi_next (&tsi_lb)) {
	tree t2;
	tree call_lb = get_call_expr_in (tsi_stmt(tsi_lb));

	
	
	if(call_lb) {
	  
	  t2 = get_callee_fndecl (call_lb);
	  if(t1 == t2) {
	    post_exec_it = tsi_lb;
	    last_break = sect_stm_num;
	    isdsp=1;
	    /* 
	       if(dsp_parallel){
	       diopsis_warn(st,"DSP doesn't support multi threading (more than one DSP task in a #pragma sections)\n");
	       last_break=-1;
	       
	       }
	       dsp_parallel++;
	    */
	  }
	  if(tm==t2){
	    post_exec_it = tsi_lb;
	    last_break = sect_stm_num;
	    isdsp=0;
	  }
	}
	sect_stm_num++;
	
	/* 
	   If there are labels here it means there is some control flow -> the execute might not
	   be always executed -> abort the optimisation
	   TODO: check there are no more cases
	*/
	if(TREE_CODE(tsi_stmt(tsi_lb))==LABEL_EXPR || TREE_CODE(tsi_stmt(tsi_lb))==GOTO_EXPR) {
	 
	  diopsis_warn (tsi_stmt(tsi_lb),"flow control not supported inside section, disabling omp optimization\n");
	  last_break = -1;
	  return;
	}
      }

      /*    
	    if(last_break>0){
	    tree renam= build_call_expr (built_in_decls[BUILT_IN_DSP_POST_EXEC_WAIT], 0);
	    tsi_link_before(&post_exec_it, renam, TSI_SAME_STMT);
	    tsi_delink(&post_exec_it);
	    } 
      */
      // We add before the OMP_SECTIONS (insert_pos) all the trees before the last BREAK.
      cnt = 0;
      for (tsi_lb = tsi_start (OMP_SECTION_BODY (st)); /*(last_break>-1) &&*/ !tsi_end_p (tsi_lb);tsi_delink(&tsi_lb)) {
	if(cnt<last_break){
	  /*  
	      fprintf(stderr," ----- moving %d: \n",cnt);
	      debug_tree(tsi_stmt(tsi_lb));
	      fprintf(stderr," ----- up in : \n");
	      debug_tree( tsi_stmt(insert_pos));
	      fprintf(stderr," ------------\n");
	  */
	  tree tcall = get_call_expr_in(tsi_stmt(tsi_lb));
	  if(tcall && isdsp && (get_callee_fndecl (tcall) == built_in_decls[BUILT_IN_DSP_EXEC]) && (cnt == (last_break-1))){
	    /* rename it.. use the no blocking function */
	    tree renam= build_call_expr (built_in_decls[BUILT_IN_DSP_EXEC_NOWAIT], 1, CALL_EXPR_ARG (tsi_stmt(tsi_lb),0));
	    tsi_link_before(&insert_pos, renam, TSI_SAME_STMT);
	  } else {
	    tsi_link_before(&insert_pos, tsi_stmt(tsi_lb), TSI_SAME_STMT);
	  }

	  //tsi_delink(&tsi_lb);
	} else {

	  //	  if(!tsi_end_p(endsections)){
	  tsi_link_before(&endsections, tsi_stmt(tsi_lb), TSI_SAME_STMT);
	  //	  tsi_delink(&tsi_lb);
	    //	  }
	}
	cnt++;
      }

    }
  } 
}

static unsigned int do_diopsis_replace_calls_tree_execute(void){
  if (diopsis == 0)                /* diopsis pass is disabled */
    return 0;
  diopsis_print_debug(10,"* diopsis pass tree \n");
  do_diopsis_replace_calls_tree(DECL_SAVED_TREE (current_function_decl),NULL);
}


struct tree_opt_pass pass_diopsis_replace_calls_tree = {
  "diopsis_replace_calls_tree",         /* name */
  NULL,                         /* gate */
  do_diopsis_replace_calls_tree_execute,        /* execute */
  NULL,                         /* sub */
  NULL,                         /* next */
  0,                            /* static_pass_number */
  TV_REST_OF_COMPILATION,       /* tv_id - used for timing information, see timevar.def */
  0,                     /* properties_required */
  0,                            /* properties_provided */
  0,                            /* properties_destroyed */
  0,                            /* todo_flags_start */
  TODO_dump_func | TODO_dump_cgraph,    /* todo_flags_finish - used to dump information if debug enabled */
  0                             /* letter */
};

static unsigned int do_hartes_parallel_opt_execute(void) {
  if (diopsis == 0)              /* diopsis pass is disabled */
    return 0;


  diopsis_omp_sections_opt(DECL_SAVED_TREE (current_function_decl));

  return 0;
}

struct tree_opt_pass pass_hartes_parallel_optimisation = {
  "hartes_parallel_optimisation",       /* name */
  NULL,                               /* gate */
  do_hartes_parallel_opt_execute,               /* execute */
  NULL,                               /* sub */
  NULL,                               /* next */
  0,                                  /* static_pass_number */
  TV_REST_OF_COMPILATION,             /* tv_id - used for timing information, see timevar.def */
  0,                                  /* properties_required */
  0,                                  /* properties_provided */
  0,                                  /* properties_destroyed */
  0,                                  /* todo_flags_start */
  TODO_dump_func,                     /* todo_flags_finish - used to dump information if debug enabled */
  0                                   /* letter */
};


/* Scan all the statements starting at STMT_P.  CTX contains context
   information about the OpenMP directives and clauses found during
   the scan.  */

