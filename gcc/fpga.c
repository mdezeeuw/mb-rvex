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
#include "errors.h"
#include "tm_p.h"
#include "fpga-config.h"
#include "fpga-pass-utils.h"
#include "hxml.h"

enum { DSPDEV, GPPDEV, FPGADEV, NDEVS};

typedef enum {IO_PROLOGUE, IO_EPILOGUE} io_type;

static int fpga_warn(tree expr, const char *msg, ...);
static int fpga_error(tree expr, const char *msg, ...);
int fpga_print_debug(char level, const char *msg, ...);

static unsigned int do_fpga_replace_calls_tree_execute(void);
static void do_fpga_replace_calls_tree(tree r,tree_stmt_iterator *tsi_parent);
static void replace_call_tree_tsi (tree_stmt_iterator *tsi);
static int is_param_const(tree decl, int arg);

// Variable used by hartes_stats
int trx_bytes;

struct tree_opt_pass pass_fpga_replace_calls_tree = {
    "fpga_replace_calls_tree",          /* name */
    NULL,                               /* gate */
    do_fpga_replace_calls_tree_execute, /* execute */
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

extern const char *fpga_configuration_file_name;

static unsigned int do_fpga_replace_calls_tree_execute(void) {
    if (fpga == 0)              /* fpga pass is disabled */
        return 0;

    fpga_print_debug (10, "FPGA PASS TREE\n");

    do_fpga_replace_calls_tree(DECL_SAVED_TREE (current_function_decl),NULL);
    return 0;
}


/*
  We redo the code in walk_stmt, because with that one we can't modify very easy the trees
 (the tsi is not updated for all cases, example omp section + conditional inside, the tsi
 will point to outside the omp section
*/

static void do_fpga_replace_calls_tree(tree r,tree_stmt_iterator *tsi_parent) {
    tree_stmt_iterator tsi;
    switch(TREE_CODE(r)) {
        case TRY_FINALLY_EXPR:
        case TRY_CATCH_EXPR:
            do_fpga_replace_calls_tree(TREE_OPERAND (r, 0), NULL);
            do_fpga_replace_calls_tree(TREE_OPERAND (r, 1), NULL);
            break;
        case CATCH_EXPR:
            do_fpga_replace_calls_tree(CATCH_BODY (r), NULL);
            break;
        case GIMPLE_MODIFY_STMT:
            replace_call_tree_tsi(tsi_parent);
            break;
        case BIND_EXPR:
            do_fpga_replace_calls_tree(BIND_EXPR_BODY(r),NULL);
            break;
        case STATEMENT_LIST:
            for (tsi = tsi_start (r); !tsi_end_p (tsi); tsi_end_p(tsi)?0:(tsi_next(&tsi),0) ) {
               tree st = tsi_stmt (tsi);
               do_fpga_replace_calls_tree(st,&tsi);
            }
            break;
        case OMP_PARALLEL:
            do_fpga_replace_calls_tree(OMP_PARALLEL_BODY (r),NULL);
            break;
        case OMP_SECTIONS:
          for (tsi = tsi_start (OMP_SECTIONS_BODY (r)); !tsi_end_p (tsi);  tsi_next (&tsi)) {
            tree st = tsi_stmt (tsi);
              if(TREE_CODE (st) == OMP_SECTION) {
                  do_fpga_replace_calls_tree(OMP_SECTION_BODY (st),NULL);
               }
          }
          break;
        case COND_EXPR:
            do_fpga_replace_calls_tree(COND_EXPR_THEN(r),NULL);
            do_fpga_replace_calls_tree(COND_EXPR_ELSE(r),NULL);
            break;
        case CALL_EXPR:
            replace_call_tree_tsi(tsi_parent);
            break;
        default:
            ;
    }
}

static void expand_parameters(tree decl, tree_stmt_iterator *tsi, tree call,io_type io_t, int fpga_id) {
  tree move;
  
  tree const fpga_movet = built_in_decls[BUILT_IN_FPGA_MOVET];
  tree const fpga_movet_addr = built_in_decls[BUILT_IN_FPGA_MOVET_ADDR];
  tree const fpga_movef_addr = built_in_decls[BUILT_IN_FPGA_MOVEF_ADDR];
  
  int i;
  tree t;
  for (i = 0; i < call_expr_nargs (call); i++) {
      tree arg = CALL_EXPR_ARG (call, i);
      tree arg_type=TREE_TYPE(arg);
      /* TODO the second parameter should be the size of the transfer. 
	  for things like arrays or addresses it shouldn't be 4. */
      
      switch(TREE_CODE(arg)) {
	case ADDR_EXPR:{
	  tree op=TREE_OPERAND(arg,0);
	  tree op_type=TREE_TYPE(op);
	  int arr_size=0;
	  int offset=0;
	  tree attr;
	  tree vect;
	  /* skip pointer type */
	  tree inner_type=TREE_TYPE(arg_type);

	  if( (attr=lookup_attribute("section",DECL_ATTRIBUTES(op)))){
	    const char *name =  TREE_STRING_POINTER(TREE_VALUE(TREE_VALUE(attr)));
	    if(strstr(name,".hpp_ccu_ram")){	      
	      fpga_print_debug(2,"* attribute %s\n",name);
	      /* pass the pointer directly without copies*/
	      if(io_t==IO_PROLOGUE){
		move = build_call_expr (fpga_movet, 3, build_int_cst (integer_type_node, fpga_id), build_int_cst (integer_type_node, i + hxml_get_start_input_xr (fpga_id)), arg);
		tsi_link_before (tsi, move, TSI_SAME_STMT);	   
	      }
	      continue;
	    }
	  }

	  switch(TREE_CODE(inner_type)){
	    case REAL_TYPE:
	    case INTEGER_TYPE:
	      break;
	    default:
	      debug_tree(inner_type);
	      fpga_error(call,"arg %d pointer type not handled\n",arg+1);
	      gcc_unreachable();
	  } /* inner type*/

	  vect = op_type;
	  if(TREE_CODE(op) == ARRAY_REF){
	    t=TREE_TYPE(TREE_OPERAND(op,0));
	    offset = TREE_INT_CST_LOW (TREE_OPERAND(op,1));
	    arr_size = TREE_INT_CST_LOW (TYPE_SIZE_UNIT(vect));
	    fpga_print_debug(2,"\"%s\" arg %d is an array reference of an array of %d elements starting from %d\n",IDENTIFIER_POINTER(DECL_NAME(decl)),i+1,arr_size,offset);
	    arr_size -= offset;
	  } else if(TREE_CODE(op_type)==ARRAY_TYPE){
	    arr_size = TREE_INT_CST_LOW (TYPE_SIZE_UNIT(vect));    
	  } else if(TREE_CODE(op) == VAR_DECL){
	    arr_size = TREE_INT_CST_LOW (TYPE_SIZE_UNIT(vect));    
	    fpga_print_debug(2,"\"%s\" arg %d is a reference of a variable of size %d \n",IDENTIFIER_POINTER(DECL_NAME(decl)),i+1,arr_size);
	  } else {
	    debug_tree(arg);
	    fpga_error(call,"\"%s\" remote FPGA call,  cannot handle arg %d\n",IDENTIFIER_POINTER(DECL_NAME(decl)),i+1);
	    return;
	  }
	  
	  trx_bytes += arr_size*4;
	  // TODO optimization for volatile
	  switch(io_t) {
	    case IO_PROLOGUE:
	      move = build_call_expr (fpga_movet_addr, 5, build_int_cst (integer_type_node, fpga_id), build_int_cst (integer_type_node, i + hxml_get_start_input_xr (fpga_id)), arg, build_int_cst (integer_type_node, arr_size), build_int_cst (integer_type_node, 1-is_param_const(decl,i)));
	      tsi_link_before (tsi, move, TSI_SAME_STMT);
	      break;
	    case IO_EPILOGUE:
	      if(is_param_const(decl,i)==0) {
		move = build_call_expr (fpga_movef_addr, 4, build_int_cst (integer_type_node, fpga_id), build_int_cst (integer_type_node, i + hxml_get_start_input_xr (fpga_id)), arg, build_int_cst (integer_type_node, arr_size));
		tsi_link_before (tsi, move, TSI_SAME_STMT);
	      }
	      break;
	  }

	  
	  break;
	}
	case VAR_DECL:
	case PARM_DECL:
	case SSA_NAME:	  
	  if(io_t==IO_PROLOGUE) {	    
	    switch(TREE_CODE(arg_type)){
	      case POINTER_TYPE:
		fpga_warn (call, "in remote FPGA call \"%s\" arg type pointer is not automatically supported, remote call will fail if pointer is NOT allocated in the FPGA internal memory\n",IDENTIFIER_POINTER(DECL_NAME(decl)),arg+1);
		move = build_call_expr (fpga_movet_addr, 5, build_int_cst (integer_type_node, fpga_id), build_int_cst (integer_type_node, i + hxml_get_start_input_xr (fpga_id)), arg, build_int_cst (integer_type_node, 0), build_int_cst (integer_type_node, 1-is_param_const(decl,i)));
		tsi_link_before (tsi, move, TSI_SAME_STMT);
		break;
	      default: {
	        trx_bytes +=4;
		move = build_call_expr (fpga_movet, 3, build_int_cst (integer_type_node, fpga_id), build_int_cst (integer_type_node, i + hxml_get_start_input_xr (fpga_id)), arg);
		tsi_link_before (tsi, move, TSI_SAME_STMT);
	      }
	    }
	    break;
	  } else if(io_t==IO_EPILOGUE && is_param_const(decl,i)==0)  {
	    switch(TREE_CODE(arg_type)){
	      case POINTER_TYPE:
		fpga_warn (call, "in remote FPGA call \"%s\" arg type pointer is not automatically supported, remote call will fail if pointer is NOT allocated in the FPGA internal memory\n",IDENTIFIER_POINTER(DECL_NAME(decl)),arg+1);
		move = build_call_expr (fpga_movef_addr, 4, build_int_cst (integer_type_node, fpga_id), build_int_cst (integer_type_node, i + hxml_get_start_input_xr (fpga_id)), arg, build_int_cst (integer_type_node, 0));
		tsi_link_before (tsi, move, TSI_SAME_STMT);
		break;
	      default: {
		/* Nothing to do */
	      }
	    }
	    break;
	  }
	default: {
	  fpga_print_debug (20, "Arg %d is %s\n", i,tree_code_name[(int) TREE_CODE(arg)]);
	  if(io_t==IO_PROLOGUE) {
	    trx_bytes +=4;
	    move = build_call_expr (fpga_movet, 3, build_int_cst (integer_type_node, fpga_id), build_int_cst (integer_type_node, i + hxml_get_start_input_xr (fpga_id)), arg);
	    tsi_link_before (tsi, move, TSI_SAME_STMT);
	  }
	}
      }
  }
}

int fpga_id_dummy = -1;

static void replace_call_tree_tsi (tree_stmt_iterator *tsi)
{
    tree const fpga_set = built_in_decls[BUILT_IN_FPGA_SET];
    tree const fpga_movef = built_in_decls[BUILT_IN_FPGA_MOVEF];
    tree const fpga_execute = built_in_decls[BUILT_IN_FPGA_EXECUTE];
    tree const fpga_break = built_in_decls[BUILT_IN_FPGA_BREAK];

    tree fpga_return_adress_expression;

    tree fpga_return_int = create_tmp_var (integer_type_node, "__fpga_return_int");
    tree fpga_return_float = create_tmp_var (float_type_node, "__fpga_return_float");
    tree fpga_return;

    tree decl, attr;
    tree stmt = tsi_stmt (*tsi);
    tree call = get_call_expr_in (stmt);

    if (call && (decl = get_callee_fndecl (call))) {
        int fpga_id = -1;
        attr = lookup_attribute ("call_hw", DECL_ATTRIBUTES (decl));

        if(attr!=NULL) {
	   /* 
	     changed call_hw tree to support new attributes
	   */
	  tree t=TREE_VALUE (attr);
	  hartes_call_hw_get_params(TREE_STRING_POINTER(t),&fpga_id,0,0,0);
	  /*            fpga_id = TREE_INT_CST_LOW (TREE_VALUE (attr));*/
        }

        if(strcmp(IDENTIFIER_POINTER(DECL_NAME(decl)), "__builtin_molen_dummy")==0) {
           tree arg = CALL_EXPR_ARG (call, 0);
	   hartes_call_hw_get_params(TREE_STRING_POINTER(arg),&fpga_id,0,0,0);
           hxml_read_xml ();
           if((hxml_get_functional_component_type (fpga_id) == FPGA)) {
               tsi_delink (tsi);
               tsi_prev (tsi);
           } else {
               fpga_id = -1;
           }

           if(fpga_id_dummy!=-1) {
               fpga_error(stmt,"\"%s\" two molen_dummy found.\n",IDENTIFIER_POINTER(DECL_NAME(decl)));
           }
           fpga_id_dummy = fpga_id;
           return;
        }

        if(fpga_id_dummy!=-1) {
           fpga_id = fpga_id_dummy;
           fpga_id_dummy = -1;
        }

        if (fpga_id!=-1) {
            tree move;
            tree call_fpga_execute, call_fpga_break;

            tree new_call;
            tree stats;

            tree t;
            char symbol_name[1024];

            const char *func_name=IDENTIFIER_POINTER(DECL_NAME(decl));

            if (hartes_configuration_xml == 1) {
                hxml_read_xml ();
            }

            if (hxml_get_functional_component_type (fpga_id) != FPGA) {
                return;
            }

            if (fpga_return == NULL) {
                fpga_return = create_tmp_var (integer_type_node, "__fpga_return");
            }

            fpga_print_debug (8, "Found fpga call %d\n", fpga_id);

            if(fpga_call_generate_set) {
               snprintf(symbol_name,1024,"%s_%d__fpga_text__",func_name,fpga_id);
               fpga_print_debug(10, "Generating set for symbol %s\n",symbol_name);

               t = build_decl (VAR_DECL, get_identifier (symbol_name), ptr_type_node);
               TREE_STATIC (t) = 1;
               TREE_PUBLIC (t) = 1;
               DECL_EXTERNAL (t) = 1;
               TREE_USED (t) = 1;
               TREE_THIS_VOLATILE (t) = 1;
               DECL_ARTIFICIAL (t) = 1;
               DECL_IGNORED_P (t) = 1;

               new_call = build_call_expr (fpga_set, 1, build_fold_addr_expr(t));
               tsi_link_before (tsi, new_call, TSI_SAME_STMT);
            }

            if(fpgastat){
              const char *file_name = EXPR_FILENAME(stmt);
              int lineno            = EXPR_LINENO(stmt);

              stats = build_call_expr (built_in_decls[BUILT_IN_HARTES_STATS_BEGIN], 4,build_string_literal(strlen(func_name)+1,func_name),build_string_literal(strlen(file_name)+1,file_name),build_int_cst (integer_type_node, EXPR_LINENO(stmt)),build_int_cst (integer_type_node, FPGADEV));
              tsi_link_before (tsi, stats, TSI_SAME_STMT);
              fpga_print_debug(4,"*generate statistics for %s file %s line: %d\n",func_name,file_name,lineno);
            }

            expand_parameters(decl,tsi,call,IO_PROLOGUE,fpga_id);

            if(fpgastat && trx_bytes){
              stats = build_call_expr (built_in_decls[BUILT_IN_HARTES_STATS_PARMIN], 1, build_int_cst (integer_type_node, trx_bytes));
              tsi_link_before (tsi, stats, TSI_SAME_STMT);
            }

            call_fpga_execute = build_call_expr (fpga_execute, 1, build_int_cst (integer_type_node, fpga_id));
            tsi_link_before (tsi, call_fpga_execute, TSI_SAME_STMT);

            call_fpga_break = build_call_expr (fpga_break, 1, build_int_cst (integer_type_node, fpga_id));
            tsi_link_before (tsi, call_fpga_break, TSI_SAME_STMT);

            if(fpgastat) {
              stats = build_call_expr (built_in_decls[BUILT_IN_HARTES_STATS_EXEC], 0);
              tsi_link_before (tsi, stats, TSI_SAME_STMT);
            }

            expand_parameters(decl,tsi,call,IO_EPILOGUE,fpga_id);

            if(fpgastat && trx_bytes){
              stats = build_call_expr (built_in_decls[BUILT_IN_HARTES_STATS_PARMOUT], 1, build_int_cst (integer_type_node, trx_bytes));
              tsi_link_before (tsi, stats, TSI_SAME_STMT);
            }

	    /* Based on the type of the operand we will use the int or the float version
	      of fpga_return. */
	    
	    switch(TREE_CODE (TREE_TYPE(call))){
	      case INTEGER_TYPE:     
		fpga_return = fpga_return_int;
		break;
	      case REAL_TYPE:
		fpga_return = fpga_return_float;
		break;
	      case POINTER_TYPE:
		fpga_warn(call, "\"%s\" remote DSP call returns pointer \n",IDENTIFIER_POINTER(DECL_NAME(decl)));
	      case VOID_TYPE:
		fpga_return = fpga_return_int;
		break;
	      case COMPLEX_TYPE:
	      case VECTOR_TYPE:
	      case ENUMERAL_TYPE:	      
	      default:	      
		fpga_error(stmt,"\"%s\" remote FPGA unsupported return type\n",IDENTIFIER_POINTER(DECL_NAME(decl)));
		gcc_unreachable();
	    }
	    
            fpga_return_adress_expression = build_fold_addr_expr (fpga_return);
            move = build_call_expr (fpga_movef, 3, build_int_cst (integer_type_node, fpga_id), build_int_cst (integer_type_node, 0 + hxml_get_start_output_xr (fpga_id)), fpga_return_adress_expression);
            tsi_link_before (tsi, move, TSI_SAME_STMT);

            if (TREE_CODE (stmt) == GIMPLE_MODIFY_STMT) {
                GIMPLE_STMT_OPERAND (stmt, 1) = fpga_return;
            }
            else if (TREE_CODE (stmt) == CALL_EXPR) {
                tsi_delink (tsi);
                if(!tsi_end_p(*tsi)) {
                  tsi_prev (tsi);
                }
            }
            else {
                GIMPLE_STMT_OPERAND (stmt, 0) = fpga_return;
            }
        }
        else if ((attr = lookup_attribute ("call_fpga", DECL_ATTRIBUTES (decl)))) {
            warning (OPT_Wpragmas, "Pragma call_fpga is ignored if fpga calls is activated. Please use call_hw");
        }
    }
}

static void fpga_omp_sections_opt_1(tree r, tree_stmt_iterator insert_pos);
static void fpga_omp_sections_opt(tree r);
static unsigned int do_fpga_parallel_opt_execute(void);

struct tree_opt_pass pass_fpga_parallel_optimisation = {
    "fpga_parallel_optimisation",       /* name */
    NULL,                               /* gate */
    do_fpga_parallel_opt_execute,               /* execute */
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

static unsigned int do_fpga_parallel_opt_execute(void) {
//    if (fpga == 0)              /* fpga pass is disabled */
//        return 0;

    fpga_print_debug (10, "FPGA PASS TREE\n");

    fpga_omp_sections_opt(DECL_SAVED_TREE (current_function_decl));

    return 0;
}

static void fpga_omp_sections_opt_1(tree r, tree_stmt_iterator insert_pos);

static void fpga_omp_sections_opt(tree r) {
    tree_stmt_iterator tsi;

    if(r==NULL) {
      // printf("pass_fpga_parallel_optimisation: unexpected NULL pointer\n");
      return;
    }
     
    if(TREE_CODE(r)==BIND_EXPR) {
        r = BIND_EXPR_BODY(r);
    }
    
    if(TREE_CODE(r)!=STATEMENT_LIST) {
      printf("pass_fpga_parallel_optimisation: unexpected tree code\n");    
      dump_node(r,0,stdout);
      return;
    }
    
    for (tsi = tsi_start (r); !tsi_end_p (tsi); ) {
        tree st = tsi_stmt (tsi);

	switch(TREE_CODE (st)) {
	  case OMP_SECTIONS:
	    fpga_omp_sections_opt_1(st, tsi);
	    tsi_delink(&tsi);
	    break;
	  case OMP_PARALLEL:
	    fpga_omp_sections_opt(OMP_PARALLEL_BODY(st));
	    tsi_link_before(&tsi,OMP_PARALLEL_BODY(st),TSI_SAME_STMT);
	    tsi_delink(&tsi);      
	    break;
	  case COND_EXPR:
	    fpga_omp_sections_opt(COND_EXPR_THEN(st));
	    fpga_omp_sections_opt(COND_EXPR_ELSE(st));
	    tsi_next (&tsi);
	    break;
	  default:
           tsi_next (&tsi);
        }
    }
}

static void fpga_omp_sections_opt_1(tree r, tree_stmt_iterator insert_pos) {
    tree_stmt_iterator tsi,tsis;

    for (tsi = tsi_start (OMP_SECTIONS_BODY (r)); !tsi_end_p (tsi);  tsi_next (&tsi)) {
        tree st;
        st = tsi_stmt (tsi);
        if(TREE_CODE (st) == OMP_SECTION) {
            fpga_omp_sections_opt(OMP_SECTION_BODY (st));
        }
    }

    // Now we know there are no nested sections

    // Add what is before the break
    for (tsi = tsi_start (OMP_SECTIONS_BODY (r)); !tsi_end_p (tsi);  tsi_next (&tsi)) {
        tree st;
        st = tsi_stmt (tsi);
        if(TREE_CODE (st) == OMP_SECTION) {
            int cnt = 0;
            int last_break = -1;
            tree_stmt_iterator tsi_lb;

            // Find the last BREAK
            for (tsi_lb = tsi_start (OMP_SECTION_BODY (st)); !tsi_end_p (tsi_lb);  tsi_next (&tsi_lb)) {
                tree call_lb = get_call_expr_in (tsi_stmt(tsi_lb));
                tree t1 = built_in_decls[BUILT_IN_FPGA_BREAK];
                tree t2;
                if(call_lb) {
                    t2 = get_callee_fndecl (call_lb);
                    if(t1 == t2) {
                        last_break = cnt;
                    }
                }
                cnt++;

                // If there are labels here it means there is some control flow -> the execute might not
                // be always executed -> abort the optimisation
                // TODO: check there are no more cases
                if(TREE_CODE(tsi_stmt(tsi_lb))==LABEL_EXPR || TREE_CODE(tsi_stmt(tsi_lb))==GOTO_EXPR) {
                    last_break = -1;
                }
            }

            // We add before the OMP_SECTIONS (insert_pos) all the trees before the last BREAK.
            cnt = 0;
            for (tsi_lb = tsi_start (OMP_SECTION_BODY (st)); !tsi_end_p (tsi_lb);) {
                if(cnt == last_break) {
                    break;
                }
                tsi_link_before(&insert_pos, tsi_stmt(tsi_lb), TSI_SAME_STMT);
                tsi_delink(&tsi_lb);
                cnt++;
            }
        }
    }

    // Add what is after the break
    for (tsi = tsi_start (OMP_SECTIONS_BODY (r)); !tsi_end_p (tsi);  tsi_next (&tsi)) {
        tree st;
        st = tsi_stmt (tsi);
        if(TREE_CODE (st) == OMP_SECTION) {
            tree_stmt_iterator tsi_lb;

           // We add after the OMP_SECTIONS (insert_pos) all the trees after the last BREAK.
            for (tsi_lb = tsi_start (OMP_SECTION_BODY (st)); !tsi_end_p (tsi_lb);) {
                tsi_link_before(&insert_pos, tsi_stmt(tsi_lb), TSI_SAME_STMT);
                tsi_delink(&tsi_lb);
            }
        }
    }

    // Now we insert all sections in the body of omp_sections
    for (tsi = tsi_start (OMP_SECTIONS_BODY (r)); !tsi_end_p (tsi);  tsi_next (&tsi)) {
        tree st;
        st = tsi_stmt (tsi);
        if(TREE_CODE (st) == OMP_SECTION) {
            for (tsis = tsi_start (OMP_SECTION_BODY (st)); !tsi_end_p (tsis); tsi_next (&tsis)) {
            }
        }
    }
}

static int is_param_const(tree decl, int arg) {
  int cnt = 0;
  tree parmlist = TYPE_ARG_TYPES (TREE_TYPE (decl));		  
  for (; parmlist; parmlist = TREE_CHAIN (parmlist)){
    if(cnt == arg) {
      tree parm=TREE_VALUE (parmlist);
      if(TYPE_NAME(parm)&& (TREE_CODE(TYPE_NAME(parm))==TYPE_DECL)&& DECL_ORIGINAL_TYPE (TYPE_NAME(parm))){
	parm = DECL_ORIGINAL_TYPE (TYPE_NAME(parm));
      }
      if(TREE_CODE(parm)==POINTER_TYPE && TREE_READONLY(TREE_TYPE(parm))) {
	return 1;
      }
      return 0;
    }
    cnt++;
  }
  return 0;
}

static int fpga_warn(tree expr, const char *msg, ...) {
  const char *file_name=EXPR_FILENAME(expr);
  int line=EXPR_LINENO(expr);
  va_list args;
  va_start(args, msg);
  fprintf (stderr,"%s:%d: warning: ",file_name,line);
  vfprintf (stderr,msg, args);
  va_end(args);
  return 0; 
}

static int fpga_error(tree expr, const char *msg, ...) {
  char *file_name=EXPR_FILENAME(expr);
  int line=EXPR_LINENO(expr);
  va_list args;
  va_start(args, msg);
  fprintf (stderr,"%s:%d: error: ",file_name,line);
  vfprintf (stderr,msg, args);
  va_end(args);
  have_error = 1;
  exit(1);
  return 0; 
}
