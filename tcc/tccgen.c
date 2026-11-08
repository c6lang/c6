/*
 *  TCC - Tiny C Compiler
 *
 *  Copyright (c) 2001-2004 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define USING_GLOBALS
#include "tcc.h"

/********************************************************/
/* global variables */

/* loc : local variable index
   ind : output code index
   rsym: return symbol
   anon_sym: anonymous symbol index
*/
ST_DATA int rsym, anon_sym, ind, loc;

ST_DATA Sym *global_stack;
ST_DATA Sym *local_stack;
ST_DATA Sym *define_stack;
ST_DATA Sym *global_label_stack;
ST_DATA Sym *local_label_stack;

static Sym *sym_free_first;
static void **sym_pools;
static int nb_sym_pools;

static Sym *all_cleanups, *pending_gotos;
static int local_scope;
ST_DATA char debug_modes;

ST_DATA SValue *vtop;
static SValue _vstack[1 + VSTACK_SIZE];
#define vstack (_vstack + 1)

ST_DATA int nocode_wanted; /* no code generation wanted */
#define NODATA_WANTED (nocode_wanted > 0) /* no static data output wanted either */
#define DATA_ONLY_WANTED 0x80000000 /* ON outside of functions and for static initializers */

/* no code output after unconditional jumps such as with if (0) ... */
#define CODE_OFF_BIT 0x20000000
#define CODE_OFF() if(!nocode_wanted)(nocode_wanted |= CODE_OFF_BIT)
#define CODE_ON() (nocode_wanted &= ~CODE_OFF_BIT)

/* no code output when parsing sizeof()/typeof() etc. (using nocode_wanted++/--) */
#define NOEVAL_MASK 0x0000FFFF
#define NOEVAL_WANTED (nocode_wanted & NOEVAL_MASK)

/* no code output when parsing constant expressions */
#define CONST_WANTED_BIT  0x00010000
#define CONST_WANTED_MASK 0x0FFF0000
#define CONST_WANTED  (nocode_wanted & CONST_WANTED_MASK)

ST_DATA int global_expr;  /* true if compound literals must be allocated globally (used during initializers parsing */
ST_DATA CType func_vt; /* current function return type (used by return instruction) */
ST_DATA int func_var; /* true if current function is variadic (used by return instruction) */
ST_DATA int func_vc; /* stack address for implicit struct return storage */
ST_DATA int func_ind; /* function start address */
static int func_old;
ST_DATA const char *funcname;
ST_DATA CType int_type, func_old_type, char_type, char_pointer_type;
static CString initstr;

#if PTR_SIZE == 4
#define VT_SIZE_T (VT_INT | VT_UNSIGNED)
#define VT_PTRDIFF_T VT_INT
#elif LONG_SIZE == 4
#define VT_SIZE_T (VT_LLONG | VT_UNSIGNED)
#define VT_PTRDIFF_T VT_LLONG
#else
#define VT_SIZE_T (VT_LONG | VT_LLONG | VT_UNSIGNED)
#define VT_PTRDIFF_T (VT_LONG | VT_LLONG)
#endif

static struct switch_t {
    struct case_t {
        int64_t v1, v2;
        int ind, line;
    } **p; int n; /* list of case ranges */
    int def_sym; /* default symbol */
    int nocode_wanted;
    int *bsym;
    struct scope *scope;
    struct switch_t *prev;
    SValue sv;
} *cur_switch; /* current switch */

#define MAX_TEMP_LOCAL_VARIABLE_NUMBER 8
/*list of temporary local variables on the stack in current function. */
static struct temp_local_variable {
	int location; //offset on stack. Svalue.c.i
	short size;
	short align;
} arr_temp_local_vars[MAX_TEMP_LOCAL_VARIABLE_NUMBER];
static int nb_temp_local_vars;

static struct scope {
    struct scope *prev;
    struct { int loc, locorig, num; } vla;
    struct { Sym *s; int n; } cl;
    int *bsym, *csym;
    Sym *lstk, *llstk;
    int depth; /* lexical scope depth (new_scope only) */
} *cur_scope, *loop_scope, *root_scope;

#ifdef C6_EXT
/* ------------------------------------------------------------------------- */
/* c6 extensions: defer / errdefer state (per function)                  */

typedef struct C6DeferSite {
    int id;
    Sym *label;
    struct C6DeferSite *next;
} C6DeferSite;

typedef struct C6DeferResume {
    int id;
    Sym *label;
    struct C6DeferResume *next;
} C6DeferResume;

static struct {
    int enabled;      /* c6 extensions active for current function */
    int has_sites;    /* at least one defer/errdefer site parsed so far */
    int body_depth;   /* > 0 while compiling a deferred statement body */
    int next_site;
    int next_resume;
    int sp0_off;      /* local slot holding this frame's defer stack base */
    int step_off;     /* local slot for __tcc_defer_step() result */
    int disp_jmps;    /* pending jumps to the function dispatcher */
    C6DeferSite *sites;
    C6DeferSite **sites_tail;
    C6DeferResume *resumes;
    C6DeferResume **resumes_tail;
} defer_state;
static int c6_label_seq;
static int c6_implicit_ret0;
/* pending hidden 'self' argument of a method / interface method call */
static int c6_self_pending;
static int c6_self_loc;
static CType c6_self_type;
/* registered 'ret f(struct T *self, ...)' methods: an ordinary function
   whose first parameter points at a struct or union type; 'name' is the
   spelling used in the source, so a method defined inside a package is
   still found under its bare name */
typedef struct C6Method {
    int type_tag;    /* struct/union tag of the receiver */
    int name;        /* method name as written (before package mangling) */
    int sym_tok;     /* token of the emitted function symbol */
    struct C6Method *next;
} C6Method;
static C6Method *c6_methods;
static int c6_nb_methods;
/* an interface converted from a struct type: one vtable object per pair */
typedef struct C6AutoImpl {
    struct C6Iface *iface;
    int type_tag;
    int vt_tok;      /* token of the emitted vtable object */
    Sym *vt_sym;     /* its forward symbol */
    int vt_emitted;  /* the object definition has been generated */
} C6AutoImpl;
static C6AutoImpl **c6_auto_impls;
static int c6_nb_auto_impls;
/* 'T[:]' slice views: one internal struct per (unqualified) element type,
   'struct { T *const data; const size_t len; }'.  The layout is fixed (data
   at 0, len at PTR_SIZE) and is the ABI shared with libtcc1 and with other
   translation units, so the object is built here instead of going through
   struct_layout() (which #pragma pack could alter).  Slice tags live on the
   global stack: a type built inside a function must survive the block. */
typedef struct C6Slice {
    CType elem;          /* element type, top level qualifiers dropped */
    CType elem_ptr;      /* 'elem *' */
    Sym *tag;            /* the internal struct tag */
    int elem_size;       /* sizeof(elem) */
    struct C6Slice *next;
} C6Slice;
static C6Slice *c6_slices;
/* the descriptor of TYPE when TYPE is a slice, NULL otherwise */
static C6Slice *c6_slice_of_type(CType *type)
{
    C6Slice *sl;
    if (!tcc_state->c6_ext)
        return NULL;
    if ((type->t & VT_BTYPE) != VT_STRUCT || !type->ref)
        return NULL;
    for (sl = c6_slices; sl; sl = sl->next)
        if (sl->tag == type->ref)
            return sl;
    return NULL;
}
/* '(T1, T2, ...)' tuples: one internal struct per element-type list, with a
   fixed natural layout (offsets are computed when the type is built, so
   '#pragma pack' cannot change it).  The layout is the ABI shared by the
   whole translation unit and by other translation units, so it must not
   depend on where the type happens to be written.  The tag is *named*
   ('__c6_tuple_N'): the async/go signature serializer spells types back into
   source tokens, so a named tag makes a tuple reusable there with no special
   case at all.  Tuples live as long as the unit, hence the global stack. */
typedef struct C6Tuple {
    int n;                /* element count (>= 2) */
    CType *elem;          /* element types, top level qualifiers dropped */
    int *off;             /* element offsets in the internal struct */
    Sym *tag;             /* the internal struct tag */
    struct C6Tuple *next;
} C6Tuple;
static C6Tuple *c6_tuples;
/* set when a tuple type specifier consumed the ')' of its own group, so the
   cast path in unary() knows not to expect a second one */
static int c6_tuple_ate_paren;
static int c6_tuple_seq;
static int c6_tuple_tmp_seq;
static C6Tuple *c6_tuple_of_type(CType *type);
static CType c6_tuple_ctype(C6Tuple *tp);
static C6Tuple *c6_tuple_get(CType *elems, int n);
static int c6_tuple_btype(CType *type);
static int c6_tuple_subscript(void);
static int c6_tuple_lead_kind(void);

/* the tuple whose tag is TYPE's tag, or NULL */
static C6Tuple *c6_tuple_of_type(CType *type)
{
    C6Tuple *tp;
    if (!tcc_state->c6_ext)
        return NULL;
    if ((type->t & VT_BTYPE) != VT_STRUCT || !type->ref)
        return NULL;
    for (tp = c6_tuples; tp; tp = tp->next)
        if (tp->tag == type->ref)
            return tp;
    return NULL;
}

/* the value type of tuple TP */
static CType c6_tuple_ctype(C6Tuple *tp)
{
    CType t;
    t.t = VT_STRUCT;
    t.ref = tp->tag;
    return t;
}

/* non-zero while vtop holds the result of a call to a 'T!error' function */
static int c6_value_fallible;
/* symbol of the function being compiled (needed by 'try' to check that the
   enclosing function may propagate errors) */
static Sym *c6_func_sym;
/* set by block() when the statement just parsed leaves the block */
static int c6_last_stmt_leaves;
/* set by type_decl() when it consumed a '!error' suffix that sits between
   the pointer stars and the declarator name ('int *!error f(void)') */
static int c6_decl_fallible;

static void c6_fallible_use(void);
static int c6_parse_fallible_suffix(void);
static void c6_check_error_param(CType *type);
static void c6_check_var_initialized(CType *type, int has_init, int v,
                                         AttributeDef *ad);
static void c6_defer_func_init(void);
static void c6_defer_func_end(void);
static void c6_defer_begin(int mode, int depth);
static void c6_defer_parse(int kind);
static void c6_err_parse(void);
static void c6_errdefer_parse(void);
static void c6_emit_propagate(void);
static void c6_try_parse(void);
static void c6_catch_parse(void);
static void c6_test_parse(void);
static void c6_package_parse(void);
static void c6_import_parse(void);
static void c6_interface_parse(void);
static int c6_emit_auto_impls(void);
static void c6_tu_reset(void);
/* 'module NAME;' namespace prefixing and 'import NAME;' declaration loading */
static int c6_mod_name(int v);
static void c6_module_parse(void);
static void c6_module_import(const char *name);
static void c6_skip_decl_tail(void);
static void c6_header_record(int kind, int name, CType *type, Sym *sym);
static void c6_emit_header(TCCState *s1);
static int c6_importing;       /* replaying an imported module's declarations */
static char *c6_expected_module; /* module name the file being parsed must declare */
static int c6_iface_convert(CType *dt);
typedef struct C6Iface C6Iface_t;
static C6Iface *c6_iface_of_type(CType *type);
static int c6_iface_has_method(C6Iface *ifc, int m);
static void c6_iface_call_prep(C6Iface *ifc, int mname);
static Sym *c6_import_lookup(int v);
static Sym *c6_import_tag_lookup(int v);
static int c6_mangle_tok(const char *pkg, int v);
static int c6_pkg_imported(const char *name);
static void c6_method_register(int name, int sym_tok, CType *type);
static void c6_gc_drop_register(int name, int sym_tok, CType *type, int tag);
static int c6_gc_is_drop(int sym_tok);
static int c6_gc_drop_callee(Sym *s);
static C6Method *c6_method_lookup(CType *type, int name);
static int c6_has_field(CType *type, int v);
static void c6_method_call_prep(C6Method *m);
static CType c6_slice_ctype(C6Slice *sl);
static C6Slice *c6_slice_get(CType *elem);
static void c6_slice_subscript(void);
static int c6_slice_convert(CType *dt);
static void gen_function(Sym *sym);
/* c6 async/await: an 'async' function compiles to the body
   'int __c6_body_NAME(void *task)'.  A declared call becomes a task
   creation, and 'await' suspends the body and resumes it later. */
static void c6_async_declare(CType *ftype, int name);
static void c6_async_definition(Sym *sym, CType *ftype);
static void c6_async_call(Sym *s);
static void c6_async_func_begin(void);
static void c6_async_params_copy(void);
static void c6_async_func_end(void);
static void c6_await_parse(void);
static void c6_async_return(void);
static void c6_return_value(CType *type);
static void c6_async_await_io(Sym *s);
static void c6_co_parse(void);
static void c6_go_parse(void);
static void c6_sp_flush(void);
static int c6_soft_kw(int t);
static int c6_soft_kw_shadowed(int t);
static int c6_gc_qual(int t);
static int c6_is_managed(CType *type);
static int c6_gc_mode(CType *type);
static int c6_type_gq(CType *type);
static void c6_gc_plain(CType *type);
/* nonzero while the initializer of a managed object is written */
static int c6_gc_init_store;
/* nonzero inside the body of an 'async' function: the native frame is moved
   into the task, so a weak slot would point at the wrong stack */
static int c6_async_in_body;
/* nonzero inside the body of a destructor: it runs while the object is being
   released, so it can neither fail nor suspend */
static int c6_gc_in_drop;
/* nonzero while the compiler stores a managed value for its own bookkeeping
   (return slot, argument slot): those stores must not be rewritten */
static int c6_gc_no_hook;
static int c6_gc_store_hook(void);
static int c6_gc_new_lead(void);
static void c6_gc_new_expr(void);
static int c6_gc_move_lead(void);
static void c6_gc_move_expr(void);
static void c6_gc_return_hook(void);
static void c6_gc_discard_hook(void);
static void c6_gc_site(int off, CType *type);
static void c6_gc_root_add(int tok, CType *type);
static int c6_gc_elem(CType *type, CType *elem, int *count, int *stride);
static void c6_gc_size_of(C6GcType *gt, CType *st);
static void c6_gc_arg_mark(void);
static void c6_gc_arg_temp(void);
static void c6_gc_arg_release(int has_value);
static void c6_gc_collect(CType *type, int is_union);
static void c6_gc_flush_descs(void);
static int c6_gc_desc_for(CType *type);
static C6GcType *c6_gc_type_find(int tag);
static void c6_gc_value_check(CType *type, const char *what);
#define C6_KW_NONE 0
#define C6_KW_CO   1
#define C6_KW_GO   2
#define C6_KW_TASK 3
/* trampolines that still have to be emitted (one per spawned function) */
typedef struct C6SpPending {
    struct C6SpPending *next;
    int name;      /* the spawned function (already mangled) */
    int tramp;     /* its trampoline */
    CType ftype;
} C6SpPending;
static C6SpPending *c6_sp_pending;
static void c6_task_decl(void);
static void c6_foreach_parse(void);
static void c6_task_check_handle(CType *t, const char *what);
static void c6_task_defer_destroy(int slot);
static void c6_async_propagate(void);
static int c6_async_cur;      /* non-zero while generating an async body */
static int c6_await_pending;  /* set by 'await', cleared by the call hook */
/* 'test' is an x86 assembler mnemonic: on x86 targets the keyword string is
   already interned as TOK_ASM_test, so accept both spellings. */
static int c6_is_test_kw(int t)
{
    if (t == TOK_C6_TEST)
        return 1;
#if defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64
    if (t == TOK_ASM_test)
        return 1;
#endif
    return 0;
}
static Sym *c6_new_label(const char *tag);
static void c6_def_label(Sym *s);
static int c6_ext_on(void);
#endif

typedef struct {
    Section *sec;
    int local_offset;
    Sym *flex_array_ref;
} init_params;

#if 1
#define precedence_parser
static void init_prec(void);
#endif

static void block(int flags);
#define STMT_EXPR 1
#define STMT_COMPOUND 2

static void gen_cast(CType *type);
static void gen_cast_s(int t);
static inline CType *pointed_type(CType *type);
static void check_restrict_type(CType *type);
static int type_qualifiers(CType *type);
static void parse_btype_qualify(CType *type, int qualifiers);
static int is_compatible_types(CType *type1, CType *type2);
static int parse_btype(CType *type, AttributeDef *ad, int ignore_label);
static CType *type_decl(CType *type, AttributeDef *ad, int *v, int td);
static void parse_expr_type(CType *type);
static void init_putv(init_params *p, CType *type, unsigned long c);
static void decl_initializer(init_params *p, CType *type, unsigned long c, int flags);
static void decl_initializer_alloc(CType *type, AttributeDef *ad, int r, int has_init, int v, int scope);
static int decl(int l);
static void expr_eq(void);
static void vpush_type_size(CType *type, int *a);
static int is_compatible_unqualified_types(CType *type1, CType *type2);
static inline int64_t expr_const64(void);
static void vpush64(int ty, unsigned long long v);
static void vpush(CType *type);
static int gvtst(int inv, int t);
static void gen_inline_functions(TCCState *s);
static void free_inline_functions(TCCState *s);
static void skip_or_save_block(TokenString **str);
static void gv_dup(void);
static int get_temp_local_var(int size,int align,int *r2);
static void cast_error(CType *st, CType *dt);
static void end_switch(void);
static void do_Static_assert(void);

/* ------------------------------------------------------------------------- */
/* Automagical code suppression */

/* Clear 'nocode_wanted' at forward label if it was used */
ST_FUNC void gsym(int t)
{
  if (t) {
    gsym_addr(t, ind);
    CODE_ON();
  }
}

/* Clear 'nocode_wanted' if current pc is a label */
static int gind()
{
  int t = ind;
  CODE_ON();
  if (debug_modes)
    tcc_tcov_block_begin(tcc_state);
  return t;
}

/* Set 'nocode_wanted' after unconditional (backwards) jump */
static void gjmp_addr_acs(int t)
{
  gjmp_addr(t);
  CODE_OFF();
}

/* Set 'nocode_wanted' after unconditional (forwards) jump */
static int gjmp_acs(int t)
{
  t = gjmp(t);
  CODE_OFF();
  return t;
}

/* These are #undef'd at the end of this file */
#define gjmp_addr gjmp_addr_acs
#define gjmp gjmp_acs
/* ------------------------------------------------------------------------- */

ST_INLN int is_float(int t)
{
    int bt = t & VT_BTYPE;
    return bt == VT_LDOUBLE
        || bt == VT_DOUBLE
        || bt == VT_FLOAT
        || bt == VT_QFLOAT;
}

static inline int is_integer_btype(int bt)
{
    return bt == VT_BYTE
        || bt == VT_BOOL
        || bt == VT_SHORT
        || bt == VT_INT
        || bt == VT_LLONG;
}

static int btype_size(int bt)
{
    return bt == VT_BYTE || bt == VT_BOOL ? 1 :
        bt == VT_SHORT ? 2 :
        bt == VT_INT ? 4 :
        bt == VT_LLONG ? 8 :
        bt == VT_PTR ? PTR_SIZE : 0;
}

/* returns function return register from type */
static int R_RET(int t)
{
    if (!is_float(t))
        return REG_IRET;
#ifdef TCC_TARGET_X86_64
    if ((t & VT_BTYPE) == VT_LDOUBLE)
        return TREG_ST0;
#elif defined TCC_TARGET_RISCV64
    if ((t & VT_BTYPE) == VT_LDOUBLE)
        return REG_IRET;
#endif
    return REG_FRET;
}

/* returns 2nd function return register, if any */
static int R2_RET(int t)
{
    t &= VT_BTYPE;
#if PTR_SIZE == 4
    if (t == VT_LLONG)
        return REG_IRE2;
#elif defined TCC_TARGET_X86_64
    if (t == VT_QLONG)
        return REG_IRE2;
    if (t == VT_QFLOAT)
        return REG_FRE2;
#elif defined TCC_TARGET_RISCV64
    if (t == VT_LDOUBLE)
        return REG_IRE2;
#endif
    return VT_CONST;
}

/* returns true for two-word types */
#define USING_TWO_WORDS(t) (R2_RET(t) != VT_CONST)

/* put function return registers to stack value */
static void PUT_R_RET(SValue *sv, int t)
{
    sv->r = R_RET(t), sv->r2 = R2_RET(t);
}

/* returns function return register class for type t */
static int RC_RET(int t)
{
    return reg_classes[R_RET(t)] & ~(RC_FLOAT | RC_INT);
}

/* returns generic register class for type t */
static int RC_TYPE(int t)
{
    if (!is_float(t))
        return RC_INT;
#ifdef TCC_TARGET_X86_64
    if ((t & VT_BTYPE) == VT_LDOUBLE)
        return RC_ST0;
    if ((t & VT_BTYPE) == VT_QFLOAT)
        return RC_FRET;
#elif defined TCC_TARGET_RISCV64
    if ((t & VT_BTYPE) == VT_LDOUBLE)
        return RC_INT;
#endif
    return RC_FLOAT;
}

/* returns 2nd register class corresponding to t and rc */
static int RC2_TYPE(int t, int rc)
{
    if (!USING_TWO_WORDS(t))
        return 0;
#ifdef RC_IRE2
    if (rc == RC_IRET)
        return RC_IRE2;
#endif
#ifdef RC_FRE2
    if (rc == RC_FRET)
        return RC_FRE2;
#endif
    if (rc & RC_FLOAT)
        return RC_FLOAT;
    return RC_INT;
}

/* we use our own 'finite' function to avoid potential problems with
   non standard math libs */
/* XXX: endianness dependent */
ST_FUNC int ieee_finite(double d)
{
    int p[4];
    memcpy(p, &d, sizeof(double));
    return ((unsigned)((p[1] | 0x800fffff) + 1)) >> 31;
}

ST_FUNC void test_lvalue(void)
{
    if (!(vtop->r & VT_LVAL))
        expect("lvalue");
}

ST_FUNC void check_vstack(void)
{
    if (vtop != vstack - 1)
        tcc_error("internal compiler error: vstack leak (%d)",
                  (int)(vtop - vstack + 1));
}

#if 0
/* dump 'b' vstack entries starting with 'a'  */
void pv (const char *lbl, int a, int b)
{
    int i;
    for (i = a; i < a + b; ++i) {
        SValue *p = &vtop[-i];
        printf("%s vtop[-%d] : type.t:%04x  r:%04x  r2:%04x  c.i:%d\n",
            lbl, i, p->type.t, p->r, p->r2, (int)p->c.i);
    }
}

/* dump symbols on stack from s ... last */
static inline void psyms(const char *msg, Sym *s, Sym *last)
{
    printf("%-8s scope         v        c        r   type.t\n", msg);
    while (s && s != last) {
        printf("      %8x  %08x %08x %08x %08x %s\n",
            s->sym_scope, s->v, s->c, s->r, s->type.t, get_tok_str(s->v, 0));
        s = s->prev;
    }
}

static void type_to_str(char *buf, int buf_size, CType *type, const char *varstr);

/* print type */
static void ptype(const char *msg, CType *type, int v)
{
    char buf[500];
    type_to_str(buf, sizeof(buf), type,
        (v & ~SYM_FIELD) ? get_tok_str(v, NULL) : NULL);
    printf("%s : %s;\n", msg, buf);
}
#endif

/* ------------------------------------------------------------------------- */
/* initialize vstack and types.  This must be done also for tcc -E */
ST_FUNC void tccgen_init(TCCState *s1)
{
    vtop = vstack - 1;
    memset(vtop, 0, sizeof *vtop);

    /* define some often used types */
    int_type.t = VT_INT;

    char_type.t = VT_BYTE;
    if (s1->char_is_unsigned)
        char_type.t |= VT_UNSIGNED;
    char_pointer_type = char_type;
    mk_pointer(&char_pointer_type);

    func_old_type.t = VT_FUNC;
    func_old_type.ref = sym_push(SYM_FIELD, &int_type, 0, 0);
    func_old_type.ref->f.func_call = FUNC_CDECL;
    func_old_type.ref->f.func_type = FUNC_OLD;
#ifdef precedence_parser
    init_prec();
#endif
    cstr_new(&initstr);
}

ST_FUNC int tccgen_compile(TCCState *s1)
{
    funcname = "";
    func_ind = -1;
    anon_sym = SYM_FIRST_ANOM;
    nocode_wanted = DATA_ONLY_WANTED; /* no code outside of functions */
#ifdef C6_EXT
    /* the state below outlives a translation unit: after an error the
       parser is abandoned mid-recursion, so start each unit from scratch */
    defer_state.body_depth = 0;
    defer_state.enabled = 0;
    defer_state.has_sites = 0;
    c6_func_sym = NULL;
    c6_value_fallible = 0;
    c6_last_stmt_leaves = 0;
    c6_decl_fallible = 0;
    c6_tu_reset();
#endif
    debug_modes = (s1->do_debug ? 1 : 0) | s1->test_coverage << 1;
    global_expr = 0;

    tcc_debug_start(s1);
    tcc_tcov_start (s1);
#ifdef TCC_TARGET_ARM
    arm_init(s1);
#endif
#ifdef INC_DEBUG
    printf("%s: **** new file\n", file->filename);
#endif
    parse_flags = PARSE_FLAG_PREPROCESS | PARSE_FLAG_TOK_NUM | PARSE_FLAG_TOK_STR;
    next();
    decl(VT_CONST);
#ifdef C6_EXT
    /* trampolines for the functions some 'go' of this unit spawned */
    c6_sp_flush();
    /* interfaces converted in this unit get their vtable object here, but a
       'static inline' body is only compiled now, and such a body may contain
       a conversion itself: alternate until nothing new turns up */
    for (;;) {
        gen_inline_functions(s1);
        if (!c6_emit_auto_impls())
            break;
    }
    /* the type descriptors last: a 'drop' method may be declared after the
       struct, and a 'static inline' body may still add a type */
    c6_gc_flush_descs();
#else
    gen_inline_functions(s1);
#endif
#ifdef C6_EXT
    if (s1->c6_emit_header)
        c6_emit_header(s1);
#endif
    check_vstack();
    /* end of translation unit info */
#if TCC_EH_FRAME
    tcc_eh_frame_end(s1);
#endif
    tcc_debug_end(s1);
    tcc_tcov_end(s1);
    return 0;
}

ST_FUNC void tccgen_finish(TCCState *s1)
{
    tcc_debug_end(s1); /* just in case of errors: free memory */
    free_inline_functions(s1);
    sym_pop(&global_stack, NULL, 0);
    sym_pop(&local_stack, NULL, 0);
    /* free preprocessor macros */
    free_defines(NULL);
    /* free sym_pools */
    dynarray_reset(&sym_pools, &nb_sym_pools);
    cstr_free(&initstr);
    dynarray_reset(&stk_data, &nb_stk_data);
    while (cur_switch)
        end_switch();
    local_scope = 0;
    loop_scope = NULL;
    all_cleanups = NULL;
    pending_gotos = NULL;
    nb_temp_local_vars = 0;
    global_label_stack = NULL;
    local_label_stack = NULL;
    cur_text_section = NULL;
    sym_free_first = NULL;
}

/* ------------------------------------------------------------------------- */
ST_FUNC ElfSym *elfsym(Sym *s)
{
  if (!s || !s->c)
    return NULL;
  return &((ElfSym *)symtab_section->data)[s->c];
}

/* apply storage attributes to Elf symbol */
ST_FUNC void update_storage(Sym *sym)
{
    ElfSym *esym;
    int sym_bind, old_sym_bind;

    esym = elfsym(sym);
    if (!esym)
        return;

    if (sym->a.visibility)
        esym->st_other = (esym->st_other & ~ELFW(ST_VISIBILITY)(-1))
            | sym->a.visibility;

    if (sym->type.t & (VT_STATIC | VT_INLINE))
        sym_bind = STB_LOCAL;
    else if (sym->a.weak)
        sym_bind = STB_WEAK;
    else
        sym_bind = STB_GLOBAL;
    old_sym_bind = ELFW(ST_BIND)(esym->st_info);
    if (sym_bind != old_sym_bind) {
        esym->st_info = ELFW(ST_INFO)(sym_bind, ELFW(ST_TYPE)(esym->st_info));
    }

#ifdef TCC_TARGET_PE
    if (sym->a.dllimport)
        esym->st_other |= ST_PE_IMPORT;
    if (sym->a.dllexport)
        esym->st_other |= ST_PE_EXPORT;
#endif

#if 0
    printf("storage %s: bind=%c vis=%d exp=%d imp=%d\n",
        get_tok_str(sym->v, NULL),
        sym_bind == STB_WEAK ? 'w' : sym_bind == STB_LOCAL ? 'l' : 'g',
        sym->a.visibility,
        sym->a.dllexport,
        sym->a.dllimport
        );
#endif
}

/* ------------------------------------------------------------------------- */
/* update sym->c so that it points to an external symbol in section
   'section' with value 'value' */

ST_FUNC void put_extern_sym2(Sym *sym, int sh_num,
                            addr_t value, unsigned long size,
                            int can_add_underscore)
{
    int sym_type, sym_bind, info, other, t;
    ElfSym *esym;
    const char *name;
    char buf1[256];

    if (!sym->c) {
        name = get_tok_str(sym->v, NULL);
        t = sym->type.t;
        if ((t & VT_BTYPE) == VT_FUNC) {
            sym_type = STT_FUNC;
        } else if ((t & VT_BTYPE) == VT_VOID) {
            sym_type = STT_NOTYPE;
            if (IS_ASM_FUNC(t))
                sym_type = STT_FUNC;
        } else if (t & VT_TLS) {
            sym_type = STT_TLS;
        } else {
            sym_type = STT_OBJECT;
        }
        if (t & (VT_STATIC | VT_INLINE))
            sym_bind = STB_LOCAL;
        else
            sym_bind = STB_GLOBAL;
        other = 0;

#ifdef TCC_TARGET_PE
        if (sym_type == STT_FUNC && sym->type.ref) {
            Sym *ref = sym->type.ref;
            if (ref->a.nodecorate) {
                can_add_underscore = 0;
            }
            if (ref->f.func_call == FUNC_STDCALL && can_add_underscore) {
                sprintf(buf1, "_%s@%d", name, ref->f.func_args * PTR_SIZE);
                name = buf1;
                other |= ST_PE_STDCALL;
                can_add_underscore = 0;
            }
        }
#endif

        if (sym->asm_label) {
            name = get_tok_str(sym->asm_label, NULL);
            can_add_underscore = 0;
        }

        if (tcc_state->leading_underscore && can_add_underscore) {
            buf1[0] = '_';
            pstrcpy(buf1 + 1, sizeof(buf1) - 1, name);
            name = buf1;
        }

        info = ELFW(ST_INFO)(sym_bind, sym_type);
        sym->c = put_elf_sym(symtab_section, value, size, info, other, sh_num, name);

        if (debug_modes)
            tcc_debug_extern_sym(tcc_state, sym, sh_num, sym_bind, sym_type);

    } else {
        esym = elfsym(sym);
        esym->st_value = value;
        esym->st_size = size;
        esym->st_shndx = sh_num;
    }
    update_storage(sym);
}

ST_FUNC void put_extern_sym(Sym *sym, Section *s, addr_t value, unsigned long size)
{
    if (nocode_wanted && (NODATA_WANTED || (s && s == cur_text_section)))
        return;
    put_extern_sym2(sym, s ? s->sh_num : SHN_UNDEF, value, size, 1);
}

/* add a new relocation entry to symbol 'sym' in section 's' */
ST_FUNC void greloca(Section *s, Sym *sym, unsigned long offset, int type,
                     addr_t addend)
{
    int c = 0;

    if (nocode_wanted && s == cur_text_section)
        return;

    if (sym) {
        if (0 == sym->c) {
            put_extern_sym(sym, NULL, 0, 0);
            if (sym->sym_scope
                && (sym->type.t & (VT_STATIC|VT_EXTERN)) == (VT_STATIC|VT_EXTERN)) {
                /* when a local function declaraion redeclares a global static one
                   then tccelf would not resolve them to the same symbol. */
                Sym *s = sym;
                while (s->prev_tok)
                    s = s->prev_tok;
                s->c = sym->c;
            }
        }
        c = sym->c;
    }

    /* now we can add ELF relocation info */
    put_elf_reloca(symtab_section, s, offset, type, c, addend);
}

#if PTR_SIZE == 4
ST_FUNC void greloc(Section *s, Sym *sym, unsigned long offset, int type)
{
    greloca(s, sym, offset, type, 0);
}
#endif

/* ------------------------------------------------------------------------- */
/* symbol allocator */
static Sym *__sym_malloc(void)
{
    Sym *sym_pool, *sym, *last_sym;
    int i;

    sym_pool = tcc_malloc(SYM_POOL_NB * sizeof(Sym));
    dynarray_add(&sym_pools, &nb_sym_pools, sym_pool);

    last_sym = sym_free_first;
    sym = sym_pool;
    for(i = 0; i < SYM_POOL_NB; i++) {
        sym->next = last_sym;
        last_sym = sym;
        sym++;
    }
    sym_free_first = last_sym;
    return last_sym;
}

static inline Sym *sym_malloc(void)
{
    Sym *sym;
#ifndef SYM_DEBUG
    sym = sym_free_first;
    if (!sym)
        sym = __sym_malloc();
    sym_free_first = sym->next;
    return sym;
#else
    sym = tcc_malloc(sizeof(Sym));
    return sym;
#endif
}

ST_INLN void sym_free(Sym *sym)
{
#ifndef SYM_DEBUG
    sym->next = sym_free_first;
    sym_free_first = sym;
#else
    tcc_free(sym);
#endif
}

/* push, without hashing */
ST_FUNC Sym *sym_push2(Sym **ps, int v, int t, int c)
{
    Sym *s;

    s = sym_malloc();
    memset(s, 0, sizeof *s);
    s->v = v;
    s->type.t = t;
    s->c = c;
    /* add in stack */
    s->prev = *ps;
    *ps = s;
    return s;
}

/* find a symbol and return its associated structure. 's' is the top
   of the symbol stack */
ST_FUNC Sym *sym_find2(Sym *s, int v)
{
    while (s) {
        if (s->v == v)
            return s;
        s = s->prev;
    }
    return NULL;
}

/* structure lookup, no module fallback */
static inline Sym *struct_find_raw(int v)
{
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_struct;
}

/* structure lookup.  In a module file a tag declared at file scope lives
   under the module prefix ('math__Point'), so an unprefixed reference must
   look there; a block scope tag still wins, as in C. */
ST_INLN Sym *struct_find(int v)
{
    Sym *s = struct_find_raw(v);
#ifdef C6_EXT
    if (tcc_state && tcc_state->c6_ext && (!s || s->sym_scope == 0)) {
        Sym *m;
        if (file && file->c6_module) {
            int t = c6_mod_name(v);
            if (t != v) {
                m = struct_find_raw(t);
                if (m)
                    return m;
            }
        }
        m = c6_import_tag_lookup(v);
        if (m)
            return m;
    }
#endif
    return s;
}

/* find an identifier, no package/module fallback */
static inline Sym *sym_find_raw(int v)
{
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_identifier;
}

/* find an identifier.  A block scope binding always wins; in a module a
   file scope name is looked up under the module prefix before a plain C
   global of the same spelling, and imported packages come last. */
ST_INLN Sym *sym_find(int v)
{
    Sym *s = sym_find_raw(v);
#ifdef C6_EXT
    if (tcc_state) {
        if (s && s->sym_scope != 0)
            return s;
        if (tcc_state->c6_ext && file && file->c6_module) {
            int t = c6_mod_name(v);
            if (t != v) {
                Sym *m = sym_find_raw(t);
                if (m)
                    return m;
            }
        }
        if (!s && (tcc_state->c6_package || tcc_state->c6_nb_imports))
            s = c6_import_lookup(v);
    }
#endif
    return s;
}

/* make sym in-/visible to the parser */
static inline void sym_link(Sym *s, int yes)
{
    TokenSym *ts = table_ident[(s->v & ~SYM_STRUCT) - TOK_IDENT];
    Sym **ps;
    if (s->v & SYM_STRUCT)
        ps = &ts->sym_struct;
    else
        ps = &ts->sym_identifier;
    if (yes) {
        s->prev_tok = *ps, *ps = s;
        s->sym_scope = local_scope;
    } else {
        *ps = s->prev_tok;
    }
}

static inline int sym_scope_ex(Sym *s)
{
    /* enums have 'sym_scope' overwritten by 'enum_val' */
    return IS_ENUM_VAL (s->type.t)
        ? s->type.ref->sym_scope
        : s->sym_scope;
}

/* push a given symbol on the symbol stack */
ST_FUNC Sym *sym_push(int v, CType *type, int r, int c)
{
    Sym *s, **ps;
    if (local_stack)
        ps = &local_stack;
    else
        ps = &global_stack;
    s = sym_push2(ps, v, type->t, c);
    s->type.ref = type->ref;
#ifdef C6_EXT
#ifdef C6_EXT
    /* only a validated copy: a byte nobody initialised must not leak into a
       symbol's type and look like '@T' */
    s->type.gq = c6_type_gq(type);
#else
    s->type.gq = type->gq;      /* '@T': the managed reference qualifier */
#endif
#endif
    s->r = r;
    /* don't record fields or anonymous symbols */
    if ((v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
        /* record symbol in token array */
        sym_link(s, 1);
        if (s->prev_tok && sym_scope_ex(s->prev_tok) == local_scope)
            tcc_error("redeclaration of '%s'", get_tok_str(s->v, NULL));
    }
    return s;
}

/* push a global identifier */
ST_FUNC Sym *global_identifier_push(int v, int t, int c)
{
    Sym *s, **ps;
    s = sym_push2(&global_stack, v, t, c);
    s->r = VT_CONST | VT_SYM;
    /* don't record anonymous symbol */
    if (v < SYM_FIRST_ANOM) {
        ps = &table_ident[v - TOK_IDENT]->sym_identifier;
        /* modify the top most local identifier, so that sym_identifier will
           point to 's' when popped; happens when called from inline asm */
        while (*ps != NULL && (*ps)->sym_scope)
            ps = &(*ps)->prev_tok;
        s->prev_tok = *ps;
        *ps = s;
    }
    return s;
}

/* pop symbols until top reaches 'b'.  If KEEP is non-zero don't really
   pop them yet from the list, but do remove them from the token array.  */
ST_FUNC void sym_pop(Sym **ptop, Sym *b, int keep)
{
    Sym *s, *ss;
    int v;

    s = *ptop;
    while(s != b) {
        ss = s->prev;
        v = s->v;
        /* remove symbol in token array */
        if ((v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
            sym_link(s, 0);
	if (!keep)
	    sym_free(s);
        s = ss;
    }
    if (!keep)
	*ptop = b;
}

/* label lookup */
ST_FUNC Sym *label_find(int v)
{
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_label;
}

ST_FUNC Sym *label_push(Sym **ptop, int v, int flags)
{
    Sym *s, **ps;
    s = sym_push2(ptop, v, VT_STATIC, 0);
    s->r = flags;
    ps = &table_ident[v - TOK_IDENT]->sym_label;
    if (ptop == &global_label_stack) {
        /* modify the top most local identifier, so that
           sym_identifier will point to 's' when popped */
        while (*ps != NULL)
            ps = &(*ps)->prev_tok;
    }
    s->prev_tok = *ps;
    *ps = s;
    return s;
}

/* pop labels until element last is reached. Look if any labels are
   undefined. Define symbols if '&&label' was used. */
ST_FUNC void label_pop(Sym **ptop, Sym *slast, int keep)
{
    Sym *s, *s1;
    for(s = *ptop; s != slast; s = s1) {
        s1 = s->prev;
        if (s->r == LABEL_DECLARED) {
            tcc_warning_c(warn_all)("label '%s' declared but not used", get_tok_str(s->v, NULL));
        } else if (s->r == LABEL_FORWARD) {
                tcc_error("label '%s' used but not defined",
                      get_tok_str(s->v, NULL));
        } else {
            if (s->c) {
                /* define corresponding symbol. A size of
                   1 is put. */
                put_extern_sym(s, cur_text_section, s->jind, 1);
            }
        }
        /* remove label */
        if (s->r != LABEL_GONE)
            table_ident[s->v - TOK_IDENT]->sym_label = s->prev_tok;
        if (!keep)
            sym_free(s);
        else
            s->r = LABEL_GONE;
    }
    if (!keep)
        *ptop = slast;
}

/* ------------------------------------------------------------------------- */
static void vcheck_cmp(void)
{
    /* cannot let cpu flags if other instruction are generated. Also
       avoid leaving VT_JMP anywhere except on the top of the stack
       because it would complicate the code generator.

       Don't do this when nocode_wanted.  vtop might come from
       !nocode_wanted regions (see 88_codeopt.c) and transforming
       it to a register without actually generating code is wrong
       as their value might still be used for real.  All values
       we push under nocode_wanted will eventually be popped
       again, so that the VT_CMP/VT_JMP value will be in vtop
       when code is unsuppressed again. */

    /* However if it's just automatic suppression via CODE_OFF/ON()
       then it seems that we better let things work undisturbed.
       How can it work at all under nocode_wanted?  Well, gv() will
       actually clear it at the gsym() in load()/VT_JMP in the
       generator backends */

    if (vtop->r == VT_CMP && 0 == (nocode_wanted & ~CODE_OFF_BIT))
        gv(RC_INT);
}

static void vsetc(CType *type, int r, CValue *vc)
{
    if (vtop >= vstack + (VSTACK_SIZE - 1))
        tcc_error("memory full (vstack)");
    vcheck_cmp();
    vtop++;
    vtop->type = *type;
    vtop->r = r;
    vtop->r2 = VT_CONST;
    vtop->c = *vc;
    vtop->sym = NULL;
#ifdef C6_EXT
    /* a value that is pushed here is not a fresh managed reference: only
       'new' marks its result, and only until the value is consumed */
    vtop->gc_state = C6_GC_BORROW;
    vtop->gc_src = 0;
    vtop->gc_atomic = 0;
#endif
}

ST_FUNC void vswap(void)
{
    SValue tmp;

    vcheck_cmp();
    tmp = vtop[0];
    vtop[0] = vtop[-1];
    vtop[-1] = tmp;
}

/* pop stack value */
ST_FUNC void vpop(void)
{
    int v;
    v = vtop->r & VT_VALMASK;
#if defined(TCC_TARGET_I386) || defined(TCC_TARGET_X86_64)
    /* for x86, we need to pop the FP stack */
    if (v == TREG_ST0) {
        o(0xd8dd); /* fstp %st(0) */
    } else
#endif
    if (v == VT_CMP) {
        /* need to put correct jump if && or || without test */
        gsym(vtop->jtrue);
        gsym(vtop->jfalse);
    }
    vtop--;
}

/* push constant of type "type" with useless value */
static void vpush(CType *type)
{
    vset(type, VT_CONST, 0);
}

/* push arbitrary 64bit constant */
static void vpush64(int ty, unsigned long long v)
{
    CValue cval;
    CType ctype;
    ctype.t = ty;
    ctype.ref = NULL;
    cval.i = v;
    vsetc(&ctype, VT_CONST, &cval);
}

/* push integer constant */
ST_FUNC void vpushi(int v)
{
    vpush64(VT_INT, v);
}

/* push a pointer sized constant */
static void vpushs(addr_t v)
{
    vpush64(VT_SIZE_T, v);
}

/* push long long constant */
static inline void vpushll(long long v)
{
    vpush64(VT_LLONG, v);
}

ST_FUNC void vset(CType *type, int r, int v)
{
    CValue cval;
    cval.i = v;
    vsetc(type, r, &cval);
}

static void vseti(int r, int v)
{
    CType type;
    type.t = VT_INT;
    type.ref = NULL;
    vset(&type, r, v);
}

ST_FUNC void vpushv(SValue *v)
{
    if (vtop >= vstack + (VSTACK_SIZE - 1))
        tcc_error("memory full (vstack)");
    vtop++;
    *vtop = *v;
}

static void vdup(void)
{
    vpushv(vtop);
}

/* rotate the stack element at position n-1 to the top */
ST_FUNC void vrotb(int n)
{
    SValue tmp;
    if (--n < 1)
        return;
    vcheck_cmp();
    tmp = vtop[-n];
    memmove(vtop - n, vtop - n + 1, sizeof *vtop * n);
    vtop[0] = tmp;
}

/* rotate the top stack element into position n-1 */
ST_FUNC void vrott(int n)
{
    SValue tmp;
    if (--n < 1)
        return;
    vcheck_cmp();
    tmp = vtop[0];
    memmove(vtop - n + 1, vtop - n, sizeof *vtop * n);
    vtop[-n] = tmp;
}

/* reverse order of the the first n stack elements */
ST_FUNC void vrev(int n)
{
    int i;
    SValue tmp;
    vcheck_cmp();
    for (i = 0, n = -n; i > ++n; --i)
        tmp = vtop[i], vtop[i] = vtop[n], vtop[n] = tmp;
}

/* ------------------------------------------------------------------------- */
/* vtop->r = VT_CMP means CPU-flags have been set from comparison or test. */

/* called from generators to set the result from relational ops  */
ST_FUNC void vset_VT_CMP(int op)
{
    vtop->r = VT_CMP;
    vtop->cmp_op = op;
    vtop->jfalse = 0;
    vtop->jtrue = 0;
}

/* called once before asking generators to load VT_CMP to a register */
static void vset_VT_JMP(void)
{
    int op = vtop->cmp_op;

    if (vtop->jtrue || vtop->jfalse) {
        int origt = vtop->type.t;
        /* we need to jump to 'mov $0,%R' or 'mov $1,%R' */
        int inv = op & (op < 2); /* small optimization */
        vseti(VT_JMP+inv, gvtst(inv, 0));
        vtop->type.t |= origt & (VT_UNSIGNED | VT_DEFSIGN);
    } else {
        /* otherwise convert flags (rsp. 0/1) to register */
        vtop->c.i = op;
        if (op < 2) /* doesn't seem to happen */
            vtop->r = VT_CONST;
    }
}

/* Set CPU Flags, doesn't yet jump */
static void gvtst_set(int inv, int t)
{
    int *p;

    if (vtop->r != VT_CMP) {
        vpushi(0);
        gen_op(TOK_NE);
        if (vtop->r != VT_CMP) /* must be VT_CONST then */
            vset_VT_CMP(vtop->c.i != 0);
    }

    p = inv ? &vtop->jfalse : &vtop->jtrue;
    *p = gjmp_append(*p, t);
}

/* Generate value test
 *
 * Generate a test for any value (jump, comparison and integers) */
static int gvtst(int inv, int t)
{
    int op, x, u;

#ifdef C6_EXT
    c6_fallible_use();  /* 'if (f())' would drop the error */
#endif
    gvtst_set(inv, t);
    t = vtop->jtrue, u = vtop->jfalse;
    if (inv)
        x = u, u = t, t = x;
    op = vtop->cmp_op;

    /* jump to the wanted target */
    if (op > 1)
        t = gjmp_cond(op ^ inv, t);
    else if (op != inv)
        t = gjmp(t);
    /* resolve complementary jumps to here */
    gsym(u);

    vtop--;
    return t;
}

/* generate a zero or nozero test */
static void gen_test_zero(int op)
{
    if (vtop->r == VT_CMP) {
        int j;
        if (op == TOK_EQ) {
            j = vtop->jfalse;
            vtop->jfalse = vtop->jtrue;
            vtop->jtrue = j;
            vtop->cmp_op ^= 1;
        }
    } else {
        vpushi(0);
        gen_op(op);
    }
}

/* ------------------------------------------------------------------------- */
/* push a symbol value of TYPE */
ST_FUNC void vpushsym(CType *type, Sym *sym)
{
    CValue cval;
    cval.i = 0;
    vsetc(type, VT_CONST | VT_SYM, &cval);
    vtop->sym = sym;
}

/* Return a static symbol pointing to a section */
ST_FUNC Sym *get_sym_ref(CType *type, Section *sec, unsigned long offset, unsigned long size)
{
    int v;
    Sym *sym;

    v = anon_sym++;
    sym = sym_push(v, type, VT_CONST | VT_SYM, 0);
    sym->type.t |= VT_STATIC;
    put_extern_sym(sym, sec, offset, size);
    return sym;
}

/* push a reference to a section offset by adding a dummy symbol */
static void vpush_ref(CType *type, Section *sec, unsigned long offset, unsigned long size)
{
    vpushsym(type, get_sym_ref(type, sec, offset, size));  
}

/* define a new external reference to a symbol 'v' of type 'u' */
ST_FUNC Sym *external_global_sym(int v, CType *type)
{
    Sym *s;

    s = sym_find(v);
    if (!s) {
        /* push forward reference */
        s = global_identifier_push(v, type->t | VT_EXTERN, 0);
        s->type.ref = type->ref;
    } else if (IS_ASM_SYM(s)) {
        s->type.t = type->t | (s->type.t & VT_EXTERN);
        s->type.ref = type->ref;
        update_storage(s);
    }
    return s;
}

/* create an external reference with no specific type similar to asm labels.
   This avoids type conflicts if the symbol is used from C too */
ST_FUNC Sym *external_helper_sym(int v)
{
    CType ct = { VT_ASM_FUNC, NULL };
    return external_global_sym(v, &ct);
}

/* push a reference to an helper function (such as memmove) */
ST_FUNC void vpush_helper_func(int v)
{
    vpushsym(&func_old_type, external_helper_sym(v));
}

/* Merge symbol attributes.  */
static void merge_symattr(struct SymAttr *sa, struct SymAttr *sa1)
{
    if (sa1->aligned && !sa->aligned)
      sa->aligned = sa1->aligned;
    sa->packed |= sa1->packed;
    sa->weak |= sa1->weak;
    sa->nodebug |= sa1->nodebug;
    if (sa1->visibility != STV_DEFAULT) {
	int vis = sa->visibility;
	if (vis == STV_DEFAULT
	    || vis > sa1->visibility)
	  vis = sa1->visibility;
	sa->visibility = vis;
    }
    sa->dllexport |= sa1->dllexport;
    sa->nodecorate |= sa1->nodecorate;
    sa->dllimport |= sa1->dllimport;
}

/* Merge function attributes.  */
static void merge_funcattr(struct FuncAttr *fa, struct FuncAttr *fa1)
{
    if (fa1->func_call && !fa->func_call)
      fa->func_call = fa1->func_call;
    if (fa1->func_type && !fa->func_type)
      fa->func_type = fa1->func_type;
    if (fa1->func_args && !fa->func_args)
      fa->func_args = fa1->func_args;
    if (fa1->func_noreturn)
      fa->func_noreturn = 1;
    if (fa1->func_ctor)
      fa->func_ctor = 1;
    if (fa1->func_dtor)
      fa->func_dtor = 1;
#ifdef C6_EXT
    if (fa1->func_fallible)
      fa->func_fallible = 1;
#endif
}

/* Merge attributes.  */
static void merge_attr(AttributeDef *ad, AttributeDef *ad1)
{
    merge_symattr(&ad->a, &ad1->a);
    merge_funcattr(&ad->f, &ad1->f);

    if (ad1->section)
      ad->section = ad1->section;
    if (ad1->alias_target)
      ad->alias_target = ad1->alias_target;
    if (ad1->asm_label)
      ad->asm_label = ad1->asm_label;
    if (ad1->attr_mode)
      ad->attr_mode = ad1->attr_mode;
}

/* Merge some type attributes.  */
static void patch_type(Sym *sym, CType *type)
{
    if (!(type->t & VT_EXTERN) || IS_ENUM_VAL(sym->type.t)) {
        if (!(sym->type.t & VT_EXTERN))
            tcc_error("redefinition of '%s'", get_tok_str(sym->v, NULL));
        sym->type.t &= ~VT_EXTERN;
    }

    if (IS_ASM_SYM(sym)) {
        /* stay static if both are static */
        sym->type.t = type->t & (sym->type.t | ~VT_STATIC);
        sym->type.ref = type->ref;
        if ((type->t & VT_BTYPE) != VT_FUNC && !(type->t & VT_ARRAY))
            sym->r |= VT_LVAL;
    }

    if (!is_compatible_types(&sym->type, type)) {
        tcc_error("incompatible types for redefinition of '%s'",
                  get_tok_str(sym->v, NULL));

    } else if ((sym->type.t & VT_BTYPE) == VT_FUNC) {
        int static_proto = sym->type.t & VT_STATIC;
        int ft1 = sym->type.ref->f.func_type;
        int ft2 = type->ref->f.func_type;

        /* warn if static follows non-static function declaration */
        if ((type->t & VT_STATIC) && !static_proto
            /* XXX this test for inline shouldn't be here.  Until we
               implement gnu-inline mode again it silences a warning for
               mingw caused by our workarounds.  */
            && !((type->t | sym->type.t) & VT_INLINE))
            tcc_warning("static storage ignored for redefinition of '%s'",
                get_tok_str(sym->v, NULL));

        /* set 'inline' if both agree or if one has static */
        if ((type->t | sym->type.t) & VT_INLINE) {
            if (!((type->t ^ sym->type.t) & VT_INLINE)
             || ((type->t | sym->type.t) & VT_STATIC))
                static_proto |= VT_INLINE;
        }

        if (0 == (type->t & VT_EXTERN)) {
            struct FuncAttr f = sym->type.ref->f;
            /* put complete type, use static from prototype */
            sym->type.t = (type->t & ~(VT_STATIC|VT_INLINE)) | static_proto;
            if (ft1 != FUNC_OLD)
                type->ref->f.func_type = ft1;
            sym->type.ref = type->ref;
            merge_funcattr(&sym->type.ref->f, &f);
        } else {
            sym->type.t &= ~VT_INLINE | static_proto;
            if (ft1 == FUNC_OLD && ft2 != FUNC_OLD)
                sym->type.ref = type->ref;
        }

    } else {
        if ((sym->type.t & VT_ARRAY) && type->ref->c >= 0) {
            /* set array size if it was omitted in extern declaration */
            sym->type.ref->c = type->ref->c;
        }
        if ((type->t ^ sym->type.t) & VT_STATIC)
            tcc_warning("storage mismatch for redefinition of '%s'",
                get_tok_str(sym->v, NULL));
    }
}

/* Merge some storage attributes.  */
static void patch_storage(Sym *sym, AttributeDef *ad, CType *type)
{
    if (type)
        patch_type(sym, type);

#ifdef TCC_TARGET_PE
    if (sym->a.dllimport != ad->a.dllimport)
        tcc_error("incompatible dll linkage for redefinition of '%s'",
            get_tok_str(sym->v, NULL));
#endif
    merge_symattr(&sym->a, &ad->a);
    if (ad->asm_label)
        sym->asm_label = ad->asm_label;
    update_storage(sym);
}

/* copy sym to other stack */
static Sym *sym_copy(Sym *s0, Sym **ps)
{
    Sym *s;
    s = sym_malloc(), *s = *s0;
    s->prev = *ps, *ps = s;
    if ((s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
        sym_link(s, 1);
    return s;
}

/* Symbol 's' was locally declared 'extern' (or as function), and is
   on global_stack.  Now must copy its 'ref' to global_stack too */
static void move_ref_to_global(Sym *s)
{
    Sym *l, **lp;
    int n, bt;

    bt = s->type.t & VT_BTYPE;
    if (!(bt == VT_PTR
       || bt == VT_FUNC
       || bt == VT_STRUCT
       || IS_ENUM(s->type.t)))
        return;

    for (s = s->type.ref, n = 0; s; s = s->next) {
        for (lp = &local_stack; !!(l = *lp); lp = &l->prev) {
            if (l == s) {
                *lp = s->prev;
                s->prev = global_stack, global_stack = s;
                if (n || bt == VT_PTR || bt == VT_FUNC) {
                    move_ref_to_global(s);
                } else {
                    if ((s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
                        /* copy struct/enum tag to local scope */
                        s->v |= SYM_FIELD;
                        l = sym_copy(s, lp);
                        l->v &= ~SYM_FIELD;
                    }
                }
                if (bt != VT_PTR)
                    n = 1;
                break;
            }
        }
        if (n == 0) /* no next (VT_PTR) or ref not on local_stack */
            break;
    }
}

/* define a new external reference to a symbol 'v' */
static Sym *external_sym(int v, CType *type, int r, AttributeDef *ad)
{
    Sym *s;

    /* look for global symbol */
    s = sym_find(v);
    while (s && s->sym_scope)
        s = s->prev_tok;

    if (!s) {
        /* push forward reference */
        s = global_identifier_push(v, type->t, 0);
        s->r |= r;
        s->a = ad->a;
        s->asm_label = ad->asm_label;
        s->type.ref = type->ref;
    } else {
        patch_storage(s, ad, type);
    }
    if (local_stack) {
        /* make sure that type->ref is on global stack */
        move_ref_to_global(s);
        /* put into local scope */
        sym_copy(s, &local_stack);
    }
    return s;
}

/* save registers up to (vtop - n) stack entry */
ST_FUNC void save_regs(int n)
{
    SValue *p, *p1;
    for(p = vstack, p1 = vtop - n; p <= p1; p++)
        save_reg(p->r);
}

/* save r to the memory stack, and mark it as being free */
ST_FUNC void save_reg(int r)
{
    save_reg_upstack(r, 0);
}

/* save r to the memory stack, and mark it as being free,
   if seen up to (vtop - n) stack entry */
ST_FUNC void save_reg_upstack(int r, int n)
{
    int l, size, align, bt, r2;
    SValue *p, *p1, sv;

    if ((r &= VT_VALMASK) >= VT_CONST)
        return;
    if (nocode_wanted)
        return;
    l = r2 = 0;
    for(p = vstack, p1 = vtop - n; p <= p1; p++) {
        if ((p->r & VT_VALMASK) == r || p->r2 == r) {
            /* must save value on stack if not already done */
            if (!l) {
                bt = p->type.t & VT_BTYPE;
                if (bt == VT_VOID)
                    continue;
                if ((p->r & VT_LVAL) || bt == VT_FUNC)
                    bt = VT_PTR;
                sv.type.t = bt;
                size = type_size(&sv.type, &align);
                l = get_temp_local_var(size, align, &r2);
                sv.r = VT_LOCAL | VT_LVAL;
                sv.c.i = l;
		sv.sym = NULL;
                store(p->r & VT_VALMASK, &sv);
#if defined(TCC_TARGET_I386) || defined(TCC_TARGET_X86_64)
                /* x86 specific: need to pop fp register ST0 if saved */
                if (r == TREG_ST0) {
                    o(0xd8dd); /* fstp %st(0) */
                }
#endif
                /* special long long case */
                if (p->r2 < VT_CONST && USING_TWO_WORDS(bt)) {
                    sv.c.i += PTR_SIZE;
                    store(p->r2, &sv);
                }
            }
            /* mark that stack entry as being saved on the stack */
            if (p->r & VT_LVAL) {
                /* also clear the bounded flag because the
                   relocation address of the function was stored in
                   p->c.i */
                p->r = (p->r & ~(VT_VALMASK | VT_BOUNDED)) | VT_LLOCAL;
            } else {
                p->r = VT_LVAL | VT_LOCAL;
                p->type.t &= ~VT_ARRAY; /* cannot combine VT_LVAL with VT_ARRAY */
            }
            p->sym = NULL;
            p->r2 = r2;
            p->c.i = l;
        }
    }
}

#ifdef TCC_TARGET_ARM
/* find a register of class 'rc2' with at most one reference on stack.
 * If none, call get_reg(rc) */
ST_FUNC int get_reg_ex(int rc, int rc2)
{
    int r;
    SValue *p;
    
    for(r=0;r<NB_REGS;r++) {
        if (reg_classes[r] & rc2) {
            int n;
            n=0;
            for(p = vstack; p <= vtop; p++) {
                if ((p->r & VT_VALMASK) == r ||
                    p->r2 == r)
                    n++;
            }
            if (n <= 1)
                return r;
        }
    }
    return get_reg(rc);
}
#endif

/* find a free register of class 'rc'. If none, save one register */
ST_FUNC int get_reg(int rc)
{
    int r;
    SValue *p;

    /* find a free register */
    for(r=0;r<NB_REGS;r++) {
        if (reg_classes[r] & rc) {
            if (nocode_wanted)
                return r;
            for(p=vstack;p<=vtop;p++) {
                if ((p->r & VT_VALMASK) == r ||
                    p->r2 == r)
                    goto notfound;
            }
            return r;
        }
    notfound: ;
    }
    
    /* no register left : free the first one on the stack (VERY
       IMPORTANT to start from the bottom to ensure that we don't
       spill registers used in gen_opi()) */
    for(p=vstack;p<=vtop;p++) {
        /* look at second register (if long long) */
        r = p->r2;
        if (r < VT_CONST && (reg_classes[r] & rc))
            goto save_found;
        r = p->r & VT_VALMASK;
        if (r < VT_CONST && (reg_classes[r] & rc)) {
        save_found:
            save_reg(r);
            return r;
        }
    }
    /* Should never comes here */
    return -1;
}

/* find a free temporary local variable (return the offset on stack) match
   size and align. If none, add new temporary stack variable */
static int get_temp_local_var(int size,int align, int *r2)
{
    int i;
    struct temp_local_variable *temp_var;
    SValue *p;
    int r;
    unsigned used = 0;

    /* mark locations that are still in use */
    for (p = vstack; p <= vtop; p++) {
	r = p->r & VT_VALMASK;
	if (r == VT_LOCAL || r == VT_LLOCAL) {
	    r = p->r2 - (VT_CONST + 1);
	    if (r >= 0 && r < MAX_TEMP_LOCAL_VARIABLE_NUMBER)
	        used |= 1<<r;
	}
    }
    for (i=0;i<nb_temp_local_vars;i++) {
	temp_var=&arr_temp_local_vars[i];
	if(!(used & 1<<i)
	 && temp_var->size>=size
	 && temp_var->align>=align) {
ret_tmp:
	    *r2 = (VT_CONST + 1) + i;
	    return temp_var->location;
	}
    }
    loc = (loc - size) & -align;
    if (nb_temp_local_vars<MAX_TEMP_LOCAL_VARIABLE_NUMBER) {
	temp_var=&arr_temp_local_vars[i];
	temp_var->location=loc;
	temp_var->size=size;
	temp_var->align=align;
	nb_temp_local_vars++;
	goto ret_tmp;
    }
    *r2 = VT_CONST;
    return loc;
}

/* move register 's' (of type 't') to 'r', and flush previous value of r to memory
   if needed */
static void move_reg(int r, int s, int t)
{
    SValue sv;

    if (r != s) {
        save_reg(r);
        sv.type.t = t;
        sv.type.ref = NULL;
        sv.r = s;
        sv.c.i = 0;
        load(r, &sv);
    }
}

/* get address of vtop (vtop MUST BE an lvalue) */
ST_FUNC void gaddrof(void)
{
#ifdef C6_EXT
    c6_value_fallible = 0;      /* gaddrof only applies to lvalues */
#endif
    vtop->r &= ~VT_LVAL;
    /* tricky: if saved lvalue, then we can go back to lvalue */
    if ((vtop->r & VT_VALMASK) == VT_LLOCAL)
        vtop->r = (vtop->r & ~VT_VALMASK) | VT_LOCAL | VT_LVAL;
}

#ifdef CONFIG_TCC_BCHECK
/* generate a bounded pointer addition */
static void gen_bounded_ptr_add(void)
{
    int save = (vtop[-1].r & VT_VALMASK) == VT_LOCAL;
    if (save) {
      vpushv(&vtop[-1]);
      vrott(3);
    }
    vpush_helper_func(TOK___bound_ptr_add);
    vrott(3);
    gfunc_call(2);
    vtop -= save;
    vpushi(0);
    /* returned pointer is in REG_IRET */
    vtop->r = REG_IRET | VT_BOUNDED;
    if (nocode_wanted)
        return;
    /* relocation offset of the bounding function call point */
    vtop->c.i = (cur_text_section->reloc->data_offset - sizeof(ElfW_Rel));
}

/* patch pointer addition in vtop so that pointer dereferencing is
   also tested */
static void gen_bounded_ptr_deref(void)
{
    addr_t func;
    int size, align;
    ElfW_Rel *rel;
    Sym *sym;

    if (nocode_wanted)
        return;

    size = type_size(&vtop->type, &align);
    switch(size) {
    case  1: func = TOK___bound_ptr_indir1; break;
    case  2: func = TOK___bound_ptr_indir2; break;
    case  4: func = TOK___bound_ptr_indir4; break;
    case  8: func = TOK___bound_ptr_indir8; break;
    case 12: func = TOK___bound_ptr_indir12; break;
    case 16: func = TOK___bound_ptr_indir16; break;
    default:
        /* may happen with struct member access */
        return;
    }
    sym = external_helper_sym(func);
    if (!sym->c)
        put_extern_sym(sym, NULL, 0, 0);
    /* patch relocation */
    /* XXX: find a better solution ? */
    rel = (ElfW_Rel *)(cur_text_section->reloc->data + vtop->c.i);
    rel->r_info = ELFW(R_INFO)(sym->c, ELFW(R_TYPE)(rel->r_info));
}

/* generate lvalue bound code */
static void gbound(void)
{
    CType type1;

    vtop->r &= ~VT_MUSTBOUND;
    /* if lvalue, then use checking code before dereferencing */
    if (vtop->r & VT_LVAL) {
        /* if not VT_BOUNDED value, then make one */
        if (!(vtop->r & VT_BOUNDED)) {
            /* must save type because we must set it to int to get pointer */
            type1 = vtop->type;
            vtop->type.t = VT_PTR;
            gaddrof();
            vpushi(0);
            gen_bounded_ptr_add();
            vtop->r |= VT_LVAL;
            vtop->type = type1;
        }
        /* then check for dereferencing */
        gen_bounded_ptr_deref();
    }
}

/* we need to call __bound_ptr_add before we start to load function
   args into registers */
ST_FUNC void gbound_args(int nb_args)
{
    int i, v;
    SValue *sv;

    for (i = 1; i <= nb_args; ++i)
        if (vtop[1 - i].r & VT_MUSTBOUND) {
            vrotb(i);
            gbound();
            vrott(i);
        }

    sv = vtop - nb_args;
    if (sv->r & VT_SYM) {
        v = sv->sym->v;
        if (v == TOK_setjmp
          || v == TOK__setjmp
#ifndef TCC_TARGET_PE
          || v == TOK_sigsetjmp
          || v == TOK___sigsetjmp
#endif
          ) {
            vpush_helper_func(TOK___bound_setjmp);
            vpushv(sv + 1);
            gfunc_call(1);
            func_bound_add_epilog = 1;
        }
        if (v == TOK_alloca)
            func_bound_add_epilog = 1;
#if TARGETOS_NetBSD
        if (v == TOK_longjmp) /* undo rename to __longjmp14 */
            sv->sym->asm_label = TOK___bound_longjmp;
#endif
    }
}

/* Add bounds for local symbols from S to E (via ->prev) */
static void add_local_bounds(Sym *s, Sym *e)
{
    for (; s != e; s = s->prev) {
        if (!s->v || (s->r & VT_VALMASK) != VT_LOCAL)
          continue;
        /* Add arrays/structs/unions because we always take address */
        if ((s->type.t & VT_ARRAY)
            || (s->type.t & VT_BTYPE) == VT_STRUCT
            || s->a.addrtaken) {
            /* add local bound info */
            int align, size = type_size(&s->type, &align);
            addr_t *bounds_ptr = section_ptr_add(lbounds_section,
                                                 2 * sizeof(addr_t));
            bounds_ptr[0] = s->c;
            bounds_ptr[1] = size;
        }
    }
}
#endif

/* add debug info for locals or function parameters, optionally
   register bounds */
static void tcc_debug_end_scope(Sym *b, int bounds)
{
#ifdef CONFIG_TCC_BCHECK
    if (tcc_state->do_bounds_check && bounds)
        add_local_bounds(local_stack, b);
#endif
    tcc_add_debug_info (tcc_state, local_stack, b);
}

/* increment an lvalue pointer */
static void incr_offset(int offset)
{
    int t = vtop->type.t;
    gaddrof(); /* remove VT_LVAL */
    vtop->type.t = VT_PTRDIFF_T; /* set scalar type */
    vpushs(offset);
    gen_op('+');
    vtop->r |= VT_LVAL;
    vtop->type.t = t;
}

static void incr_bf_adr(int o)
{
    vtop->type.t = VT_BYTE | VT_UNSIGNED;
    incr_offset(o);
}

/* single-byte load mode for packed or otherwise unaligned bitfields */
static void load_packed_bf(CType *type, int bit_pos, int bit_size)
{
    int n, o, bits;
    save_reg_upstack(vtop->r, 1);
    vpush64(type->t & VT_BTYPE, 0); // B X
    bits = 0, o = bit_pos >> 3, bit_pos &= 7;
    do {
        vswap(); // X B
        incr_bf_adr(o);
        vdup(); // X B B
        n = 8 - bit_pos;
        if (n > bit_size)
            n = bit_size;
        if (bit_pos)
            vpushi(bit_pos), gen_op(TOK_SHR), bit_pos = 0; // X B Y
        if (n < 8)
            vpushi((1 << n) - 1), gen_op('&');
        gen_cast(type);
        if (bits)
            vpushi(bits), gen_op(TOK_SHL);
        vrotb(3); // B Y X
        gen_op('|'); // B X
        bits += n, bit_size -= n, o = 1;
    } while (bit_size);
    vswap(), vpop();
    if (!(type->t & VT_UNSIGNED)) {
        n = ((type->t & VT_BTYPE) == VT_LLONG ? 64 : 32) - bits;
        vpushi(n), gen_op(TOK_SHL);
        vpushi(n), gen_op(TOK_SAR);
    }
}

/* single-byte store mode for packed or otherwise unaligned bitfields */
static void store_packed_bf(int bit_pos, int bit_size)
{
    int bits, n, o, m, c;
    c = (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    vswap(); // X B
    save_reg_upstack(vtop->r, 1);
    bits = 0, o = bit_pos >> 3, bit_pos &= 7;
    do {
        incr_bf_adr(o); // X B
        vswap(); //B X
        c ? vdup() : gv_dup(); // B V X
        vrott(3); // X B V
        if (bits)
            vpushi(bits), gen_op(TOK_SHR);
        if (bit_pos)
            vpushi(bit_pos), gen_op(TOK_SHL);
        n = 8 - bit_pos;
        if (n > bit_size)
            n = bit_size;
        if (n < 8) {
            m = ((1 << n) - 1) << bit_pos;
            vpushi(m), gen_op('&'); // X B V1
            vpushv(vtop-1); // X B V1 B
            vpushi(m & 0x80 ? ~m & 0x7f : ~m);
            gen_op('&'); // X B V1 B1
            gen_op('|'); // X B V2
        }
        vdup(), vtop[-1] = vtop[-2]; // X B B V2
        vstore(), vpop(); // X B
        bits += n, bit_size -= n, bit_pos = 0, o = 1;
    } while (bit_size);
    vpop(), vpop();
}

static int adjust_bf(SValue *sv, int bit_pos, int bit_size)
{
    int t;
    if (0 == sv->type.ref)
        return 0;
    t = sv->type.ref->auxtype;
    if (t != -1 && t != VT_STRUCT) {
        sv->type.t = (sv->type.t & ~(VT_BTYPE | VT_LONG)) | t;
        sv->r |= VT_LVAL;
    }
    return t;
}

/* store vtop a register belonging to class 'rc'. lvalues are
   converted to values. Cannot be used if cannot be converted to
   register value (such as structures). */
ST_FUNC int gv(int rc)
{
    int r, r2, r_ok, r2_ok, rc2, bt;
    int bit_pos, bit_size, size, align;

    /* NOTE: get_reg can modify vstack[] */
    if (vtop->type.t & VT_BITFIELD) {
        CType type;

        bit_pos = BIT_POS(vtop->type.t);
        bit_size = BIT_SIZE(vtop->type.t);
        /* remove bit field info to avoid loops */
        vtop->type.t &= ~VT_STRUCT_MASK;

        type.ref = NULL;
        type.t = vtop->type.t & VT_UNSIGNED;
        if ((vtop->type.t & VT_BTYPE) == VT_BOOL)
            type.t |= VT_UNSIGNED;

        r = adjust_bf(vtop, bit_pos, bit_size);

        if ((vtop->type.t & VT_BTYPE) == VT_LLONG)
            type.t |= VT_LLONG;
        else
            type.t |= VT_INT;

        if (r == VT_STRUCT) {
            load_packed_bf(&type, bit_pos, bit_size);
        } else {
            int bits = (type.t & VT_BTYPE) == VT_LLONG ? 64 : 32;
            /* cast to int to propagate signedness in following ops */
            gen_cast(&type);
            /* generate shifts */
            vpushi(bits - (bit_pos + bit_size));
            gen_op(TOK_SHL);
            vpushi(bits - bit_size);
            /* NOTE: transformed to SHR if unsigned */
            gen_op(TOK_SAR);
        }
        r = gv(rc);
    } else {
        if (is_float(vtop->type.t) && 
            (vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST) {
            /* CPUs usually cannot use float constants, so we store them
               generically in data segment */
            init_params p = { rodata_section };
            unsigned long offset;
            size = type_size(&vtop->type, &align);
            if (NODATA_WANTED)
                size = 0, align = 1;
            offset = section_add(p.sec, size, align);
            vpush_ref(&vtop->type, p.sec, offset, size);
	    vswap();
	    init_putv(&p, &vtop->type, offset);
	    vtop->r |= VT_LVAL;
        }
#ifdef CONFIG_TCC_BCHECK
        if (vtop->r & VT_MUSTBOUND) 
            gbound();
#endif

        bt = vtop->type.t & VT_BTYPE;
        if (bt == VT_VOID || bt == VT_STRUCT) /* should not happen */
            return vtop->r;

#ifdef TCC_TARGET_RISCV64
        /* XXX mega hack */
        if (bt == VT_LDOUBLE && rc == RC_FLOAT)
          rc = RC_INT;
#endif
        rc2 = RC2_TYPE(bt, rc);

        /* need to reload if:
           - constant
           - lvalue (need to dereference pointer)
           - already a register, but not in the right class */
        r = vtop->r & VT_VALMASK;
        r_ok = !(vtop->r & VT_LVAL) && (r < VT_CONST) && (reg_classes[r] & rc);
        r2_ok = !rc2 || ((vtop->r2 < VT_CONST) && (reg_classes[vtop->r2] & rc2));

        if (!r_ok || !r2_ok) {

            if (!r_ok) {
                if (1 /* we can 'mov (r),r' in cases */
                    && r < VT_CONST
                    && (reg_classes[r] & rc)
                    && !rc2
                    )
                    save_reg_upstack(r, 1);
                else
                    r = get_reg(rc);
            }

            if (rc2) {
                int load_type = (bt == VT_QFLOAT) ? VT_DOUBLE : VT_PTRDIFF_T;
                int original_type = vtop->type.t;

                /* two register type load :
                   expand to two words temporarily */
                if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST) {
                    /* load constant */
                    unsigned long long ll = vtop->c.i;
                    vtop->c.i = ll; /* first word */
                    load(r, vtop);
                    vtop->r = r; /* save register value */
                    vpushi(ll >> 32); /* second word */
                } else if (vtop->r & VT_LVAL) {
                    /* We do not want to modifier the long long pointer here.
                       So we save any other instances down the stack */
                    save_reg_upstack(vtop->r, 1);
                    /* load from memory */
                    vtop->type.t = load_type;
                    load(r, vtop);
                    vdup();
                    vtop[-1].r = r; /* save register value */
                    /* increment pointer to get second word */
                    incr_offset(PTR_SIZE);
                } else {
                    /* move registers */
                    if (!r_ok)
                        load(r, vtop);
                    if (r2_ok && vtop->r2 < VT_CONST)
                        goto done;
                    vdup();
                    vtop[-1].r = r; /* save register value */
                    vtop->r = vtop[-1].r2;
                }
                /* Allocate second register. Here we rely on the fact that
                   get_reg() tries first to free r2 of an SValue. */
                r2 = get_reg(rc2);
                load(r2, vtop);
                vpop();
                /* write second register */
                vtop->r2 = r2;
            done:
                vtop->type.t = original_type;
            } else {
                if (vtop->r == VT_CMP)
                    vset_VT_JMP();
                /* one register type load */
                load(r, vtop);
            }
        }
        vtop->r = r;
#ifdef TCC_TARGET_C67
        /* uses register pairs for doubles */
        if (bt == VT_DOUBLE)
            vtop->r2 = r+1;
#endif
    }
    return r;
}

/* generate vtop[-1] and vtop[0] in resp. classes rc1 and rc2 */
ST_FUNC void gv2(int rc1, int rc2)
{
    /* generate more generic register first. But VT_JMP or VT_CMP
       values must be generated first in all cases to avoid possible
       reload errors */
    if (vtop->r != VT_CMP && rc1 <= rc2) {
        vswap();
        gv(rc1);
        vswap();
        gv(rc2);
        /* test if reload is needed for first register */
        if ((vtop[-1].r & VT_VALMASK) >= VT_CONST) {
            vswap();
            gv(rc1);
            vswap();
        }
    } else {
        gv(rc2);
        vswap();
        gv(rc1);
        vswap();
        /* test if reload is needed for first register */
        if ((vtop[0].r & VT_VALMASK) >= VT_CONST) {
            gv(rc2);
        }
    }
}

#if PTR_SIZE == 4
/* expand 64bit on stack in two ints */
ST_FUNC void lexpand(void)
{
    int u, v;
    u = vtop->type.t & (VT_DEFSIGN | VT_UNSIGNED);
    v = vtop->r & (VT_VALMASK | VT_LVAL);
    if (v == VT_CONST) {
        vdup();
        vtop[0].c.i >>= 32;
    } else if (v == (VT_LVAL|VT_CONST) || v == (VT_LVAL|VT_LOCAL)) {
        vdup();
        vtop[0].c.i += 4;
    } else {
        gv(RC_INT);
        vdup();
        vtop[0].r = vtop[-1].r2;
        vtop[0].r2 = vtop[-1].r2 = VT_CONST;
    }
    vtop[0].type.t = vtop[-1].type.t = VT_INT | u;
}
#endif

#if PTR_SIZE == 4
/* build a long long from two ints */
static void lbuild(int t)
{
    gv2(RC_INT, RC_INT);
    vtop[-1].r2 = vtop[0].r;
    vtop[-1].type.t = t;
    vpop();
}
#endif

/* convert stack entry to register and duplicate its value in another
   register */
static void gv_dup(void)
{
    int t, rc, r;

    t = vtop->type.t;
#if PTR_SIZE == 4
    if ((t & VT_BTYPE) == VT_LLONG) {
        if (t & VT_BITFIELD) {
            gv(RC_INT);
            t = vtop->type.t;
        }
        lexpand();
        gv_dup();
        vswap();
        vrotb(3);
        gv_dup();
        vrotb(4);
        /* stack: H L L1 H1 */
        lbuild(t);
        vrotb(3);
        vrotb(3);
        vswap();
        lbuild(t);
        vswap();
        return;
    }
#endif
    /* duplicate value */
    rc = RC_TYPE(t);
    gv(rc);
    r = get_reg(rc);
    vdup();
    load(r, vtop);
    vtop->r = r;
}

#if PTR_SIZE == 4
/* generate CPU independent (unsigned) long long operations */
static void gen_opl(int op)
{
    int t, a, b, op1, c, i;
    int func;
    unsigned short reg_iret = REG_IRET;
    unsigned short reg_lret = REG_IRE2;
    SValue tmp;

    switch(op) {
    case '/':
    case TOK_PDIV:
        func = TOK___divdi3;
        goto gen_func;
    case TOK_UDIV:
        func = TOK___udivdi3;
        goto gen_func;
    case '%':
        func = TOK___moddi3;
        goto gen_mod_func;
    case TOK_UMOD:
        func = TOK___umoddi3;
    gen_mod_func:
#ifdef TCC_ARM_EABI
        reg_iret = TREG_R2;
        reg_lret = TREG_R3;
#endif
    gen_func:
        /* call generic long long function */
        vpush_helper_func(func);
        vrott(3);
        gfunc_call(2);
        vpushi(0);
        vtop->r = reg_iret;
        vtop->r2 = reg_lret;
        break;
    case '^':
    case '&':
    case '|':
    case '*':
    case '+':
    case '-':
        //pv("gen_opl A",0,2);
        t = vtop->type.t;
        vswap();
        lexpand();
        vrotb(3);
        lexpand();
        /* stack: L1 H1 L2 H2 */
        tmp = vtop[0];
        vtop[0] = vtop[-3];
        vtop[-3] = tmp;
        tmp = vtop[-2];
        vtop[-2] = vtop[-3];
        vtop[-3] = tmp;
        vswap();
        /* stack: H1 H2 L1 L2 */
        //pv("gen_opl B",0,4);
        if (op == '*') {
            vpushv(vtop - 1);
            vpushv(vtop - 1);
            gen_op(TOK_UMULL);
            lexpand();
            /* stack: H1 H2 L1 L2 ML MH */
            for(i=0;i<4;i++)
                vrotb(6);
            /* stack: ML MH H1 H2 L1 L2 */
            tmp = vtop[0];
            vtop[0] = vtop[-2];
            vtop[-2] = tmp;
            /* stack: ML MH H1 L2 H2 L1 */
            gen_op('*');
            vrotb(3);
            vrotb(3);
            gen_op('*');
            /* stack: ML MH M1 M2 */
            gen_op('+');
            gen_op('+');
        } else if (op == '+' || op == '-') {
            /* XXX: add non carry method too (for MIPS or alpha) */
            if (op == '+')
                op1 = TOK_ADDC1;
            else
                op1 = TOK_SUBC1;
            gen_op(op1);
            /* stack: H1 H2 (L1 op L2) */
            vrotb(3);
            vrotb(3);
            gen_op(op1 + 1); /* TOK_xxxC2 */
        } else {
            gen_op(op);
            /* stack: H1 H2 (L1 op L2) */
            vrotb(3);
            vrotb(3);
            /* stack: (L1 op L2) H1 H2 */
            gen_op(op);
            /* stack: (L1 op L2) (H1 op H2) */
        }
        /* stack: L H */
        lbuild(t);
        break;
    case TOK_SAR:
    case TOK_SHR:
    case TOK_SHL:
        if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
            t = vtop[-1].type.t;
            vswap();
            lexpand();
            vrotb(3);
            /* stack: L H shift */
            c = (int)vtop->c.i;
            /* constant: simpler */
            /* NOTE: all comments are for SHL. the other cases are
               done by swapping words */
            vpop();
            if (op != TOK_SHL)
                vswap();
            if (c >= 32) {
                /* stack: L H */
                vpop();
                if (c > 32) {
                    vpushi(c - 32);
                    gen_op(op);
                }
                if (op != TOK_SAR) {
                    vpushi(0);
                } else {
                    gv_dup();
                    vpushi(31);
                    gen_op(TOK_SAR);
                }
                vswap();
            } else {
                vswap();
                gv_dup();
                /* stack: H L L */
                vpushi(c);
                gen_op(op);
                vswap();
                vpushi(32 - c);
                if (op == TOK_SHL)
                    gen_op(TOK_SHR);
                else
                    gen_op(TOK_SHL);
                vrotb(3);
                /* stack: L L H */
                vpushi(c);
                if (op == TOK_SHL)
                    gen_op(TOK_SHL);
                else
                    gen_op(TOK_SHR);
                gen_op('|');
            }
            if (op != TOK_SHL)
                vswap();
            lbuild(t);
        } else {
            /* XXX: should provide a faster fallback on x86 ? */
            switch(op) {
            case TOK_SAR:
                func = TOK___ashrdi3;
                goto gen_func;
            case TOK_SHR:
                func = TOK___lshrdi3;
                goto gen_func;
            case TOK_SHL:
                func = TOK___ashldi3;
                goto gen_func;
            }
        }
        break;
    default:
        /* compare operations */
        t = vtop->type.t;
        vswap();
        lexpand();
        vrotb(3);
        lexpand();
        /* stack: L1 H1 L2 H2 */
        tmp = vtop[-1];
        vtop[-1] = vtop[-2];
        vtop[-2] = tmp;
        /* stack: L1 L2 H1 H2 */
        if (!cur_switch || cur_switch->bsym) {
            /* avoid differnt registers being saved in branches.
               This is not needed when comparing switch cases */
            save_regs(4);
        }
        /* compare high */
        op1 = op;
        /* when values are equal, we need to compare low words. since
           the jump is inverted, we invert the test too. */
        if (op1 == TOK_LT)
            op1 = TOK_LE;
        else if (op1 == TOK_GT)
            op1 = TOK_GE;
        else if (op1 == TOK_ULT)
            op1 = TOK_ULE;
        else if (op1 == TOK_UGT)
            op1 = TOK_UGE;
        a = 0;
        b = 0;
        gen_op(op1);
        if (op == TOK_NE) {
            b = gvtst(0, 0);
        } else {
            a = gvtst(1, 0);
            if (op != TOK_EQ) {
                /* generate non equal test */
                vpushi(0);
                vset_VT_CMP(TOK_NE);
                b = gvtst(0, 0);
            }
        }
        /* compare low. Always unsigned */
        op1 = op;
        if (op1 == TOK_LT)
            op1 = TOK_ULT;
        else if (op1 == TOK_LE)
            op1 = TOK_ULE;
        else if (op1 == TOK_GT)
            op1 = TOK_UGT;
        else if (op1 == TOK_GE)
            op1 = TOK_UGE;
        gen_op(op1);
#if 0//def TCC_TARGET_I386
        if (op == TOK_NE) { gsym(b); break; }
        if (op == TOK_EQ) { gsym(a); break; }
#endif
        gvtst_set(1, a);
        gvtst_set(0, b);
        break;
    }
}
#endif

/* normalize values */
static uint64_t value64(uint64_t l1, int t)
{
    if ((t & VT_BTYPE) == VT_LLONG
        || (PTR_SIZE == 8 && (t & VT_BTYPE) == VT_PTR))
        return l1;
    else if (t & VT_UNSIGNED)
        return (uint32_t)l1;
    else
        return (uint32_t)l1 | -(l1 & 0x80000000);
}

static uint64_t gen_opic_sdiv(uint64_t a, uint64_t b)
{
    uint64_t x = (a >> 63 ? -a : a) / (b >> 63 ? -b : b);
    return (a ^ b) >> 63 ? -x : x;
}

static int gen_opic_lt(uint64_t a, uint64_t b)
{
    return (a ^ (uint64_t)1 << 63) < (b ^ (uint64_t)1 << 63);
}

/* handle integer constant optimizations and various machine
   independent opt */
static void gen_opic(int op)
{
    SValue *v1 = vtop - 1;
    SValue *v2 = vtop;
    int t1 = v1->type.t & VT_BTYPE;
    int t2 = v2->type.t & VT_BTYPE;
    int c1 = (v1->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    int c2 = (v2->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    uint64_t l1 = c1 ? value64(v1->c.i, v1->type.t) : 0;
    uint64_t l2 = c2 ? value64(v2->c.i, v2->type.t) : 0;
    int shm = (t1 == VT_LLONG) ? 63 : 31;
    int r;

    if (c1 && c2) {
        switch(op) {
        case '+': l1 += l2; break;
        case '-': l1 -= l2; break;
        case '&': l1 &= l2; break;
        case '^': l1 ^= l2; break;
        case '|': l1 |= l2; break;
        case '*': l1 *= l2; break;

        case TOK_PDIV:
        case '/':
        case '%':
        case TOK_UDIV:
        case TOK_UMOD:
            /* if division by zero, generate explicit division */
            if (l2 == 0) {
                if (CONST_WANTED && !NOEVAL_WANTED)
                    tcc_error("division by zero in constant");
                goto general_case;
            }
            switch(op) {
            default: l1 = gen_opic_sdiv(l1, l2); break;
            case '%': l1 = l1 - l2 * gen_opic_sdiv(l1, l2); break;
            case TOK_UDIV: l1 = l1 / l2; break;
            case TOK_UMOD: l1 = l1 % l2; break;
            }
            break;
        case TOK_SHL: l1 <<= (l2 & shm); break;
        case TOK_SHR: l1 >>= (l2 & shm); break;
        case TOK_SAR:
            l1 = (l1 >> 63) ? ~(~l1 >> (l2 & shm)) : l1 >> (l2 & shm);
            break;
            /* tests */
        case TOK_ULT: l1 = l1 < l2; break;
        case TOK_UGE: l1 = l1 >= l2; break;
        case TOK_EQ: l1 = l1 == l2; break;
        case TOK_NE: l1 = l1 != l2; break;
        case TOK_ULE: l1 = l1 <= l2; break;
        case TOK_UGT: l1 = l1 > l2; break;
        case TOK_LT: l1 = gen_opic_lt(l1, l2); break;
        case TOK_GE: l1 = !gen_opic_lt(l1, l2); break;
        case TOK_LE: l1 = !gen_opic_lt(l2, l1); break;
        case TOK_GT: l1 = gen_opic_lt(l2, l1); break;
            /* logical */
        case TOK_LAND: l1 = l1 && l2; break;
        case TOK_LOR: l1 = l1 || l2; break;
        default:
            goto general_case;
        }
        v1->c.i = value64(l1, v1->type.t);
        v1->r |= v2->r & VT_NONCONST;
        vtop--;
    } else {
        /* if commutative ops, put c2 as constant */
        if (c1 && (op == '+' || op == '&' || op == '^' || 
                   op == '|' || op == '*' || op == TOK_EQ || op == TOK_NE)) {
            vswap();
            c2 = c1; //c = c1, c1 = c2, c2 = c;
            l2 = l1; //l = l1, l1 = l2, l2 = l;
        }
        if (c1 && ((l1 == 0 &&
                    (op == TOK_SHL || op == TOK_SHR || op == TOK_SAR)) ||
                   (l1 == -1 && op == TOK_SAR))) {
            /* treat (0 << x), (0 >> x) and (-1 >> x) as constant */
            vpop();
        } else if (c2 && ((l2 == 0 && (op == '&' || op == '*')) ||
                          (op == '|' &&
                            (l2 == -1 || (l2 == 0xFFFFFFFF && t2 != VT_LLONG))) ||
                          (l2 == 1 && (op == '%' || op == TOK_UMOD)))) {
            /* treat (x & 0), (x * 0), (x | -1) and (x % 1) as constant */
            if (l2 == 1)
                vtop->c.i = 0;
            vswap();
            vtop--;
        } else if (c2 && (((op == '*' || op == '/' || op == TOK_UDIV ||
                          op == TOK_PDIV) &&
                           l2 == 1) ||
                          ((op == '+' || op == '-' || op == '|' || op == '^' ||
                            op == TOK_SHL || op == TOK_SHR || op == TOK_SAR) &&
                           l2 == 0) ||
                          (op == '&' &&
                            (l2 == -1 || (l2 == 0xFFFFFFFF && t2 != VT_LLONG))))) {
            /* filter out NOP operations like x*1, x-0, x&-1... */
            vtop--;
        } else if (c2 && (op == '*' || op == TOK_PDIV || op == TOK_UDIV || op == TOK_UMOD)) {
            /* try to use shifts instead of muls or divs */
            if (l2 > 0 && (l2 & (l2 - 1)) == 0) {
                int n = -1;
                if (op == TOK_UMOD) {
                    vtop->c.i = l2 - 1;
                    op = '&';
                    goto general_case;
                }
                while (l2) {
                    l2 >>= 1;
                    n++;
                }
                vtop->c.i = n;
                if (op == '*')
                    op = TOK_SHL;
                else if (op == TOK_PDIV)
                    op = TOK_SAR;
                else
                    op = TOK_SHR;
            }
            goto general_case;
        } else if (c2 && (op == '+' || op == '-') &&
                   (r = vtop[-1].r & (VT_VALMASK | VT_LVAL | VT_SYM),
                    r == (VT_CONST | VT_SYM) || r == VT_LOCAL)) {
            /* symbol + constant case */
            if (op == '-')
                l2 = -l2;
	    l2 += vtop[-1].c.i;
	    /* The backends can't always deal with addends to symbols
	       larger than +-1<<31.  Don't construct such.  */
	    if ((int)l2 != l2)
	        goto general_case;
            vtop--;
            vtop->c.i = l2;
        } else {
        general_case:
                /* call low level op generator */
                if (t1 == VT_LLONG || t2 == VT_LLONG ||
                    (PTR_SIZE == 8 && (t1 == VT_PTR || t2 == VT_PTR)))
                    gen_opl(op);
                else
                    gen_opi(op);
        }
        if (vtop->r == VT_CONST)
            vtop->r |= VT_NONCONST; /* is const, but only by optimization */
    }
}

#if defined TCC_TARGET_X86_64 || defined TCC_TARGET_I386 || defined TCC_TARGET_ARM64
# define gen_negf gen_opf
#elif defined TCC_TARGET_ARM
void gen_negf(int op)
{
    /* arm will detect 0-x and replace by vneg */
    vpushi(0), vswap(), gen_op('-');
}
#else
/* XXX: implement in gen_opf() for other backends too */
void gen_negf(int op)
{
    /* In IEEE negate(x) isn't subtract(0,x).  Without NaNs it's
       subtract(-0, x), but with them it's really a sign flip
       operation.  We implement this with bit manipulation and have
       to do some type reinterpretation for this, which TCC can do
       only via memory.  */
    int align, size, bt;
    size = type_size(&vtop->type, &align);
    bt = vtop->type.t & VT_BTYPE;
#if defined TCC_TARGET_X86_64 || defined TCC_TARGET_I386
    /* sizeof long double is 12 or 16 here, but it's really the 80bit
       extended float format.  */
    if (bt == VT_LDOUBLE)
        size = 10;
#endif
    if (nocode_wanted) /* save_reg() wont work */
        goto gv2;
    save_reg(gv(RC_TYPE(bt)));
    vdup();
    incr_bf_adr(size - 1);
    vdup();
    vpushi(0x80); /* flip sign */
    gen_op('^');
    vstore();
    vpop();
gv2:
    gv(RC_TYPE(bt)); /* -n is not a lvalue */
}
#endif

/* generate a floating point operation with constant propagation */
static void gen_opif(int op)
{
    int c1, c2, i, bt;
    SValue *v1, *v2;
#if defined _MSC_VER && defined __x86_64__
    /* avoid bad optimization with f1 -= f2 for f1:-0.0, f2:0.0 */
    volatile
#endif
    long double f1, f2;

    v1 = vtop - 1;
    v2 = vtop;
    if (op == TOK_NEG)
        v1 = v2;
    bt = v1->type.t & VT_BTYPE;

    /* currently, we cannot do computations with forward symbols */
    c1 = (v1->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    c2 = (v2->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    if (c1 && c2) {
        if (bt == VT_FLOAT) {
            f1 = v1->c.f;
            f2 = v2->c.f;
        } else if (bt == VT_DOUBLE) {
            f1 = v1->c.d;
            f2 = v2->c.d;
        } else {
            f1 = v1->c.ld;
            f2 = v2->c.ld;
        }
        /* NOTE: we only do constant propagation if finite number (not
           NaN or infinity) (ANSI spec) */
        if (!(ieee_finite(f1) || !ieee_finite(f2)) && !CONST_WANTED)
            goto general_case;
        switch(op) {
        case '+': f1 += f2; break;
        case '-': f1 -= f2; break;
        case '*': f1 *= f2; break;
        case '/': 
            if (f2 == 0.0) {
                union { float f; unsigned u; } x1, x2, y;
		/* If not in initializer we need to potentially generate
		   FP exceptions at runtime, otherwise we want to fold.  */
                if (!CONST_WANTED)
                    goto general_case;
                /* the run-time result of 0.0/0.0 on x87, also of other compilers
                   when used to compile the f1 /= f2 below, would be -nan */
                x1.f = f1, x2.f = f2;
                if (f1 == 0.0)
                    y.u = 0x7fc00000; /* nan */
                else
                    y.u = 0x7f800000; /* infinity */
                y.u |= (x1.u ^ x2.u) & 0x80000000; /* set sign */
                f1 = y.f;
                break;
            }
            f1 /= f2;
            break;
        case TOK_NEG:
            f1 = -f1;
            goto unary_result;
        case TOK_EQ:
            i = f1 == f2;
	make_int:
            vtop -= 2;
            vpushi(i);
            return;
        case TOK_NE:
            i = f1 != f2;
	    goto make_int;
        case TOK_LT:
            i = f1 < f2;
	    goto make_int;
        case TOK_GE:
            i = f1 >= f2;
	    goto make_int;
        case TOK_LE:
            i = f1 <= f2;
	    goto make_int;
        case TOK_GT:
            i = f1 > f2;
	    goto make_int;
        default:
            goto general_case;
        }
        vtop--;
    unary_result:
        /* XXX: overflow test ? */
        if (bt == VT_FLOAT) {
            v1->c.f = f1;
        } else if (bt == VT_DOUBLE) {
            v1->c.d = f1;
        } else {
            v1->c.ld = f1;
        }
    } else {
    general_case:
        if (op == TOK_NEG) {
            gen_negf(op);
        } else {
            gen_opf(op);
        }
    }
}

/* print a type. If 'varstr' is not NULL, then the variable is also
   printed in the type */
/* XXX: union */
/* XXX: add array and function pointers */
static void type_to_str(char *buf, int buf_size,
                 CType *type, const char *varstr)
{
    int bt, v, t;
    Sym *s, *sa;
    char buf1[256];
    const char *tstr;

    t = type->t;
    bt = t & VT_BTYPE;
    buf[0] = '\0';

    if (t & VT_EXTERN)
        pstrcat(buf, buf_size, "extern ");
    if (t & VT_STATIC)
        pstrcat(buf, buf_size, "static ");
    if (t & VT_TYPEDEF)
        pstrcat(buf, buf_size, "typedef ");
    if (t & VT_INLINE)
        pstrcat(buf, buf_size, "inline ");
    if (bt != VT_PTR) {
        if (t & VT_RESTRICT)
            pstrcat(buf, buf_size, "restrict ");
        if (t & VT_VOLATILE)
            pstrcat(buf, buf_size, "volatile ");
        if (t & VT_CONSTANT)
            pstrcat(buf, buf_size, "const ");
    }
    if (((t & VT_DEFSIGN) && bt == VT_BYTE)
        || ((t & VT_UNSIGNED)
            && (bt == VT_SHORT || bt == VT_INT || bt == VT_LLONG)
            && !IS_ENUM(t)
            ))
        pstrcat(buf, buf_size, (t & VT_UNSIGNED) ? "unsigned " : "signed ");

    buf_size -= strlen(buf);
    buf += strlen(buf);

    switch(bt) {
    case VT_VOID:
        tstr = "void";
        goto add_tstr;
    case VT_BOOL:
        tstr = "_Bool";
        goto add_tstr;
    case VT_BYTE:
        tstr = "char";
        goto add_tstr;
    case VT_SHORT:
        tstr = "short";
        goto add_tstr;
    case VT_INT:
        tstr = "int";
        goto maybe_long;
    case VT_LLONG:
        tstr = "long long";
    maybe_long:
        if (t & VT_LONG)
            tstr = "long";
        if (!IS_ENUM(t))
            goto add_tstr;
        tstr = "enum ";
        goto tstruct;
    case VT_FLOAT:
        tstr = "float";
        goto add_tstr;
    case VT_DOUBLE:
        tstr = "double";
        if (!(t & VT_LONG))
            goto add_tstr;
    case VT_LDOUBLE:
        tstr = "long double";
    add_tstr:
        pstrcat(buf, buf_size, tstr);
        break;
    case VT_STRUCT:
#ifdef C6_EXT
        /* a slice prints as 'elem[:]', so that diagnostics talk about the
           type the user wrote instead of the internal struct tag */
        {
            C6Slice *sl = c6_slice_of_type(type);
            if (sl) {
                type_to_str(buf, buf_size, &sl->elem, NULL);
                pstrcat(buf, buf_size, "[:]");
                break;
            }
        }
        /* a tuple prints as '(T1, T2)', not as its internal struct tag */
        {
            C6Tuple *tp = c6_tuple_of_type(type);
            if (tp) {
                char ebuf[256];
                int i;
                pstrcat(buf, buf_size, "(");
                for (i = 0; i < tp->n; i++) {
                    if (i)
                        pstrcat(buf, buf_size, ", ");
                    type_to_str(ebuf, sizeof ebuf, &tp->elem[i], NULL);
                    pstrcat(buf, buf_size, ebuf);
                }
                pstrcat(buf, buf_size, ")");
                break;
            }
        }
#endif
        tstr = "struct ";
        if (IS_UNION(t))
            tstr = "union ";
    tstruct:
        pstrcat(buf, buf_size, tstr);
        v = type->ref->v & ~SYM_STRUCT;
        if (v >= SYM_FIRST_ANOM)
            pstrcat(buf, buf_size, "<anonymous>");
        else
            pstrcat(buf, buf_size, get_tok_str(v, NULL));
        break;
    case VT_FUNC:
        s = type->ref;
        buf1[0]=0;
        if (varstr && '*' == *varstr) {
            pstrcat(buf1, sizeof(buf1), "(");
            pstrcat(buf1, sizeof(buf1), varstr);
            pstrcat(buf1, sizeof(buf1), ")");
        }
        pstrcat(buf1, sizeof(buf1), "(");
        sa = s->next;
        while (sa != NULL) {
            char buf2[256];
            type_to_str(buf2, sizeof(buf2), &sa->type, NULL);
            pstrcat(buf1, sizeof(buf1), buf2);
            sa = sa->next;
            if (sa)
                pstrcat(buf1, sizeof(buf1), ", ");
        }
        if (s->f.func_type == FUNC_ELLIPSIS)
            pstrcat(buf1, sizeof(buf1), ", ...");
        pstrcat(buf1, sizeof(buf1), ")");
        type_to_str(buf, buf_size, &s->type, buf1);
        goto no_var;
    case VT_PTR:
        s = type->ref;
        if (t & (VT_ARRAY|VT_VLA)) {
            if (varstr && '*' == *varstr)
                snprintf(buf1, sizeof(buf1), "(%s)[%d]", varstr, s->c);
            else
                snprintf(buf1, sizeof(buf1), "%s[%d]", varstr ? varstr : "", s->c);
            type_to_str(buf, buf_size, &s->type, buf1);
            goto no_var;
        }
#ifdef C6_EXT
        /* a managed reference prints as '@T' rather than '*' */
        if (c6_type_gq(type) & C6_GQ_MANAGED) {
            int l, gq = c6_type_gq(type);
            if (gq & C6_GQ_WEAK)
                pstrcat(buf, buf_size, "weak ");
            if (gq & C6_GQ_OWN)
                pstrcat(buf, buf_size, "own ");
            if (gq & C6_GQ_SHARED)
                pstrcat(buf, buf_size, "shared ");
            pstrcat(buf, buf_size, "@");
            l = strlen(buf);
            pstrcpy(buf1, sizeof(buf1), varstr ? varstr : "");
            type_to_str(buf + l, buf_size - l, &s->type, varstr ? buf1 : NULL);
            goto no_var;
        }
#endif
        pstrcpy(buf1, sizeof(buf1), "*");
        if (t & VT_RESTRICT)
            pstrcat(buf1, sizeof(buf1), "restrict ");
        if (t & VT_CONSTANT)
            pstrcat(buf1, sizeof(buf1), "const ");
        if (t & VT_VOLATILE)
            pstrcat(buf1, sizeof(buf1), "volatile ");
        if (varstr)
            pstrcat(buf1, sizeof(buf1), varstr);
        type_to_str(buf, buf_size, &s->type, buf1);
        goto no_var;
    }
    if (varstr) {
        pstrcat(buf, buf_size, " ");
        pstrcat(buf, buf_size, varstr);
    }
 no_var: ;
}

static void type_incompatibility_error(CType* st, CType* dt, const char* fmt)
{
    char buf1[256], buf2[256];
    type_to_str(buf1, sizeof(buf1), st, NULL);
    type_to_str(buf2, sizeof(buf2), dt, NULL);
    tcc_error(fmt, buf1, buf2);
}

static void type_incompatibility_warning(CType* st, CType* dt, const char* fmt)
{
    char buf1[256], buf2[256];
    type_to_str(buf1, sizeof(buf1), st, NULL);
    type_to_str(buf2, sizeof(buf2), dt, NULL);
    tcc_warning(fmt, buf1, buf2);
}

static int pointed_size(CType *type)
{
    int align;
    return type_size(pointed_type(type), &align);
}

static inline int is_null_pointer(SValue *p)
{
    if ((p->r & (VT_VALMASK | VT_LVAL | VT_SYM | VT_NONCONST)) != VT_CONST)
        return 0;
    return ((p->type.t & VT_BTYPE) == VT_INT && (uint32_t)p->c.i == 0) ||
        ((p->type.t & VT_BTYPE) == VT_LLONG && p->c.i == 0) ||
        ((p->type.t & VT_BTYPE) == VT_PTR &&
         (PTR_SIZE == 4 ? (uint32_t)p->c.i == 0 : p->c.i == 0) &&
         ((pointed_type(&p->type)->t & VT_BTYPE) == VT_VOID) &&
         0 == (pointed_type(&p->type)->t & VT_QUAL)
         );
}

/* compare function types. OLD functions match any new functions */
static int is_compatible_func(CType *type1, CType *type2)
{
    Sym *s1, *s2;

    s1 = type1->ref;
    s2 = type2->ref;
    if (s1->f.func_call != s2->f.func_call)
        return 0;
    if (s1->f.func_type != s2->f.func_type
        && s1->f.func_type != FUNC_OLD
        && s2->f.func_type != FUNC_OLD)
        return 0;
    for (;;) {
        if (!is_compatible_unqualified_types(&s1->type, &s2->type))
            return 0;
        if (s1->f.func_type == FUNC_OLD || s2->f.func_type == FUNC_OLD )
            return 1;
        s1 = s1->next;
        s2 = s2->next;
        if (!s1)
            return !s2;
        if (!s2)
            return 0;
    }
}

/* return true if type1 and type2 are the same.  If unqualified is
   true, qualifiers on the types are ignored.
 */
static int compare_types(CType *type1, CType *type2, int unqualified)
{
    int bt1, t1, t2;

    if (IS_ENUM(type1->t)) {
        if (IS_ENUM(type2->t))
            return type1->ref == type2->ref;
        type1 = &type1->ref->type;
    } else if (IS_ENUM(type2->t))
        type2 = &type2->ref->type;

    t1 = type1->t & VT_TYPE;
    t2 = type2->t & VT_TYPE;
    if (unqualified) {
        /* strip qualifiers before comparing */
        t1 &= ~VT_QUAL;
        t2 &= ~VT_QUAL;
    }

    /* Default Vs explicit signedness only matters for char */
    if ((t1 & VT_BTYPE) != VT_BYTE) {
        t1 &= ~VT_DEFSIGN;
        t2 &= ~VT_DEFSIGN;
    }
    /* XXX: bitfields ? */
    if (t1 != t2)
        return 0;

    if ((t1 & VT_ARRAY)
        && !(type1->ref->c < 0
          || type2->ref->c < 0
          || type1->ref->c == type2->ref->c))
            return 0;

    /* test more complicated cases */
    bt1 = t1 & VT_BTYPE;
    if (bt1 == VT_PTR) {
        type1 = pointed_type(type1);
        type2 = pointed_type(type2);
        if (unqualified && (t1 & VT_ARRAY))
            return compare_types(type1, type2, 1);
        return is_compatible_types(type1, type2);
    } else if (bt1 == VT_STRUCT) {
        return (type1->ref == type2->ref);
    } else if (bt1 == VT_FUNC) {
        return is_compatible_func(type1, type2);
    } else {
        return 1;
    }
}

#define CMP_OP 'C'
#define SHIFT_OP 'S'

/* Check if OP1 and OP2 can be "combined" with operation OP, the combined
   type is stored in DEST if non-null (except for pointer plus/minus) . */
static int combine_types(CType *dest, SValue *op1, SValue *op2, int op)
{
    CType *type1, *type2, type;
    int t1, t2, bt1, bt2;
    int ret = 1;

    /* for shifts, 'combine' only left operand */
    if (op == SHIFT_OP)
        op2 = op1;

    type1 = &op1->type, type2 = &op2->type;
    t1 = type1->t, t2 = type2->t;
    bt1 = t1 & VT_BTYPE, bt2 = t2 & VT_BTYPE;

    type.t = VT_VOID;
    type.ref = NULL;

    if (bt1 == VT_VOID || bt2 == VT_VOID) {
        if (op != '?')
            tcc_error("operation on void value");
        /* NOTE: as an extension, we accept void on only one side */
        type.t = VT_VOID;
    } else if (bt1 == VT_PTR || bt2 == VT_PTR) {
        if (op == '+') {
          if (!is_integer_btype(bt1 == VT_PTR ? bt2 : bt1))
            ret = 0;
        }
        /* http://port70.net/~nsz/c/c99/n1256.html#6.5.15p6 */
        /* If one is a null ptr constant the result type is the other.  */
        else if (is_null_pointer (op2)) type = *type1;
        else if (is_null_pointer (op1)) type = *type2;
        else if (bt1 != bt2) {
            /* accept comparison or cond-expr between pointer and integer
               with a warning */
            if ((op == '?' || op == CMP_OP)
                && (is_integer_btype(bt1) || is_integer_btype(bt2)))
              tcc_warning("pointer/integer mismatch in %s",
                          op == '?' ? "conditional expression" : "comparison");
            else if (op != '-' || !is_integer_btype(bt2))
              ret = 0;
            type = *(bt1 == VT_PTR ? type1 : type2);
        } else {
            CType *pt1 = pointed_type(type1);
            CType *pt2 = pointed_type(type2);
            int pbt1 = pt1->t & VT_BTYPE;
            int pbt2 = pt2->t & VT_BTYPE;
            int newquals, copied = 0;
            if (pbt1 != VT_VOID && pbt2 != VT_VOID
                && !compare_types(pt1, pt2, 1/*unqualif*/)) {
                if (op != '?' && op != CMP_OP)
                  ret = 0;
                else
                  type_incompatibility_warning(type1, type2,
                    op == '?'
                     ? "pointer type mismatch in conditional expression ('%s' and '%s')"
                     : "pointer type mismatch in comparison('%s' and '%s')");
            }
            if (op == '?') {
                /* pointers to void get preferred, otherwise the
                   pointed to types minus qualifs should be compatible */
                type = *((pbt1 == VT_VOID) ? type1 : type2);
                /* combine qualifs */
                newquals = type_qualifiers(pt1) | type_qualifiers(pt2);
                if ((~type_qualifiers(pointed_type(&type))) & newquals)
                  {
                    /* copy the pointer target symbol */
                    type.ref = sym_push(SYM_FIELD, &type.ref->type,
                                        0, type.ref->c);
                    copied = 1;
                    parse_btype_qualify(pointed_type(&type), newquals);
                  }
                /* pointers to incomplete arrays get converted to
                   pointers to completed ones if possible */
                if (pt1->t & VT_ARRAY
                    && pt2->t & VT_ARRAY
                    && pointed_type(&type)->ref->c < 0
                    && (pt1->ref->c > 0 || pt2->ref->c > 0))
                  {
                    if (!copied)
                      type.ref = sym_push(SYM_FIELD, &type.ref->type,
                                          0, type.ref->c);
                    pointed_type(&type)->ref =
                        sym_push(SYM_FIELD, &pointed_type(&type)->ref->type,
                                 0, pointed_type(&type)->ref->c);
                    pointed_type(&type)->ref->c =
                        0 < pt1->ref->c ? pt1->ref->c : pt2->ref->c;
                  }
            }
        }
        if (op == CMP_OP)
          type.t = VT_SIZE_T;
    } else if (bt1 == VT_STRUCT || bt2 == VT_STRUCT) {
        if (op != '?' || !compare_types(type1, type2, 1))
          ret = 0;
        type = *type1;
    } else if (is_float(bt1) || is_float(bt2)) {
        if (bt1 == VT_LDOUBLE || bt2 == VT_LDOUBLE) {
            type.t = VT_LDOUBLE;
        } else if (bt1 == VT_DOUBLE || bt2 == VT_DOUBLE) {
            type.t = VT_DOUBLE;
        } else {
            type.t = VT_FLOAT;
        }
    } else if (bt1 == VT_LLONG || bt2 == VT_LLONG) {
        /* cast to biggest op */
        type.t = VT_LLONG | VT_LONG;
        if (bt1 == VT_LLONG)
          type.t &= t1;
        if (bt2 == VT_LLONG)
          type.t &= t2;
        /* convert to unsigned if it does not fit in a long long */
        if ((t1 & (VT_BTYPE | VT_UNSIGNED)) == (VT_LLONG | VT_UNSIGNED) ||
            (t2 & (VT_BTYPE | VT_UNSIGNED)) == (VT_LLONG | VT_UNSIGNED))
          type.t |= VT_UNSIGNED;
    } else {
        /* integer operations */
        type.t = VT_INT | (VT_LONG & (t1 | t2));
        /* convert to unsigned if it does not fit in an integer */
        if (((t1 & (VT_BTYPE | VT_UNSIGNED)) == (VT_INT | VT_UNSIGNED)
                && (!(t1 & VT_BITFIELD) || BIT_SIZE(t1) == 32))
         || ((t2 & (VT_BTYPE | VT_UNSIGNED)) == (VT_INT | VT_UNSIGNED)
                && (!(t2 & VT_BITFIELD) || BIT_SIZE(t2) == 32)))
          type.t |= VT_UNSIGNED;
    }
    if (dest)
      *dest = type;
    return ret;
}

/* generic gen_op: handles types problems */
ST_FUNC void gen_op(int op)
{
    int t1, t2, bt1, bt2, t;
    CType type1, combtype;
    int op_class = op;
#ifdef C6_EXT
    c6_fallible_use();          /* 'f() + x' loses the error */
#endif

    if (op == TOK_SHR || op == TOK_SAR || op == TOK_SHL)
        op_class = SHIFT_OP;
    else if (TOK_ISCOND(op)) /* == != > ... */
        op_class = CMP_OP;

redo:
    t1 = vtop[-1].type.t;
    t2 = vtop[0].type.t;
    bt1 = t1 & VT_BTYPE;
    bt2 = t2 & VT_BTYPE;
        
    if (bt1 == VT_FUNC || bt2 == VT_FUNC) {
	if (bt2 == VT_FUNC) {
	    mk_pointer(&vtop->type);
	    gaddrof();
	}
	if (bt1 == VT_FUNC) {
	    vswap();
	    mk_pointer(&vtop->type);
	    gaddrof();
	    vswap();
	}
	goto redo;
    } else if (!combine_types(&combtype, vtop - 1, vtop, op_class)) {
op_err:
        tcc_error("invalid operand types for binary operation");
    } else if (bt1 == VT_PTR || bt2 == VT_PTR) {
        /* at least one operand is a pointer */
        /* relational op: must be both pointers */
        int align;
        if (op_class == CMP_OP)
            goto std_op;
        /* if both pointers, then it must be the '-' op */
        if (bt1 == VT_PTR && bt2 == VT_PTR) {
            if (op != '-')
                goto op_err;
            vpush_type_size(pointed_type(&vtop[-1].type), &align);
            vtop->type.t &= ~VT_UNSIGNED;
            vrott(3);
            gen_opic(op);
            vtop->type.t = VT_PTRDIFF_T;
            vswap();
            gen_op(TOK_PDIV);
        } else {
            /* exactly one pointer : must be '+' or '-'. */
            if (op != '-' && op != '+')
                goto op_err;
            /* Put pointer as first operand */
            if (bt2 == VT_PTR) {
                vswap();
                t = t1, t1 = t2, t2 = t;
                bt2 = bt1;
            }
#if PTR_SIZE == 4
            if (bt2 == VT_LLONG)
                /* XXX: truncate here because gen_opl can't handle ptr + long long */
                gen_cast_s(VT_INT);
#endif
            type1 = vtop[-1].type;
            vpush_type_size(pointed_type(&vtop[-1].type), &align);
            vtop->type.t &= ~VT_UNSIGNED;
            gen_op('*');
#ifdef CONFIG_TCC_BCHECK
            if (tcc_state->do_bounds_check && !CONST_WANTED) {
                /* if bounded pointers, we generate a special code to
                   test bounds */
                if (op == '-') {
                    vpushi(0);
                    vswap();
                    gen_op('-');
                }
                gen_bounded_ptr_add();
            } else
#endif
            {
                gen_opic(op);
            }
            type1.t &= ~(VT_ARRAY|VT_VLA);
            /* put again type if gen_opic() swaped operands */
            vtop->type = type1;
        }
    } else {
        /* floats can only be used for a few operations */
        if (is_float(combtype.t)
            && op != '+' && op != '-' && op != '*' && op != '/'
            && op_class != CMP_OP) {
            goto op_err;
        }
    std_op:
        t = t2 = combtype.t;
        /* special case for shifts and long long: we keep the shift as
           an integer */
        if (op_class == SHIFT_OP)
            t2 = VT_INT;
        /* XXX: currently, some unsigned operations are explicit, so
           we modify them here */
        if (t & VT_UNSIGNED) {
            if (op == TOK_SAR)
                op = TOK_SHR;
            else if (op == '/')
                op = TOK_UDIV;
            else if (op == '%')
                op = TOK_UMOD;
            else if (op == TOK_LT)
                op = TOK_ULT;
            else if (op == TOK_GT)
                op = TOK_UGT;
            else if (op == TOK_LE)
                op = TOK_ULE;
            else if (op == TOK_GE)
                op = TOK_UGE;
        }
        vswap();
        gen_cast_s(t);
        vswap();
        gen_cast_s(t2);
        if (is_float(t))
            gen_opif(op);
        else
            gen_opic(op);
        if (op_class == CMP_OP) {
            /* relational op: the result is an int */
            vtop->type.t = VT_INT;
        } else {
            vtop->type.t = t;
        }
    }
    // Make sure that we have converted to an rvalue:
    if (vtop->r & VT_LVAL)
        gv(RC_TYPE(vtop->type.t));
}

#if defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64 || defined TCC_TARGET_ARM
#define gen_cvt_itof1 gen_cvt_itof
#else
/* generic itof for unsigned long long case */
static void gen_cvt_itof1(int t)
{
    if ((vtop->type.t & (VT_BTYPE | VT_UNSIGNED)) == 
        (VT_LLONG | VT_UNSIGNED)) {

        if (t == VT_FLOAT)
            vpush_helper_func(TOK___floatundisf);
#if LDOUBLE_SIZE != 8
        else if (t == VT_LDOUBLE)
            vpush_helper_func(TOK___floatundixf);
#endif
        else
            vpush_helper_func(TOK___floatundidf);
        vrott(2);
        gfunc_call(1);
        vpushi(0);
        PUT_R_RET(vtop, t);
    } else {
        gen_cvt_itof(t);
    }
}
#endif

#if defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64
#define gen_cvt_ftoi1 gen_cvt_ftoi
#else
/* generic ftoi for unsigned long long case */
static void gen_cvt_ftoi1(int t)
{
    int st;
    if (t == (VT_LLONG | VT_UNSIGNED)) {
        /* not handled natively */
        st = vtop->type.t & VT_BTYPE;
        if (st == VT_FLOAT)
            vpush_helper_func(TOK___fixunssfdi);
#if LDOUBLE_SIZE != 8
        else if (st == VT_LDOUBLE)
            vpush_helper_func(TOK___fixunsxfdi);
#endif
        else
            vpush_helper_func(TOK___fixunsdfdi);
        vrott(2);
        gfunc_call(1);
        vpushi(0);
        PUT_R_RET(vtop, t);
    } else {
        gen_cvt_ftoi(t);
    }
}
#endif

/* special delayed cast for char/short */
static void force_charshort_cast(void)
{
    int sbt = BFGET(vtop->r, VT_MUSTCAST) == 2 ? VT_LLONG : VT_INT;
    int dbt = vtop->type.t;
    vtop->r &= ~VT_MUSTCAST;
    vtop->type.t = sbt;
    gen_cast_s(dbt == VT_BOOL ? VT_BYTE|VT_UNSIGNED : dbt);
    vtop->type.t = dbt;
}

static void gen_cast_s(int t)
{
    CType type;
    type.t = t;
    type.ref = NULL;
    gen_cast(&type);
}

/* cast 'vtop' to 'type'. Casting to bitfields is forbidden. */
static void gen_cast(CType *type)
{
#ifdef C6_EXT
    c6_fallible_use();          /* a cast would drop the error */
#endif
    int sbt, dbt, sf, df, c;
    int dbt_bt, sbt_bt, ds, ss, bits, trunc;

    /* special delayed cast for char/short */
    if (vtop->r & VT_MUSTCAST)
        force_charshort_cast();

    /* bitfields first get cast to ints */
    if (vtop->type.t & VT_BITFIELD)
        gv(RC_INT);

    if (IS_ENUM(type->t) && type->ref->c < 0)
        tcc_error("cast to incomplete type");

    dbt = type->t & (VT_BTYPE | VT_UNSIGNED);
    sbt = vtop->type.t & (VT_BTYPE | VT_UNSIGNED);
    if (sbt == VT_FUNC)
        sbt = VT_PTR;

again:
    if (sbt != dbt) {
        sf = is_float(sbt);
        df = is_float(dbt);
        dbt_bt = dbt & VT_BTYPE;
        sbt_bt = sbt & VT_BTYPE;
        if (dbt_bt == VT_VOID) {
            /* do not confuse backends with VT_VOID in registers */
            vpop(), vpushi(0);
            goto done;
        }
        if (sbt_bt == VT_VOID) {
error:
            cast_error(&vtop->type, type);
        }

        c = (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
        if (c) {
            /* constant case: we can do it now */
            /* XXX: in ISOC, cannot do it if error in convert */
            if (sbt == VT_FLOAT)
                vtop->c.ld = vtop->c.f;
            else if (sbt == VT_DOUBLE)
                vtop->c.ld = vtop->c.d;

            if (df) {
                if (sbt_bt == VT_LLONG) {
                    if ((sbt & VT_UNSIGNED) || !(vtop->c.i >> 63))
                        vtop->c.ld = vtop->c.i;
                    else
                        vtop->c.ld = -(long double)-vtop->c.i;
                } else if(!sf) {
                    if ((sbt & VT_UNSIGNED) || !(vtop->c.i >> 31))
                        vtop->c.ld = (uint32_t)vtop->c.i;
                    else
                        vtop->c.ld = -(long double)-(uint32_t)vtop->c.i;
                }

                if (dbt == VT_FLOAT)
                    vtop->c.f = (float)vtop->c.ld;
                else if (dbt == VT_DOUBLE)
                    vtop->c.d = (double)vtop->c.ld;
            } else if (sf && dbt == VT_BOOL) {
                vtop->c.i = (vtop->c.ld != 0);
            } else {
                if(sf) {
                    if (dbt & VT_UNSIGNED)
                        vtop->c.i = (uint64_t)vtop->c.ld;
                    else
                        vtop->c.i = (int64_t)vtop->c.ld;
                }
                else if (sbt_bt == VT_LLONG || (PTR_SIZE == 8 && sbt == VT_PTR))
                    ;
                else if (sbt & VT_UNSIGNED)
                    vtop->c.i = (uint32_t)vtop->c.i;
                else
                    vtop->c.i = ((uint32_t)vtop->c.i | -(vtop->c.i & 0x80000000));

                if (dbt_bt == VT_LLONG || (PTR_SIZE == 8 && dbt == VT_PTR))
                    ;
                else if (dbt == VT_BOOL)
                    vtop->c.i = (vtop->c.i != 0);
                else {
                    uint32_t m = dbt_bt == VT_BYTE ? 0xff :
                                 dbt_bt == VT_SHORT ? 0xffff :
                                  0xffffffff;
                    vtop->c.i &= m;
                    if (!(dbt & VT_UNSIGNED))
                        vtop->c.i |= -(vtop->c.i & ((m >> 1) + 1));
                }
            }
            goto done;

        } else if (dbt == VT_BOOL
            && (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM))
                == (VT_CONST | VT_SYM)) {
            /* addresses are considered non-zero (see tcctest.c:sinit23) */
            vtop->r = VT_CONST;
            vtop->c.i = 1;
            goto done;
        }

        /* cannot generate code for global or static initializers */
        if (nocode_wanted & DATA_ONLY_WANTED) {
            if (df)
                vtop->r = get_reg(RC_FLOAT); /* don't confuse backends */
            goto done;
        }

        /* non constant case: generate code */
        if (dbt == VT_BOOL) {
            gen_test_zero(TOK_NE);
            goto done;
        }

        if (sf || df) {
            if (sf && df) {
                /* convert from fp to fp */
                gen_cvt_ftof(dbt);
            } else if (df) {
                /* convert int to fp */
                gen_cvt_itof1(dbt);
            } else {
                /* convert fp to int */
                sbt = dbt;
                if (dbt_bt != VT_LLONG && dbt_bt != VT_INT)
                    sbt = VT_INT;
                gen_cvt_ftoi1(sbt);
                goto again; /* may need char/short cast */
            }
            goto done;
        }

        ds = btype_size(dbt_bt);
        ss = btype_size(sbt_bt);
        if (ds == 0 || ss == 0)
            goto error;

        /* same size and no sign conversion needed */
        if (ds == ss && ds >= 4)
            goto done;
        if (dbt_bt == VT_PTR || sbt_bt == VT_PTR) {
            tcc_warning("cast between pointer and integer of different size");
            if (sbt_bt == VT_PTR) {
                /* put integer type to allow logical operations below */
                vtop->type.t = (PTR_SIZE == 8 ? VT_LLONG : VT_INT);
            }
        }

        /* processor allows { int a = 0, b = *(char*)&a; }
           That means that if we cast to less width, we can just
           change the type and read it still later. */
        #define ALLOW_SUBTYPE_ACCESS 1

        if (ALLOW_SUBTYPE_ACCESS && (vtop->r & VT_LVAL)) {
            /* value still in memory */
            if (ds <= ss)
                goto done;
            /* ss <= 4 here */
            if (ds <= 4 && !(dbt == (VT_SHORT | VT_UNSIGNED) && sbt == VT_BYTE)) {
                gv(RC_INT);
                goto done; /* no 64bit envolved */
            }
        }
        gv(RC_INT);

        trunc = 0;
#if PTR_SIZE == 4
        if (ds == 8) {
            /* generate high word */
            if (sbt & VT_UNSIGNED) {
                vpushi(0);
                gv(RC_INT);
            } else {
                gv_dup();
                vpushi(31);
                gen_op(TOK_SAR);
            }
            lbuild(dbt);
        } else if (ss == 8) {
            /* from long long: just take low order word */
            lexpand();
            vpop();
        }
        ss = 4;

#elif PTR_SIZE == 8
        if (ds == 8) {
            /* need to convert from 32bit to 64bit */
            if (sbt & VT_UNSIGNED) {
#if defined(TCC_TARGET_RISCV64)
                /* RISC-V keeps 32bit vals in registers sign-extended.
                   So here we need a zero-extension.  */
                trunc = 32;
#else
                goto done;
#endif
            } else {
                gen_cvt_sxtw();
                goto done;
            }
            ss = ds, ds = 4, dbt = sbt;
        } else if (ss == 8) {
            /* RISC-V keeps 32bit vals in registers sign-extended.
               So here we need a sign-extension for signed types and
               zero-extension. for unsigned types. */
#if !defined(TCC_TARGET_RISCV64)
            trunc = 32; /* zero upper 32 bits for non RISC-V targets */
#endif
        } else {
            ss = 4;
        }
#endif

        if (ds >= ss)
            goto done;
#if defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64 || defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64
    if (ss == 4) {
        gen_cvt_csti(dbt);
        goto done;
    }
#endif
        bits = (ss - ds) * 8;
        /* for unsigned, gen_op will convert SAR to SHR */
        vtop->type.t = (ss == 8 ? VT_LLONG : VT_INT) | (dbt & VT_UNSIGNED);
        vpushi(bits);
        gen_op(TOK_SHL);
        vpushi(bits - trunc);
        gen_op(TOK_SAR);
        vpushi(trunc);
        gen_op(TOK_SHR);
    }
done:
    vtop->type = *type;
    vtop->type.t &= ~ ( VT_QUAL | VT_ARRAY | VT_TLS );
}

/* return type size as known at compile time. Put alignment at 'a' */
ST_FUNC int type_size(CType *type, int *a)
{
    Sym *s;
    int bt;

    bt = type->t & VT_BTYPE;
    if (bt == VT_STRUCT) {
        /* struct/union */
        s = type->ref;
        *a = s->r;
        return s->c;
    } else if (bt == VT_PTR) {
        if (type->t & VT_ARRAY) {
            int ts;
            s = type->ref;
            ts = type_size(&s->type, a);
            if (s->c < 0)
                return s->c;
            return ts * s->c;
        } else {
            *a = PTR_SIZE;
            return PTR_SIZE;
        }
    } else if (IS_ENUM(type->t) && type->ref->c < 0) {
        *a = 0;
        return -1; /* incomplete enum */
    } else if (bt == VT_LDOUBLE) {
        *a = LDOUBLE_ALIGN;
        return LDOUBLE_SIZE;
    } else if (bt == VT_DOUBLE || bt == VT_LLONG) {
#if (defined TCC_TARGET_I386 && !defined TCC_TARGET_PE) \
 || (defined TCC_TARGET_ARM && !defined TCC_ARM_EABI)
        *a = 4;
#else
        *a = 8;
#endif
        return 8;
    } else if (bt == VT_INT || bt == VT_FLOAT) {
        *a = 4;
        return 4;
    } else if (bt == VT_SHORT) {
        *a = 2;
        return 2;
    } else if (bt == VT_QLONG || bt == VT_QFLOAT) {
        *a = 8;
        return 16;
    } else {
        /* char, void, function, _Bool */
        *a = 1;
        return 1;
    }
}

/* push type size as known at runtime time on top of value stack. Put
   alignment at 'a' */
static void vpush_type_size(CType *type, int *a)
{
    if (type->t & VT_VLA) {
        type_size(&type->ref->type, a);
        vset(&int_type, VT_LOCAL|VT_LVAL, type->ref->c);
    } else {
        int size = type_size(type, a);
        if (size < 0)
            tcc_error("unknown type size");
        vpushs(size);
    }
}

/* return the pointed type of t */
static inline CType *pointed_type(CType *type)
{
    return &type->ref->type;
}

static void check_restrict_type(CType *type)
{
    while ((type->t & VT_BTYPE) == VT_PTR) {
        if ((type->t & VT_RESTRICT)
            && (pointed_type(type)->t & VT_BTYPE) == VT_FUNC)
            tcc_error("restrict-qualified type must be a pointer to object or incomplete type");
        type = pointed_type(type);
    }
    if (type->t & VT_RESTRICT)
        tcc_error("restrict-qualified type must be a pointer to object or incomplete type");
}

/* Array qualifiers are represented on the element type. */
static int type_qualifiers(CType *type)
{
    while (type->t & VT_ARRAY)
        type = pointed_type(type);
    return type->t & VT_QUAL;
}

/* modify type so that its it is a pointer to type. */
ST_FUNC void mk_pointer(CType *type)
{
    Sym *s;
    s = sym_push(SYM_FIELD, type, 0, -1);
    type->t = VT_PTR | (type->t & VT_STORAGE);
    type->ref = s;
    /* A fresh pointer is never a managed reference; and the pointee symbol
       carries the trusted copy of the GC qualifier (0 = plain pointer), so
       that a byte nobody initialised can never look like '@T'. */
    type->gq = 0;
    s->type.gq = 0;
}

/* return true if type1 and type2 are exactly the same (including
   qualifiers). 
*/
static int is_compatible_types(CType *type1, CType *type2)
{
    return compare_types(type1,type2,0);
}

/* return true if type1 and type2 are the same (ignoring qualifiers).
*/
static int is_compatible_unqualified_types(CType *type1, CType *type2)
{
    return compare_types(type1,type2,1);
}

static void cast_error(CType *st, CType *dt)
{
    type_incompatibility_error(st, dt, "cannot convert '%s' to '%s'");
}

/* verify type compatibility to store vtop in 'dt' type */
static void verify_assign_cast(CType *dt)
{
    CType *st, *type1, *type2;
    int dbt, sbt, qualwarn, lvl;

    st = &vtop->type; /* source type */
    dbt = dt->t & VT_BTYPE;
    sbt = st->t & VT_BTYPE;
    if (dt->t & VT_CONSTANT)
        tcc_warning("assignment of read-only location");
    switch(dbt) {
    case VT_VOID:
        if (sbt != dbt)
            tcc_error("assignment to void expression");
        break;
    case VT_PTR:
        /* special cases for pointers */
        /* '0' can also be a pointer */
        if (is_null_pointer(vtop))
            break;
        /* accept implicit pointer to integer cast with warning */
        if (is_integer_btype(sbt)) {
            tcc_warning("assignment makes pointer from integer without a cast");
            break;
        }
        type1 = pointed_type(dt);
        if (sbt == VT_PTR)
            type2 = pointed_type(st);
        else if (sbt == VT_FUNC)
            type2 = st; /* a function is implicitly a function pointer */
        else
            goto error;
        if (is_compatible_types(type1, type2))
            break;
        for (qualwarn = lvl = 0;; ++lvl) {
            if ((type2->t & ~type1->t) & VT_QUAL)
                qualwarn = 1;
            dbt = type1->t & (VT_BTYPE|VT_LONG);
            sbt = type2->t & (VT_BTYPE|VT_LONG);
            if (dbt != VT_PTR || sbt != VT_PTR)
                break;
            type1 = pointed_type(type1);
            type2 = pointed_type(type2);
        }
        if (!is_compatible_unqualified_types(type1, type2)) {
            if ((dbt == VT_VOID || sbt == VT_VOID) && lvl == 0) {
                /* void * can match anything */
            } else if (dbt == sbt
                && is_integer_btype(sbt & VT_BTYPE)
                && IS_ENUM(type1->t) + IS_ENUM(type2->t)
                    + !!((type1->t ^ type2->t) & VT_UNSIGNED) < 2) {
		/* Like GCC don't warn by default for merely changes
		   in pointer target signedness.  Do warn for different
		   base types, though, in particular for unsigned enums
		   and signed int targets.  */
            } else {
                tcc_warning("assignment from incompatible pointer type");
                break;
            }
        }
        if (qualwarn)
            tcc_warning_c(warn_discarded_qualifiers)("assignment discards qualifiers from pointer target type");
        break;
    case VT_BYTE:
    case VT_SHORT:
    case VT_INT:
    case VT_LLONG:
        if (sbt == VT_PTR || sbt == VT_FUNC) {
            tcc_warning("assignment makes integer from pointer without a cast");
        } else if (sbt == VT_STRUCT) {
            goto case_VT_STRUCT;
        }
        /* XXX: more tests */
        break;
    case VT_STRUCT:
    case_VT_STRUCT:
        if (!is_compatible_unqualified_types(dt, st)) {
    error:
            cast_error(st, dt);
        }
        break;
    }
}

static void gen_assign_cast(CType *dt)
{
#ifdef C6_EXT
    if (c6_slice_convert(dt))
        return;
    if (c6_iface_convert(dt))
        return;
#endif
    verify_assign_cast(dt);
    gen_cast(dt);
}

/* store vtop in lvalue pushed on stack */
ST_FUNC void vstore(void)
{
    int sbt, dbt, ft, r, size, align, bit_size, bit_pos, delayed_cast;
#ifdef C6_EXT
    c6_fallible_use();          /* 'x = f()' would drop the error */
    if (c6_gc_store_hook())     /* 'managed lvalue = value' */
        return;
    if (tcc_state->c6_ext && !c6_gc_no_hook
        && (vtop[-1].type.t & VT_BTYPE) == VT_STRUCT)
        c6_gc_value_check(&vtop[-1].type, "an assignment");
#endif

    ft = vtop[-1].type.t;
#ifdef C6_EXT
    /* an array assigned to a slice becomes a view of the array */
    if (!c6_slice_convert(&vtop[-1].type))
        c6_iface_convert(&vtop[-1].type);
    ft = vtop[-1].type.t;
#endif
    sbt = vtop->type.t & VT_BTYPE;
    dbt = ft & VT_BTYPE;
    verify_assign_cast(&vtop[-1].type);

    if (sbt == VT_STRUCT) {
        /* if structure, only generate pointer */
        /* structure assignment : generate memcpy */
        size = type_size(&vtop->type, &align);
        /* destination, keep on stack() as result */
        vpushv(vtop - 1);
#ifdef CONFIG_TCC_BCHECK
        if (vtop->r & VT_MUSTBOUND)
            gbound(); /* check would be wrong after gaddrof() */
#endif
        vtop->type.t = VT_PTR;
        gaddrof();
        /* source */
        vswap();
#ifdef CONFIG_TCC_BCHECK
        if (vtop->r & VT_MUSTBOUND)
            gbound();
#endif
        vtop->type.t = VT_PTR;
        gaddrof();

#ifdef TCC_TARGET_NATIVE_STRUCT_COPY
        if (1
#ifdef CONFIG_TCC_BCHECK
            && !tcc_state->do_bounds_check
#endif
            ) {
            gen_struct_copy(size);
        } else
#endif
        {
            /* type size */
            vpushi(size);
            /* Use memmove, rather than memcpy, as dest and src may be same: */
#ifdef TCC_ARM_EABI
            if(!(align & 7))
                vpush_helper_func(TOK_memmove8);
            else if(!(align & 3))
                vpush_helper_func(TOK_memmove4);
            else
#endif
            vpush_helper_func(TOK_memmove);
            vrott(4);
            gfunc_call(3);
        }

    } else if (ft & VT_BITFIELD) {
        /* bitfield store handling */

        /* save lvalue as expression result (example: s.b = s.a = n;) */
        vdup(), vtop[-1] = vtop[-2];

        bit_pos = BIT_POS(ft);
        bit_size = BIT_SIZE(ft);
        /* remove bit field info to avoid loops */
        vtop[-1].type.t = ft & ~VT_STRUCT_MASK;

        if (dbt == VT_BOOL) {
            gen_cast(&vtop[-1].type);
            vtop[-1].type.t = (vtop[-1].type.t & ~VT_BTYPE) | (VT_BYTE | VT_UNSIGNED);
        }
        r = adjust_bf(vtop - 1, bit_pos, bit_size);
        if (dbt != VT_BOOL) {
            gen_cast(&vtop[-1].type);
            dbt = vtop[-1].type.t & VT_BTYPE;
        }
        if (r == VT_STRUCT) {
            store_packed_bf(bit_pos, bit_size);
        } else {
            unsigned long long mask = (1ULL << bit_size) - 1;
            if (dbt != VT_BOOL) {
                /* mask source */
                if (dbt == VT_LLONG)
                    vpushll(mask);
                else
                    vpushi((unsigned)mask);
                gen_op('&');
            }
            /* shift source */
            vpushi(bit_pos);
            gen_op(TOK_SHL);
            vswap();
            /* duplicate destination */
            vdup();
            vrott(3);
            /* load destination, mask and or with source */
            if (dbt == VT_LLONG)
                vpushll(~(mask << bit_pos));
            else
                vpushi(~((unsigned)mask << bit_pos));
            gen_op('&');
            gen_op('|');
            /* store result */
            vstore();
            /* ... and discard */
            vpop();
        }
    } else if (dbt == VT_VOID) {
        --vtop;
    } else {
            /* optimize char/short casts */
            delayed_cast = 0;
            if ((dbt == VT_BYTE || dbt == VT_SHORT)
                && is_integer_btype(sbt)
                ) {
                if ((vtop->r & VT_MUSTCAST)
                    && btype_size(dbt) > btype_size(sbt)
                    )
                    force_charshort_cast();
                delayed_cast = 1;
            } else {
                gen_cast(&vtop[-1].type);
            }

#ifdef CONFIG_TCC_BCHECK
            /* bound check case */
            if (vtop[-1].r & VT_MUSTBOUND) {
                vswap();
                gbound();
                vswap();
            }
#endif
            gv(RC_TYPE(dbt)); /* generate value */

            if (delayed_cast) {
                vtop->r |= BFVAL(VT_MUSTCAST, (sbt == VT_LLONG) + 1);
                //tcc_warning("deley cast %x -> %x", sbt, dbt);
                vtop->type.t = ft & VT_TYPE;
            }

            /* if lvalue was saved on stack, must read it */
            if ((vtop[-1].r & VT_VALMASK) == VT_LLOCAL) {
                SValue sv;
                r = get_reg(RC_INT);
                sv.type.t = VT_PTRDIFF_T;
                sv.r = VT_LOCAL | VT_LVAL;
                sv.c.i = vtop[-1].c.i;
		sv.sym = NULL;
                load(r, &sv);
                vtop[-1].r = r | VT_LVAL;
            }

            r = vtop->r & VT_VALMASK;
            /* two word case handling :
               store second register at word + 4 (or +8 for x86-64)  */
            if (USING_TWO_WORDS(dbt)) {
                int load_type = (dbt == VT_QFLOAT) ? VT_DOUBLE : VT_PTRDIFF_T;
                vtop[-1].type.t = load_type;
                store(r, vtop - 1);
                vswap();
                incr_offset(PTR_SIZE);
                vswap();
                /* XXX: it works because r2 is spilled last ! */
                store(vtop->r2, vtop - 1);
            } else {
                /* single word */
                store(r, vtop - 1);
            }
        vswap();
        vtop--; /* NOT vpop() because on x86 it would flush the fp stack */
    }
}

/* post defines POST/PRE add. c is the token ++ or -- */
ST_FUNC void inc(int post, int c)
{
#ifdef C6_EXT
    c6_fallible_use();
#endif
    test_lvalue();
    vdup(); /* save lvalue */
    if (post) {
        gv_dup(); /* duplicate value */
        vrotb(3);
        vrotb(3);
    }
    /* add constant */
    vpushi(c - TOK_MID); 
    gen_op('+');
    vstore(); /* store value */
    if (post)
        vpop(); /* if post op, return saved value */
}

ST_FUNC CString* parse_mult_str (const char *msg)
{
    /* read the string */
    if (tok != TOK_STR)
        expect(msg);
    cstr_reset(&initstr);
    while (tok == TOK_STR) {
        /* XXX: add \0 handling too ? */
        cstr_cat(&initstr, tokc.str.data, -1);
        next();
    }
    cstr_ccat(&initstr, '\0');
    return &initstr;
}

/* If I is >= 1 and a power of two, returns log2(i)+1.
   If I is 0 returns 0.  */
ST_FUNC int exact_log2p1(int i)
{
  int ret;
  if (!i)
    return 0;
  for (ret = 1; i >= 1 << 8; ret += 8)
    i >>= 8;
  if (i >= 1 << 4)
    ret += 4, i >>= 4;
  if (i >= 1 << 2)
    ret += 2, i >>= 2;
  if (i >= 1 << 1)
    ret++;
  return ret;
}

/* Parse __attribute__((...)) GNUC extension. */
static void parse_attribute(AttributeDef *ad)
{
    int t, n;
    char *astr;
    AttributeDef ad_tmp;
    
redo:
    if (tok != TOK_ATTRIBUTE1 && tok != TOK_ATTRIBUTE2)
        return;
    if (NULL == ad) /* skip over / ignore attributes */
        ad = &ad_tmp;

    next();
    skip('(');
    skip('(');
    while (tok != ')') {
        if (tok < TOK_IDENT)
            expect("attribute name");
        t = tok;
        next();
        switch(t) {
	case TOK_CLEANUP1:
	case TOK_CLEANUP2:
	{
	    Sym *s;

	    skip('(');
	    s = sym_find(tok);
	    if (!s) {
	        tcc_warning_c(warn_implicit_function_declaration)(
                    "implicit declaration of function '%s'", get_tok_str(tok, &tokc));
	        s = external_global_sym(tok, &func_old_type);
            } else if ((s->type.t & VT_BTYPE) != VT_FUNC)
                tcc_error("'%s' is not declared as function", get_tok_str(tok, &tokc));
	    ad->cleanup_func = s;
	    next();
            skip(')');
	    break;
	}
        case TOK_CONSTRUCTOR1:
        case TOK_CONSTRUCTOR2:
            ad->f.func_ctor = 1;
            break;
        case TOK_DESTRUCTOR1:
        case TOK_DESTRUCTOR2:
            ad->f.func_dtor = 1;
            break;
        case TOK_ALWAYS_INLINE1:
        case TOK_ALWAYS_INLINE2:
            ad->f.func_alwinl = 1;
            break;
        case TOK_SECTION1:
        case TOK_SECTION2:
            skip('(');
	    astr = parse_mult_str("section name")->data;
            n = tcc_state->nb_sections;
            ad->section = find_section(tcc_state, astr);
            ad->new_section = n < tcc_state->nb_sections;
            skip(')');
            break;
        case TOK_ALIAS1:
        case TOK_ALIAS2:
            skip('(');
	    astr = parse_mult_str("alias(\"target\")")->data;
            /* save string as token, for later */
            ad->alias_target = tok_alloc_const(astr);
            skip(')');
            break;
	case TOK_VISIBILITY1:
	case TOK_VISIBILITY2:
            skip('(');
	    astr = parse_mult_str("visibility(\"default|hidden|internal|protected\")")->data;
	    if (!strcmp (astr, "default"))
	        ad->a.visibility = STV_DEFAULT;
	    else if (!strcmp (astr, "hidden"))
	        ad->a.visibility = STV_HIDDEN;
	    else if (!strcmp (astr, "internal"))
	        ad->a.visibility = STV_INTERNAL;
	    else if (!strcmp (astr, "protected"))
	        ad->a.visibility = STV_PROTECTED;
	    else
                expect("visibility(\"default|hidden|internal|protected\")");
            skip(')');
            break;
        case TOK_ALIGNED1:
        case TOK_ALIGNED2:
            if (tok == '(') {
                next();
                n = expr_const();
                if (n <= 0 || (n & (n - 1)) != 0) 
                    tcc_error("alignment must be a positive power of two");
                skip(')');
            } else {
                n = MAX_ALIGN;
            }
            ad->a.aligned = exact_log2p1(n);
	    if (n != 1 << (ad->a.aligned - 1))
	      tcc_error("alignment of %d is larger than implemented", n);
            break;
        case TOK_PACKED1:
        case TOK_PACKED2:
            ad->a.packed = 1;
            break;
        case TOK_WEAK1:
        case TOK_WEAK2:
            ad->a.weak = 1;
            break;
        case TOK_NODEBUG1:
        case TOK_NODEBUG2:
            ad->a.nodebug = 1;
            break;
        case TOK_USED1:
        case TOK_USED2:
        case TOK_UNUSED1:
        case TOK_UNUSED2:
            /* currently, no need to handle it because tcc does not
               track used/unused objects */
            break;
        case TOK_CONST1:
        case TOK_CONST2:
        case TOK_CONST3:
        case TOK_PURE1:
        case TOK_PURE2:
	    /* ignored */
            break;
        case TOK_NOINLINE:
	    /* ignored */
	    break;
        case TOK_FORMAT1:
        case TOK_FORMAT2:
	    /* ignored */
            goto skip_param;
        case TOK_NORETURN1:
        case TOK_NORETURN2:
            ad->f.func_noreturn = 1;
            break;
        case TOK_CDECL1:
        case TOK_CDECL2:
        case TOK_CDECL3:
            ad->f.func_call = FUNC_CDECL;
            break;
        case TOK_STDCALL1:
        case TOK_STDCALL2:
        case TOK_STDCALL3:
            ad->f.func_call = FUNC_STDCALL;
            break;
#ifdef TCC_TARGET_I386
        case TOK_REGPARM1:
        case TOK_REGPARM2:
            skip('(');
            n = expr_const();
            if (n > 3) 
                n = 3;
            else if (n < 0)
                n = 0;
            if (n > 0)
                ad->f.func_call = FUNC_FASTCALL1 + n - 1;
            skip(')');
            break;
        case TOK_FASTCALL1:
        case TOK_FASTCALL2:
        case TOK_FASTCALL3:
            ad->f.func_call = FUNC_FASTCALLW;
            break;
        case TOK_THISCALL1:
        case TOK_THISCALL2:
        case TOK_THISCALL3:
            ad->f.func_call = FUNC_THISCALL;
            break;
#endif
        case TOK_MODE:
            skip('(');
            switch(tok) {
                case TOK_MODE_DI:
                    ad->attr_mode = VT_LLONG + 1;
                    break;
                case TOK_MODE_QI:
                    ad->attr_mode = VT_BYTE + 1;
                    break;
                case TOK_MODE_HI:
                    ad->attr_mode = VT_SHORT + 1;
                    break;
                case TOK_MODE_SI:
                case TOK_MODE_word:
                    ad->attr_mode = VT_INT + 1;
                    break;
                default:
                    tcc_warning("__mode__(%s) not supported\n", get_tok_str(tok, NULL));
                    break;
            }
            next();
            skip(')');
            break;
        case TOK_DLLEXPORT:
            ad->a.dllexport = 1;
            break;
        case TOK_NODECORATE:
            ad->a.nodecorate = 1;
            break;
        case TOK_DLLIMPORT:
            ad->a.dllimport = 1;
            break;
        default:
            tcc_warning_c(warn_unsupported)("'%s' attribute ignored", get_tok_str(t, NULL));
            /* skip parameters */
skip_param:
            if (tok == '(') {
                int parenthesis = 0;
                do {
                    if (tok == '(') 
                        parenthesis++;
                    else if (tok == ')') 
                        parenthesis--;
                    next();
                } while (parenthesis && tok != -1);
            }
            break;
        }
        if (tok != ',')
            break;
        next();
    }
    skip(')');
    skip(')');
    goto redo;
}

static Sym * find_field (CType *type, int v, int *cumofs)
{
    Sym *s = type->ref;
    int v1 = v | SYM_FIELD;
    if (!(v & SYM_FIELD)) { /* top-level call */
        if ((type->t & VT_BTYPE) != VT_STRUCT)
            expect("struct or union");
        if (v < TOK_UIDENT)
            expect("field name");
        if (s->c < 0)
            tcc_error("dereferencing incomplete type '%s'",
                get_tok_str(s->v & ~SYM_STRUCT, 0));
    }
    while ((s = s->next) != NULL) {
        if (s->v == v1) {
            *cumofs = s->c;
            return s;
        }
        if ((s->type.t & VT_BTYPE) == VT_STRUCT
          && s->v >= (SYM_FIRST_ANOM | SYM_FIELD)) {
            /* try to find field in anonymous sub-struct/union */
            Sym *ret = find_field (&s->type, v1, cumofs);
            if (ret) {
                *cumofs += s->c;
                return ret;
            }
        }
    }
    if (!(v & SYM_FIELD))
        tcc_error("field not found: %s", get_tok_str(v, NULL));
    return s;
}

static void check_fields (CType *type, int check)
{
    Sym *s = type->ref;

    while ((s = s->next) != NULL) {
        int v = s->v & ~SYM_FIELD;
        if (v < SYM_FIRST_ANOM) {
            TokenSym *ts = table_ident[v - TOK_IDENT];
            if (check && (ts->tok & SYM_FIELD))
                tcc_error("duplicate member '%s'", get_tok_str(v, NULL));
            ts->tok ^= SYM_FIELD;
        } else if ((s->type.t & VT_BTYPE) == VT_STRUCT)
            check_fields (&s->type, check);
    }
}

static void struct_layout(CType *type, AttributeDef *ad)
{
    int size, align, maxalign, offset, c, bit_pos, bit_size;
    int packed, a, bt, prevbt, prev_bit_size;
    int pcc = !tcc_state->ms_bitfields;
    int pragma_pack = *tcc_state->pack_stack_ptr;
    Sym *f;

    maxalign = 1;
    offset = 0;
    c = 0;
    bit_pos = 0;
    prevbt = VT_STRUCT; /* make it never match */
    prev_bit_size = 0;

//#define BF_DEBUG

    for (f = type->ref->next; f; f = f->next) {
        if (f->type.t & VT_BITFIELD)
            bit_size = BIT_SIZE(f->type.t);
        else
            bit_size = -1;
        size = type_size(&f->type, &align);
        a = f->a.aligned ? 1 << (f->a.aligned - 1) : 0;
        packed = 0;

        if (pcc && bit_size == 0) {
            /* in pcc mode, packing does not affect zero-width bitfields */

        } else {
            /* in pcc mode, attribute packed overrides if set. */
            if (pcc && (f->a.packed || ad->a.packed))
                align = packed = 1;

            /* pragma pack overrides align if lesser and packs bitfields always */
            if (pragma_pack) {
                packed = 1;
                if (pragma_pack < align)
                    align = pragma_pack;
                /* in pcc mode pragma pack also overrides individual align */
                if (pcc && pragma_pack < a)
                    a = 0;
            }
        }
        /* some individual align was specified */
        if (a)
            align = a;

        if (type->ref->type.t == VT_UNION) {
	    if (pcc && bit_size >= 0)
	        size = (bit_size + 7) >> 3;
	    offset = 0;
	    if (size > c)
	        c = size;

	} else if (bit_size < 0) {
            if (pcc)
                c += (bit_pos + 7) >> 3;
	    c = (c + align - 1) & -align;
	    offset = c;
	    if (size > 0)
	        c += size;
	    bit_pos = 0;
	    prevbt = VT_STRUCT;
	    prev_bit_size = 0;

	} else {
	    /* A bit-field.  Layout is more complicated.  There are two
	       options: PCC (GCC) compatible and MS compatible */
            if (pcc) {
		/* In PCC layout a bit-field is placed adjacent to the
                   preceding bit-fields, except if:
                   - it has zero-width
                   - an individual alignment was given
                   - it would overflow its base type container and
                     there is no packing */
                if (bit_size == 0) {
            new_field:
		    c = (c + ((bit_pos + 7) >> 3) + align - 1) & -align;
		    bit_pos = 0;
                } else if (f->a.aligned) {
                    goto new_field;
                } else if (!packed) {
                    int a8 = align * 8;
	            int ofs = ((c * 8 + bit_pos) % a8 + bit_size + a8 - 1) / a8;
                    if (ofs > size / align)
                        goto new_field;
                }

                /* in pcc mode, long long bitfields have type int if they fit */
                if (size == 8 && bit_size <= 32)
                    f->type.t = (f->type.t & ~VT_BTYPE) | VT_INT, size = 4;

                while (bit_pos >= align * 8)
                    c += align, bit_pos -= align * 8;
                offset = c;

		/* In PCC layout named bit-fields influence the alignment
		   of the containing struct using the base types alignment,
		   except for packed fields (which here have correct align).  */
		if (f->v & SYM_FIRST_ANOM
                    // && bit_size // ??? gcc on ARM/rpi does that
                    )
		    align = 1;

	    } else {
		bt = f->type.t & VT_BTYPE;
		if ((bit_pos + bit_size > size * 8)
                    || (bit_size > 0) == (bt != prevbt)
                    ) {
		    c = (c + align - 1) & -align;
		    offset = c;
		    bit_pos = 0;
		    /* In MS bitfield mode a bit-field run always uses
		       at least as many bits as the underlying type.
		       To start a new run it's also required that this
		       or the last bit-field had non-zero width.  */
		    if (bit_size || prev_bit_size)
		        c += size;
		}
		/* In MS layout the records alignment is normally
		   influenced by the field, except for a zero-width
		   field at the start of a run (but by further zero-width
		   fields it is again).  */
		if (bit_size == 0 && prevbt != bt)
		    align = 1;
		prevbt = bt;
                prev_bit_size = bit_size;
	    }

	    f->type.t = (f->type.t & ~(0x3f << VT_STRUCT_SHIFT))
		        | (bit_pos << VT_STRUCT_SHIFT);
	    bit_pos += bit_size;
	}
	if (align > maxalign)
	    maxalign = align;

#ifdef BF_DEBUG
	printf("set field %s offset %-2d size %-2d align %-2d",
	       get_tok_str(f->v & ~SYM_FIELD, NULL), offset, size, align);
	if (f->type.t & VT_BITFIELD) {
	    printf(" pos %-2d bits %-2d",
                    BIT_POS(f->type.t),
                    BIT_SIZE(f->type.t)
                    );
	}
	printf("\n");
#endif

        f->c = offset;
	f->r = 0;
    }

    if (pcc)
        c += (bit_pos + 7) >> 3;

    /* store size and alignment */
    a = bt = ad->a.aligned ? 1 << (ad->a.aligned - 1) : 1;
    if (a < maxalign)
        a = maxalign;
    type->ref->r = a;
    if (pragma_pack && pragma_pack < maxalign && 0 == pcc) {
        /* can happen if individual align for some member was given.  In
           this case MSVC ignores maxalign when aligning the size */
        a = pragma_pack;
        if (a < bt)
            a = bt;
    }
    c = (c + a - 1) & -a;
    type->ref->c = c;

#ifdef BF_DEBUG
    printf("struct size %-2d align %-2d\n\n", c, a), fflush(stdout);
#endif

    /* check whether we can access bitfields by their type */
    for (f = type->ref->next; f; f = f->next) {
        int s, px, cx, c0;
        CType t;

        if (0 == (f->type.t & VT_BITFIELD))
            continue;
        f->type.ref = f;
        f->auxtype = -1;
        bit_size = BIT_SIZE(f->type.t);
        if (bit_size == 0)
            continue;
        bit_pos = BIT_POS(f->type.t);
        size = type_size(&f->type, &align);

        if (bit_pos + bit_size <= size * 8 && f->c + size <= c
#ifdef TCC_TARGET_ARM
            && !(f->c & (align - 1))
#endif
            )
            continue;

        /* try to access the field using a different type */
        c0 = -1, s = align = 1;
        t.t = VT_BYTE;
        for (;;) {
            px = f->c * 8 + bit_pos;
            cx = (px >> 3) & -align;
            px = px - (cx << 3);
            if (c0 == cx)
                break;
            s = (px + bit_size + 7) >> 3;
            if (s > 4) {
                t.t = VT_LLONG;
            } else if (s > 2) {
                t.t = VT_INT;
            } else if (s > 1) {
                t.t = VT_SHORT;
            } else {
                t.t = VT_BYTE;
            }
            s = type_size(&t, &align);
            c0 = cx;
        }

        if (px + bit_size <= s * 8 && cx + s <= c
#ifdef TCC_TARGET_ARM
            && !(cx & (align - 1))
#endif
            ) {
            /* update offset and bit position */
            f->c = cx;
            bit_pos = px;
	    f->type.t = (f->type.t & ~(0x3f << VT_STRUCT_SHIFT))
		        | (bit_pos << VT_STRUCT_SHIFT);
            if (s != size)
                f->auxtype = t.t;
#ifdef BF_DEBUG
            printf("FIX field %s offset %-2d size %-2d align %-2d "
                "pos %-2d bits %-2d\n",
                get_tok_str(f->v & ~SYM_FIELD, NULL),
                cx, s, align, px, bit_size);
#endif
        } else {
            /* fall back to load/store single-byte wise */
            f->auxtype = VT_STRUCT;
#ifdef BF_DEBUG
            printf("FIX field %s : load byte-wise\n",
                 get_tok_str(f->v & ~SYM_FIELD, NULL));
#endif
        }
    }
}

/* Does 'n' fit into integer type 't' ? */
static int in_range(long long n, int t)
{
    unsigned long long m = (1ULL << (btype_size(t & VT_BTYPE) * 8 - 1)) - 1;
    if (t & VT_UNSIGNED)
        return n <= (m << 1) + 1;
    return n >= -(long long)m - 1 && n <= (long long)m;
}

/* enum/struct/union declaration. u is VT_ENUM/VT_STRUCT/VT_UNION */
static void struct_decl(CType *type, int u)
{
    int v, c, size, align, flexible;
    int bit_size, bsize, bt, ut;
#ifdef C6_EXT
    int c6_is_def;
#endif
    Sym *s, *ss, **ps;
    AttributeDef ad, ad1;
    CType type1, btype;

    memset(&ad, 0, sizeof ad);
    next();
    parse_attribute(&ad);

    v = 0;
    if (tok >= TOK_IDENT) /* struct/enum tag */
        v = tok, next();

#ifdef C6_EXT
    c6_is_def = (tok == '{' || tok == ';');
    /* a tag declared at file scope in a module is registered under the
       module prefix; references keep the source spelling and struct_find()
       falls back to the prefixed tag (then to a plain C tag) */
    if (v && file->c6_module && local_scope == 0 && c6_is_def)
        v = c6_mod_name(v);
#endif

    bt = ut = 0;
    if (u == VT_ENUM) {
        ut = VT_INT;
        if (tok == ':') { /* C2x enum : <type> ... */
            next();
            if (!parse_btype(&btype, &ad1, 0)
             || !is_integer_btype(btype.t & VT_BTYPE))
                expect("enum type");
            bt = ut = btype.t & (VT_BTYPE|VT_LONG|VT_UNSIGNED|VT_DEFSIGN);
        }
    }

    if (v) {
        /* struct already defined ? return it.  A tag defined in a block
           must be looked up raw, otherwise the module tag of the same
           spelling would be treated as already defined. */
#ifdef C6_EXT
        s = (local_scope != 0 && c6_is_def) ? struct_find_raw(v)
                                            : struct_find(v);
#else
        s = struct_find(v);
#endif
        if (s && (s->sym_scope == local_scope || (tok != '{' && tok != ';'))) {
            if (u == s->type.t)
                goto do_decl;
            if (u == VT_ENUM && IS_ENUM(s->type.t)) /* XXX: check integral types */
                goto do_decl;
            tcc_error("redeclaration of '%s'", get_tok_str(v, NULL));
        }
    } else {
        if (tok != '{')
            expect("struct/union/enum name");
        v = anon_sym++;
    }
    /* Record the original enum/struct/union token.  */
    type1.t = u | ut;
    type1.ref = NULL;
    /* we put an undefined size for struct/union */
    s = sym_push(v | SYM_STRUCT, &type1, 0, bt ? 0 : -1);
    s->r = 0; /* default alignment is zero as gcc */
do_decl:
    type->t = s->type.t;
    type->ref = s;

    if (tok == '{') {
        next();
        if (s->c != -1
            && !(u == VT_ENUM && s->c == 0)) /* not yet defined typed enum */
            tcc_error("struct/union/enum already defined");
        s->c = -2;
        /* cannot be empty */
        /* non empty enums are not allowed */
        ps = &s->next;
        if (u == VT_ENUM) {
            long long ll = 0, pl = 0, nl = 0;
	    CType t;
            t.ref = s;
            s->sym_scope = local_scope; /* anonymous symbol won't have set */
            /* enum symbols have static storage */
            t.t = VT_INT|VT_STATIC|VT_ENUM_VAL;
            if (bt)
                t.t = bt|VT_STATIC|VT_ENUM_VAL;
            for(;;) {
                v = tok;
                if (v < TOK_UIDENT)
                    expect("identifier");
#ifdef C6_EXT
                /* enumerators of a file scope enum are exported names */
                if (file->c6_module && local_scope == 0)
                    v = c6_mod_name(v);
#endif
                next();
                if (tok == '=') {
                    next();
		    ll = expr_const64();
                }
                if (bt && !in_range(ll, t.t))
		    tcc_error("enumerator '%s' out of range of its type",
			get_tok_str(v, NULL));
                ss = sym_push(v, &t, VT_CONST, 0);
                ss->enum_val = ll;
                *ps = ss, ps = &ss->next;
                if (ll < nl)
                    nl = ll;
                if (ll > pl)
                    pl = ll;
                if (tok != ',')
                    break;
                next();
                ll++;
                /* NOTE: we accept a trailing comma */
                if (tok == '}')
                    break;
            }
            skip('}');

            if (bt) {
                t.t = bt;
                s->c = 2;
                goto enum_done;
            }

            /* set integral type of the enum */
            t.t = VT_INT;
            if (nl >= 0) {
                if (pl != (unsigned)pl)
                    t.t = (LONG_SIZE==8 ? VT_LLONG|VT_LONG : VT_LLONG);
                t.t |= VT_UNSIGNED;
            } else if (pl != (int)pl || nl != (int)nl)
                t.t = (LONG_SIZE==8 ? VT_LLONG|VT_LONG : VT_LLONG);

            /* set type for enum members */
            for (ss = s->next; ss; ss = ss->next) {
                ll = ss->enum_val;
                if (ll == (int)ll) /* default is int if it fits */
                    continue;
                if (t.t & VT_UNSIGNED) {
                    ss->type.t |= VT_UNSIGNED;
                    if (ll == (unsigned)ll)
                        continue;
                }
                ss->type.t = (ss->type.t & ~VT_BTYPE)
                    | (LONG_SIZE==8 ? VT_LLONG|VT_LONG : VT_LLONG);
            }
            s->c = 1;
        enum_done:
            s->type.t = type->t = t.t | VT_ENUM;

        } else {
            c = 0;
            flexible = 0;
            while (tok != '}') {
                if (!parse_btype(&btype, &ad1, 0)) {
                    if (tok == TOK_STATIC_ASSERT) {
                        do_Static_assert();
                        continue;
                    }
		    skip(';');
		    continue;
		}
                while (1) {
		    if (flexible)
		        tcc_error("flexible array member '%s' not at the end of struct",
                              get_tok_str(v, NULL));
                    bit_size = -1;
                    v = 0;
                    type1 = btype;
                    if (tok != ':') {
			if (tok != ';')
                            type_decl(&type1, &ad1, &v, TYPE_DIRECT);
                        if (v == 0) {
                    	    if ((type1.t & VT_BTYPE) != VT_STRUCT)
                        	expect("identifier");
                    	    else {
				int v = btype.ref->v;
				if ((v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
				    if (tcc_state->ms_extensions == 0)
                        		expect("identifier");
				}
                    	    }
                        }
                        if (type_size(&type1, &align) < 0) {
			    if ((u == VT_STRUCT) && (type1.t & VT_ARRAY) && c)
			        flexible = 1;
			    else
			        tcc_error("field '%s' has incomplete type",
                                      get_tok_str(v, NULL));
                        }
                        if ((type1.t & VT_BTYPE) == VT_FUNC ||
			    (type1.t & VT_BTYPE) == VT_VOID ||
                            (type1.t & VT_STORAGE))
                            tcc_error("invalid type for '%s'", 
                                  get_tok_str(v, NULL));
                    }
                    if (tok == ':') {
                        next();
                        bit_size = expr_const();
                        /* XXX: handle v = 0 case for messages */
                        if (bit_size < 0)
                            tcc_error("negative width in bit-field '%s'", 
                                  get_tok_str(v, NULL));
                        if (v && bit_size == 0)
                            tcc_error("zero width for bit-field '%s'", 
                                  get_tok_str(v, NULL));
			parse_attribute(&ad1);
                    }
                    size = type_size(&type1, &align);
                    if (bit_size >= 0) {
                        bt = type1.t & VT_BTYPE;
                        if (bt != VT_INT && 
                            bt != VT_BYTE && 
                            bt != VT_SHORT &&
                            bt != VT_BOOL &&
                            bt != VT_LLONG)
                            tcc_error("bitfields must have scalar type");
                        bsize = size * 8;
                        if (bit_size > bsize) {
                            tcc_error("width of '%s' exceeds its type",
                                  get_tok_str(v, NULL));
                        } else if (bit_size == bsize
				    && !*tcc_state->pack_stack_ptr
                                    && !ad.a.packed && !ad1.a.packed) {
                            /* no need for bit fields */
                            ;
                        } else if (bit_size == 64) {
                            tcc_error("field width 64 not implemented");
                        } else {
                            type1.t = (type1.t & ~VT_STRUCT_MASK)
                                | VT_BITFIELD
                                | ((unsigned)bit_size << (VT_STRUCT_SHIFT + 6));
                        }
                    }
                    if (v != 0 || (type1.t & VT_BTYPE) == VT_STRUCT) {
                        /* Remember we've seen a real field to check
			   for placement of flexible array member. */
			c = 1;
                    }
		    /* If member is a struct or bit-field, enforce
		       placing into the struct (as anonymous).  */
                    if (v == 0 &&
			((type1.t & VT_BTYPE) == VT_STRUCT ||
			 bit_size >= 0)) {
		        v = anon_sym++;
		    }
                    if (v) {
                        ss = sym_push(v | SYM_FIELD, &type1, 0, 0);
                        ss->a = ad1.a;
                        *ps = ss;
                        ps = &ss->next;
                    }
                    if (tok == ';' || tok == TOK_EOF)
                        break;
                    skip(',');
                }
                skip(';');
            }
            skip('}');
	    parse_attribute(&ad);
            if (ad.cleanup_func) {
                tcc_warning("attribute '__cleanup__' ignored on type");
            }
	    check_fields(type, 1);
	    check_fields(type, 0);
            struct_layout(type, &ad);
#ifdef C6_EXT
            if (u == VT_STRUCT || u == VT_UNION)
                c6_gc_collect(type, u == VT_UNION);  /* managed fields */
#endif
        }
        if (debug_modes)
            tcc_debug_fix_forw(tcc_state, type);
    }
#ifdef C6_EXT
    /* collect the exported tags of a module for -emit-header (an incomplete
       struct is recorded too: the header emits a forward declaration) */
    if (file->c6_module && local_scope == 0 && v
        && (s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM && s->c != -2)
        c6_header_record(u == VT_ENUM ? C6_HD_ENUM : C6_HD_STRUCT,
                         s->v & ~SYM_STRUCT, type, s);
#endif
}

static void sym_to_attr(AttributeDef *ad, Sym *s)
{
    merge_symattr(&ad->a, &s->a);
    merge_funcattr(&ad->f, &s->f);
}

/* Add type qualifiers to a type. If the type is an array then the qualifiers
   are added to the element type, copied because it could be a typedef. */
static void parse_btype_qualify(CType *type, int qualifiers)
{
    while (type->t & VT_ARRAY) {
        type->ref = sym_push(SYM_FIELD, &type->ref->type, 0, type->ref->c);
        type = &type->ref->type;
    }
    if (qualifiers & VT_RESTRICT)
        check_restrict_type(type);
    type->t |= qualifiers;
}

#ifdef C6_EXT
/* Try to read the recorded group TS as '( type-name {, type-name} )'.  TS is
   replayed as a token stream, so no token of the real input is consumed: the
   replay ends with TOK_EOF, which keeps 'next()' inside the replay instead of
   popping back to the file.  Nothing is diagnosed here, a type name that does
   not fit simply means "not a tuple". */
static int c6_tuple_speculate(TokenString *ts, CType **pelems, int *pn)
{
    CType *elems = NULL, t;
    AttributeDef ad;
    int n = 0, max = 0, name, ok = 1, closed = 0;
    int save_tok = tok;
    CValue save_val = tokc;

    begin_macro(ts, 1);
    next();                             /* the '(' of the group */
    next();                             /* the first element type */
    for (;;) {
        memset(&ad, 0, sizeof ad);
        if (tok == TOK_EOF || !parse_btype(&t, &ad, 0)
            || (t.t & VT_STORAGE)) {
            ok = 0;
            break;
        }
        type_decl(&t, &ad, &name, TYPE_ABSTRACT);
        if (name) {                     /* '(int x, ...)' is a pattern */
            ok = 0;
            break;
        }
        if (n == max) {
            max = max ? 2 * max : 4;
            elems = tcc_realloc(elems, max * sizeof *elems);
        }
        elems[n++] = t;
        if (tok == ',') {
            next();
            continue;
        }
        closed = tok == ')';
        break;
    }
    end_macro();                        /* back to the real input */
    tok = save_tok;
    tokc = save_val;
    if (ok && closed && n >= 2) {
        next();                         /* skip the real ')' */
        *pelems = elems;
        *pn = n;
        return 1;
    }
    tcc_free(elems);
    return 0;
}

/* '( type-name , type-name ... )' in a type position is a tuple type.  The
   whole parenthesised group is scanned first and only a group holding a top
   level ',' is tried, so an ordinary cast such as '(void)printf(..)' or
   '(int)x' never reaches the speculative parser.  A '{' rules the group out:
   that is a struct or enum body, and defining it twice would be wrong.  When
   the group is not a tuple its tokens are pushed back, so the caller sees the
   very same '(' again and parses a parenthesised declarator or an expression
   exactly as before.  Returns 1 and fills TYPE on success. */
static int c6_tuple_btype(CType *type)
{
    TokenString *back = tok_str_alloc();
    TokenString *replay = tok_str_alloc();
    CType *elems = NULL, tup;
    int n = 0, depth = 0, comma = 0, broken = 0, ok = 0;

    c6_tuple_ate_paren = 0;

    for (;;) {
        tok_str_add_tok(back);
        tok_str_add_tok(replay);
        if (tok == '(' || tok == '[') {
            depth++;
        } else if (tok == ']') {
            if (depth > 1)
                depth--;
        } else if (tok == ')') {
            if (--depth == 0)
                break;
        } else if (tok == '{' || tok == '}') {
            broken = 1;
        } else if (tok == ',' && depth == 1) {
            comma = 1;
        } else if (tok == TOK_EOF) {
            broken = 1;
            break;
        }
        next();
    }
    tok_str_add(back, 0);               /* pops back to the real input */
    tok_str_add(replay, TOK_EOF);       /* never pops */
    if (!broken && comma && tok == ')')
        ok = c6_tuple_speculate(replay, &elems, &n);
    else
        tok_str_free(replay);
    if (ok) {
        tok_str_free(back);
        tup = c6_tuple_ctype(c6_tuple_get(elems, n));
        tcc_free(elems);
        *type = tup;
        c6_tuple_ate_paren = 1;
        return 1;
    }
    /* not a tuple: hand the group back to the caller */
    begin_macro(back, 1);
    next();                             /* tok is '(' again */
    return 0;
}
#endif

#ifdef C6_EXT
/* does FIRST name the leading segment of a package/module (current or
   imported)?  Used to keep 'a.b' expressions from looking like a type. */
static int c6_pkg_prefix_known(const char *first)
{
    int n = strlen(first), i;
    const char *s;

    if (!tcc_state || !tcc_state->c6_ext)
        return 0;
#define C6_PREFIX_MATCH(s) \
    (!strncmp((s), first, n) \
     && ((s)[n] == 0 || (s)[n] == '.' \
         || ((s)[n] == '_' && (s)[n + 1] == '_')))
    if (tcc_state->c6_package && C6_PREFIX_MATCH(tcc_state->c6_package))
        return 1;
    if (file && file->c6_module && C6_PREFIX_MATCH(file->c6_module))
        return 1;
    for (i = 0; i < tcc_state->c6_nb_imports; i++) {
        s = tcc_state->c6_imports[i];
        if (C6_PREFIX_MATCH(s))
            return 1;
    }
#undef C6_PREFIX_MATCH
    return 0;
}

/* 'pkg.Type' (also 'a.b.Type'): a type name qualified by the current
   package/module or by an imported one.  Returns the typedef or tag symbol
   and sets *is_tag; tok is left after the name.  Returns NULL (and puts the
   first token back) when the input is not a qualified type name. */
static Sym *c6_qualified_type(int *is_tag)
{
    TokenString *back;
    char pfx[160], pkgdisp[200];
    int first = tok, seg, mt, n;
    Sym *s;

    *is_tag = 0;
    if (sym_find(first))        /* a visible name keeps its C meaning */
        return NULL;
    n = snprintf(pfx, sizeof pfx, "%s", get_tok_str(first, NULL));
    if (n >= (int)sizeof pfx)
        tcc_error("package name too long");
    if (!c6_pkg_prefix_known(pfx))
        return NULL;
    back = tok_str_alloc();
    tok_str_add_tok(back);      /* the first segment */
    snprintf(pkgdisp, sizeof pkgdisp, "%s", pfx);
    next();
    while (tok == '.') {
        tok_str_add_tok(back);  /* '.' */
        next();
        if (tok < TOK_UIDENT)
            goto fail;          /* not a qualified name after all */
        seg = tok;
        tok_str_add_tok(back);
        mt = c6_mangle_tok(pfx, seg);
        s = sym_find_raw(mt);
        if (s && (s->type.t & VT_TYPEDEF)) {
            if (!c6_pkg_imported(pfx))
                tcc_error("'%s' is not an imported package "
                          "(use 'import %s;')", pkgdisp, pkgdisp);
            next();
            tok_str_free(back);
            return s;
        }
        s = struct_find_raw(mt);
        if (s) {
            if (!c6_pkg_imported(pfx))
                tcc_error("'%s' is not an imported package "
                          "(use 'import %s;')", pkgdisp, pkgdisp);
            next();
            tok_str_free(back);
            *is_tag = 1;
            return s;
        }
        next();                 /* the segment was not a type */
        if (tok != '.')
            goto fail;
        n = strlen(pfx);
        if (n + 2 + strlen(get_tok_str(seg, NULL)) >= (int)sizeof pfx)
            tcc_error("package name too long");
        snprintf(pfx + n, sizeof pfx - n, "__%s",
                 get_tok_str(seg, NULL));
        n = strlen(pkgdisp);
        if (n + 1 + strlen(get_tok_str(seg, NULL))
            >= (int)sizeof pkgdisp)
            tcc_error("package name too long");
        snprintf(pkgdisp + n, sizeof pkgdisp - n, ".%s",
                 get_tok_str(seg, NULL));
    }
fail:
    /* not a type: hand the same tokens back to the caller.  The token
       after the last segment was already read, so replay it too. */
    tok_str_add_tok(back);
    tok_str_add(back, 0);
    begin_macro(back, 1);
    next();
    return NULL;
}
#endif

/* return 0 if no type declaration. otherwise, return the basic type
   and skip it. 
 */
static int parse_btype(CType *type, AttributeDef *ad, int ignore_label)
{    int t, u, bt, st, type_found, typespec_found, g, n;
    Sym *s;
    CType type1;
#ifdef C6_EXT
    int gc_qual = 0, gc_managed = 0;
#endif

    memset(ad, 0, sizeof(AttributeDef));
    type_found = 0;
    typespec_found = 0;
    t = VT_INT;
    bt = st = -1;
    type->ref = NULL;
    type->gq = 0;

    while(1) {
#ifdef C6_EXT
        /* a managed reference: '@' is a prefix on the base type
           ('@Node head;'), its qualifiers come first ('weak @Node p;') */
        if (tcc_state->c6_ext) {
            if (tok == '@') {
                gc_managed = 1;
                next();
                continue;
            }
            g = c6_gc_qual(tok);
            if (g && !c6_soft_kw_shadowed(tok)) {
                gc_qual |= g;
                next();
                continue;
            }
        }
#endif
        switch(tok) {
        case TOK_EXTENSION:
            /* currently, we really ignore extension */
            next();
            continue;

            /* basic types */
        case TOK_CHAR:
            u = VT_BYTE;
        basic_type:
            next();
        basic_type1:
            if (u == VT_SHORT || u == VT_LONG) {
                if (st != -1 || (bt != -1 && bt != VT_INT))
                    tmbt: tcc_error("too many basic types");
                st = u;
            } else {
                if (bt != -1 || (st != -1 && u != VT_INT))
                    goto tmbt;
                if ((t & VT_DEFSIGN) && (u == VT_VOID || u > VT_LLONG))
                    goto tmbt;
                bt = u;
            }
            if (u != VT_INT)
                t = (t & ~(VT_BTYPE|VT_LONG)) | u;
            typespec_found = 1;
            break;
        case TOK_VOID:
            u = VT_VOID;
            goto basic_type;
        case TOK_SHORT:
            u = VT_SHORT;
            goto basic_type;
        case TOK_INT:
            u = VT_INT;
            goto basic_type;
        case TOK_ALIGNAS:
            { int n;
              AttributeDef ad1;
              next();
              skip('(');
              memset(&ad1, 0, sizeof(AttributeDef));
              if (parse_btype(&type1, &ad1, 0)) {
                  type_decl(&type1, &ad1, &n, TYPE_ABSTRACT);
                  if (ad1.a.aligned)
                    n = 1 << (ad1.a.aligned - 1);
                  else
                    type_size(&type1, &n);
              } else {
                  n = expr_const();
                  if (n < 0 || (n & (n - 1)) != 0)
                    tcc_error("alignment must be a positive power of two");
              }
              skip(')');
              ad->a.aligned = exact_log2p1(n);
            }
            continue;
        case TOK_LONG:
            if ((t & VT_BTYPE) == VT_DOUBLE) {
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LDOUBLE;
            } else if ((t & (VT_BTYPE|VT_LONG)) == VT_LONG) {
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LLONG;
            } else {
                u = VT_LONG;
                goto basic_type;
            }
            next();
            break;
        case TOK_BOOL:
            u = VT_BOOL;
            goto basic_type;
        case TOK_COMPLEX:
            tcc_error("_Complex is not yet supported");
        case TOK_FLOAT:
            u = VT_FLOAT;
            goto basic_type;
        case TOK_DOUBLE:
            if ((t & (VT_BTYPE|VT_LONG)) == VT_LONG) {
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LDOUBLE;
            } else {
                u = VT_DOUBLE;
                goto basic_type;
            }
            next();
            break;
        case TOK_ENUM:
            struct_decl(&type1, VT_ENUM);
        basic_type2:
            u = type1.t;
            type->ref = type1.ref;
            goto basic_type1;
        case TOK_STRUCT:
            struct_decl(&type1, VT_STRUCT);
            goto basic_type2;
        case TOK_UNION:
            struct_decl(&type1, VT_UNION);
            goto basic_type2;

        case '(':
#ifdef C6_EXT
            /* '(T1, T2)' - a tuple type.  'c6_tuple_btype' claims the whole
               parenthesised group only when it really parses as two or more
               type names, and hands it back otherwise, so an ordinary cast
               or a parenthesised declarator keeps its meaning. */
            if (tcc_state->c6_ext && !typespec_found) {
                CType tup;
                if (c6_tuple_btype(&tup)) {
                    /* keep what was written before the tuple: storage
                       classes ('typedef (int,int) DM;') and qualifiers */
                    t = tup.t | (t & (VT_STORAGE | VT_QUAL));
                    type->ref = tup.ref;
                    typespec_found = 1;
                    break;
                }
            }
#endif
            goto the_end;

        case '[':
#ifdef C6_EXT
            /* 'T[:]' - a slice type.  A '[' directly after a type specifier
               is not valid C (array declarators come after the declarator
               name), so only '[:]' is claimed here; anything else, e.g.
               'int [3] a;', is handed back to the original code. */
            if (tcc_state->c6_ext && typespec_found) {
                CType elem, st;
                int storage;
                next();                 /* skip '[' */
                if (tok == ':') {
                    next();             /* skip ':' */
                    skip(']');
                    elem.t = t & ~VT_STORAGE;
                    elem.ref = type->ref;
                    storage = t & VT_STORAGE;
                    st = c6_slice_ctype(c6_slice_get(&elem));
                    t = st.t | storage;
                    type->ref = st.ref;
                    break;
                }
                unget_tok('[');         /* not a slice: restore and stop */
            }
#endif
            goto the_end;

            /* type modifiers */
        case TOK__Atomic:
            next();
            type->t = t;
            parse_btype_qualify(type, VT_ATOMIC);
            t = type->t;
            if (tok == '(') {
                parse_expr_type(&type1);
                /* remove all storage modifiers except typedef */
                type1.t &= ~(VT_STORAGE&~VT_TYPEDEF);
                if (type1.ref)
                    sym_to_attr(ad, type1.ref);
                goto basic_type2;
            }
            break;
        case TOK_CONST1:
        case TOK_CONST2:
        case TOK_CONST3:
            type->t = t;
            parse_btype_qualify(type, VT_CONSTANT);
            t = type->t;
            next();
            break;
        case TOK_VOLATILE1:
        case TOK_VOLATILE2:
        case TOK_VOLATILE3:
            type->t = t;
            parse_btype_qualify(type, VT_VOLATILE);
            t = type->t;
            next();
            break;
        case TOK_SIGNED1:
        case TOK_SIGNED2:
        case TOK_SIGNED3:
            if ((t & (VT_DEFSIGN|VT_UNSIGNED)) == (VT_DEFSIGN|VT_UNSIGNED))
                tcc_error("signed and unsigned modifier");
            t |= VT_DEFSIGN;
            next();
            typespec_found = 1;
            break;
        case TOK_RESTRICT1:
        case TOK_RESTRICT2:
        case TOK_RESTRICT3:
            t |= VT_RESTRICT;
            next();
            break;
        case TOK_REGISTER:
        case TOK_AUTO:
            next();
            break;
        case TOK_UNSIGNED:
            if ((t & (VT_DEFSIGN|VT_UNSIGNED)) == VT_DEFSIGN)
                tcc_error("signed and unsigned modifier");
            t |= VT_DEFSIGN | VT_UNSIGNED;
            next();
            typespec_found = 1;
            break;

            /* storage */
        case TOK_EXTERN:
            g = VT_EXTERN;
            goto storage;
        case TOK_STATIC:
            g = VT_STATIC;
            goto storage;
        case TOK_TYPEDEF:
            g = VT_TYPEDEF;
            goto storage;
       storage:
            if (t & (VT_EXTERN|VT_STATIC|VT_TYPEDEF) & ~g)
                tcc_error("multiple storage classes");
            t |= g;
            next();
            break;
        case TOK_INLINE1:
        case TOK_INLINE2:
        case TOK_INLINE3:
            t |= VT_INLINE;
            next();
            break;
#ifdef C6_EXT
        case TOK_C6_ASYNC:
            /* 'async T f(...)' : the function is a stackless coroutine */
            ad->f.func_async = 1;
            next();
            break;
#endif
        case TOK_NORETURN3:
            next();
            ad->f.func_noreturn = 1;
            break;
            /* GNUC attribute */
        case TOK_ATTRIBUTE1:
        case TOK_ATTRIBUTE2:
            parse_attribute(ad);
            if (ad->attr_mode) {
                u = ad->attr_mode -1;
                t = (t & ~(VT_BTYPE|VT_LONG)) | u;
            }
            continue;
            /* GNUC typeof */
        case TOK_TYPEOF1:
        case TOK_TYPEOF2:
        case TOK_TYPEOF3:
            next();
            parse_expr_type(&type1);
            /* remove all storage modifiers except typedef */
            type1.t &= ~(VT_STORAGE&~VT_TYPEDEF);
            if (type1.ref) {
                sym_to_attr(ad, type1.ref);
                if (type1.t & VT_ARRAY)
                    type1.t |= VT_BT_ARRAY;
            }
            goto basic_type2;
        case TOK_THREAD_LOCAL:
        case TOK___thread:
            if (t & VT_TLS)
                tcc_error("multiple thread-local storage specifiers");
            t |= VT_TLS;
            next();
            break;
        default:
            if (typespec_found)
                goto the_end;
#ifdef C6_EXT
            /* 'pkg.Type' / 'module.Type' */
            if (tcc_state->c6_ext && tok >= TOK_UIDENT) {
                int qtag;
                Sym *qs = c6_qualified_type(&qtag);
                if (qs) {
                    if (qtag) {
                        type1.t = qs->type.t;
                        type1.ref = qs;
                        goto basic_type2;
                    }
                    /* same as the typedef branch below */
                    t &= ~(VT_BTYPE|VT_LONG);
                    u = t & ~VT_QUAL, t ^= u;
                    type->t = (qs->type.t & ~VT_TYPEDEF) | u;
                    type->ref = qs->type.ref;
                    if (t)
                        parse_btype_qualify(type, t);
                    t = type->t;
                    if (t & VT_ARRAY)
                        t |= VT_BT_ARRAY;
                    sym_to_attr(ad, qs);
                    typespec_found = 1;
                    st = bt = -2;
                    break;
                }
            }
#endif
            s = sym_find(tok);
            if (!s) {
#ifdef C6_EXT
                Sym *tag;
                /* c6: a visible struct/union/enum tag is a type name even
                   without the keyword.  Only when the ordinary namespace
                   does not own the name: 'struct stat' next to 'stat()'
                   keeps C's meaning, so 'stat(...)' stays a call. */
                if (tcc_state->c6_ext && (tag = struct_find(tok)) != NULL) {
                    n = tok, next();
                    if (tok == ':' && ignore_label) {
                        /* ignore if it's a label */
                        unget_tok(n);
                        goto the_end;
                    }
                    /* same as 'struct/union/enum TAG' */
                    type1.t = tag->type.t;
                    type1.ref = tag;
                    goto basic_type2;
                }
#endif
                goto the_end;
            }
            if (!(s->type.t & VT_TYPEDEF))
                goto the_end;

            n = tok, next();
            if (tok == ':' && ignore_label) {
                /* ignore if it's a label */
                unget_tok(n);
                goto the_end;
            }

            t &= ~(VT_BTYPE|VT_LONG);
            u = t & ~VT_QUAL, t ^= u;
            type->t = (s->type.t & ~VT_TYPEDEF) | u;
            type->ref = s->type.ref;
            if (t)
                parse_btype_qualify(type, t);
            t = type->t;
            if (t & VT_ARRAY)
                t |= VT_BT_ARRAY;
            /* get attributes from typedef */
            sym_to_attr(ad, s);
            typespec_found = 1;
            st = bt = -2;
            break;
        }
        type_found = 1;
    }
the_end:
#ifdef C6_EXT
    if (gc_managed) {
        /* '@T' is a pointer to T with the managed qualifier attached: the
           layout, the ABI and every pointer operation stay those of 'T *' */
        type->t = t;
        mk_pointer(type);
        type->gq = C6_GQ_MANAGED | gc_qual;
        type->ref->type.gq = type->gq;  /* the trusted copy */
        t = type->t;
        if ((type->ref->type.t & VT_BTYPE) == VT_VOID)
            tcc_error("'@' needs an object type, not 'void'");
    } else if (gc_qual) {
        tcc_error("'weak', 'own' and 'shared' are only valid in front of '@'");
    }
#endif
    if (tcc_state->char_is_unsigned) {
        if ((t & (VT_DEFSIGN|VT_BTYPE)) == VT_BYTE)
            t |= VT_UNSIGNED;
    }
    /* VT_LONG is used just as a modifier for VT_INT / VT_LLONG */
    bt = t & (VT_BTYPE|VT_LONG);
    if (bt == VT_LONG)
        t |= LONG_SIZE == 8 ? VT_LLONG : VT_INT;
#ifdef TCC_USING_DOUBLE_FOR_LDOUBLE
    if (bt == VT_LDOUBLE)
        t = (t & ~(VT_BTYPE|VT_LONG)) | (VT_DOUBLE|VT_LONG);
#endif
    type->t = t;
    check_restrict_type(type);
    return type_found;
}

/* convert a function parameter type (array to pointer and function to
   function pointer) */
static inline void convert_parameter_type(CType *pt)
{
    /* array must be transformed to pointer according to ANSI C */
    pt->t &= ~(VT_ARRAY | VT_VLA);
    if ((pt->t & VT_BTYPE) == VT_FUNC) {
        mk_pointer(pt);
    }
}

/* apply the conversions required for an expression value */
static inline void convert_expression_type(CType *pt)
{
    pt->t &= ~VT_QUAL;
    convert_parameter_type(pt);
}

ST_FUNC CString* parse_asm_str(void)
{
    skip('(');
    return parse_mult_str("string constant");
}

/* Parse an asm label and return the token */
static int asm_label_instr(void)
{
    int v;
    char *astr;

    next();
    astr = parse_asm_str()->data;
    skip(')');
#ifdef ASM_DEBUG
    printf("asm_alias: \"%s\"\n", astr);
#endif
    v = tok_alloc_const(astr);
    return v;
}

static int post_type(CType *type, AttributeDef *ad, int storage, int td)
{
    int n, l, t1, arg_size, align;
    Sym **plast, *s, *first, **ps, *sr;
    AttributeDef ad1;
    CType pt;
    TokenString *vla_array_tok = NULL;
    int *vla_array_str = NULL;

    if (tok == '(') {
        /* function type, or recursive declarator (return if so) */
        next();
	if (TYPE_DIRECT == (td & (TYPE_DIRECT|TYPE_ABSTRACT)))
	  return 0;

        /* we push a anonymous symbol which will contain the function prototype */
        /* it also serves as a boundary for the function parameter scope */
        ps = local_stack ? &local_stack : &global_stack;
        ++local_scope;
        sr = sym_push2(ps, SYM_FIELD, 0, 0);

	if (tok == ')')
	  l = 0;
	else if (parse_btype(&pt, &ad1, 0))
	  l = FUNC_NEW;
	else if (td & (TYPE_DIRECT|TYPE_ABSTRACT)) {
            sym_pop(ps, sr->prev, 0);
            --local_scope;
	    merge_attr (ad, &ad1);
	    return 0;
	} else
	  l = FUNC_OLD;

        first = NULL;
        plast = &first;
        arg_size = 0;
        if (l) {
            for(;;) {
                /* read param name and compute offset */
                if (l != FUNC_OLD) {
                    if ((pt.t & VT_BTYPE) == VT_VOID && tok == ')')
                        break;
                    type_decl(&pt, &ad1, &n, TYPE_DIRECT | TYPE_ABSTRACT | TYPE_PARAM);
                    if ((pt.t & VT_BTYPE) == VT_VOID)
                        tcc_error("parameter declared as void");
#ifdef C6_EXT
                    c6_check_error_param(&pt);
#endif
                    if (n == 0 || (td & TYPE_PARAM))
                        n |= SYM_FIELD;
                } else {
                    n = tok;
                    pt.t = VT_INT | VT_EXTERN; /* default type */
                    pt.ref = NULL;
                    next();
                }
                if (n < TOK_UIDENT)
                    expect("identifier");
                convert_parameter_type(&pt);
                arg_size += (type_size(&pt, &align) + PTR_SIZE - 1) / PTR_SIZE;
                /* these symbols may be evaluated for VLArrays (see below, under
                   nocode_wanted) Example: int func(int a, int b[++a]); */
                s = sym_push(n, &pt, VT_LOCAL|VT_LVAL, 0);
                *plast = s;
                plast = &s->next;
                if (tok == ')')
                    break;
                skip(',');
                if (l == FUNC_NEW && tok == TOK_DOTS) {
                    l = FUNC_ELLIPSIS;
                    next();
                    break;
                }
		if (l == FUNC_NEW && !parse_btype(&pt, &ad1, 0))
		    tcc_error("invalid type");
            }
        } else
            /* if no parameters, then old type prototype */
            l = FUNC_OLD;
        skip(')');
        /* A function return value has the unqualified version of its
           declared type. */
        type->t &= ~VT_QUAL;
        /* some ancient pre-K&R C allows a function to return an array
           and the array brackets to be put after the arguments, such 
           that "int c()[]" means something like "int[] c()" */
        if (tok == '[') {
            next();
            skip(']'); /* only handle simple "[]" */
            mk_pointer(type);
        }
        ad->f.func_args = arg_size;
        ad->f.func_type = l;
        sr->type = *type, s = sr;
        s->a = ad->a;
        s->f = ad->f;
        s->next = first;
        type->t = VT_FUNC;
        type->ref = s;
        /* unlink parameter symbols from the token table, keep on stack */
        sym_pop(ps, sr, 1);
        --local_scope;

    } else if (tok == '[') {
	int saved_nocode_wanted = nocode_wanted;
        int array_qualifiers = 0;
        /* array definition */
        next();
        n = -1;
        t1 = 0;
        if (td & TYPE_PARAM) while (1) {
	    /* XXX The optional type-quals and static should only be accepted
	       in parameter decls.  The '*' as well, and then even only
	       in prototypes (not function defs).  */
	    switch (tok) {
	    case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
		array_qualifiers |= VT_CONSTANT;
		next();
		continue;
	    case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
		array_qualifiers |= VT_VOLATILE;
		next();
		continue;
	    case TOK_RESTRICT1: case TOK_RESTRICT2: case TOK_RESTRICT3:
		array_qualifiers |= VT_RESTRICT;
		next();
		continue;
	    case TOK_STATIC:
	    case '*':
		next();
		continue;
	    default:
		break;
	    }
            if (tok != ']') {
		/* Code generation is not done now but has to be done
		   at start of function. Save code here for later use. */
	        nocode_wanted = 1;
		skip_or_save_block(&vla_array_tok);
		unget_tok(0);
		vla_array_str = vla_array_tok->str;
		begin_macro(vla_array_tok, 2);
		next();
	        gexpr();
		end_macro();
		next();
		goto check;
            }
            break;

	} else if (tok != ']') {
            if (!local_stack || (storage & VT_STATIC))
                vpushi(expr_const());
            else {
		/* VLAs (which can only happen with local_stack && !VT_STATIC)
		   length must always be evaluated, even under nocode_wanted,
		   so that its size slot is initialized (e.g. under sizeof
		   or typeof).  */
		nocode_wanted = 0;
		gexpr();
	    }
check:
            if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
                n = vtop->c.i;
                if (n < 0)
                    tcc_error("invalid array size");
            } else {
                if (!is_integer_btype(vtop->type.t & VT_BTYPE))
                    tcc_error("size of variable length array should be an integer");
                n = 0;
                t1 = VT_VLA;
            }
        }
        skip(']');
        /* parse next post type */
        post_type(type, ad, storage, (td & ~(TYPE_DIRECT|TYPE_ABSTRACT)) | TYPE_NEST);

        if ((type->t & VT_BTYPE) == VT_FUNC)
            tcc_error("declaration of an array of functions");
        if ((type->t & VT_BTYPE) == VT_VOID
            || type_size(type, &align) < 0)
            tcc_error("declaration of an array of incomplete type elements");

        t1 |= type->t & VT_VLA;

        if (t1 & VT_VLA) {
            if (n < 0) {
		if  (td & TYPE_NEST)
                    tcc_error("need explicit inner array size in VLAs");
	    }
	    else {
                loc -= type_size(&int_type, &align);
                loc &= -align;
                n = loc;

                vpush_type_size(type, &align);
                gen_op('*');
                vset(&int_type, VT_LOCAL|VT_LVAL, n);
                vswap();
                vstore();
	    }
        }
        if (n != -1)
            vpop();
	nocode_wanted = saved_nocode_wanted;
                
        /* we push an anonymous symbol which will contain the array
           element type */
        s = sym_push(SYM_FIELD, type, 0, n);
        type->t = (t1 ? VT_VLA : VT_ARRAY) | VT_PTR;
        type->ref = s;
        type->t |= array_qualifiers;

        if (vla_array_str) {
            /* for function args, the top dimension is converted to pointer */
	    if ((t1 & VT_VLA) && (td & TYPE_NEST))
	        s->vla_array_str = vla_array_str;
	    else
	        tok_str_free_str(vla_array_str);
	}
    }
    return 1;
}

/* Parse a type declarator (except basic type), and return the type
   in 'type'. 'td' is a bitmask indicating which kind of type decl is
   expected. 'type' should contain the basic type. 'ad' is the
   attribute definition of the basic type. It can be modified by
   type_decl().  If this (possibly abstract) declarator is a pointer chain
   it returns the innermost pointed to type (equals *type, but is a different
   pointer), otherwise returns type itself, that's used for recursive calls.  */
static CType *type_decl(CType *type, AttributeDef *ad, int *v, int td)
{
    CType *post, *ret;
    int qualifiers, storage;

    /* recursive type, remove storage bits first, apply them later again */
    storage = type->t & VT_STORAGE;
    type->t &= ~VT_STORAGE;
    post = ret = type;

    while (tok == '*') {
        qualifiers = 0;
    redo:
        next();
        switch(tok) {
        case TOK__Atomic:
            qualifiers |= VT_ATOMIC;
            goto redo;
        case TOK_CONST1:
        case TOK_CONST2:
        case TOK_CONST3:
            qualifiers |= VT_CONSTANT;
            goto redo;
        case TOK_VOLATILE1:
        case TOK_VOLATILE2:
        case TOK_VOLATILE3:
            qualifiers |= VT_VOLATILE;
            goto redo;
        case TOK_RESTRICT1:
        case TOK_RESTRICT2:
        case TOK_RESTRICT3:
            qualifiers |= VT_RESTRICT;
            goto redo;
	/* XXX: clarify attribute handling */
	case TOK_ATTRIBUTE1:
	case TOK_ATTRIBUTE2:
	    parse_attribute(ad);
	    break;
        }
        mk_pointer(type);
        type->t |= qualifiers;
	if (ret == type)
	    /* innermost pointed to type is the one for the first derivation */
	    ret = pointed_type(type);
    }

#ifdef C6_EXT
    /* 'int *!error f(void)': the annotation may also sit between the
       pointer stars and the declarator name */
    if (tcc_state->c6_ext && c6_parse_fallible_suffix())
        c6_decl_fallible = 1;
#endif
    if (tok == '(') {
	/* This is possibly a parameter type list for abstract declarators
	   ('int ()'), use post_type for testing this.  */
	if (!post_type(type, ad, 0, td)) {
	    /* It's not, so it's a nested declarator, and the post operations
	       apply to the innermost pointed to type (if any).  */
	    /* XXX: this is not correct to modify 'ad' at this point, but
	       the syntax is not clear */
	    parse_attribute(ad);
	    post = type_decl(type, ad, v, td);
	    skip(')');
	} else
	  goto abstract;
    } else if (tok >= TOK_IDENT && (td & TYPE_DIRECT)) {
	/* type identifier */
	*v = tok;
	next();
    } else {
  abstract:
	if (!(td & TYPE_ABSTRACT))
	  expect("identifier");
	*v = 0;
    }
    post_type(post, ad, post != ret ? 0 : storage,
              td & ~(TYPE_DIRECT|TYPE_ABSTRACT));
    check_restrict_type(type);
    parse_attribute(ad);
    type->t |= storage;
    return ret;
}

/* indirection with full error checking and bound check */
ST_FUNC void indir(void)
{
#ifdef C6_EXT
    c6_fallible_use();          /* '*f()' would drop the error */
#endif
#ifdef C6_EXT
    if (tcc_state->c6_ext && (c6_type_gq(&vtop->type) & C6_GQ_WEAK)) {
        /* the object may die between the load and the use: a weak reference
           has to be upgraded to a counted one first */
        char b[160];
        type_to_str(b, sizeof b, &vtop->type, NULL);
        tcc_error("cannot use a '%s' directly: upgrade it first "
                  "('@T q = w;')", b);
    }
#endif
    if ((vtop->type.t & VT_BTYPE) != VT_PTR) {
        if ((vtop->type.t & VT_BTYPE) == VT_FUNC)
            return;
        expect("pointer");
    }
    if (vtop->r & VT_LVAL)
        gv(RC_INT);
    vtop->type = *pointed_type(&vtop->type);
    /* Arrays and functions are never lvalues */
    if (!(vtop->type.t & (VT_ARRAY | VT_VLA))
        && (vtop->type.t & VT_BTYPE) != VT_FUNC) {
        vtop->r |= VT_LVAL;
        /* if bound checking, the referenced pointer must be checked */
#ifdef CONFIG_TCC_BCHECK
        if (tcc_state->do_bounds_check)
            vtop->r |= VT_MUSTBOUND;
#endif
    }
}

/* pass a parameter to a function and do type checking and casting */
static void gfunc_param_typed(Sym *func, Sym *arg)
{
    int func_type;
    CType type;

    func_type = func->f.func_type;
    if (func_type == FUNC_OLD ||
        (func_type == FUNC_ELLIPSIS && arg == NULL)) {
        /* default casting : only need to convert float to double */
        if ((vtop->type.t & VT_BTYPE) == VT_FLOAT) {
            gen_cast_s(VT_DOUBLE);
        } else if (vtop->type.t & VT_BITFIELD) {
            type.t = vtop->type.t & (VT_BTYPE | VT_UNSIGNED);
	    type.ref = vtop->type.ref;
            gen_cast(&type);
        } else if (vtop->r & VT_MUSTCAST) {
            force_charshort_cast();
        }
    } else if (arg == NULL) {
        tcc_error("too many arguments to function");
    } else {
#ifdef C6_EXT
        c6_gc_value_check(&arg->type, "a parameter");
#endif
        type = arg->type;
        type.t &= ~VT_CONSTANT; /* need to do that to avoid false warning */
        gen_assign_cast(&type);
    }
}

/* parse an expression and return its type without any side effect. */
static void expr_type(CType *type, void (*expr_fn)(void))
{
    nocode_wanted++;
    expr_fn();
    *type = vtop->type;
    vpop();
    nocode_wanted--;
#ifdef C6_EXT
    /* nothing is called in this context, so a 'T!error' mark is moot */
    c6_value_fallible = 0;
#endif
}

/* parse an expression of the form '(type)' or '(expr)' and return its
   type */
static void parse_expr_type(CType *type)
{
    int n;
    AttributeDef ad;

    skip('(');
    if (parse_btype(type, &ad, 0)) {
        type_decl(type, &ad, &n, TYPE_ABSTRACT);
    } else {
        expr_type(type, gexpr);
    }
    skip(')');
}

static void parse_type(CType *type)
{
    AttributeDef ad;
    int n;

    if (!parse_btype(type, &ad, 0)) {
        expect("type");
    }
    type_decl(type, &ad, &n, TYPE_ABSTRACT);
}

static void parse_builtin_params(int nc, const char *args)
{
    char c, sep = '(';
    CType type;
    if (nc)
        nocode_wanted++;
    next();
    if (*args == 0)
	skip(sep);
    while ((c = *args++)) {
	skip(sep);
	sep = ',';
        if (c == 't') {
            parse_type(&type);
	    vpush(&type);
	    continue;
        }
        expr_eq();
        type.ref = NULL;
        type.t = 0;
	switch (c) {
	    case 'e':
		continue;
	    case 'V':
                type.t = VT_CONSTANT;
	    case 'v':
                type.t |= VT_VOID;
                mk_pointer (&type);
                break;
	    case 'S':
                type.t = VT_CONSTANT;
	    case 's':
                type.t |= char_type.t;
                mk_pointer (&type);
                break;
	    case 'i':
                type.t = VT_INT;
                break;
	    case 'l':
                type.t = VT_SIZE_T;
                break;
	    default:
                break;
	}
        gen_assign_cast(&type);
    }
    skip(')');
    if (nc)
        nocode_wanted--;
}

static void parse_atomic(int atok)
{
    int size, align, arg, t, save = 0;
    CType *atom, *atom_ptr, ct = {0};
    SValue store;
    char buf[40];
    static const char *const templates[] = {
        /*
         * Each entry consists of callback and function template.
         * The template represents argument types and return type.
         *
         * ? void (return-only)
         * b bool
         * a atomic
         * A read-only atomic
         * p pointer to memory
         * v value
         * l load pointer
         * s save pointer
         * m memory model
         */

        /* keep in order of appearance in tcctok.h: */
        /* __atomic_store */            "alm.?",
        /* __atomic_load */             "Asm.v",
        /* __atomic_exchange */         "alsm.v",
        /* __atomic_compare_exchange */ "aplbmm.b",
        /* __atomic_fetch_add */        "avm.v",
        /* __atomic_fetch_sub */        "avm.v",
        /* __atomic_fetch_or */         "avm.v",
        /* __atomic_fetch_xor */        "avm.v",
        /* __atomic_fetch_and */        "avm.v",
        /* __atomic_fetch_nand */       "avm.v",
        /* __atomic_and_fetch */        "avm.v",
        /* __atomic_sub_fetch */        "avm.v",
        /* __atomic_or_fetch */         "avm.v",
        /* __atomic_xor_fetch */        "avm.v",
        /* __atomic_and_fetch */        "avm.v",
        /* __atomic_nand_fetch */       "avm.v"
    };
    const char *template = templates[(atok - TOK___atomic_store)];

    atom = atom_ptr = NULL;
    size = 0; /* pacify compiler */
    next();
    skip('(');
    for (arg = 0;;) {
        expr_eq();
        switch (template[arg]) {
        case 'a':
        case 'A':
            atom_ptr = &vtop->type;
            if ((atom_ptr->t & VT_BTYPE) != VT_PTR)
                expect("pointer");
            atom = pointed_type(atom_ptr);
            size = type_size(atom, &align);
            if (size > 8
                || (size & (size - 1))
                || (atok > TOK___atomic_compare_exchange
                    && (0 == btype_size(atom->t & VT_BTYPE)
                        || (atom->t & VT_BTYPE) == VT_PTR)))
                expect("integral or integer-sized pointer target type");
            /* GCC does not care either: */
            /* if (!(atom->t & VT_ATOMIC))
                tcc_warning("pointer target declaration is missing '_Atomic'"); */
            break;

        case 'p':
            if ((vtop->type.t & VT_BTYPE) != VT_PTR
             || type_size(pointed_type(&vtop->type), &align) != size)
                tcc_error("pointer target type mismatch in argument %d", arg + 1);
            gen_assign_cast(atom_ptr);
            break;
        case 'v':
            gen_assign_cast(atom);
            break;
        case 'l':
            indir();
            gen_assign_cast(atom);
            break;
        case 's':
            save = 1;
            indir();
            store = *vtop;
            vpop();
            break;
        case 'm':
            gen_assign_cast(&int_type);
            break;
        case 'b':
            ct.t = VT_BOOL;
            gen_assign_cast(&ct);
            break;
        }
        if ('.' == template[++arg])
            break;
        skip(',');
    }
    skip(')');

    ct.t = VT_VOID;
    switch (template[arg + 1]) {
    case 'b':
        ct.t = VT_BOOL;
        break;
    case 'v':
        ct = *atom;
        break;
    }

    sprintf(buf, "%s_%d", get_tok_str(atok, 0), size);
    vpush_helper_func(tok_alloc_const(buf));
    vrott(arg - save + 1);
    gfunc_call(arg - save);

    vpush(&ct);
    PUT_R_RET(vtop, ct.t);
    t = ct.t & VT_BTYPE;
    if (t == VT_BYTE || t == VT_SHORT || t == VT_BOOL) {
#ifdef PROMOTE_RET
        vtop->r |= BFVAL(VT_MUSTCAST, 1);
#else
        vtop->type.t = VT_INT;
#endif
    }
    gen_cast(&ct);
    if (save) {
        vpush(&ct);
        *vtop = store;
        vswap();
        vstore();
    }
}

ST_FUNC void unary(void)
{
    int n, t, align, size, r;
    CType type;
    Sym *s;
    AttributeDef ad;
#ifdef C6_EXT
    /* how the current identifier was spelled in the source, when it was a
       'pkg.member' package-qualified access (kept for diagnostics) */
    int dot_disp = 0;
#endif

    /* generate line number info */
    if (debug_modes)
        tcc_debug_line(tcc_state), tcc_tcov_check_line (tcc_state, 1);

    type.ref = NULL;
    /* XXX: GCC 2.95.3 does not generate a table although it should be
       better here */
 tok_next:
#ifdef C6_EXT
    if (c6_gc_new_lead()) {     /* 'new T' / 'new T[n]' */
        c6_gc_new_expr();
        return;
    }
    if (c6_gc_move_lead()) {    /* 'move(p)' */
        c6_gc_move_expr();
        return;
    }
#endif
    switch(tok) {
    case TOK_EXTENSION:
        next();
        goto tok_next;
    case TOK_LCHAR:
#ifdef TCC_TARGET_PE
        t = VT_SHORT|VT_UNSIGNED;
        goto push_tokc;
#endif
    case TOK_CINT:
    case TOK_CCHAR: 
	t = VT_INT;
 push_tokc:
	type.t = t;
	vsetc(&type, VT_CONST, &tokc);
        next();
        break;
    case TOK_CUINT:
        t = VT_INT | VT_UNSIGNED;
        goto push_tokc;
    case TOK_CLLONG:
        t = VT_LLONG;
	goto push_tokc;
    case TOK_CULLONG:
        t = VT_LLONG | VT_UNSIGNED;
	goto push_tokc;
    case TOK_CFLOAT:
        t = VT_FLOAT;
	goto push_tokc;
    case TOK_CDOUBLE:
        t = VT_DOUBLE;
	goto push_tokc;
    case TOK_CLDOUBLE:
#ifdef TCC_USING_DOUBLE_FOR_LDOUBLE
        t = VT_DOUBLE | VT_LONG;
        tokc.d = tokc.ld;
#else
        t = VT_LDOUBLE;
#endif
	goto push_tokc;
    case TOK_CLONG:
        t = (LONG_SIZE == 8 ? VT_LLONG : VT_INT) | VT_LONG;
	goto push_tokc;
    case TOK_CULONG:
        t = (LONG_SIZE == 8 ? VT_LLONG : VT_INT) | VT_LONG | VT_UNSIGNED;
	goto push_tokc;
    case TOK___FUNCTION__:
        if (!gnu_ext)
            goto tok_identifier;
        /* fall thru */
    case TOK___FUNC__:
        tok = TOK_STR;
        cstr_reset(&tokcstr);
        cstr_cat(&tokcstr, funcname, 0);
        tokc.str.size = tokcstr.size;
        tokc.str.data = tokcstr.data;
        goto case_TOK_STR;
    case TOK_LSTR:
#ifdef TCC_TARGET_PE
        t = VT_SHORT | VT_UNSIGNED;
#else
        t = VT_INT;
#endif
        goto str_init;
    case TOK_STR:
    case_TOK_STR:
        /* string parsing */
        t = char_type.t;
    str_init:
        if (tcc_state->warn_write_strings & WARN_ON)
            t |= VT_CONSTANT;
        type.t = t;
        mk_pointer(&type);
        type.t |= VT_ARRAY;
        memset(&ad, 0, sizeof(AttributeDef));
        ad.section = rodata_section;
        decl_initializer_alloc(&type, &ad, VT_CONST, 2, 0, 0);
        break;
    case TOK_SOTYPE:
    case '(':
        t = tok;
#ifdef C6_EXT
        /* '(T1, T2)' in expression position: a cast type or the type of a
           compound literal (the declaration forms are handled in decl()) */
        if (c6_ext_on()) {
            CType tup;
            if (c6_tuple_btype(&tup)) {
                if (t == TOK_SOTYPE) {      /* from sizeof/alignof (...) */
                    if (tok == ')')
                        skip(')');
                    vpush(&tup);
                    return;
                }
                if (tok == '{') {           /* '(int,int){q, r}' */
                    if (global_expr)
                        r = VT_CONST;
                    else
                        r = VT_LOCAL;
                    if (!(tup.t & VT_ARRAY))
                        r |= VT_LVAL;
                    memset(&ad, 0, sizeof(AttributeDef));
                    decl_initializer_alloc(&tup, &ad, r, 1, 0, 0);
                } else {                    /* '(int,int)value' */
                    unary();
                    gen_cast(&tup);
                }
                break;
            }
        }
#endif
        next();
#ifdef C6_EXT
        c6_tuple_ate_paren = 0;
#endif
        /* cast ? */
        if (parse_btype(&type, &ad, 0)) {
            int wrap = 0;
            type_decl(&type, &ad, &n, TYPE_ABSTRACT);
#ifdef C6_EXT
            /* a tuple type specifier consumes the ')' of its own group.  When
               the operand follows directly ('((int,int)x)'), this '()' wraps
               the whole cast and its ')' comes last. */
            if (c6_tuple_ate_paren && tok != ')')
                wrap = 1;
            c6_tuple_ate_paren = 0;
#endif
            if (!wrap)
                skip(')');
            /* check ISOC99 compound literal */
            if (tok == '{') {
                    /* data is allocated locally by default */
                if (global_expr)
                    r = VT_CONST;
                else
                    r = VT_LOCAL;
                /* all except arrays are lvalues */
                if (!(type.t & VT_ARRAY))
                    r |= VT_LVAL;
                memset(&ad, 0, sizeof(AttributeDef));
                decl_initializer_alloc(&type, &ad, r, 1, 0, 0);
                if (wrap)
                    skip(')');
            } else if (t == TOK_SOTYPE) { /* from sizeof/alignof (...) */
                if (wrap)
                    skip(')');
                vpush(&type);
                return;
            } else {
                unary();
#ifdef C6_EXT
                /* '(int[:])a' and '(Shape)circle' convert explicitly */
                if (c6_slice_convert(&type) || c6_iface_convert(&type)) {
                    if (wrap)
                        skip(')');
                    break;
                }
#endif
                gen_cast(&type);
                if (wrap)
                    skip(')');
            }
        } else if (tok == '{') {
	    int saved_nocode_wanted = nocode_wanted;
            if (CONST_WANTED && !NOEVAL_WANTED)
                expect("constant");
            if (0 == local_scope)
                tcc_error("statement expression outside of function");
            /* save all registers */
            save_regs(0);
            /* statement expression : we do not accept break/continue
               inside as GCC does.  We do retain the nocode_wanted state,
	       as statement expressions can't ever be entered from the
	       outside, so any reactivation of code emission (from labels
	       or loop heads) can be disabled again after the end of it. */
            /* default return value is (void) */
            vpushi(0), vtop->type.t = VT_VOID;
            block(STMT_EXPR);
            /* If the statement expr can be entered, then we retain the current
               nocode_wanted state (from e.g. a 'return 0;' in the stmt-expr).
               If it can't be entered then the state is that from before the
               statement expression.  */
            if (saved_nocode_wanted)
              nocode_wanted = saved_nocode_wanted;
            skip(')');
        } else {
            gexpr();
            skip(')');
        }
        break;
    case '*':
        next();
        unary();
        indir();
        break;
    case '&':
        next();
        unary();
        /* functions names must be treated as function pointers,
           except for unary '&' and sizeof. Since we consider that
           functions are not lvalues, we only have to handle it
           there and in function calls. */
        /* arrays can also be used although they are not lvalues */
        if ((vtop->type.t & VT_BTYPE) != VT_FUNC &&
            !(vtop->type.t & (VT_ARRAY | VT_VLA)))
            test_lvalue();
        if (vtop->sym)
          vtop->sym->a.addrtaken = 1;
        mk_pointer(&vtop->type);
        gaddrof();
        break;
    case '!':
        next();
        unary();
        gen_test_zero(TOK_EQ);
        break;
    case '~':
        next();
        unary();
        vpushi(-1);
        gen_op('^');
        break;
    case '+':
        next();
        unary();
        if ((vtop->type.t & VT_BTYPE) == VT_PTR)
            tcc_error("pointer not accepted for unary plus");
        /* In order to force cast, we add zero, except for floating point
	   where we really need an noop (otherwise -0.0 will be transformed
	   into +0.0).  */
	if (!is_float(vtop->type.t)) {
	    vpushi(0);
	    gen_op('+');
	}
        break;
    case TOK_SIZEOF:
    case TOK_ALIGNOF1:
    case TOK_ALIGNOF2:
    case TOK_ALIGNOF3:
        t = tok;
        next();
        if (tok == '(')
            tok = TOK_SOTYPE;
        expr_type(&type, unary);
        if (t == TOK_SIZEOF) {
            vpush_type_size(&type, &align);
            gen_cast_s(VT_SIZE_T);
        } else {
            type_size(&type, &align);
            s = NULL;
            if (vtop[1].r & VT_SYM)
                s = vtop[1].sym; /* hack: accessing previous vtop */
            if (s && s->a.aligned)
                align = 1 << (s->a.aligned - 1);
            vpushs(align);
        }
        break;

    case TOK_builtin_expect:
	/* __builtin_expect is a no-op for now */
	parse_builtin_params(0, "ee");
	vpop();
        break;
    case TOK_builtin_types_compatible_p:
	parse_builtin_params(0, "tt");
	n = compare_types(&vtop[-1].type, &vtop[0].type, 1);
	vtop -= 2;
	vpushi(n);
        break;
    case TOK_builtin_choose_expr:
	{
	    int64_t c;
	    next();
	    skip('(');
	    c = expr_const64();
	    skip(',');
	    if (!c) {
		nocode_wanted++;
	    }
	    expr_eq();
	    if (!c) {
		vpop();
		nocode_wanted--;
	    }
	    skip(',');
	    if (c) {
		nocode_wanted++;
	    }
	    expr_eq();
	    if (c) {
		vpop();
		nocode_wanted--;
	    }
	    skip(')');
	}
        break;
    case TOK_builtin_constant_p:
	parse_builtin_params(1, "e");
	n = 1;
	if ((vtop->r & (VT_VALMASK | VT_LVAL)) != VT_CONST
	    || ((vtop->r & VT_SYM) && vtop->sym->a.addrtaken)
	    )
	    n = 0;
	vtop--;
	vpushi(n);
        break;
    case TOK_builtin_unreachable:
	parse_builtin_params(0, ""); /* just skip '()' */
        type.t = VT_VOID;
        vpush(&type);
        CODE_OFF();
        break;
    case TOK_builtin_frame_address:
    case TOK_builtin_return_address:
        {
            int tok1 = tok;
            int level;
            next();
            skip('(');
            level = expr_const();
            if (level < 0)
                tcc_error("%s only takes positive integers", get_tok_str(tok1, 0));
            skip(')');
            type.t = VT_VOID;
            mk_pointer(&type);
            vset(&type, VT_LOCAL, 0);       /* local frame */
            while (level--) {
#ifdef TCC_TARGET_RISCV64
                vpushi(2*PTR_SIZE);
                gen_op('-');
#endif
                mk_pointer(&vtop->type);
                indir();                    /* -> parent frame */
            }
            if (tok1 == TOK_builtin_return_address) {
                // assume return address is just above frame pointer on stack
#ifdef TCC_TARGET_ARM
                vpushi(2*PTR_SIZE);
                gen_op('+');
#elif defined TCC_TARGET_RISCV64
                vpushi(PTR_SIZE);
                gen_op('-');
#else
                vpushi(PTR_SIZE);
                gen_op('+');
#endif
                mk_pointer(&vtop->type);
                indir();
            }
        }
        break;
#ifdef TCC_TARGET_RISCV64
    case TOK_builtin_va_start:
        parse_builtin_params(0, "ee");
        r = vtop->r & VT_VALMASK;
        if (r == VT_LLOCAL)
            r = VT_LOCAL;
        if (r != VT_LOCAL)
            tcc_error("__builtin_va_start expects a local variable");
        gen_va_start();
	vstore();
        break;
#endif
#ifdef TCC_TARGET_X86_64
#ifdef TCC_TARGET_PE
    case TOK_builtin_va_start:
	parse_builtin_params(0, "ee");
        r = vtop->r & VT_VALMASK;
        if (r == VT_LLOCAL)
            r = VT_LOCAL;
        if (r != VT_LOCAL)
            tcc_error("__builtin_va_start expects a local variable");
        vtop->r = r;
	vtop->type = char_pointer_type;
	vtop->c.i += 8;
	vstore();
        break;
#else
    case TOK_builtin_va_arg_types:
	parse_builtin_params(0, "t");
	vpushi(classify_x86_64_va_arg(&vtop->type));
	vswap();
	vpop();
	break;
#endif
#endif

#ifdef TCC_TARGET_ARM64
    case TOK_builtin_va_start: {
	parse_builtin_params(0, "ee");
        //xx check types
        gen_va_start();
        vpushi(0);
        vtop->type.t = VT_VOID;
        break;
    }
    case TOK_builtin_va_arg: {
	parse_builtin_params(0, "et");
	type = vtop->type;
	vpop();
        //xx check types
        gen_va_arg(&type);
        vtop->type = type;
        break;
    }
#endif
#ifdef TCC_TARGET_ARM64
    case TOK___arm64_clear_cache: {
	parse_builtin_params(0, "ee");
        gen_clear_cache();
        vpushi(0);
        vtop->type.t = VT_VOID;
        break;
    }
#endif
#ifdef TCC_TARGET_RISCV64
    case TOK___riscv64_clear_cache: {
	parse_builtin_params(0, "ee");
	vpop();
	vpop();
        gen_clear_cache();
        vpushi(0);
        vtop->type.t = VT_VOID;
        break;
    }
#endif

    /* atomic operations */
    case TOK___atomic_store:
    case TOK___atomic_load:
    case TOK___atomic_exchange:
    case TOK___atomic_compare_exchange:
    case TOK___atomic_fetch_add:
    case TOK___atomic_fetch_sub:
    case TOK___atomic_fetch_or:
    case TOK___atomic_fetch_xor:
    case TOK___atomic_fetch_and:
    case TOK___atomic_fetch_nand:
    case TOK___atomic_add_fetch:
    case TOK___atomic_sub_fetch:
    case TOK___atomic_or_fetch:
    case TOK___atomic_xor_fetch:
    case TOK___atomic_and_fetch:
    case TOK___atomic_nand_fetch:
        parse_atomic(tok);
        break;

    /* pre operations */
    case TOK_INC:
    case TOK_DEC:
        t = tok;
        next();
        unary();
        inc(0, t);
        break;
    case '-':
        next();
        unary();
	if (is_float(vtop->type.t)) {
            gen_opif(TOK_NEG);
	} else {
            vpushi(0);
            vswap();
            gen_op('-');
        }
        break;
    case TOK_LAND:
        if (!gnu_ext)
            goto tok_identifier;
        next();
        /* allow to take the address of a label */
        if (tok < TOK_UIDENT)
            expect("label identifier");
        s = label_find(tok);
        if (!s) {
            s = label_push(&global_label_stack, tok, LABEL_FORWARD);
        } else {
            if (s->r == LABEL_DECLARED)
                s->r = LABEL_FORWARD;
        }
        if ((s->type.t & VT_BTYPE) != VT_PTR) {
            s->type.t = VT_VOID;
            mk_pointer(&s->type);
            s->type.t |= VT_STATIC;
        }
        vpushsym(&s->type, s);
        next();
        break;

    case TOK_GENERIC:
    {
	CType controlling_type;
	int has_default = 0;
	int has_match = 0;
	int learn = 0;
	TokenString *str = NULL;
	int saved_nocode_wanted = nocode_wanted;
        nocode_wanted &= ~CONST_WANTED_MASK;

        next();
	skip('(');
	expr_type(&controlling_type, expr_eq);
	convert_expression_type(&controlling_type);

        nocode_wanted = saved_nocode_wanted;

        for (;;) {
	    learn = 0;
	    skip(',');
	    if (tok == TOK_DEFAULT) {
		if (has_default)
		    tcc_error("too many 'default'");
		has_default = 1;
		if (!has_match)
		    learn = 1;
		next();
	    } else {
		int v;
		parse_btype(&type, &ad, 0);
		type_decl(&type, &ad, &v, TYPE_ABSTRACT);
		if (compare_types(&controlling_type, &type, 0)) {
		    if (has_match) {
		      tcc_error("type match twice");
		    }
		    has_match = 1;
		    learn = 1;
		}
	    }
	    skip(':');
	    if (learn) {
		if (str)
		    tok_str_free(str);
		skip_or_save_block(&str);
	    } else {
		skip_or_save_block(NULL);
	    }
	    if (tok == ')')
		break;
	}
	if (!str) {
	    char buf[60];
	    type_to_str(buf, sizeof buf, &controlling_type, NULL);
	    tcc_error("type '%s' does not match any association", buf);
	}
	begin_macro(str, 1);
	next();
	expr_eq();
	if (tok != TOK_EOF)
	    expect(",");
	end_macro();
        next();
	break;
    }
    // special qnan , snan and infinity values
    case TOK___NAN__:
        n = 0x7fc00000;
special_math_val:
	vpushi(n);
	vtop->type.t = VT_FLOAT;
        next();
        break;
    case TOK___SNAN__:
	n = 0x7f800001;
	goto special_math_val;
    case TOK___INF__:
	n = 0x7f800000;
	goto special_math_val;

#ifdef C6_EXT
    case TOK_C6_TRY:
        if (c6_gc_in_drop)
            tcc_error("a destructor cannot fail: it runs while the object "
                      "is being released, where an error has nowhere to go");
        c6_try_parse();
        break;
    case TOK_C6_AWAIT:
        c6_await_parse();
        break;
#endif

    default:
    tok_identifier:
        if (tok < TOK_UIDENT)
            tcc_error("expression expected before '%s'", get_tok_str(tok, &tokc));
        t = tok;
        next();
        s = sym_find(t);
#ifdef C6_EXT
        /* 'pkg.member', possibly multi-level: the dotted spelling is a
           package-qualified access that resolves to 'pkg__member'.  The
           package part is grown one segment at a time, so 'a.b.f' first
           tries 'a__b' and only then 'a__b__f': 'pkg.obj.field' therefore
           still reaches a package global and hands the trailing '.field'
           back to the normal postfix code.  A qualified access is only
           accepted for a package imported before this point (or for the
           package we are currently inside of); a plain visible identifier
           keeps normal C member-access semantics. */
        if (!s && tcc_state->c6_ext && tok == '.') {
            char pfx[160], pkgdisp[200], disp[200];
            int mt, n, seg;

            n = snprintf(pfx, sizeof pfx, "%s", get_tok_str(t, NULL));
            if (n >= (int)sizeof pfx)
                tcc_error("package name too long");
            snprintf(pkgdisp, sizeof pkgdisp, "%s", pfx);
            snprintf(disp, sizeof disp, "%s", pfx);
            for (;;) {
                next();                 /* skip '.' */
                if (tok < TOK_UIDENT)
                    tcc_error("identifier expected after '.'");
                seg = tok;
                n = strlen(disp);
                snprintf(disp + n, sizeof disp - n, ".%s",
                         get_tok_str(seg, NULL));
                if (n + 1 >= (int)sizeof disp)
                    tcc_error("package name too long");
                mt = c6_mangle_tok(pfx, seg);
                if (!(s = sym_find_raw(mt))) {
                    /* no such member: either the segment was part of the
                       package name after all, or the member is missing */
                    next();
                    if (tok == '.') {
                        /* one more segment follows: grow the package name */
                        n = strlen(pfx);
                        if (n + 2 + strlen(get_tok_str(seg, NULL))
                            >= (int)sizeof pfx)
                            tcc_error("package name too long");
                        sprintf(pfx + n, "__%s", get_tok_str(seg, NULL));
                        n = strlen(pkgdisp);
                        snprintf(pkgdisp + n, sizeof pkgdisp - n, ".%s",
                                 get_tok_str(seg, NULL));
                        if (n + 1 >= (int)sizeof pkgdisp)
                            tcc_error("package name too long");
                        continue;
                    }
                    /* nothing left to name a member: report it */
                    if (!c6_pkg_imported(pfx))
                        tcc_error("'%s' is not an imported package "
                                  "(use 'import %s;')", pkgdisp, pkgdisp);
                    dot_disp = tok_alloc_const(disp);
                    t = mt;
                    break;
                }
                if (!c6_pkg_imported(pfx))
                    tcc_error("'%s' is not an imported package "
                              "(use 'import %s;')", pkgdisp, pkgdisp);
                dot_disp = tok_alloc_const(disp);
                t = mt;
                next();                 /* skip the member name */
                break;
            }
        }
#endif
        if (!s || IS_ASM_SYM(s)) {
            const char *name = get_tok_str(t, NULL);
#ifdef C6_EXT
            /* report the spelling used in the source, not the mangled name */
            if (dot_disp)
                name = get_tok_str(dot_disp, NULL);
#endif
            if (tok != '(')
                tcc_error("'%s' undeclared", name);
            /* for simple function calls, we tolerate undeclared
               external reference to int() function */
            if (!func_old)
            tcc_warning_c(warn_implicit_function_declaration)(
                "implicit declaration of function '%s'", name);
            s = external_global_sym(t, &func_old_type);
        }

        r = s->r;
        /* A symbol that has a register is a local register variable,
           which starts out as VT_LOCAL value.  */
        if ((r & VT_VALMASK) < VT_CONST)
            r = (r & ~VT_VALMASK) | VT_LOCAL;

        vset(&s->type, r, s->c);
        /* Point to s as backpointer (even without r&VT_SYM).
	   Will be used by at least the x86 inline asm parser for
	   regvars.  */
	vtop->sym = s;

        if (r & VT_SYM) {
            vtop->c.i = 0;
#ifdef TCC_TARGET_PE
            if (s->a.dllimport) {
                mk_pointer(&vtop->type);
                vtop->r |= VT_LVAL;
                indir();
            }
#endif
        } else if (r == VT_CONST && IS_ENUM_VAL(s->type.t)) {
            vtop->c.i = s->enum_val;
        }
#ifdef C6_EXT
        /* a call of an 'async' function creates a task; the handle replaces
           the call expression, so the postfix operators still apply */
        if (tcc_state->c6_ext && (s->type.t & VT_BTYPE) == VT_FUNC
            && s->type.ref->f.func_async) {
            if (tok != '(')
                tcc_error("an 'async' function can only be called");
            vpop();
            c6_async_call(s);
            break;
        }
        /* 'c6_io(fd, ev)' is the leaf suspension primitive */
        if (tcc_state->c6_ext && (s->type.t & VT_BTYPE) == VT_FUNC
            && s->v == tok_alloc_const("c6_io")) {
            if (!c6_await_pending)
                tcc_error("'c6_io' can only be used as "
                          "'await c6_io(fd, ev)'");
            c6_await_pending = 0;
            vpop();
            c6_async_await_io(s);
            break;
        }
#endif
        break;
    }
    
    /* post operations */
    while (1) {
        if (tok == TOK_INC || tok == TOK_DEC) {
#ifdef C6_EXT
            c6_fallible_use();
#endif
            inc(1, tok);
            next();
        } else if (tok == '.' || tok == TOK_ARROW) {
            int qualifiers, cumofs;
#ifdef C6_EXT
            int arrow;
            c6_fallible_use();  /* 'f().x' would drop the error */
            arrow = tok == TOK_ARROW;
            next();                     /* skip '.' / '->' */
            if (arrow && tok >= TOK_UIDENT) {
                /* 'p->f(...)' is a method call when F is a registered method
                   of the type P points at; a real field of that name keeps
                   plain C semantics */
                C6Method *m = c6_method_lookup(&vtop->type, tok);
                if (m && !c6_has_field(&vtop->type, tok)) {
                    int mname = tok;
                    next();
                    if (tok != '(')
                        tcc_error("method '%s' must be called",
                                  get_tok_str(mname, NULL));
                    c6_method_call_prep(m);
                    continue;           /* let the '(' case do the call */
                }
            }
            if (arrow)
                indir();
#else
            /* field */
            if (tok == TOK_ARROW)
                indir();
            next();
#endif
            qualifiers = vtop->type.t & VT_QUAL;
            test_lvalue();
#ifdef C6_EXT
            /* an interface value dispatches through its vtable */
            if (tok >= TOK_UIDENT) {
                C6Iface *ifc = c6_iface_of_type(&vtop->type);
                if (ifc) {
                    int mname = tok;
                    next();
                    if (tok == '(' && c6_iface_has_method(ifc, mname)) {
                        c6_iface_call_prep(ifc, mname);
                        continue;   /* let the '(' case handle the call */
                    }
                    unget_tok(mname);   /* plain field access */
                }
            }
#endif
            /* expect pointer on structure */
	    s = find_field(&vtop->type, tok, &cumofs);
            /* add field offset to pointer */
            gaddrof();
            vtop->type = char_pointer_type; /* change type to 'char *' */
            vpushi(cumofs);
            gen_op('+');
            /* change type to field type, and set to lvalue */
            vtop->type = s->type;
            if (qualifiers)
                parse_btype_qualify(&vtop->type, qualifiers);
            /* an array is never an lvalue */
            if (!(vtop->type.t & VT_ARRAY)) {
                vtop->r |= VT_LVAL;
#ifdef CONFIG_TCC_BCHECK
                /* if bound checking, the referenced pointer must be checked */
                if (tcc_state->do_bounds_check)
                    vtop->r |= VT_MUSTBOUND;
#endif
            }
            next();
        } else if (tok == '[') {
#ifdef C6_EXT
            /* 't[0]' on a tuple, or a slice subscript and a range: those are
               bounds checked and may produce a value, so they need their own
               parser; an array or pointer subscript with no ':' keeps the
               original code */
            if (tcc_state->c6_ext) {
                if (!c6_tuple_subscript())
                    c6_slice_subscript();
            } else
#endif
            {
                next();
                gexpr();
                gen_op('+');
                indir();
                skip(']');
            }
#ifdef C6_EXT
            c6_value_fallible = 0;
#endif
        } else if (tok == '(') {
            SValue ret;
            Sym *sa;
            int nb_args, ret_nregs, ret_align, regsize, variadic;
            TokenString *p, *p2;

            /* function call  */
            if ((vtop->type.t & VT_BTYPE) != VT_FUNC) {
                /* pointer test (no array accepted) */
                if ((vtop->type.t & (VT_BTYPE | VT_ARRAY)) == VT_PTR) {
                    vtop->type = *pointed_type(&vtop->type);
                    if ((vtop->type.t & VT_BTYPE) != VT_FUNC)
                        goto error_func;
                } else {
                error_func:
                    expect("function pointer");
                }
            } else {
                vtop->r &= ~VT_LVAL; /* no lvalue */
            }
            /* get return type */
            s = vtop->type.ref;
            next();
#ifdef C6_EXT
            if (c6_gc_drop_callee(vtop->sym))
                tcc_error("a destructor cannot be called: it runs when the "
                          "last reference is released (use 'p = NULL')");
            if (s->f.func_fallible) {
                /* a 'T!error' call starts from a clean slot: only the error
                   raised by this very call may be observed afterwards, so a
                   stale value can never trigger a spurious 'catch' */
                save_regs(0);
                vpush_helper_func(tok_alloc_const("__tcc_err_clear"));
                gfunc_call(0);
            }
#endif
            sa = s->next; /* first parameter */
            nb_args = regsize = 0;
            ret.r2 = VT_CONST;
            /* compute first implicit argument if a structure is returned */
            if ((s->type.t & VT_BTYPE) == VT_STRUCT) {
                variadic = (s->f.func_type == FUNC_ELLIPSIS);
                ret_nregs = gfunc_sret(&s->type, variadic, &ret.type,
                                       &ret_align, &regsize);
                if (ret_nregs <= 0) {
                    /* get some space for the returned structure */
                    size = type_size(&s->type, &align);
#ifdef TCC_TARGET_ARM64
                /* On arm64, a small struct is return in registers.
                   It is much easier to write it to memory if we know
                   that we are allowed to write some extra bytes, so
                   round the allocated space up to a power of 2: */
                if (size < 16)
                    while (size & (size - 1))
                        size = (size | (size - 1)) + 1;
#endif
                    loc = (loc - size) & -align;
                    ret.type = s->type;
                    ret.r = VT_LOCAL | VT_LVAL;
                    /* pass it as 'int' to avoid structure arg passing
                       problems */
                    vseti(VT_LOCAL, loc);
#ifdef CONFIG_TCC_BCHECK
                    if (tcc_state->do_bounds_check)
                        --loc;
#endif
                    ret.c = vtop->c;
                    if (ret_nregs < 0)
                      vtop--;
                    else
                      nb_args++;
                }
            } else {
                ret_nregs = 1;
                ret.type = s->type;
            }
#ifdef C6_EXT
            if (c6_self_pending) {
                /* method / interface method call: the hidden 'self' pointer
                   is the first explicit argument, so it must come after a
                   possible structure-return buffer and before the rest */
                c6_self_pending = 0;
                vset(&c6_self_type, VT_LOCAL | VT_LVAL, c6_self_loc);
                gfunc_param_typed(s, sa);
                nb_args++;
                if (sa)
                    sa = sa->next;
            }
#endif

            if (ret_nregs > 0) {
                /* return in register */
                ret.c.i = 0;
                PUT_R_RET(&ret, ret.type.t);
            }

            p = NULL;
#ifdef C6_EXT
            c6_gc_arg_mark();   /* fresh arguments are released after the call */
#endif
            if (tok != ')') {
                r = tcc_state->reverse_funcargs;
                for(;;) {
                    if (r) {
                        skip_or_save_block(&p2);
                        p2->prev = p, p = p2;
                    } else {
                        expr_eq();
#ifdef C6_EXT
                        c6_gc_arg_temp();
#endif
                        gfunc_param_typed(s, sa);
                    }
                    nb_args++;
                    if (sa)
                        sa = sa->next;
                    if (tok == ')')
                        break;
                    skip(',');
                }
            }
            if (sa)
                tcc_error("too few arguments to function");

            if (p) { /* with reverse_funcargs */
                for (n = 0; p; p = p2, ++n) {
                    p2 = p, sa = s;
                    do {
                        sa = sa->next, p2 = p2->prev;
                    } while (p2 && sa);
                    p2 = p->prev;
                    begin_macro(p, 1), next();
                    expr_eq();
#ifdef C6_EXT
                    c6_gc_arg_temp();
#endif
                    gfunc_param_typed(s, sa);
                    end_macro();
                }
                vrev(n);
            }

            next();
            vcheck_cmp(); /* the generators don't like VT_CMP on vtop */
            gfunc_call(nb_args);

            if (ret_nregs < 0) {
                vsetc(&ret.type, ret.r, &ret.c);
#ifdef TCC_TARGET_RISCV64
                arch_transfer_ret_regs(1);
#endif
            } else {
                /* return value */
                n = ret_nregs;
                while (n > 1) {
                    int rc = reg_classes[ret.r] & ~(RC_INT | RC_FLOAT);
                    /* We assume that when a structure is returned in multiple
                       registers, their classes are consecutive values of the
                       suite s(n) = 2^n */
                    rc <<= --n;
                    for (r = 0; r < NB_REGS; ++r)
                        if (reg_classes[r] & rc)
                            break;
                    vsetc(&ret.type, r, &ret.c);
                }
                vsetc(&ret.type, ret.r, &ret.c);
                vtop->r2 = ret.r2;

                /* handle packed struct return */
                if (((s->type.t & VT_BTYPE) == VT_STRUCT) && ret_nregs) {
                    int addr, offset;

                    size = type_size(&s->type, &align);
                    /* We're writing whole regs often, make sure there's enough
                       space.  Assume register size is power of 2.  */
                    size = (size + regsize - 1) & -regsize;
                    if (ret_align > align)
                        align = ret_align;
                    loc = (loc - size) & -align;
                    addr = loc;
                    offset = 0;
                    for (;;) {
                        vset(&ret.type, VT_LOCAL | VT_LVAL, addr + offset);
                        vswap();
                        vstore();
                        vtop--;
                        if (--ret_nregs == 0)
                          break;
                        offset += regsize;
                    }
                    vset(&s->type, VT_LOCAL | VT_LVAL, addr);
                }

                /* Promote char/short return values. This is matters only
                   for calling function that were not compiled by TCC and
                   only on some architectures.  For those where it doesn't
                   matter we expect things to be already promoted to int,
                   but not larger.  */
                t = s->type.t & VT_BTYPE;
                if (t == VT_BYTE || t == VT_SHORT || t == VT_BOOL) {
#ifdef PROMOTE_RET
                    vtop->r |= BFVAL(VT_MUSTCAST, 1);
#else
                    vtop->type.t = VT_INT;
#endif
                }
            }
#ifdef C6_EXT
            /* remember whether this value is the result of a call to a
               function declared with the 'T!error' suffix ('try'/'catch'
               consume this flag).  Set only here, at the end of the call
               handling: spilling a struct returned in registers stores
               values internally and must not trip the check. */
            c6_value_fallible = s->f.func_fallible;
            {
                int c6_has_val = (ret.type.t & VT_BTYPE) != VT_VOID;
                /* a call of a function that returns a managed reference
                   hands the caller one reference ('return p' counted it) */
                if (c6_has_val && c6_is_managed(&vtop->type))
                    vtop->gc_state = C6_GC_FRESH;
                c6_gc_arg_release(c6_has_val);
            }
#endif
            if (s->f.func_noreturn) {
                if (debug_modes)
	            tcc_tcov_block_end(tcc_state, -1);
                CODE_OFF();
	    }
        } else {
            break;
        }
    }
}

#ifndef precedence_parser /* original top-down parser */

static void expr_prod(void)
{
    int t;

    unary();
    while ((t = tok) == '*' || t == '/' || t == '%') {
        next();
        unary();
        gen_op(t);
    }
}

static void expr_sum(void)
{
    int t;

    expr_prod();
    while ((t = tok) == '+' || t == '-') {
        next();
        expr_prod();
        gen_op(t);
    }
}

static void expr_shift(void)
{
    int t;

    expr_sum();
    while ((t = tok) == TOK_SHL || t == TOK_SAR) {
        next();
        expr_sum();
        gen_op(t);
    }
}

static void expr_cmp(void)
{
    int t;

    expr_shift();
    while (((t = tok) >= TOK_ULE && t <= TOK_GT) ||
           t == TOK_ULT || t == TOK_UGE) {
        next();
        expr_shift();
        gen_op(t);
    }
}

static void expr_cmpeq(void)
{
    int t;

    expr_cmp();
    while ((t = tok) == TOK_EQ || t == TOK_NE) {
        next();
        expr_cmp();
        gen_op(t);
    }
}

static void expr_and(void)
{
    expr_cmpeq();
    while (tok == '&') {
        next();
        expr_cmpeq();
        gen_op('&');
    }
}

static void expr_xor(void)
{
    expr_and();
    while (tok == '^') {
        next();
        expr_and();
        gen_op('^');
    }
}

static void expr_or(void)
{
    expr_xor();
    while (tok == '|') {
        next();
        expr_xor();
        gen_op('|');
    }
}

static void expr_landor(int op);

static void expr_land(void)
{
    expr_or();
    if (tok == TOK_LAND)
        expr_landor(tok);
}

static void expr_lor(void)
{
    expr_land();
    if (tok == TOK_LOR)
        expr_landor(tok);
}

# define expr_landor_next(op) op == TOK_LAND ? expr_or() : expr_land()
#else /* defined precedence_parser */
# define expr_landor_next(op) unary(), expr_infix(precedence(op) + 1)
# define expr_lor() unary(), expr_infix(1)

static int precedence(int tok)
{
    switch (tok) {
        case TOK_LOR: return 1;
        case TOK_LAND: return 2;
	case '|': return 3;
	case '^': return 4;
	case '&': return 5;
	case TOK_EQ: case TOK_NE: return 6;
 relat: case TOK_ULT: case TOK_UGE: return 7;
	case TOK_SHL: case TOK_SAR: return 8;
	case '+': case '-': return 9;
	case '*': case '/': case '%': return 10;
	default:
	    if (tok >= TOK_ULE && tok <= TOK_GT)
	        goto relat;
	    return 0;
    }
}
static unsigned char prec[256];
static void init_prec(void)
{
    int i;
    for (i = 0; i < 256; i++)
	prec[i] = precedence(i);
}
#define precedence(i) ((unsigned)i < 256 ? prec[i] : 0)

static void expr_landor(int op);

static void expr_infix(int p)
{
    int t = tok, p2;
    while ((p2 = precedence(t)) >= p) {
        if (t == TOK_LOR || t == TOK_LAND) {
            expr_landor(t);
        } else {
            next();
            unary();
            if (precedence(tok) > p2)
              expr_infix(p2 + 1);
            gen_op(t);
        }
        t = tok;
    }
}
#endif

/* Assuming vtop is a value used in a conditional context
   (i.e. compared with zero) return 0 if it's false, 1 if
   true and -1 if it can't be statically determined.  */
static int condition_3way(void)
{
    int c = -1;
    if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST &&
	(!(vtop->r & VT_SYM) || !vtop->sym->a.weak)) {
	vdup();
        gen_cast_s(VT_BOOL);
	c = vtop->c.i;
	vpop();
    }
    return c;
}

static void expr_landor(int op)
{
    int t = 0, cc = 1, f = 0, i = op == TOK_LAND, c;
    for(;;) {
        c = f ? i : condition_3way();
        if (c < 0)
            save_regs(1), cc = 0;
        else if (c != i)
            nocode_wanted++, f = 1;
        if (tok != op)
            break;
        if (c < 0)
            t = gvtst(i, t);
        else
            vpop();
        next();
        expr_landor_next(op);
    }
    if (cc || f) {
        vpop();
        vpushi(i ^ f);
        gsym(t);
        nocode_wanted -= f;
    } else {
        gvtst_set(i, t);
    }
}

static int is_cond_bool(SValue *sv)
{
    if ((sv->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST
        && (sv->type.t & VT_BTYPE) == VT_INT)
        return (unsigned)sv->c.i < 2;
    if (sv->r == VT_CMP)
        return 1;
    return 0;
}

/* c6: 'f() catch e { ... }' binds tighter than '?:' and '=' but looser
   than '||', so 'x = f() catch e {...}' initializes 'x' with the value of
   the whole catch expression (only on the success path). */
static void expr_catch(void)
{
    expr_lor();
#ifdef C6_EXT
    if (tok == TOK_C6_CATCH && tcc_state->c6_ext) {
        if (c6_gc_in_drop)
            tcc_error("a destructor cannot fail: it runs while the object "
                      "is being released, where an error has nowhere to go");
        c6_catch_parse();
    }
#endif
}

static void expr_cond(void)
{
    int tt, u, r1, r2, rc, t1, t2, islv, c, g;
    SValue sv;
    CType type;

    expr_catch();
    if (tok == '?') {
        next();
	c = condition_3way();
        g = (tok == ':' && gnu_ext);
        tt = 0;
        if (!g) {
            if (c < 0) {
                save_regs(1);
                tt = gvtst(1, 0);
            } else {
                vpop();
            }
        } else if (c < 0) {
            /* needed to avoid having different registers saved in
               each branch */
            save_regs(1);
            gv_dup();
            tt = gvtst(0, 0);
        }

        if (c == 0)
          nocode_wanted++;
        if (!g)
          gexpr();

        if ((vtop->type.t & VT_BTYPE) == VT_FUNC)
          mk_pointer(&vtop->type);
        sv = *vtop; /* save value to handle it later */
        vtop--; /* no vpop so that FP stack is not flushed */

        if (g) {
            u = tt;
        } else if (c < 0) {
            u = gjmp(0);
            gsym(tt);
        } else
          u = 0;

        if (c == 0)
          nocode_wanted--;
        if (c == 1)
          nocode_wanted++;
        skip(':');
        expr_cond();

        if ((vtop->type.t & VT_BTYPE) == VT_FUNC)
          mk_pointer(&vtop->type);

        /* cast operands to correct type according to ISOC rules */
        if (!combine_types(&type, &sv, vtop, '?'))
          type_incompatibility_error(&sv.type, &vtop->type,
            "type mismatch in conditional expression (have '%s' and '%s')");

        if (c < 0 && is_cond_bool(vtop) && is_cond_bool(&sv)) {
            /* optimize "if (f ? a > b : c || d) ..." for example, where normally
               "a < b" and "c || d" would be forced to "(int)0/1" first, whereas
               this code jumps directly to the if's then/else branches. */
            t1 = gvtst(0, 0);
            t2 = gjmp(0);
            gsym(u);
            vpushv(&sv);
            /* combine jump targets of 2nd op with VT_CMP of 1st op */
            gvtst_set(0, t1);
            gvtst_set(1, t2);
            gen_cast(&type);
            //  tcc_warning("two conditions expr_cond");
            return;
        }

        /* keep structs lvalue by transforming `(expr ? a : b)` to `*(expr ? &a : &b)` so
           that `(expr ? a : b).mem` does not error  with "lvalue expected" */
        islv = VT_STRUCT == (type.t & VT_BTYPE);

        /* now we convert second operand */
        if (c != 1) {
            gen_cast(&type);
            if (islv) {
                mk_pointer(&vtop->type);
                gaddrof();
            }
        }

        rc = RC_TYPE(type.t);
        /* for long longs, we use fixed registers to avoid having
           to handle a complicated move */
        if (USING_TWO_WORDS(type.t))
          rc = RC_RET(type.t);

        tt = r2 = 0;
        if (c < 0) {
            if (type.t != VT_VOID)
                r2 = gv(rc);
            tt = gjmp(0);
        }
        gsym(u);
        if (c == 1)
          nocode_wanted--;

        /* this is horrible, but we must also convert first
           operand */
        if (c != 0) {
            *vtop = sv;
            gen_cast(&type);
            if (islv) {
                mk_pointer(&vtop->type);
                gaddrof();
            }
        }

        if (c < 0) {
            if (type.t != VT_VOID) {
                r1 = gv(rc);
                move_reg(r2, r1, islv ? VT_PTR : type.t);
                vtop->r = r2;
            }
            gsym(tt);
        }

        if (islv)
          indir();
    }
}

static void expr_eq(void)
{
    int t;
    
    expr_cond();
    if ((t = tok) == '=' || TOK_ASSIGN(t)) {
        test_lvalue();
        next();
        if (t == '=') {
            expr_eq();
        } else {
#ifdef C6_EXT
            if (c6_is_managed(&vtop->type))
                tcc_error("'%s' is not supported for a managed reference: "
                          "it cannot do pointer arithmetic",
                          get_tok_str(t, NULL));
#endif
            vdup();
            expr_eq();
            gen_op(TOK_ASSIGN_OP(t));
        }
        vstore();
    }
}

ST_FUNC void gexpr(void)
{
    expr_eq();
    if (tok == ',') {
        do {
            vpop();
            next();
            expr_eq();
        } while (tok == ',');

        /* convert array & function to pointer */
        convert_expression_type(&vtop->type);

        /* make builtin_constant_p((1,2)) return 0 (like on gcc) */
        if ((vtop->r & VT_VALMASK) == VT_CONST && nocode_wanted && !CONST_WANTED)
            if (vtop->type.t != VT_VOID && (vtop->type.t & VT_BTYPE) != VT_STRUCT)
                gv(RC_TYPE(vtop->type.t));
    }
}

/* parse a constant expression and return value in vtop.  */
static void expr_const1(void)
{
    nocode_wanted += CONST_WANTED_BIT;
    expr_cond();
    nocode_wanted -= CONST_WANTED_BIT;
}

/* parse an integer constant and return its value. */
static inline int64_t expr_const64(void)
{
    int64_t c;
    expr_const1();
    if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM | VT_NONCONST)) != VT_CONST)
        expect("constant expression");
    c = vtop->c.i;
    vpop();
    return c;
}

/* parse an integer constant and return its value.
   Complain if it doesn't fit 32bit (signed or unsigned).  */
ST_FUNC int expr_const(void)
{
    int c;
    int64_t wc = expr_const64();
    c = wc;
    if (c != wc && (unsigned)c != wc)
        tcc_error("constant exceeds 32 bit");
    return c;
}

/* ------------------------------------------------------------------------- */
/* return from function */

#ifndef TCC_TARGET_ARM64
static void gfunc_return(CType *func_type)
{
    if ((func_type->t & VT_BTYPE) == VT_STRUCT) {
        CType type, ret_type;
        int ret_align, ret_nregs, regsize;
        ret_nregs = gfunc_sret(func_type, func_var, &ret_type,
                               &ret_align, &regsize);
        if (ret_nregs < 0) {
#ifdef TCC_TARGET_RISCV64
            arch_transfer_ret_regs(0);
#endif
        } else if (0 == ret_nregs) {
            /* if returning structure, must copy it to implicit
               first pointer arg location */
            type = *func_type;
            mk_pointer(&type);
            vset(&type, VT_LOCAL | VT_LVAL, func_vc);
            indir();
            vswap();
            /* copy structure value to pointer */
            vstore();
        } else {
            /* returning structure packed into registers */
            int size, addr, align, rc, n;
            size = type_size(func_type,&align);
            if (ret_nregs * regsize > size ||
		((align & (ret_align - 1))
                 && ((vtop->r & VT_VALMASK) < VT_CONST /* pointer to struct */
                     || (vtop->c.i & (ret_align - 1))
                     ))) {
		if (ret_nregs * regsize > size)
		    size = ret_nregs * regsize;
		if (ret_align > align)
		    align = ret_align;
                loc = (loc - size) & -align;
                addr = loc;
                type = *func_type;
                vset(&type, VT_LOCAL | VT_LVAL, addr);
                vswap();
                vstore();
                vpop();
                vset(&ret_type, VT_LOCAL | VT_LVAL, addr);
            }
            vtop->type = ret_type;
            rc = RC_RET(ret_type.t);
            //printf("struct return: n:%d t:%02x rc:%02x\n", ret_nregs, ret_type.t, rc);
            for (n = ret_nregs; --n > 0;) {
                vdup();
                gv(rc);
                vswap();
                incr_offset(regsize);
                /* We assume that when a structure is returned in multiple
                   registers, their classes are consecutive values of the
                   suite s(n) = 2^n */
                rc <<= 1;
            }
            gv(rc);
            vtop -= ret_nregs - 1;
        }
    } else {
        gv(RC_RET(func_type->t));
    }
    vtop--; /* NOT vpop() because on x86 it would flush the fp stack */
}
#endif

static void check_func_return(void)
{
    if ((func_vt.t & VT_BTYPE) == VT_VOID)
        return;
    if ((!strcmp(funcname, "main") || func_old
#ifdef C6_EXT
         || c6_implicit_ret0
#endif
        ) && (func_vt.t & VT_BTYPE) == VT_INT) {
        /* main returns 0 by default */
        vpushi(0);
        gfunc_return(&func_vt);
    } else {
        tcc_warning("function might return no value: '%s'", funcname);
    }
}

/* ------------------------------------------------------------------------- */
/* switch/case */

static int case_cmp(uint64_t a, uint64_t b)
{
    if (cur_switch->sv.type.t & VT_UNSIGNED)
        return a < b ? -1 : a > b;
    else
        return (int64_t)a < (int64_t)b ? -1 : (int64_t)a > (int64_t)b;
}

static int case_cmp_qs(const void *pa, const void *pb)
{
    return case_cmp((*(struct case_t**)pa)->v1, (*(struct case_t**)pb)->v1);
}

static void case_sort(struct switch_t *sw)
{
    struct case_t **p;
    if (sw->n < 2)
        return;
    qsort(sw->p, sw->n, sizeof *sw->p, case_cmp_qs);
    p = sw->p;
    while (p < sw->p + sw->n - 1) {
        if (case_cmp(p[0]->v2, p[1]->v1) >= 0) {
            int l1 = p[0]->line, l2 = p[1]->line;
            /* using special format "%i:..." to show specific line */
            tcc_error("%i:duplicate case value", l1 > l2 ? l1 : l2);
        } else if (p[0]->v2 + 1 == p[1]->v1 && p[0]->ind == p[1]->ind) {
            /* treat "case 1: case 2: case 3:" like "case 1 ... 3: */
            p[1]->v1 = p[0]->v1;
            tcc_free(p[0]);
            memmove(p, p + 1, (--sw->n - (p - sw->p)) * sizeof *p);
        } else
            ++p;
    }
}

static int gcase(struct case_t **base, int len, int dsym)
{
    struct case_t *p;
    int t, l2, e;

    t = vtop->type.t & VT_BTYPE;
    if (t != VT_LLONG)
        t = VT_INT;
    while (len) {
        /* binary search while len > 8, else linear */
        l2 = len > 8 ? len/2 : 0;
        p = base[l2];
        vdup(), vpush64(t, p->v2);
        if (l2 == 0 && p->v1 == p->v2) {
            gen_op(TOK_EQ); /* jmp to case when equal */
            gsym_addr(gvtst(0, 0), p->ind);
        } else {
            /* case v1 ... v2 */
            gen_op(TOK_GT); /* jmp over when > V2 */
            if (len == 1) /* last case test jumps to default when false */
                dsym = gvtst(0, dsym), e = 0;
            else
                e = gvtst(0, 0);
            vdup(), vpush64(t, p->v1);
            gen_op(TOK_GE); /* jmp to case when >= V1 */
            gsym_addr(gvtst(0, 0), p->ind);
            dsym = gcase(base, l2, dsym);
            gsym(e);
        }
        ++l2, base += l2, len -= l2;
    }
    /* jump automagically will suppress more jumps */
    return gjmp(dsym);
}

static void end_switch(void)
{
    struct switch_t *sw = cur_switch;
    dynarray_reset(&sw->p, &sw->n);
    cur_switch = sw->prev;
    tcc_free(sw);
}

/* ------------------------------------------------------------------------- */
/* __attribute__((cleanup(fn))) */

/* protect symbol lvalues from further modification  */
static void save_lvalues(void)
{
    SValue *sv = vtop;
    while (sv >= vstack) {
        if (sv->sym && (sv->r & VT_LVAL)) {
            int align, size = type_size(&sv->type, &align);
            int r2, l = get_temp_local_var(size, align, &r2);
            vset(&sv->type, VT_LOCAL | VT_LVAL, l), vtop->r2 = r2;
            vpushv(sv), *sv = vtop[-1], vstore(), --vtop;
        }
        --sv;
    }
}

static void try_call_scope_cleanup(Sym *stop)
{
    Sym *cls = cur_scope->cl.s;
    for (; cls != stop; cls = cls->next) {
	Sym *fs = cls->cleanup_func;
	Sym *vs = cls->cleanup_sym;
	save_lvalues();
	vpushsym(&fs->type, fs);
	vset(&vs->type, vs->r, vs->c);
	vtop->sym = vs;
        mk_pointer(&vtop->type);
	gaddrof();
	gfunc_call(1);
    }
}


static void try_call_cleanup_goto(Sym *cleanupstate)
{
    Sym *oc, *cc;
    int ocd, ccd;

    if (!cur_scope->cl.s)
	return;

    /* search NCA of both cleanup chains given parents and initial depth */
    ocd = cleanupstate ? cleanupstate->v & ~SYM_FIELD : 0;
    for (ccd = cur_scope->cl.n, oc = cleanupstate; ocd > ccd; --ocd, oc = oc->next)
      ;
    for (cc = cur_scope->cl.s; ccd > ocd; --ccd, cc = cc->next)
      ;
    for (; cc != oc; cc = cc->next, oc = oc->next, --ccd)
      ;

    try_call_scope_cleanup(cc);
}

/* call 'func' for each __attribute__((cleanup(func))) */
static void block_cleanup(struct scope *o)
{
    int jmp = 0;
    Sym *g, **pg;
    for (pg = &pending_gotos; (g = *pg) && g->c > o->cl.n;) {
        if (g->cleanup_label->r & LABEL_FORWARD) {
            Sym *pcl = g->next;
            if (!jmp)
                jmp = gjmp(0);
            gsym(pcl->jnext);
            try_call_scope_cleanup(o->cl.s);
            pcl->jnext = gjmp(0);
            if (!o->cl.n)
                goto remove_pending;
            g->c = o->cl.n;
            pg = &g->prev;
        } else {
    remove_pending:
            *pg = g->prev;
            sym_free(g);
        }
    }
    gsym(jmp);
    try_call_scope_cleanup(o->cl.s);
}

#ifdef C6_EXT
/* ------------------------------------------------------------------------- */
/* c6 extensions: defer / errdefer code generation                      */

/* value returned by a function that raised an error with 'err(...)' */
#define TCC_ERR_SENTINEL (-1)

/* ------------------------------------------------------------------------- */
/* c6 extensions: 'package NAME { ... }' and 'import NAME;'             */
/*                                                                          */
/* A package is a name-mangling sugar: global symbols declared inside are   */
/* registered as NAME__symbol.  'import NAME;' makes unqualified lookups    */
/* fall back to NAME__symbol.  Macro and tag namespaces are not affected.   */

static int c6_mangle_tok(const char *pkg, int v)
{
    char buf[512];
    const char *name = get_tok_str(v, NULL);
    if (strlen(pkg) + strlen(name) + 3 > sizeof(buf))
        tcc_error("package symbol name too long");
    sprintf(buf, "%s__%s", pkg, name);
    return tok_alloc_const(buf);
}

/* Parse 'ident ( "." ident )*' and return the package prefix: the dotted
   spelling is purely a name separator, so 'acme.util' becomes
   "acme__util".  Nothing here looks at directories or files. */
static char *c6_pkg_name(int *ptok)
{
    char buf[128];
    int n;

    if (*ptok < TOK_UIDENT)
        expect("package name");
    n = snprintf(buf, sizeof buf, "%s", get_tok_str(*ptok, NULL));
    if (n >= (int)sizeof buf)
        tcc_error("package name too long");
    next();
    while (*ptok == '.') {
        next();
        if (*ptok < TOK_UIDENT)
            tcc_error("package name expected after '.'");
        n += snprintf(buf + n, sizeof buf - n, "__%s",
                      get_tok_str(*ptok, NULL));
        if (n >= (int)sizeof buf)
            tcc_error("package name too long");
        next();
    }
    return tcc_strdup(buf);
}

/* ------------------------------------------------------------------ */
/* modules: 'module NAME;' and 'import NAME;'                          */
/*                                                                     */
/* A module is a translation unit whose file scope names are registered */
/* under 'NAME__'.  The source keeps writing the short name; the        */
/* compiler prefixes declarations, and lookups try the module prefix    */
/* first, then a plain C name.  The emitted symbols are ordinary C      */
/* identifiers, so plain C code can call a module without knowing that  */
/* modules exist.                                                       */

/* prefix a file scope name with the current module, if any.  A name that
   already carries the prefix is left alone (a shared header may spell the
   C name explicitly), and compiler internal names are never touched. */
static int c6_mod_name(int v)
{
    char buf[512];
    const char *name;
    size_t plen;

    if (!tcc_state || !tcc_state->c6_ext || !file || !file->c6_module)
        return v;
    if (v < TOK_UIDENT)
        return v;
    name = get_tok_str(v, NULL);
    plen = strlen(file->c6_module);
    if (!strncmp(name, file->c6_module, plen))
        return v;
    if (name[0] == '_' && name[1] == '_')
        return v;
    if (plen + strlen(name) + 1 > sizeof buf)
        tcc_error("module symbol name too long");
    sprintf(buf, "%s%s", file->c6_module, name);
    return tok_alloc_const(buf);
}

/* 'module NAME;' at file scope.  The dotted spelling works like packages:
   'module geo.circle;' prefixes names with 'geo__circle__'. */
static void c6_module_parse(void)
{
    char *name;

    next();  /* skip 'module' */
    name = c6_pkg_name(&tok);
    skip(';');
    if (file->c6_decl_seen)
        tcc_error("'module' must be the first declaration in the file");
    if (file->c6_module)
        tcc_error("module '%s' already declared in this file", name);
    if (c6_expected_module && strcmp(c6_expected_module, name))
        tcc_error("module file declares '%s', expected '%s'",
                  name, c6_expected_module);
    file->c6_module = tcc_malloc(strlen(name) + 3);
    sprintf(file->c6_module, "%s__", name);
    tcc_free(name);
}

/* 'geo__circle' -> 'geo/circle' (the dotted file spelling) */
static void c6_module_dotted(const char *name, char *out, int outsz)
{
    int n = 0;

    while (*name && n < outsz - 1) {
        if (name[0] == '_' && name[1] == '_') {
            out[n++] = '/';
            name += 2;
        } else {
            out[n++] = *name++;
        }
    }
    out[n] = 0;
}

static int c6_module_try(TCCState *s1, const char *dir, const char *name,
                         const char *dotted)
{
    char buf[1024];
    int n;

    n = snprintf(buf, sizeof buf, "%s%s%s.c", dir, dir[0] ? "/" : "", name);
    if (n < (int)sizeof buf && tcc_open(s1, buf) >= 0)
        return 0;
    if (strcmp(dotted, name)) {
        n = snprintf(buf, sizeof buf, "%s%s%s.c", dir,
                     dir[0] ? "/" : "", dotted);
        if (n < (int)sizeof buf && tcc_open(s1, buf) >= 0)
            return 0;
    }
    return -1;
}

/* find and open 'NAME.c'.  Search order: the directory of the file that
   contains the 'import', the -I paths, then the current directory. */
static int c6_module_open(TCCState *s1, const char *name)
{
    char dotted[512];
    const char *dir;
    int i;

    c6_module_dotted(name, dotted, sizeof dotted);

    if (file && file->filename[0] && strcmp(file->filename, "<string>")) {
        const char *p = strrchr(file->filename, '/');
        if (p) {
            char buf[1024];
            int dlen = p - file->filename;
            snprintf(buf, sizeof buf, "%.*s", dlen, file->filename);
            if (c6_module_try(s1, buf, name, dotted) == 0)
                return 0;
        }
    }
    for (i = 0; i < s1->nb_include_paths; i++) {
        dir = s1->include_paths[i];
        if (c6_module_try(s1, dir, name, dotted) == 0)
            return 0;
    }
    return c6_module_try(s1, "", name, dotted);
}

/* Replay the declarations of the module NAME into the current translation
   unit.  Function bodies, initializers and file local objects are skipped:
   'import' supplies declarations, the module's own object supplies code.
   A missing file keeps the old 'import' behaviour (names only), so code
   that used 'import pkg;' as a namespace declaration still compiles. */
static void c6_module_import(const char *name)
{
    TCCState *s1 = tcc_state;
    char *saved_pkg, *saved_expected;
    int i, saved_tok, saved_scope, saved_nocode, saved_flags;
    CValue saved_val;
    const char *saved_func;

    if (!s1->c6_ext || !name || !name[0])
        return;
    for (i = 0; i < s1->c6_nb_loaded_modules; i++)
        if (!strcmp(s1->c6_loaded_modules[i], name))
            return;
    for (i = 0; i < s1->c6_nb_loading_modules; i++)
        if (!strcmp(s1->c6_loading_modules[i], name))
            tcc_error("circular import of module '%s'", name);
    if (c6_module_open(s1, name) < 0)
        return;

    dynarray_add(&s1->c6_loading_modules, &s1->c6_nb_loading_modules,
                 tcc_strdup(name));
    saved_tok = tok;
    saved_val = tokc;
    saved_flags = parse_flags;
    saved_pkg = s1->c6_package;
    saved_expected = c6_expected_module;
    saved_scope = local_scope;
    saved_nocode = nocode_wanted;
    saved_func = funcname;

    s1->c6_package = NULL;
    local_scope = 0;
    nocode_wanted = DATA_ONLY_WANTED;
    funcname = "";
    c6_expected_module = (char *)name;
    c6_importing++;

    next();
    decl(VT_CONST);
    if (!file->c6_module)
        tcc_error("'%s' does not declare 'module %s;'",
                  file->filename, name);
    tcc_close();

    c6_importing--;
    c6_expected_module = saved_expected;
    funcname = saved_func;
    nocode_wanted = saved_nocode;
    local_scope = saved_scope;
    s1->c6_package = saved_pkg;
    parse_flags = saved_flags;
    tok = saved_tok;
    tokc = saved_val;

    tcc_free(s1->c6_loading_modules[s1->c6_nb_loading_modules - 1]);
    s1->c6_nb_loading_modules--;
    dynarray_add(&s1->c6_loaded_modules, &s1->c6_nb_loaded_modules,
                 tcc_strdup(name));
}

/* consume an initializer (or the rest of a declarator) and stop at the
   ';' or ',' that ends it at brace/parenthesis depth 0 */
static void c6_skip_decl_tail(void)
{
    int depth = 0;

    while (tok != TOK_EOF) {
        if (tok == '{' || tok == '(' || tok == '[') {
            depth++;
        } else if (tok == '}' || tok == ')' || tok == ']') {
            if (depth)
                depth--;
            else
                break;
        } else if (depth == 0 && (tok == ';' || tok == ',')) {
            break;
        }
        next();
    }
}

/* remember one exported declaration of a module for '-emit-header' */
static void c6_header_record(int kind, int name, CType *type, Sym *sym)
{
    TCCState *s1 = tcc_state;
    C6HeaderDecl *d;
    int i;

    if (!s1->c6_emit_header || c6_importing)
        return;
    if (!file || !file->c6_module || name < TOK_UIDENT)
        return;
    if (type && (type->t & VT_STATIC))
        return;                 /* file local object */
    for (i = 0; i < s1->c6_nb_header_decls; i++) {
        d = s1->c6_header_decls[i];
        if (d->kind == kind && d->name == name) {
            if (type)
                d->type = *type;
            if (sym)
                d->sym = sym;
            return;
        }
    }
    d = tcc_mallocz(sizeof *d);
    d->kind = kind;
    d->name = name;
    if (type)
        d->type = *type;
    d->sym = sym;
    dynarray_add(&s1->c6_header_decls, &s1->c6_nb_header_decls, d);
}

/* ------------------------------------------------------------------ */
/* '-emit-header': a plain C header for one module                     */
/*                                                                     */
/* The emitter works from the declarations recorded while the module's  */
/* own TU was compiled, so the header describes exactly the symbols the */
/* object file exports.  Typedefs are expanded to their underlying      */
/* types; slices and tuples get a generated typedef name, because C has */
/* no other way to spell their layout.                                  */

typedef struct C6HdrExtra {
    char name[192];
    CType type;
    int tuple;
    struct C6HdrExtra *next;
} C6HdrExtra;

static C6HdrExtra *c6_hdr_extras;

static void c6_hdr_type(char *buf, int size, CType *type, const char *varstr);

/* the C name of a declared struct/union/enum tag, or a typedef that names
   an anonymous one */
static const char *c6_hdr_tag_name(CType *type)
{
    Sym *s = type->ref;
    int i;

    if (s && (s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
        return get_tok_str(s->v & ~SYM_STRUCT, NULL);
    for (i = 0; i < tcc_state->c6_nb_header_decls; i++) {
        C6HeaderDecl *d = tcc_state->c6_header_decls[i];
        if (d->kind == C6_HD_TYPEDEF && d->type.ref == s)
            return get_tok_str(d->name, NULL);
    }
    tcc_error("cannot write a C header for an anonymous struct type; "
              "give it a typedef first");
    return NULL;
}

/* remember the generated typedef for a slice/tuple type */
static void c6_hdr_extra(CType *type, int tuple, char *out, int outsz)
{
    C6HdrExtra *e, **pe;
    int id = 0;

    for (e = c6_hdr_extras; e; e = e->next) {
        if (e->tuple == tuple && is_compatible_types(&e->type, type)) {
            pstrcpy(out, outsz, e->name);
            return;
        }
        id++;
    }
    e = tcc_mallocz(sizeof *e);
    e->type = *type;
    e->tuple = tuple;
    snprintf(e->name, sizeof e->name, "%s%s_%d",
             (file && file->c6_module) ? file->c6_module : "__c6_",
             tuple ? "tuple" : "slice", id);
    for (pe = &c6_hdr_extras; *pe; pe = &(*pe)->next)
        ;
    *pe = e;
    pstrcpy(out, outsz, e->name);
}

static void c6_hdr_type(char *buf, int size, CType *type, const char *varstr)
{
    int bt = type->t & VT_BTYPE, t = type->t;
    char buf1[2048], tmp[1024], name[192];
    Sym *s;

    buf[0] = 0;
    if (bt == VT_STRUCT) {
        C6Slice *sl = c6_slice_of_type(type);
        C6Tuple *tp = c6_tuple_of_type(type);
        if (sl) {
            c6_hdr_type(tmp, sizeof tmp, &sl->elem, NULL);
            c6_hdr_extra(type, 0, name, sizeof name);
            pstrcat(buf, size, name);
            goto add_var;
        }
        if (tp) {
            int i;
            for (i = 0; i < tp->n; i++)
                c6_hdr_type(tmp, sizeof tmp, &tp->elem[i], NULL);
            c6_hdr_extra(type, 1, name, sizeof name);
            pstrcat(buf, size, name);
            goto add_var;
        }
    }
    if (bt == VT_PTR) {
        s = type->ref;
        if (t & (VT_ARRAY | VT_VLA)) {
            const char *v = varstr ? varstr : "";
            char tail[32];
            if (!s || s->c < 0 || (t & VT_VLA))
                pstrcpy(tail, sizeof tail, "[]");
            else
                snprintf(tail, sizeof tail, "[%d]", s->c);
            if (*v == '*')
                snprintf(buf1, sizeof buf1, "(%s)%s", v, tail);
            else
                snprintf(buf1, sizeof buf1, "%s%s", v, tail);
            c6_hdr_type(buf, size, &s->type, buf1);
        } else {
            snprintf(buf1, sizeof buf1, "*%s%s",
                     (t & VT_CONSTANT) ? "const " : "",
                     varstr ? varstr : "");
            c6_hdr_type(buf, size, &s->type, buf1);
        }
        return;
    }
    if (bt == VT_FUNC) {
        char params[2048];
        Sym *sa;
        params[0] = 0;
        for (sa = type->ref->next; sa; sa = sa->next) {
            c6_hdr_type(tmp, sizeof tmp, &sa->type, NULL);
            pstrcat(params, sizeof params, tmp);
            if (sa->next)
                pstrcat(params, sizeof params, ", ");
        }
        if (type->ref->f.func_type == FUNC_ELLIPSIS)
            pstrcat(params, sizeof params, ", ...");
        if (!params[0])
            pstrcat(params, sizeof params, "void");
        if (varstr && *varstr == '*')
            snprintf(buf1, sizeof buf1, "(%s)(%s)", varstr, params);
        else
            snprintf(buf1, sizeof buf1, "%s(%s)", varstr ? varstr : "",
                     params);
        c6_hdr_type(buf, size, &type->ref->type, buf1);
        return;
    }
    if (t & VT_CONSTANT)
        pstrcat(buf, size, "const ");
    if (t & VT_VOLATILE)
        pstrcat(buf, size, "volatile ");
    if ((t & VT_DEFSIGN) && bt == VT_BYTE)
        pstrcat(buf, size, "signed ");
    if ((t & VT_UNSIGNED) && (bt == VT_SHORT || bt == VT_INT || bt == VT_LLONG)
        && !IS_ENUM(t))
        pstrcat(buf, size, "unsigned ");
    switch (bt) {
    case VT_VOID:
        pstrcat(buf, size, "void");
        break;
    case VT_BOOL:
        pstrcat(buf, size, "_Bool");
        break;
    case VT_BYTE:
        pstrcat(buf, size, "char");
        break;
    case VT_SHORT:
        pstrcat(buf, size, "short");
        break;
    case VT_INT:
        if (IS_ENUM(t)) {
            /* an anonymous enum is only nameable through its typedef */
            if (type->ref
                && (type->ref->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
                pstrcat(buf, size, "enum ");
            pstrcat(buf, size, c6_hdr_tag_name(type));
        } else if (t & VT_LONG) {
            pstrcat(buf, size, "long");
        } else {
            pstrcat(buf, size, "int");
        }
        break;
    case VT_LLONG:
        pstrcat(buf, size, (t & VT_LONG) ? "long" : "long long");
        break;
    case VT_FLOAT:
        pstrcat(buf, size, "float");
        break;
    case VT_DOUBLE:
        pstrcat(buf, size, "double");
        break;
    case VT_LDOUBLE:
        pstrcat(buf, size, "long double");
        break;
    case VT_STRUCT:
        if (!type->ref
            || (type->ref->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
            pstrcat(buf, size, IS_UNION(t) ? "union " : "struct ");
        /* else: an anonymous tag is spelled by its typedef name */
        pstrcat(buf, size, c6_hdr_tag_name(type));
        break;
    default:
        type_to_str(buf, size, type, varstr);
        return;
    }
add_var:
    if (varstr) {
        pstrcat(buf, size, " ");
        pstrcat(buf, size, varstr);
    }
}

/* write one struct/union body (fields, bit-fields, nested anonymous members) */
static void c6_hdr_struct_body(FILE *f, Sym *s)
{
    Sym *fs;
    char buf[2048], name[128];
    int anon = 0;

    for (fs = s->next; fs; fs = fs->next) {
        int v = fs->v & ~SYM_FIELD;

        if ((fs->type.t & VT_BTYPE) == VT_STRUCT && fs->type.ref
            && (fs->type.ref->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM) {
            /* C11 anonymous member: keep it unnamed, so field access in the
               module ('s.u') keeps working in plain C */
            fprintf(f, "    %s {\n",
                    IS_UNION(fs->type.t) ? "union" : "struct");
            c6_hdr_struct_body(f, fs->type.ref);
            fprintf(f, "    };\n");
            continue;
        }
        if (v >= SYM_FIRST_ANOM)
            snprintf(name, sizeof name, "__anon%d", anon++);
        else
            pstrcpy(name, sizeof name, get_tok_str(v, NULL));
        c6_hdr_type(buf, sizeof buf, &fs->type, name);
        fprintf(f, "    %s", buf);
        if (fs->type.t & VT_BITFIELD)
            fprintf(f, " : %d", BIT_SIZE(fs->type.t));
        fprintf(f, ";\n");
    }
}

static void c6_hdr_collect_struct(Sym *s)
{
    char buf[2048];
    Sym *fs;

    for (fs = s->next; fs; fs = fs->next) {
        if ((fs->type.t & VT_BTYPE) == VT_STRUCT && fs->type.ref
            && (fs->type.ref->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM)
            c6_hdr_collect_struct(fs->type.ref);
        else
            c6_hdr_type(buf, sizeof buf, &fs->type, NULL);
    }
}

static void c6_hdr_collect_decl(C6HeaderDecl *d)
{
    char buf[2048];

    if (d->kind == C6_HD_STRUCT) {
        c6_hdr_collect_struct(d->sym);
    } else if (d->kind == C6_HD_TYPEDEF) {
        if ((d->type.t & VT_BTYPE) == VT_STRUCT && d->type.ref
            && (d->type.ref->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM)
            c6_hdr_collect_struct(d->type.ref);
        else
            c6_hdr_type(buf, sizeof buf, &d->type, NULL);
    } else if (d->kind == C6_HD_FUNC || d->kind == C6_HD_VAR) {
        c6_hdr_type(buf, sizeof buf, &d->type, NULL);
    }
}

static void c6_hdr_print_typedef(FILE *f, C6HeaderDecl *d)
{
    char buf[2048];
    const char *name = get_tok_str(d->name, NULL);

    if ((d->type.t & VT_BTYPE) == VT_STRUCT && d->type.ref
        && (d->type.ref->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM) {
        fprintf(f, "typedef %s {\n",
                IS_UNION(d->type.t) ? "union" : "struct");
        c6_hdr_struct_body(f, d->type.ref);
        fprintf(f, "} %s;\n", name);
        return;
    }
    c6_hdr_type(buf, sizeof buf, &d->type, name);
    fprintf(f, "typedef %s;\n", buf);
}

static void c6_emit_header(TCCState *s1)
{
    FILE *f;
    C6HeaderDecl *d;
    C6HdrExtra *e;
    const char *mod;
    char guard[256], plain[192];
    int i, j, n, first;

    if (!file || !file->c6_module) {
        tcc_error("-emit-header requires a 'module NAME;' declaration");
        return;
    }
    mod = file->c6_module;              /* 'math__' */
    n = snprintf(plain, sizeof plain, "%s", mod);
    if (n > 2)
        plain[n - 2] = 0;
    snprintf(guard, sizeof guard, "C6_%s_H", plain);
    for (i = 0; guard[i]; i++) {
        if (!((guard[i] >= 'A' && guard[i] <= 'Z')
              || (guard[i] >= 'a' && guard[i] <= 'z')
              || (guard[i] >= '0' && guard[i] <= '9')))
            guard[i] = '_';
    }

    /* drop any slice/tuple typedefs left by an earlier header */
    while (c6_hdr_extras) {
        e = c6_hdr_extras;
        c6_hdr_extras = e->next;
        tcc_free(e);
    }

    /* a first pass fills c6_hdr_extras (slice/tuple typedefs) */
    for (i = 0; i < s1->c6_nb_header_decls; i++)
        c6_hdr_collect_decl(s1->c6_header_decls[i]);

    f = fopen(s1->c6_emit_header, "w");
    if (!f)
        tcc_error("could not open '%s' for writing", s1->c6_emit_header);

    fprintf(f, "/* generated by tcc -std=c6 -emit-header from module '%s' */\n",
            plain);
    fprintf(f, "#ifndef %s\n#define %s\n\n", guard, guard);
    fprintf(f, "#include <stddef.h>\n#include <stdint.h>\n\n");

    /* forward declarations make pointer uses order independent */
    for (i = 0; i < s1->c6_nb_header_decls; i++) {
        d = s1->c6_header_decls[i];
        if (d->kind == C6_HD_STRUCT)
            fprintf(f, "%s %s;\n",
                    IS_UNION(d->sym->type.t) ? "union" : "struct",
                    get_tok_str(d->name, NULL));
    }

    /* generated slice/tuple layouts */
    for (e = c6_hdr_extras; e; e = e->next) {
        char buf[2048];
        if (e->tuple) {
            C6Tuple *tp = c6_tuple_of_type(&e->type);
            char fname[32];
            fprintf(f, "typedef struct {\n");
            for (j = 0; j < tp->n; j++) {
                snprintf(fname, sizeof fname, "f%d", j);
                c6_hdr_type(buf, sizeof buf, &tp->elem[j], fname);
                fprintf(f, "    %s;\n", buf);
            }
            fprintf(f, "} %s;\n", e->name);
        } else {
            C6Slice *sl = c6_slice_of_type(&e->type);
            c6_hdr_type(buf, sizeof buf, &sl->elem, "*data");
            fprintf(f, "typedef struct {\n    %s;\n    size_t len;\n} %s;\n",
                    buf, e->name);
        }
    }
    if (c6_hdr_extras)
        fputc('\n', f);

    /* struct/union/enum definitions, in source order */
    for (i = 0; i < s1->c6_nb_header_decls; i++) {
        d = s1->c6_header_decls[i];
        if (d->kind == C6_HD_STRUCT && d->sym->c >= 0) {
            fprintf(f, "%s %s {\n",
                    IS_UNION(d->sym->type.t) ? "union" : "struct",
                    get_tok_str(d->name, NULL));
            c6_hdr_struct_body(f, d->sym);
            fprintf(f, "};\n\n");
        } else if (d->kind == C6_HD_ENUM) {
            Sym *ss;
            fprintf(f, "enum %s {\n", get_tok_str(d->name, NULL));
            for (ss = d->sym->next; ss; ss = ss->next)
                fprintf(f, "    %s = %lld%s\n", get_tok_str(ss->v, NULL),
                        ss->enum_val, ss->next ? "," : "");
            fprintf(f, "};\n\n");
        }
    }

    /* typedefs, functions and variables */
    first = 1;
    for (i = 0; i < s1->c6_nb_header_decls; i++) {
        d = s1->c6_header_decls[i];
        if (d->kind == C6_HD_TYPEDEF) {
            c6_hdr_print_typedef(f, d);
            first = 0;
        }
    }
    if (!first)
        fputc('\n', f);
    for (i = 0; i < s1->c6_nb_header_decls; i++) {
        char buf[2048];
        d = s1->c6_header_decls[i];
        if (d->kind != C6_HD_FUNC && d->kind != C6_HD_VAR)
            continue;
        c6_hdr_type(buf, sizeof buf, &d->type, get_tok_str(d->name, NULL));
        if (!(d->type.t & VT_EXTERN))
            fprintf(f, "extern ");
        fprintf(f, "%s;\n", buf);
    }
    fprintf(f, "\n#endif /* %s */\n", guard);
    fclose(f);
    while (c6_hdr_extras) {
        e = c6_hdr_extras;
        c6_hdr_extras = e->next;
        tcc_free(e);
    }
}

/* 'pkg.member' is only allowed for a package that was imported, or for the
   package the code currently sits in */
static int c6_pkg_imported(const char *name)
{
    int i;

    if (tcc_state->c6_package
        && !strcmp(tcc_state->c6_package, name))
        return 1;
    if (file && file->c6_module) {
        size_t n = strlen(file->c6_module);
        if (n > 2 && strlen(name) == n - 2
            && !strncmp(name, file->c6_module, n - 2))
            return 1;
    }
    for (i = 0; i < tcc_state->c6_nb_imports; i++)
        if (!strcmp(tcc_state->c6_imports[i], name))
            return 1;
    return 0;
}

/* resolve V as a member of the current module, the current package or of an
   imported package */
static Sym *c6_import_lookup(int v)
{
    TCCState *s1 = tcc_state;
    int i, t;
    Sym *s;

    if (!s1->c6_ext || v < TOK_UIDENT)
        return NULL;
    if (file && file->c6_module) {
        t = c6_mod_name(v);
        s = sym_find_raw(t);
        if (s)
            return s;
    }
    if (s1->c6_package) {
        t = c6_mangle_tok(s1->c6_package, v);
        t = c6_mod_name(t);
        s = sym_find_raw(t);
        if (s)
            return s;
    }
    for (i = 0; i < s1->c6_nb_imports; i++) {
        t = c6_mangle_tok(s1->c6_imports[i], v);
        s = sym_find_raw(t);
        if (s)
            return s;
    }
    return NULL;
}

/* the tag ('struct'/'union'/'enum') counterpart of c6_import_lookup */
static Sym *c6_import_tag_lookup(int v)
{
    TCCState *s1 = tcc_state;
    int i, t;
    Sym *s;

    if (!s1->c6_ext || v < TOK_UIDENT || !s1->c6_nb_imports)
        return NULL;
    for (i = 0; i < s1->c6_nb_imports; i++) {
        t = c6_mangle_tok(s1->c6_imports[i], v);
        s = struct_find_raw(t);
        if (s)
            return s;
    }
    return NULL;
}

/* Compose 'outer' and 'inner' into a single prefix.  Nesting is not a
   separate namespace layer: 'package a { package b { ... } }' simply
   prefixes twice, so a symbol declared inside becomes 'a__b__sym'. */
static char *c6_pkg_compose(const char *outer, char *inner)
{
    char buf[128];
    int n;

    n = snprintf(buf, sizeof buf, "%s__%s", outer, inner);
    tcc_free(inner);
    if (n >= (int)sizeof buf)
        tcc_error("package name too long");
    return tcc_strdup(buf);
}

static void c6_package_parse(void)
{
    char *name, *saved;

    next();  /* skip 'package' */
    name = c6_pkg_name(&tok);
    if (tcc_state->c6_package)
        name = c6_pkg_compose(tcc_state->c6_package, name);
    if (tok == '{') {
        /* 'package NAME { ... }': the package ends with the block */
        saved = tcc_state->c6_package;
        tcc_state->c6_package = name;
        next();
        while (tok != '}' && tok != TOK_EOF)
            decl(VT_CONST);
        skip('}');
        tcc_state->c6_package = saved;
        tcc_free(name);
    } else {
        /* 'package NAME;' names the rest of the file (or until the next
           'package' statement); include system headers before it */
        tcc_free(tcc_state->c6_package);
        tcc_state->c6_package = name;
        skip(';');
    }
}

static void c6_import_parse(void)
{
    char *name;

    next();  /* skip 'import' */
    name = c6_pkg_name(&tok);
    skip(';');
    dynarray_add(&tcc_state->c6_imports,
                 &tcc_state->c6_nb_imports, tcc_strdup(name));
    /* if '<name>.c' exists next to this file (or in a -I directory), its
       declarations are replayed into this unit; otherwise this stays a
       pure namespace import */
    c6_module_import(name);
    tcc_free(name);
}


/* ------------------------------------------------------------------ */
/* struct methods: 'ret f(struct T *self, ...)' called as 'p->f(...)'  */
/*                                                                     */
/* A method is an ordinary global function whose first parameter is a  */
/* pointer to a struct (or union).  The receiver is that pointer, so   */
/* the symbol name and the calling convention stay untouched; only the */
/* call syntax 'p->f(a)' is rewritten into 'f(p, a)'.                  */

/* the struct/union tag a pointer type points at, or 0 */
static int c6_ptr_tag(CType *type)
{
    CType *pt;

    if ((type->t & VT_BTYPE) != VT_PTR || !type->ref)
        return 0;
    pt = pointed_type(type);
    if ((pt->t & VT_BTYPE) != VT_STRUCT || !pt->ref)
        return 0;
    return pt->ref->v & ~SYM_STRUCT;
}

/* the receiver type name, for diagnostics */
static const char *c6_tag_name(int tag)
{
    static char buf[64];

    if (tag >= SYM_FIRST_ANOM) {
        snprintf(buf, sizeof buf, "<anonymous>");
        return buf;
    }
    return get_tok_str(tag, NULL);
}

/* '<tag>_<name>': the C symbol a method is emitted under, so that two
   structures can use the same method name without separate packages.
   Returns 0 when the declaration is not a method, or when it must keep its
   plain C name:
     - 'static' / 'inline' functions: their body is emitted later (or stays
       file local), so no plain alias can be attached to it;
     - names starting with '__': those belong to the compiler / libtcc1;
     - declarations made while a system header is being parsed: 'p->fflush()'
       has to keep calling libc's fflush;
     - methods of anonymous structs: their tag number is per translation
       unit, so it cannot name a symbol shared between TUs. */
static int c6_method_symbol(int name, CType *type)
{
    int tag;
    const char *tn, *mn;
    char buf[160];

    if (!tcc_state || !tcc_state->c6_ext || !type->ref)
        return 0;
    if ((type->t & VT_BTYPE) != VT_FUNC)
        return 0;
    if (type->t & (VT_STATIC | VT_INLINE))
        return 0;
    if (type->ref->f.func_type == FUNC_OLD || type->ref->f.func_async)
        return 0;
    if (!type->ref->next)
        return 0;
    tag = c6_ptr_tag(&type->ref->next->type);
    if (!tag || tag >= SYM_FIRST_ANOM)
        return 0;
    if (file->sys_header)
        return 0;
    mn = get_tok_str(name, NULL);
    if (mn[0] == '_' && mn[1] == '_')
        return 0;
    tn = get_tok_str(tag, NULL);
    if (strlen(tn) + strlen(mn) + 2 > sizeof buf)
        return 0;
    snprintf(buf, sizeof buf, "%s_%s", tn, mn);
    return tok_alloc_const(buf);
}

/* the name a method was written with in the source, for '__func__' */
static const char *c6_method_source_name(int sym_tok)
{
    C6Method *m;

    if (!tcc_state || !tcc_state->c6_ext)
        return NULL;
    for (m = c6_methods; m; m = m->next)
        if (m->sym_tok == sym_tok)
            return get_tok_str(m->name, NULL);
    return NULL;
}

/* external_sym() without the 'import' fallback: the plain name of a method
   must not be resolved to an imported 'pkg__name' */
static Sym *c6_external_sym_raw(int v, CType *type, int r, AttributeDef *ad)
{
    Sym *s = sym_find_raw(v);

    if (!s) {
        s = global_identifier_push(v, type->t, 0);
        s->r |= r;
        s->a = ad->a;
        s->asm_label = ad->asm_label;
        s->type.ref = type->ref;
    } else {
        patch_storage(s, ad, type);
    }
    return s;
}

/* keep plain 'f(p)' working: declare the source name too.  The definition
   turns it into an alias of the mangled symbol, see c6_method_alias() */
static void c6_method_plain_decl(int name, CType *type, AttributeDef *ad)
{
    CType t = *type;
    AttributeDef a = *ad;
    int alias = c6_mod_name(name);

    if (!tcc_state->c6_ext || sym_find_raw(alias))
        return;                 /* already declared under the plain name */
    t.t = (t.t | VT_EXTERN) & ~(VT_STATIC | VT_INLINE);
    a.asm_label = 0;
    a.alias_target = 0;
    c6_external_sym_raw(alias, &t, 0, &a);
}

/* emit the source name as an alias of the mangled method symbol, so that
   plain calls, hand written C and non-c6 TUs keep linking against it.
   The alias is weak: when two structures really do share a method name the
   plain name cannot denote both, and a real (strong) C definition of that
   name, if any, has to win. */
static void c6_method_alias(int name, Sym *msym)
{
    int alias = c6_mod_name(name);
    Sym *ps = sym_find_raw(alias);
    ElfSym *es;

    if (!tcc_state->c6_ext || !ps || ps == msym)
        return;
    if ((ps->type.t & VT_BTYPE) != VT_FUNC)
        return;
    /* the plain name may already be a *different* function, typically one
       declared by a system header ('int getpid(struct F *)' must not turn
       'getpid()' into the method): only alias our own plain declaration */
    if (!is_compatible_types(&ps->type, &msym->type))
        return;
    es = elfsym(msym);
    if (!es || es->st_shndx == SHN_UNDEF)
        return;
    /* the plain name may already denote another method of this TU: keep the
       first one, so that plain calls stay stable */
    if (ps->c && elfsym(ps)->st_shndx != SHN_UNDEF)
        return;
    ps->a.weak = 1;
    /* put_extern_sym2() also patches an elfsym that a forward 'f(p)' call
       created earlier */
    put_extern_sym2(ps, es->st_shndx, es->st_value, es->st_size, 1);
#ifdef TCC_TARGET_C99
    /* C has no two definitions for one name: the C99 back-end emits the
       alias with __attribute__((alias)) instead.  Tokens belong to this
       translation unit, so record copies of the names. */
    {
        C6MethodAlias *a = tcc_mallocz(sizeof *a);
        a->plain = tcc_strdup(get_tok_str(alias, NULL));
        a->target = tcc_strdup(get_tok_str(msym->v, NULL));
        dynarray_add(&tcc_state->c6_method_aliases,
                     &tcc_state->c6_nb_method_aliases, a);
    }
#endif
}

/* the token 'drop', the one name that means "destructor" */
static int c6_gc_drop_name(void)
{
    static int tok;
    if (!tok)
        tok = tok_alloc_const("drop");
    return tok;
}

/* nonzero when SYM_TOK is a destructor: those cannot be called by hand, an
   object is destroyed by letting go of its last reference */
static int c6_gc_is_drop(int sym_tok)
{
    int i;

    if (!tcc_state->c6_ext)
        return 0;
    for (i = 0; i < tcc_state->c6_nb_gc_types; i++)
        if (tcc_state->c6_gc_types[i]
            && tcc_state->c6_gc_types[i]->drop_tok == sym_tok)
            return 1;
    return 0;
}

/* nonzero when the callee S denotes a destructor, by its mangled name or by
   the plain name 'drop' resolving to one */
static int c6_gc_drop_callee(Sym *s)
{
    C6GcType *gt;
    int tag;

    if (!tcc_state->c6_ext || !s || !s->type.ref)
        return 0;
    if (c6_gc_is_drop(s->v))
        return 1;
    if (s->v != c6_gc_drop_name() || !s->type.ref->next)
        return 0;
    tag = c6_ptr_tag(&s->type.ref->next->type);
    if (!tag)
        return 0;
    gt = c6_gc_type_find(tag);
    return gt && gt->drop_tok;
}

/* Register TAG's destructor when this declaration is one: 'void
   drop(struct T *self)' with no other parameter, which -- unlike a normal
   method -- cannot return a value, fail or suspend. */
static void c6_gc_drop_register(int name, int sym_tok, CType *type, int tag)
{
    C6GcType *gt;
    const char *n = get_tok_str(name, NULL);

    if (!n || strcmp(n, "drop") != 0)
        return;
    if (type->ref->next->next)
        return;                     /* more than the receiver: not ours */
    if ((type->ref->type.t & VT_BTYPE) != VT_VOID)
        tcc_error("'drop' must return void: it runs while the object is "
                  "being released, where a value has nowhere to go");
    if (type->ref->f.func_fallible)
        tcc_error("'drop' cannot fail ('!error'): it runs while the object "
                  "is being released, where an error has nowhere to go");
    gt = c6_gc_type_find(tag);
    if (!gt) {
        gt = tcc_mallocz(sizeof *gt);
        gt->tag = tag;
        gt->src_file = tcc_strdup(file->filename);
        gt->src_line = file->line_num;
        dynarray_add(&tcc_state->c6_gc_types, &tcc_state->c6_nb_gc_types, gt);
    }
    if (gt->drop_tok && gt->drop_tok != sym_tok)
        tcc_error("'%s' already has a destructor", c6_tag_name(tag));
    gt->drop_tok = sym_tok;
    /* a prototype may come before the struct is complete */
    c6_gc_size_of(gt, pointed_type(&type->ref->next->type));
}

/* register 'ret NAME(struct T *self, ...)'; NAME is the spelling used in
   the source, SYM_TOK the (possibly package-mangled) symbol */
static void c6_method_register(int name, int sym_tok, CType *type)
{
    C6Method *m, **pm;
    int tag;

    if (!tcc_state->c6_ext || !type->ref)
        return;
    if ((type->t & VT_BTYPE) != VT_FUNC)
        return;
    if (type->ref->f.func_type == FUNC_OLD)
        return;
    /* the first parameter is the receiver */
    if (!type->ref->next)
        return;
    tag = c6_ptr_tag(&type->ref->next->type);
    if (!tag)
        return;
    if (type->ref->f.func_async)
        tcc_error("'async' functions cannot be methods: '%s'",
                  get_tok_str(name, NULL));
    c6_gc_drop_register(name, sym_tok, type, tag);
    for (m = c6_methods; m; m = m->next) {
        if (m->type_tag == tag && m->name == name) {
            if (m->sym_tok == sym_tok)
                return;         /* prototype plus definition */
            tcc_error("duplicate method '%s' for '%s'",
                      get_tok_str(name, NULL), c6_tag_name(tag));
        }
    }
    m = tcc_mallocz(sizeof *m);
    m->type_tag = tag;
    m->name = name;
    m->sym_tok = sym_tok;
    for (pm = &c6_methods; *pm; pm = &(*pm)->next)
        ;
    *pm = m;
    c6_nb_methods++;
}

/* is there a method NAME taking a pointer to the type TYPE points at? */
static C6Method *c6_method_lookup(CType *type, int name)
{
    C6Method *m;
    int tag;

    if (!tcc_state->c6_ext || !c6_nb_methods)
        return NULL;
    tag = c6_ptr_tag(type);
    if (!tag)
        return NULL;
    /* an interface value dispatches through its vtable instead */
    if (c6_iface_of_type(pointed_type(type)))
        return NULL;
    for (m = c6_methods; m; m = m->next)
        if (m->type_tag == tag && m->name == name)
            return m;
    return NULL;
}

/* find_field() without the diagnostic: used to give a real field
   precedence over a method of the same name */
static Sym *c6_find_field_quiet(CType *type, int v)
{
    Sym *s = type->ref;

    if (!s || (type->t & VT_BTYPE) != VT_STRUCT || s->c < 0)
        return NULL;
    while ((s = s->next) != NULL) {
        if (s->v == v)
            return s;
        if ((s->type.t & VT_BTYPE) == VT_STRUCT
            && s->v >= (SYM_FIRST_ANOM | SYM_FIELD)) {
            if (c6_find_field_quiet(&s->type, v | SYM_FIELD))
                return s;
        }
    }
    return NULL;
}

static int c6_has_field(CType *type, int v)
{
    if (!c6_ptr_tag(type))
        return 0;
    return c6_find_field_quiet(pointed_type(type), v | SYM_FIELD) != 0;
}

/* leave the receiver in a temporary slot and push the method symbol, so
   that the '(' case can treat the call like any other, with the hidden
   receiver as the first argument */
static void c6_method_call_prep(C6Method *m)
{
    Sym *fs = sym_find_raw(m->sym_tok);
    int r;

    if (c6_gc_is_drop(m->sym_tok))
        tcc_error("a destructor cannot be called: it runs when the last "
                  "reference is released (use 'p = NULL')");

    if (!fs)
        tcc_error("method '%s' is not a function",
                  get_tok_str(m->name, NULL));
    c6_self_loc = (loc - PTR_SIZE) & -PTR_SIZE;
    loc = c6_self_loc;
    c6_self_type = vtop->type;
    vset(&c6_self_type, VT_LOCAL | VT_LVAL, c6_self_loc);
    vswap();
    vstore();
    vpop();
    r = fs->r;
    if ((r & VT_VALMASK) < VT_CONST)
        r = (r & ~VT_VALMASK) | VT_LOCAL;
    vset(&fs->type, r, fs->c);
    vtop->sym = fs;
    if (r & VT_SYM)
        vtop->c.i = 0;
    c6_self_pending = 1;
}



/* ------------------------------------------------------------------ */
/* interface / impl                                                    */

static C6Iface *c6_iface_find(int tag)
{
    int i;
    for (i = 0; i < tcc_state->c6_nb_ifaces; i++)
        if (tcc_state->c6_ifaces[i]->tag == tag)
            return tcc_state->c6_ifaces[i];
    return NULL;
}

/* if TYPE denotes an interface value type, return its descriptor */
static C6Iface *c6_iface_of_type(CType *type)
{
    Sym *st;
    if (!tcc_state->c6_ext || tcc_state->c6_nb_ifaces == 0)
        return NULL;
    if ((type->t & VT_BTYPE) != VT_STRUCT || !type->ref)
        return NULL;
    st = type->ref;
    if (!(st->v & SYM_STRUCT))
        return NULL;
    return c6_iface_find(st->v & ~SYM_STRUCT);
}

static int c6_iface_has_method(C6Iface *ifc, int m)
{
    int i;
    for (i = 0; i < ifc->nb_methods; i++)
        if (ifc->methods[i] == m)
            return 1;
    return 0;
}

/* Parse TS (a synthetic token string ending in TOK_EOF) as file scope
   declarations.  The current token is left untouched and warnings are
   suppressed, since the generated declarations are compiler-internal. */
static void c6_decl_tokens(TokenString *ts)
{
    int warn_none = tcc_state->warn_none;
    int save_tok = tok;
    CValue save_val = tokc;
    tcc_state->warn_none = 1;
    begin_macro(ts, 1);
    next();
    decl(VT_CONST);
    end_macro();
    tcc_state->warn_none = warn_none;
    tok = save_tok, tokc = save_val;
}

static void c6_interface_parse(void)
{
    C6Iface *ifc;
    TokenString *body = NULL, *ts;
    int name_tok, i, n, *p, is_method;
    char buf[256];

    next();                          /* skip 'interface' */
    if (tok < TOK_UIDENT)
        expect("interface name");
    if (struct_find_raw(c6_mod_name(tok)))
        tcc_error("redefinition of 'struct %s'", get_tok_str(tok, NULL));
    name_tok = tok;
    next();
    skip_or_save_block(&body);

    ifc = tcc_mallocz(sizeof *ifc);
    ifc->name = tcc_strdup(get_tok_str(name_tok, NULL));
    /* the value struct and the vtable are module-prefixed like any other
       file scope type; 'name' stays the source spelling for diagnostics */
    ifc->tag = c6_mod_name(name_tok);
    snprintf(buf, sizeof buf, "%s_vt", get_tok_str(ifc->tag, NULL));
    ifc->vt_tag = tok_alloc_const(buf);

    /* Build 'struct NAME_vt { RET (*meth)(void *, ...); ... };', that is,
       turn each method declaration into a function-pointer field whose
       first parameter is the hidden 'self' pointer. */
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, ifc->vt_tag);
    tok_str_add(ts, '{');
    p = body->str;
    n = body->len;
    is_method = 0;
    for (i = 0; i < n; i++) {
        int t = p[i];
        if (t == 0 || t == TOK_EOF)
            break;
        if (t == '{' || t == '}')
            continue;
        if (TOK_HAS_VALUE(t)) {
            /* only int-valued tokens can appear in method declarations */
            tok_str_add(ts, t);
            tok_str_add(ts, p[++i]);
            continue;
        }
        if (t == '(') {
            tok_str_add(ts, '(');
            if (is_method) {
                tok_str_add(ts, TOK_VOID);
                tok_str_add(ts, '*');
                if (i + 1 < n && p[i + 1] != ')')
                    tok_str_add(ts, ',');
            }
            is_method = 0;
            continue;
        }
        if (t >= TOK_UIDENT && i + 1 < n && p[i + 1] == '(') {
            /* method name: '( * name )' */
            tok_str_add(ts, '(');
            tok_str_add(ts, '*');
            tok_str_add(ts, t);
            tok_str_add(ts, ')');
            ifc->methods = tcc_realloc(ifc->methods,
                                       (ifc->nb_methods + 1) * sizeof(int));
            ifc->methods[ifc->nb_methods++] = t;
            is_method = 1;
            continue;
        }
        tok_str_add(ts, t);
    }
    tok_str_add(ts, '}');
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    if (ifc->nb_methods == 0)
        tcc_error("interface '%s' has no methods", ifc->name);
    c6_decl_tokens(ts);

    /* Build the interface value type:
       'struct NAME { void *self; const struct NAME_vt *vt; };' */
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, ifc->tag);
    tok_str_add(ts, '{');
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, '*');
    tok_str_add(ts, tok_alloc_const("self"));
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_CONST1);
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, ifc->vt_tag);
    tok_str_add(ts, '*');
    tok_str_add(ts, tok_alloc_const("vt"));
    tok_str_add(ts, ';');
    tok_str_add(ts, '}');
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);

    /* 'typedef struct NAME NAME;' so that the interface name can be
       used as a type, e.g. 'int describe(Shape s)' */
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_TYPEDEF);
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, ifc->tag);
    tok_str_add(ts, ifc->tag);
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);

    dynarray_add(&tcc_state->c6_ifaces,
                 &tcc_state->c6_nb_ifaces, ifc);
    tok_str_free(body);
}

/* rewrite 'iface.meth (args...)' into a vtable call passing the hidden
   'self' pointer as the first argument; leaves the function pointer on
   the stack and arms 'c6_self_pending' for the call code */
static void c6_iface_call_prep(C6Iface *ifc, int mname)
{
    Sym *fself, *fvt, *fmeth;
    CType t;
    int off_self, off_vt, off_m;

    fself = find_field(&vtop->type, tok_alloc_const("self"), &off_self);
    fvt = find_field(&vtop->type, tok_alloc_const("vt"), &off_vt);
    if (!fself || !fvt)
        tcc_error("bad interface type");

    gaddrof();                       /* address of the interface value */
    vtop->type = char_pointer_type;

    /* self (kept in a temporary local slot) */
    gv_dup();
    vpushi(off_self);
    gen_op('+');
    t = fself->type;
    mk_pointer(&t);
    vtop->type = t;
    indir();                         /* the 'self' pointer value */
    c6_self_loc = (loc - PTR_SIZE) & -PTR_SIZE;
    loc = c6_self_loc;
    vset(&fself->type, VT_LOCAL | VT_LVAL, c6_self_loc);
    vswap();
    vstore();
    vpop();
    c6_self_type = fself->type;

    /* method: d.vt->name */
    vpushi(off_vt);
    gen_op('+');
    t = fvt->type;
    mk_pointer(&t);
    vtop->type = t;
    indir();                         /* d.vt */
    fmeth = find_field(pointed_type(&vtop->type), mname, &off_m);
    if (!fmeth)
        tcc_error("interface '%s' has no method '%s'", ifc->name,
                  get_tok_str(mname, NULL));
    vtop->type = char_pointer_type;
    vpushi(off_m);
    gen_op('+');
    t = fmeth->type;
    mk_pointer(&t);
    vtop->type = t;
    indir();                         /* the function pointer */
    c6_self_pending = 1;
}

/* ------------------------------------------------------------------ */
/* structured interfaces: a struct implements an interface when it has  */
/* every method the interface declares.  There is no 'impl' syntax any  */
/* more: the conformance check happens at the first conversion, and the  */
/* vtable is an ordinary static object emitted at the end of the unit.  */

static C6AutoImpl *c6_auto_impl_find(C6Iface *ifc, int tag)
{
    int i;

    for (i = 0; i < c6_nb_auto_impls; i++)
        if (c6_auto_impls[i]->iface == ifc
            && c6_auto_impls[i]->type_tag == tag)
            return c6_auto_impls[i];
    return NULL;
}

static C6Method *c6_method_of(int tag, int name)
{
    C6Method *m;

    for (m = c6_methods; m; m = m->next)
        if (m->type_tag == tag && m->name == name)
            return m;
    return NULL;
}

/* compare one vtable field 'RET (*)(void *, ...)' with the method type */
static void c6_check_method(C6Iface *ifc, int m, Sym *meth)
{
    Sym *vt = struct_find(ifc->vt_tag);
    CType vtt;
    Sym *f, *fp, *gp, *fr, *gr;
    CType *fptr;
    const char *mname = get_tok_str(m, NULL);
    int ofs;

    if (!vt)
        return;
    vtt.t = VT_STRUCT;
    vtt.ref = vt;
    f = find_field(&vtt, m, &ofs);
    if (!f || (f->type.t & VT_BTYPE) != VT_PTR)
        return;
    fptr = pointed_type(&f->type);
    if ((fptr->t & VT_BTYPE) != VT_FUNC || !fptr->ref)
        return;
    if ((meth->type.t & VT_BTYPE) != VT_FUNC || !meth->type.ref)
        tcc_error("method '%s' of '%s' must be a function", mname, ifc->name);
    fr = fptr->ref;
    gr = meth->type.ref;
    if (!is_compatible_types(&fr->type, &gr->type))
        tcc_error("method '%s' of '%s' has an incompatible return type",
                  mname, ifc->name);
    /* both parameter lists start with the receiver */
    fp = fr->next;
    gp = gr->next;
    if (fp)
        fp = fp->next;
    if (gp)
        gp = gp->next;
    while (fp && gp) {
        if (!is_compatible_types(&fp->type, &gp->type))
            tcc_error("method '%s' of '%s' has an incompatible parameter",
                      mname, ifc->name);
        fp = fp->next;
        gp = gp->next;
    }
    if (fp || gp)
        tcc_error("method '%s' of '%s' has the wrong number of parameters",
                  mname, ifc->name);
}

/* register (once) that TYPE implements IFACE and create its vtable symbol */
static C6AutoImpl *c6_auto_impl_ensure(C6Iface *ifc, int tag)
{
    C6AutoImpl *a;
    C6Method *meth;
    CType vtt;
    AttributeDef ad;
    Sym *vt;
    char buf[256];
    int i;

    a = c6_auto_impl_find(ifc, tag);
    if (a)
        return a;
    for (i = 0; i < ifc->nb_methods; i++) {
        int m = ifc->methods[i];
        meth = c6_method_of(tag, m);
        if (!meth)
            tcc_error("'%s' does not implement interface '%s': "
                      "missing method '%s'", c6_tag_name(tag), ifc->name,
                      get_tok_str(m, NULL));
        c6_check_method(ifc, m, sym_find_raw(meth->sym_tok));
    }
    vt = struct_find(ifc->vt_tag);
    if (!vt)
        return NULL;
    if (tag >= SYM_FIRST_ANOM)
        snprintf(buf, sizeof buf, "__tcc_vt_anon%d_%s",
                 tag - SYM_FIRST_ANOM, ifc->name);
    else
        snprintf(buf, sizeof buf, "__tcc_vt_%s_%s", get_tok_str(tag, NULL),
                 ifc->name);
    a = tcc_mallocz(sizeof *a);
    a->iface = ifc;
    a->type_tag = tag;
    a->vt_tok = tok_alloc_const(buf);
    /* a forward reference (extern, so it is no definition yet), defined by
       c6_emit_auto_impls() at the end of the unit */
    vtt.t = VT_STRUCT | VT_STATIC | VT_CONSTANT | VT_EXTERN;
    vtt.ref = vt;
    memset(&ad, 0, sizeof ad);
    a->vt_sym = external_sym(a->vt_tok, &vtt, 0, &ad);
    dynarray_add(&c6_auto_impls, &c6_nb_auto_impls, a);
    return a;
}

/* emit 'static const struct IFACE_vt __tcc_vt_TYPE_IFACE = { m... };' for
   every conversion that was used in this unit; returns how many were emitted */
static int c6_emit_auto_impls(void)
{
    C6AutoImpl *a;
    C6Iface *ifc;
    TokenString *ts;
    char *saved_pkg;
    int i, j, n = 0;

    if (!c6_nb_auto_impls)
        return 0;
    /* the object name is fixed: a file level 'package NAME;' must not
       mangle it a second time */
    saved_pkg = tcc_state->c6_package;
    tcc_state->c6_package = NULL;
    for (i = 0; i < c6_nb_auto_impls; i++) {
        a = c6_auto_impls[i];
        if (a->vt_emitted)
            continue;
        a->vt_emitted = 1;
        n++;
        ifc = a->iface;
        ts = tok_str_alloc();
        tok_str_add(ts, TOK_STATIC);
        tok_str_add(ts, TOK_CONST1);
        tok_str_add(ts, TOK_STRUCT);
        tok_str_add(ts, ifc->vt_tag);
        tok_str_add(ts, a->vt_tok);
        tok_str_add(ts, '=');
        tok_str_add(ts, '{');
        for (j = 0; j < ifc->nb_methods; j++) {
            if (j)
                tok_str_add(ts, ',');
            tok_str_add(ts, '(');
            tok_str_add(ts, TOK_VOID);
            tok_str_add(ts, '(');
            tok_str_add(ts, '*');
            tok_str_add(ts, ')');
            tok_str_add(ts, '(');
            tok_str_add(ts, TOK_VOID);
            tok_str_add(ts, ')');
            tok_str_add(ts, ')');
            tok_str_add(ts, c6_method_of(a->type_tag,
                                             ifc->methods[j])->sym_tok);
        }
        tok_str_add(ts, '}');
        tok_str_add(ts, ';');
        tok_str_add(ts, TOK_EOF);
        c6_decl_tokens(ts);
    }
    tcc_state->c6_package = saved_pkg;
    return n;
}

/* ------------------------------------------------------------------ */
/* slices 'T[:]'                                                       */
/*                                                                     */
/* A slice is a two word view of a run of elements:                   */
/*                                                                     */
/*     struct { T *const data; const size_t len; }                    */
/*                                                                     */
/* The compiler builds that struct by hand (fixed layout, no packing)  */
/* and the runtime helpers in libtcc1.c take the very same shape, so a */
/* slice can be passed to and returned from another translation unit   */
/* without any extra declaration.                                      */

/* a size_t value: exactly the type 'sizeof' produces */
static CType c6_size_type(void)
{
    CType t;
    t.t = VT_SIZE_T;
    t.ref = NULL;
    return t;
}

/* the value type of slice SL */
static CType c6_slice_ctype(C6Slice *sl)
{
    CType t;
    t.t = VT_STRUCT;
    t.ref = sl->tag;
    return t;
}

/* like mk_pointer() but the pointer symbol always goes to the global stack:
   a slice type is shared by the whole translation unit, so it must not point
   at a symbol that is popped when the enclosing block ends */
static void c6_global_pointer(CType *type)
{
    Sym *s = sym_push2(&global_stack, SYM_FIELD, 0, -1);
    s->type = *type;
    s->type.gq = 0;         /* the pointee marker: a plain pointer */
    type->t = VT_PTR | (type->t & VT_STORAGE);
    type->ref = s;
    type->gq = 0;
}

/* is the tag of TYPE (through one level of pointer) reachable from file
   scope?  A tag declared inside a function is popped with its block while
   the slice type lives until the end of the unit. */
static int c6_tag_is_global(CType *type)
{
    Sym *s;
    while ((type->t & VT_BTYPE) == VT_PTR && type->ref)
        type = pointed_type(type);
    if ((type->t & VT_BTYPE) != VT_STRUCT && !IS_ENUM(type->t))
        return 1;
    for (s = global_stack; s; s = s->prev)
        if (s == type->ref)
            return 1;
    return 0;
}

static int c6_is_error_type(CType *type)
{
    Sym *tag;
    while (((type->t & VT_BTYPE) == VT_PTR || (type->t & VT_ARRAY))
           && type->ref)
        type = pointed_type(type);
    if ((type->t & VT_BTYPE) != VT_STRUCT || !type->ref)
        return 0;
    tag = struct_find(tok_alloc_const("__tcc_error"));
    return tag && type->ref == tag;
}

/* Build (or find) the slice type whose elements are ELEM. */
static C6Slice *c6_slice_get(CType *elem)
{
    C6Slice *sl;
    CType el = *elem;
    int size, align;

    el.t &= ~VT_STORAGE;
    if ((el.t & VT_BTYPE) == VT_VOID)
        tcc_error("'[:]' cannot have a 'void' element type");
    if ((el.t & VT_BTYPE) == VT_FUNC)
        tcc_error("'[:]' cannot have a function element type");
    if (el.t & VT_VLA)
        tcc_error("'[:]' cannot have a variable length array element");
    /* the documented rule: top level qualifiers of the element are dropped,
       the slice itself controls writability through 'data' being const */
    el.t &= ~VT_QUAL;
    size = type_size(&el, &align);
    if (size < 0) {
        char buf[256];
        type_to_str(buf, sizeof buf, &el, NULL);
        tcc_error("'[:]' requires a complete element type, not '%s'", buf);
    }
    if (c6_is_error_type(&el))
        tcc_error("'error' can only be a return type, not a slice element");
    if (!c6_tag_is_global(&el)) {
        char buf[256];
        type_to_str(buf, sizeof buf, &el, NULL);
        tcc_error("the element type of '[:]' must be declared at file "
                  "scope, but '%s' is local", buf);
    }

    for (sl = c6_slices; sl; sl = sl->next)
        if (is_compatible_types(&sl->elem, &el))
            return sl;

    sl = tcc_mallocz(sizeof *sl);
    sl->elem = el;
    sl->elem_ptr = el;
    c6_global_pointer(&sl->elem_ptr);
    sl->elem_size = size;
    /* 'struct { elem *const data; const size_t len; }' */
    sl->tag = sym_push2(&global_stack, anon_sym++ | SYM_STRUCT, 0,
                        2 * PTR_SIZE);
    sl->tag->type.t = VT_STRUCT;
    sl->tag->type.ref = NULL;
    sl->tag->c = 2 * PTR_SIZE;   /* size */
    sl->tag->r = PTR_SIZE;       /* alignment */
    /* fields in declaration order: struct field chains use 'next', which is
       not the link sym_push2 builds, so it is wired up explicitly */
    {
        CType lt = c6_size_type(), dt = sl->elem_ptr;
        Sym *scratch = NULL, *data_f, *len_f;
        lt.t |= VT_CONSTANT;
        dt.t |= VT_CONSTANT;
        data_f = sym_push2(&scratch, tok_alloc_const("data") | SYM_FIELD,
                           0, 0);
        data_f->type = dt;
        data_f->c = 0;
        data_f->r = 0;
        len_f = sym_push2(&scratch, tok_alloc_const("len") | SYM_FIELD,
                          0, PTR_SIZE);
        len_f->type = lt;
        len_f->c = PTR_SIZE;
        len_f->r = 0;
        data_f->next = len_f;
        len_f->next = NULL;
        sl->tag->next = data_f;
    }
    sl->next = c6_slices;
    c6_slices = sl;
    return sl;
}

/* Build (or find) the tuple type whose elements are ELEMS[0..N-1].  The
   internal struct is laid out here with plain natural alignment (each
   element aligned to its own alignment, the size rounded up to the largest)
   instead of going through struct_layout(): '#pragma pack' must not change
   the type, because this layout is the ABI shared with every other
   translation unit. */
static C6Tuple *c6_tuple_get(CType *elems, int n)
{
    C6Tuple *tp;
    CType *el;
    char buf[256], name[64];
    int i, j, size, align, off, maxalign;

    if (n < 2)
        tcc_error("a tuple needs at least two elements");
    el = tcc_mallocz(sizeof(CType) * n);
    for (i = 0; i < n; i++) {
        el[i] = elems[i];
        el[i].t &= ~VT_STORAGE;
        /* like a slice, the tuple owns writability: top level qualifiers of
           an element cannot be honoured by a whole tuple copy, so they are
           dropped */
        el[i].t &= ~VT_QUAL;
        if ((el[i].t & VT_BTYPE) == VT_VOID)
            tcc_error("'tuple' element cannot be 'void'");
        if ((el[i].t & VT_BTYPE) == VT_FUNC)
            tcc_error("'tuple' element cannot be a function type");
        if (el[i].t & VT_VLA)
            tcc_error("'tuple' element cannot be a variable length array");
        if (el[i].t & VT_ARRAY)
            tcc_error("'tuple' element cannot be an array type "
                      "(use a pointer, a slice 'T[:]' or a struct)");
        size = type_size(&el[i], &align);
        if (size < 0) {
            type_to_str(buf, sizeof buf, &el[i], NULL);
            tcc_error("'tuple' requires a complete element type, not '%s'",
                      buf);
        }
        if (c6_is_error_type(&el[i]))
            tcc_error("'error' can only be a return type, not a tuple element");
        if (!c6_tag_is_global(&el[i])) {
            type_to_str(buf, sizeof buf, &el[i], NULL);
            tcc_error("the element type of a tuple must be declared at file "
                      "scope, but '%s' is local", buf);
        }
    }
    for (tp = c6_tuples; tp; tp = tp->next) {
        if (tp->n != n)
            continue;
        for (j = 0; j < n; j++)
            if (!is_compatible_types(&tp->elem[j], &el[j]))
                break;
        if (j == n) {
            tcc_free(el);
            return tp;
        }
    }

    tp = tcc_mallocz(sizeof *tp);
    tp->n = n;
    tp->elem = el;
    tp->off = tcc_mallocz(sizeof(int) * n);
    /* a fresh internal tag.  It is a *named* tag on purpose: the async/go
       signature serializer spells a struct type back as 'struct NAME', so a
       searchable name makes a tuple usable there with no special case. */
    for (;;) {
        snprintf(name, sizeof name, "__c6_tuple_%d", c6_tuple_seq++);
        if (!struct_find(tok_alloc_const(name)))
            break;
    }
    tp->tag = sym_push2(&global_stack, tok_alloc_const(name) | SYM_STRUCT, 0, 0);
    tp->tag->type.t = VT_STRUCT;
    tp->tag->type.ref = NULL;
    {
        Sym *scratch = NULL, *prev = NULL, *f;
        off = 0;
        maxalign = 1;
        for (i = 0; i < n; i++) {
            size = type_size(&tp->elem[i], &align);
            off = (off + align - 1) & -align;
            tp->off[i] = off;
            if (align > maxalign)
                maxalign = align;
            snprintf(name, sizeof name, "__c6_t%d", i);
            f = sym_push2(&scratch, tok_alloc_const(name) | SYM_FIELD, 0, 0);
            f->type = tp->elem[i];
            f->c = off;
            f->r = 0;
            f->next = NULL;
            if (prev)
                prev->next = f;
            else
                tp->tag->next = f;
            prev = f;
            off += size;
        }
        tp->tag->c = (off + maxalign - 1) & -maxalign;
        tp->tag->r = maxalign;
    }
    sym_link(tp->tag, 1);           /* enter it in the tag namespace */
    tp->tag->sym_scope = 0;         /* ... and keep it out of any block */
    tp->next = c6_tuples;
    c6_tuples = tp;
    return tp;
}

/* Spill the slice or complete array value on vtop into a local
   '{ void *data; size_t len; }' pair and pop it.  Returns the offset of the
   pair, so that the runtime helpers can read both words from it (this is
   also the only way to keep the base value while the bounds are evaluated:
   local slots survive the calls that a bound expression may generate). */
static int c6_slice_spill(C6Slice *sl)
{
    CType sz = c6_size_type();
    int off = (loc - 2 * PTR_SIZE) & -PTR_SIZE;

    loc = off;
    if (sl) {
        CType st = c6_slice_ctype(sl);
        /* the struct layout is exactly the runtime pair */
        vset(&st, VT_LOCAL | VT_LVAL, off);
        vswap();
        vstore();
        vpop();
    } else {
        CType ept = *pointed_type(&vtop->type);
        int n = vtop->type.ref->c;
        mk_pointer(&ept);
        vtop->type = ept;
        gaddrof();                       /* 'elem *' pointing at the first */
        vset(&ept, VT_LOCAL | VT_LVAL, off);
        vswap();
        vstore();
        vpop();
        vpushs(n);
        vset(&sz, VT_LOCAL | VT_LVAL, off + PTR_SIZE);
        vswap();
        vstore();
        vpop();
    }
    return off;
}

/* f(.., a, b, ..): push the pointer to local slot OFF as an argument */
static void c6_slice_push_addr(int off)
{
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, off);
    gaddrof();
}

/* complain about a range applied to something that is neither a slice nor a
   complete array */
static void c6_slice_base_error(CType *base)
{
    if (base->t & VT_VLA)
        tcc_error("cannot slice a variable length array");
    if (base->t & VT_ARRAY)
        tcc_error("cannot slice an array of unknown length");
    if ((base->t & VT_BTYPE) == VT_PTR)
        tcc_error("cannot slice a pointer; use '(T[:]){p, n}'");
    tcc_error("cannot slice this expression");
}

/* Parse the postfix '[' of an expression, with the token already at '['.
   Consumes '[' ... ']' and leaves the addressed element lvalue (or, for a
   range, the resulting slice value) on the value stack.  A plain subscript
   of an array or of a pointer keeps the original code generation. */
static void c6_slice_subscript(void)
{
    C6Slice *sl, *rsl;
    CType sz = c6_size_type(), base, array_elem;
    int have_first = 0, have_array_elem = 0, first_off = 0, pair, res;

    next();                              /* '[' */
    base = vtop->type;
    sl = c6_slice_of_type(&base);
    if ((base.t & VT_ARRAY) && !(base.t & VT_VLA)
        && base.ref && base.ref->c >= 0) {
        /* remember the element type: the base ends up below the bounds once
           the first expression has been parsed */
        array_elem = *pointed_type(&base);
        have_array_elem = 1;
    }
    if (tok == ':' && !sl && !have_array_elem)
        c6_slice_base_error(&base);

    if (tok != ':') {
        gexpr();                         /* index or low bound */
        have_first = 1;
    }
    if (tok != ':') {
        if (!sl) {
            /* plain C subscript: byte for byte the original path */
            gen_op('+');
            indir();
            skip(']');
            return;
        }
        /* bounds checked slice index */
        first_off = (loc - PTR_SIZE) & -PTR_SIZE;
        loc = first_off;
        vset(&sz, VT_LOCAL | VT_LVAL, first_off);
        vswap();
        vstore();                        /* also converts to size_t */
        vpop();
        skip(']');
        pair = c6_slice_spill(sl);
        res = (loc - PTR_SIZE) & -PTR_SIZE;
        loc = res;
        /* __tcc_slice_index(&ptr, &pair, idx, elem_size) */
        vpush_helper_func(tok_alloc_const("__tcc_slice_index"));
        c6_slice_push_addr(res);
        c6_slice_push_addr(pair);
        vset(&sz, VT_LOCAL | VT_LVAL, first_off);
        vpushs(sl->elem_size);
        gfunc_call(4);
        vset(&sl->elem_ptr, VT_LOCAL | VT_LVAL, res);
        indir();
        return;
    }

    /* a range: keep the bounds in scratch slots, so that the base value is
       back on top of the stack when it is spilled below */
    if (!sl && !have_array_elem)
        c6_slice_base_error(&base);
    rsl = sl;
    if (!rsl)
        rsl = c6_slice_get(&array_elem);   /* the array decays */
    {
        CType rt = c6_slice_ctype(rsl);
        int lo_off, hi_off = 0, have_hi = 0;

        lo_off = (loc - PTR_SIZE) & -PTR_SIZE;
        loc = lo_off;
        if (!have_first)
            vpushi(0);                   /* lo defaults to 0 */
        vset(&sz, VT_LOCAL | VT_LVAL, lo_off);
        vswap();
        vstore();
        vpop();
        next();                          /* ':' */
        if (tok != ']') {
            gexpr();                     /* high bound */
            hi_off = (loc - PTR_SIZE) & -PTR_SIZE;
            loc = hi_off;
            vset(&sz, VT_LOCAL | VT_LVAL, hi_off);
            vswap();
            vstore();
            vpop();
            have_hi = 1;
        }
        if (tok == ':')
            tcc_error("slice bounds have no capacity");
        skip(']');
        pair = c6_slice_spill(sl);
        res = (loc - 2 * PTR_SIZE) & -PTR_SIZE;
        loc = res;
        /* __tcc_slice_sub(&out, &pair, lo, hi, elem_size) */
        vpush_helper_func(tok_alloc_const("__tcc_slice_sub"));
        c6_slice_push_addr(res);
        c6_slice_push_addr(pair);
        vset(&sz, VT_LOCAL | VT_LVAL, lo_off);
        if (have_hi)
            vset(&sz, VT_LOCAL | VT_LVAL, hi_off);
        else
            vset(&sz, VT_LOCAL | VT_LVAL, pair + PTR_SIZE);  /* hi = len */
        vpushs(rsl->elem_size);
        gfunc_call(5);
        vset(&rt, VT_LOCAL | VT_LVAL, res);
    }
}

/* 't[K]' on a tuple: K must be a constant index and the element is picked
   statically, exactly like a struct member.  Returns 1 when the base was a
   tuple and the subscript has been consumed, 0 to leave the expression to
   the ordinary array/pointer subscript. */
static int c6_tuple_subscript(void)
{
    C6Tuple *tp;
    int idx, qualifiers;

    if (!tcc_state->c6_ext || vtop < vstack)
        return 0;
    tp = c6_tuple_of_type(&vtop->type);
    if (!tp)
        return 0;
    next();                             /* '[' */
    c6_fallible_use();                  /* 'f()[0]' consumes f's result */
    idx = expr_const();
    if (idx < 0 || idx >= tp->n)
        tcc_error("tuple index %d is out of range (0..%d)", idx, tp->n - 1);
    skip(']');
    qualifiers = vtop->type.t & VT_QUAL;
    test_lvalue();
    /* add the element offset to the address, then become the element */
    gaddrof();
    vtop->type = char_pointer_type;
    vpushi(tp->off[idx]);
    gen_op('+');
    vtop->type = tp->elem[idx];
    if (qualifiers)
        parse_btype_qualify(&vtop->type, qualifiers);
    /* an array is never an lvalue */
    if (!(vtop->type.t & VT_ARRAY)) {
        vtop->r |= VT_LVAL;
#ifdef CONFIG_TCC_BCHECK
        if (tcc_state->do_bounds_check)
            vtop->r |= VT_MUSTBOUND;
#endif
    }
    return 1;
}

/* ---------- tuple patterns: '(int q, int r) = f();', '(q, r) = f();' ---- */

#define C6_TK_EXPR    0
#define C6_TK_TYPE    1
#define C6_TK_PATTERN 2
#define C6_TK_ASSIGN  3

/* Append the recorded token stream TS to OUT. */
static void c6_tok_append(TokenString *out, TokenString *ts)
{
    int save_len = ts->len, save_tok = tok;
    CValue save_val = tokc;

    begin_macro(ts, 0);
    for (;;) {
        next();
        if (tok == TOK_EOF)
            break;
        tok_str_add_tok(out);
    }
    end_macro();
    ts->len = save_len;
    tok = save_tok;
    tokc = save_val;
}

/* Non-zero when the recorded element TS is exactly the '_' placeholder. */
static int c6_tok_is_discard(TokenString *ts)
{
    int disc = 0, save_len = ts->len, save_tok = tok;
    CValue save_val = tokc;

    begin_macro(ts, 0);
    next();
    if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, NULL), "_")) {
        next();
        disc = tok == TOK_EOF;
    }
    end_macro();
    ts->len = save_len;
    tok = save_tok;
    tokc = save_val;
    return disc;
}

/* Record the tokens of one element of a tuple pattern, up to (but not
   including) the ',' or ')' that ends it, and tell whether it is the '_'
   placeholder.  In the declaration form the element is also parsed as
   'type-name [declarator]' (the declarator may be absent or '_'), giving
   *TYPE and *NAME; *NAME is 0 when the element is a bare '_'.  Returns 0
   when the element does not fit. */
static int c6_tuple_elem(TokenString *ts, CType *type, int *name, int decl)
{
    AttributeDef ad;
    int depth = 0, ok = 1, save_len, save_tok;
    CValue save_val;

    *name = 0;
    for (;;) {
        if (tok == TOK_EOF)
            return 0;
        if (depth == 0 && (tok == ',' || tok == ')'))
            break;
        if (tok == '(' || tok == '[' || tok == '{')
            depth++;
        else if (tok == ')' || tok == ']' || tok == '}')
            depth--;
        tok_str_add_tok(ts);
        next();
    }
    tok_str_add(ts, TOK_EOF);
    if (!decl)
        return 1;
    save_len = ts->len, save_tok = tok, save_val = tokc;
    begin_macro(ts, 0);
    next();
    memset(&ad, 0, sizeof ad);
    ok = parse_btype(type, &ad, 0) && !(type->t & VT_STORAGE);
    if (ok) {
        /* both flags: take a name when there is one, but never diagnose a
           missing one here (the replays must stay silent) */
        type_decl(type, &ad, name, TYPE_ABSTRACT | TYPE_DIRECT);
        ok = tok == TOK_EOF && *name;   /* a type and a name, all consumed */
    }
    end_macro();
    ts->len = save_len;
    tok = save_tok;
    tokc = save_val;
    if (!ok && c6_tok_is_discard(ts))
        ok = 1;                         /* a bare '_' is a pattern element */
    return ok;
}

/* Classify a statement that starts with '(' at block level.  Only the tokens
   are looked at: whatever the answer, the stream is left exactly where it
   was.  '(int,int) t = ..' is an ordinary declaration (C6_TK_TYPE),
   '(int q, int r) = ..' a destructuring declaration, '(q, r) = ..' a
   destructuring assignment, everything else is an expression. */
static int c6_tuple_lead_kind(void)
{
    TokenString *back = tok_str_alloc();
    TokenString *replay = tok_str_alloc();
    CType t;
    AttributeDef ad;
    int depth = 0, comma = 0, broken = 0, after = 0, nall = 0, nnames = 0;
    int ntypes = 0, nexpr = 0, name, kind;

    for (;;) {
        tok_str_add_tok(back);
        tok_str_add_tok(replay);
        if (tok == '(' || tok == '[') {
            depth++;
        } else if (tok == ']') {
            if (depth > 1)
                depth--;
        } else if (tok == ')') {
            if (--depth == 0)
                break;
        } else if (tok == '{' || tok == '}') {
            broken = 1;
        } else if (tok == ',' && depth == 1) {
            comma = 1;
        } else if (tok == TOK_EOF) {
            broken = 1;
            break;
        }
        next();
    }
    if (!broken) {
        next();                         /* step over the recorded ')' */
        after = tok;                    /* ... what follows the group */
        tok_str_add_tok(back);
        tok_str_add_tok(replay);
    }
    tok_str_add(back, 0);
    tok_str_add(replay, TOK_EOF);
    if (broken) {
        tok_str_free(replay);
        kind = C6_TK_EXPR;
    } else {
        begin_macro(replay, 1);
        next();                         /* '(' */
        next();
        for (;;) {
            int ek = 0;                 /* 0: not a type, 1: type, 2: type+name, 3: '_' */
            memset(&ad, 0, sizeof ad);
            if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, NULL), "_")) {
                next();
                if (tok == ',' || tok == ')')
                    ek = 3;
            }
            if (!ek && parse_btype(&t, &ad, 0) && !(t.t & VT_STORAGE)) {
                type_decl(&t, &ad, &name, TYPE_ABSTRACT | TYPE_DIRECT);
                if (tok == ',' || tok == ')')
                    ek = name ? 2 : 1;
            }
            nall++;
            if (ek == 1 || ek == 2)
                ntypes++;
            if (ek >= 2)
                nnames++;
            if (!ek)
                nexpr++;
            if (tok == ',') {
                next();
                continue;
            }
            if (tok == ')')
                break;
            /* not a type: skip the rest of this element, it is an
               expression (a destructuring assignment target) */
            while (tok != TOK_EOF && tok != ',' && tok != ')') {
                if (tok == '(' || tok == '[' || tok == '{')
                    depth++;
                else if (tok == ')' || tok == ']' || tok == '}')
                    depth--;
                next();
            }
            if (tok == ',') {
                next();
                continue;
            }
            break;
        }
        end_macro();
        if (nall >= 2 && nexpr == 0 && nnames == 0)
            kind = C6_TK_TYPE;
        else if (nall >= 2 && nexpr == 0 && ntypes > 0 && nnames == nall
                 && after == '=')
            kind = C6_TK_PATTERN;
        else if (ntypes == 0 && after == '=' && comma)
            kind = C6_TK_ASSIGN;
        else
            kind = C6_TK_EXPR;
    }
    /* hand the tokens back untouched: every caller parses them itself */
    begin_macro(back, 1);
    next();
    return kind;
}

/* Rewrite '(int q, int _) = f();' into 'int q = tmp.__c6_t0;' and
   '(q, r) = f();' into 'q = tmp.__c6_t0;' / 'r = tmp.__c6_t1;' statements,
   after checking that the value is a tuple of the very same arity and
   spilling it into a hidden temporary.  DECL_FORM selects the declarations
   of a pattern from the lvalues of a destructuring assignment. */
static void c6_tuple_destructure(int decl_form)
{
    TokenString *out = tok_str_alloc();
    TokenString **elems = NULL;
    int *names = NULL, *disc = NULL;
    CType *etypes = NULL, tt;
    int n = 0, max = 0, i, ndisc = 0, ttok, off, size, align;
    char buf[128];
    C6Tuple *tp;

    next();                             /* '(' */
    for (;;) {
        TokenString *ts = tok_str_alloc();
        CType et;
        int name = 0, d;
        memset(&et, 0, sizeof et);
        if (!c6_tuple_elem(ts, &et, &name, decl_form))
            tcc_error("a tuple pattern element must be a type and a name");
        d = c6_tok_is_discard(ts)
            || (name && !strcmp(get_tok_str(name, NULL), "_"));
        if (n == max) {
            max = max ? 2 * max : 4;
            elems = tcc_realloc(elems, max * sizeof *elems);
            names = tcc_realloc(names, max * sizeof *names);
            disc = tcc_realloc(disc, max * sizeof *disc);
            etypes = tcc_realloc(etypes, max * sizeof *etypes);
        }
        elems[n] = ts, names[n] = name, disc[n] = d, etypes[n] = et;
        n++;
        if (tok == ',') {
            next();
            continue;
        }
        break;
    }
    skip(')');
    skip('=');
    expr_eq();
    tp = c6_tuple_of_type(&vtop->type);
    if (!tp)
        tcc_error("destructuring requires a tuple value");
    c6_fallible_use();
    if (tp->n != n)
        tcc_error("the tuple has %d elements but the pattern has %d",
                  tp->n, n);
    for (i = 0; i < n; i++)
        ndisc += disc[i];
    if (ndisc == n)
        tcc_error("'tuple' discards every element");
    tt = vtop->type;
    /* hold the value in a hidden temporary; the element statements below
       read it back field by field */
    size = type_size(&tt, &align);
    if (size < 0)
        tcc_error("destructuring requires a complete tuple type");
    loc = (loc - size) & -align;
    off = loc;
    vset(&tt, VT_LOCAL | VT_LVAL, off);
    vswap();
    vstore();
    vpop();
    for (;;) {
        snprintf(buf, sizeof buf, "__c6_tuple_tmp%d", c6_tuple_tmp_seq++);
        ttok = tok_alloc_const(buf);
        if (!sym_find(ttok))
            break;
    }
    sym_push(ttok, &tt, VT_LOCAL | VT_LVAL, off);
    skip(';');
    for (i = 0; i < n; i++) {
        if (disc[i])
            continue;
        c6_tok_append(out, elems[i]);
        tok_str_add(out, '=');
        tok_str_add(out, ttok);
        tok_str_add(out, '.');
        snprintf(buf, sizeof buf, "__c6_t%d", i);
        tok_str_add(out, tok_alloc_const(buf));
        tok_str_add(out, ';');
    }
    /* the token that follows the ';' is already read: it must travel with
       the pushed back statements */
    tok_str_add_tok(out);
    tok_str_add(out, 0);
    /* let the ordinary declaration / assignment code do the rest */
    begin_macro(out, 1);
    next();
    if (!decl_form) {
        /* 'target = tmp.__c6_tN;' is an ordinary assignment, so parse the
           pushed back statements right here.  An unbraced if/while/for body
           pops its scope as soon as the statement is done, and the hidden
           temporary would be gone by the time the caller got around to
           reading the pushed back tokens.  The declaration form does not
           need this: its statements start with a type, so the decl() that
           called us consumes them in its own loop before returning. */
        for (i = 0; i < n; i++) {
            if (disc[i])
                continue;
            expr_eq();                  /* the value of the assignment */
            vpop();
            skip(';');
        }
    }
    for (i = 0; i < n; i++)
        tok_str_free(elems[i]);
    tcc_free(elems), tcc_free(names), tcc_free(disc), tcc_free(etypes);
}

/* Implicit conversion of a complete array to a slice value, e.g.
   'int[:] s = a;', 'f(a)' or 'return a;'.  Returns 0 when no conversion
   applies. */
static int c6_slice_convert(CType *dt)
{
    C6Slice *sl;
    CType *se;
    char b1[256], b2[256];

    if (!tcc_state->c6_ext)
        return 0;
    sl = c6_slice_of_type(dt);
    if (!sl)
        return 0;
    {
        /* a slice stays a slice: plain struct copy, but only of its own
           element type */
        C6Slice *src = c6_slice_of_type(&vtop->type);
        if (src) {
            if (src != sl) {
                type_to_str(b1, sizeof b1, &vtop->type, NULL);
                type_to_str(b2, sizeof b2, dt, NULL);
                tcc_error("cannot convert '%s' to '%s'", b1, b2);
            }
            return 0;
        }
    }
    if (vtop->type.t & VT_VLA)
        tcc_error("cannot slice a variable length array");
    if ((vtop->type.t & VT_ARRAY) && vtop->type.ref->c < 0)
        tcc_error("cannot slice an array of unknown length");
    if (vtop->type.t & VT_ARRAY) {
        se = pointed_type(&vtop->type);
        if (!is_compatible_unqualified_types(se, &sl->elem)) {
            type_to_str(b1, sizeof b1, se, NULL);
            type_to_str(b2, sizeof b2, &sl->elem, NULL);
            tcc_error("cannot make a slice of '%s' from an array of '%s'",
                      b2, b1);
        }
        if (nocode_wanted & NOEVAL_WANTED) {
            /* 'sizeof((T[:])a)' / 'typeof': only the type matters and the
               value is discarded, so no code has to be generated */
            vtop->type = c6_slice_ctype(sl);
            return 1;
        }
        if (nocode_wanted & DATA_ONLY_WANTED)
            tcc_error("a slice cannot be initialized from an array in a "
                      "static initializer; use '{ data, len }'");
        {
            CType ept = sl->elem_ptr, st = c6_slice_ctype(sl);
            CType sz = c6_size_type();
            int n = vtop->type.ref->c;
            int off = (loc - 2 * PTR_SIZE) & -PTR_SIZE;
            loc = off;
            vtop->type = ept;
            gaddrof();                   /* &a[0] */
            vset(&ept, VT_LOCAL | VT_LVAL, off);
            vswap();
            vstore();
            vpop();
            vpushs(n);
            vset(&sz, VT_LOCAL | VT_LVAL, off + PTR_SIZE);
            vswap();
            vstore();
            vpop();
            vset(&st, VT_LOCAL | VT_LVAL, off);
        }
        return 1;
    }
    if ((vtop->type.t & VT_BTYPE) == VT_PTR)
        tcc_error("cannot make a slice from a pointer; use '(T[:]){p, n}'");
    return 0;
}

/* drop everything that must not survive into the next translation unit.
   The scanner restarts its token table for every unit, and the state below
   holds tokens and descriptors that would otherwise dangle or leak into the
   next unit's lookup (imports, interfaces, methods, vtables).  The 'test'
   entries are deliberately kept: '-test' collects them across units. */
static void c6_tu_reset(void)
{
    C6Method *m, *next;
    int i;

    while (c6_sp_pending) {
        C6SpPending *p = c6_sp_pending;
        c6_sp_pending = p->next;
        tcc_free(p);
    }

    dynarray_reset(&c6_auto_impls, &c6_nb_auto_impls);
    for (m = c6_methods; m; m = next) {
        next = m->next;
        tcc_free(m);
    }
    c6_methods = NULL;
    c6_nb_methods = 0;
    for (i = 0; i < tcc_state->c6_nb_ifaces; i++) {
        C6Iface *ifc = tcc_state->c6_ifaces[i];
        if (!ifc)
            continue;
        tcc_free(ifc->name);
        tcc_free(ifc->methods);
        /* the descriptor itself goes with the array */
    }
    dynarray_reset(&tcc_state->c6_ifaces, &tcc_state->c6_nb_ifaces);
    dynarray_reset(&tcc_state->c6_imports, &tcc_state->c6_nb_imports);
    dynarray_reset(&tcc_state->c6_loaded_modules,
                   &tcc_state->c6_nb_loaded_modules);
    dynarray_reset(&tcc_state->c6_loading_modules,
                   &tcc_state->c6_nb_loading_modules);
    dynarray_reset(&tcc_state->c6_header_decls,
                   &tcc_state->c6_nb_header_decls);
    for (i = 0; i < tcc_state->c6_nb_gc_types; i++) {
        C6GcType *gt = tcc_state->c6_gc_types[i];
        if (!gt)
            continue;
        tcc_free(gt->fields);
        tcc_free(gt->src_file);
    }
    dynarray_reset(&tcc_state->c6_gc_types, &tcc_state->c6_nb_gc_types);
    tcc_free(tcc_state->c6_gc_edges);
    tcc_state->c6_gc_edges = NULL;
    tcc_state->c6_nb_gc_edges = tcc_state->c6_gc_edges_cap = 0;
    c6_importing = 0;
    c6_expected_module = NULL;
    tcc_free(tcc_state->c6_package);
    tcc_state->c6_package = NULL;
    c6_self_pending = 0;
    /* the slice descriptors hold Sym pointers into the symbol pools that
       tccgen_finish() has released: they must not be looked at again */
    while (c6_slices) {
        C6Slice *sl = c6_slices;
        c6_slices = sl->next;
        tcc_free(sl);
    }
    c6_slices = NULL;
    /* same for the tuple descriptors */
    while (c6_tuples) {
        C6Tuple *tp = c6_tuples;
        c6_tuples = tp->next;
        tcc_free(tp->elem);
        tcc_free(tp->off);
        tcc_free(tp);
    }
    c6_tuples = NULL;
    c6_tuple_seq = 0;
    c6_tuple_tmp_seq = 0;
}

/* Implicit conversion of a struct value to an interface value, e.g.
   'render(circle)' or 'Drawable d = circle;'.  Returns 0 when no
   conversion applies. */
static int c6_iface_convert(CType *dt)
{
    C6Iface *ifc;
    C6AutoImpl *a;
    Sym *vs;
    int size, align, loc2;

    if (!tcc_state->c6_ext || !tcc_state->c6_nb_ifaces)
        return 0;
    if ((vtop->type.t & VT_BTYPE) != VT_STRUCT || !vtop->type.ref)
        return 0;
    ifc = c6_iface_of_type(dt);
    if (!ifc)
        return 0;
    /* an interface value stays an interface value: plain copy */
    if (c6_iface_of_type(&vtop->type))
        return 0;
    /* a file scope initializer has no runtime code to build the value */
    if (nocode_wanted)
        return 0;
    a = c6_auto_impl_ensure(ifc, vtop->type.ref->v & ~SYM_STRUCT);
    if (!a)
        return 0;
    vs = a->vt_sym;

    size = type_size(dt, &align);
    loc2 = (loc - size) & -align;
    loc = loc2;

    /* __tcc_iface_init(&src, &vtable, &tmp) */
    gaddrof();                       /* &source struct */
    vtop->type = char_pointer_type;
    vpushsym(&vs->type, vs);
    vtop->r |= VT_LVAL;
    gaddrof();                       /* &vtable */
    vtop->type = char_pointer_type;
    vset(dt, VT_LOCAL | VT_LVAL, loc2);
    gaddrof();                       /* &tmp */
    vtop->type = char_pointer_type;
    vpush_helper_func(tok_alloc_const("__tcc_iface_init"));
    vrott(4);
    gfunc_call(3);

    vset(dt, VT_LOCAL | VT_LVAL, loc2);
    return 1;
}


static int c6_ext_on(void)
{
    return tcc_state->c6_ext;
}

static Sym *c6_new_label(const char *tag)
{
    char buf[64];
    int v;
    snprintf(buf, sizeof(buf), "__tcc_%s_%d", tag, ++c6_label_seq);
    v = tok_alloc_const(buf);
    return label_push(&global_label_stack, v, LABEL_FORWARD);
}

static void c6_def_label(Sym *s)
{
    s->r = LABEL_DEFINED;
    s->jind = gind();
    s->cleanupstate = cur_scope->cl.s;
}

static void c6_add_site(int id, Sym *label)
{
    C6DeferSite *s = tcc_malloc(sizeof *s);
    s->id = id;
    s->label = label;
    s->next = NULL;
    *defer_state.sites_tail = s;
    defer_state.sites_tail = &s->next;
}

static void c6_add_resume(int id, Sym *label)
{
    C6DeferResume *r = tcc_malloc(sizeof *r);
    r->id = id;
    r->label = label;
    r->next = NULL;
    *defer_state.resumes_tail = r;
    defer_state.resumes_tail = &r->next;
}

static void c6_free_lists(void)
{
    C6DeferSite *s, *sn;
    C6DeferResume *r, *rn;
    for (s = defer_state.sites; s; s = sn) {
        sn = s->next;
        tcc_free(s);
    }
    for (r = defer_state.resumes; r; r = rn) {
        rn = r->next;
        tcc_free(r);
    }
    defer_state.sites = NULL;
    defer_state.sites_tail = &defer_state.sites;
    defer_state.resumes = NULL;
    defer_state.resumes_tail = &defer_state.resumes;
}

static void c6_defer_func_init(void)
{
    int align = PTR_SIZE;
    defer_state.enabled = 1;
    defer_state.has_sites = 0;
    defer_state.body_depth = 0;
    defer_state.next_site = 0;
    defer_state.next_resume = 0;
    defer_state.disp_jmps = 0;
    c6_free_lists();
    loc = (loc - (int)sizeof(int)) & -align;
    defer_state.sp0_off = loc;
    loc = (loc - (int)sizeof(int)) & -align;
    defer_state.step_off = loc;
    /* __tcc_defer_func_enter(&sp0); */
    vpush_helper_func(tok_alloc_const("__tcc_defer_func_enter"));
    vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.sp0_off);
    gaddrof();
    mk_pointer(&vtop->type);
    gfunc_call(1);
}

/* emit "begin cleanup" (mode 0: scopes above DEPTH, mode 1: whole function) */
static void c6_defer_begin(int mode, int depth)
{
    int rid;
    Sym *rl;
    if (!defer_state.enabled || !defer_state.has_sites || nocode_wanted)
        return;
    rid = ++defer_state.next_resume;
    rl = c6_new_label("drs");
    /* third argument: this frame's defer stack base */
    if (mode == 0) {
        vpush_helper_func(tok_alloc_const("__tcc_defer_begin_scope"));
        vpushi(depth);
        vpushi(rid);
    } else {
        vpush_helper_func(tok_alloc_const("__tcc_defer_begin_func"));
        vpushi(mode);
        vpushi(rid);
    }
    vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.sp0_off);
    gfunc_call(3);
    defer_state.disp_jmps = gjmp(defer_state.disp_jmps);
    /* resume point: execution continues right after the cleanup run */
    c6_def_label(rl);
    c6_add_resume(rid, rl);
}

/* parse "defer STATEMENT" / "errdefer STATEMENT": register a cleanup site,
   compile the body into a cleanup block that is skipped over at
   registration time.  KIND selects defer (0) or errdefer (1). */
static void c6_defer_parse(int kind)
{
    int skip = 0, id;
    Sym *l = NULL;

    defer_state.body_depth++;
    if (!nocode_wanted) {
        id = ++defer_state.next_site;
        l = c6_new_label("dcl");
        vpush_helper_func(tok_alloc_const("__tcc_defer_push"));
        vpushi(id);
        vpushi(local_scope);
        vpushi(kind);
        gfunc_call(3);
        skip = gjmp(0);
        c6_def_label(l);
        c6_add_site(id, l);
        defer_state.has_sites = 1;
    }
    block(STMT_COMPOUND);
    if (!nocode_wanted) {
        defer_state.disp_jmps = gjmp(defer_state.disp_jmps);
        gsym(skip);
    }
    defer_state.body_depth--;
}

static void c6_errdefer_parse(void)
{
    c6_defer_parse(1);
}

/* Emit the error-path exit of the current function: build the sentinel
   value of the declared return type, run this frame's errdefer/defer in
   reverse order and return the sentinel.  Used by 'err()' and by 'try'. */
/* 'memset(local + OFF, 0, SIZE)'.  An aggregate 'T!error' value cannot be
   '-1', and it does not need to be: the error is reported through the error
   slot, so the value itself is never observed. */
static void c6_zero_local(int off, int size)
{
    vpush_helper_func(TOK_memset);
    vseti(VT_LOCAL, off);
    vpushi(0);
    vpushs(size);
#if defined TCC_TARGET_ARM && defined TCC_ARM_EABI
    vswap();  /* using __aeabi_memset(void*, size_t, int) */
#endif
    gfunc_call(3);
}

static void c6_emit_propagate(void)
{
    int b, ret_off = 0, ret_saved = 0;
#ifdef C6_EXT
    if (c6_async_cur) {
        /* an async body ends the task with KIND_ERR instead of returning the
           sentinel to a caller that is the scheduler */
        c6_async_propagate();
        return;
    }
#endif

    b = (func_vt.t & VT_BTYPE) != VT_VOID;
    if (b) {
        int align, size = type_size(&func_vt, &align);
        if (size < 0)
            tcc_error("cannot propagate an error from a function with "
                      "an incomplete return type");
        loc = (loc - size) & -align;
        ret_off = loc;
        ret_saved = 1;
        if ((func_vt.t & VT_BTYPE) == VT_STRUCT) {
            c6_zero_local(ret_off, size);
        } else {
            vpushi(TCC_ERR_SENTINEL);
            vtop->type = func_vt;   /* '(TYPE)-1', no conversion warning */
            vset(&func_vt, VT_LOCAL | VT_LVAL, ret_off);
            vswap();
            vstore();
            vpop();
        }
    }
    if (defer_state.has_sites)
        c6_defer_begin(2, 0);
    if (ret_saved)
        vset(&func_vt, VT_LOCAL | VT_LVAL, ret_off);
    if (b)
        gfunc_return(&func_vt);
    rsym = gjmp(rsym);
    CODE_OFF();
}

/* parse "err(ID, MSG)": record the error, run the error-path cleanups
   (errdefer and defer) of the whole function frame and return.  'err'
   always returns immediately, so there is no re-raise form: callers that
   want to forward an error use 'try' instead. */
static void c6_err_parse(void)
{
    if (defer_state.body_depth > 0)
        tcc_error("'err' is not allowed in a defer body");
    if (!c6_func_sym || !c6_func_sym->type.ref->f.func_fallible)
        tcc_error("'err' requires the enclosing function to return 'T!error'");

    skip('(');
    if (tok == ')')
        tcc_error("'err' requires an error id and a message");
    /* __tcc_err_set(id, msg) */
    vpush_helper_func(tok_alloc_const("__tcc_err_set"));
    expr_eq();
    skip(',');
    expr_eq();
    skip(')');
    gfunc_call(2);

    c6_emit_propagate();
    skip(';');
}


/* parse 'test "description" { ... }' at file scope: synthesize
   'static int __tcc_test_N(void) { ... }' and remember the description
   so that -test can run it. */
static void c6_test_parse(void)
{
    TCCState *s1 = tcc_state;
    C6TestEntry *e;
    CType ft;
    Sym *fs, *sym;
    AttributeDef ad;
    char name[64];
    int v;

    if (c6_importing) {
        /* tests belong to the module's own translation unit */
        next();
        if (tok == TOK_STR)
            next();
        skip_or_save_block(NULL);
        return;
    }
    next();             /* skip the 'test' keyword */
    if (tok != TOK_STR)
        expect("string");
    snprintf(name, sizeof(name), "__tcc_test_%d", s1->c6_test_count);
    {
        int n = 0;
        /* the description token value is the decoded string */
        const char *d = tokc.str.data;
        while (d[n])
            n++;
        e = tcc_malloc(sizeof *e);
        e->desc = tcc_malloc(n + 1);
        memcpy(e->desc, d, n + 1);
        e->fn_name = tcc_strdup(name);
    }
    dynarray_add(&s1->c6_tests, &s1->c6_test_count, e);
    v = tok_alloc_const(name);

    next();             /* skip the description */
    if (tok != '{')
        expect("{");

    /* build the type of "int NAME(void)" */
    memset(&ad, 0, sizeof ad);
    fs = sym_push(SYM_FIELD, &int_type, 0, 0);
    fs->f.func_call = FUNC_CDECL;
    fs->f.func_type = FUNC_NEW;
    /* a test block may use 'try' to fail: its result is the error slot */
    fs->f.func_fallible = 1;
    ft.t = VT_FUNC;
    ft.ref = fs;
    sym = external_sym(v, &ft, 0, &ad);

    cur_text_section = text_section;
    c6_implicit_ret0 = 1;
    gen_function(sym);
}

/* emit the per-function cleanup dispatcher */
static void c6_defer_func_end(void)
{
    C6DeferSite *s;
    C6DeferResume *r;
    int fall;

    if (!defer_state.enabled)
        return;
    if (!defer_state.has_sites && !defer_state.resumes) {
        /* no defer/errdefer in this function: nothing to dispatch */
        c6_free_lists();
        defer_state.enabled = 0;
        return;
    }
    CODE_ON();
    fall = gjmp(0);
    gsym(defer_state.disp_jmps);
    defer_state.disp_jmps = 0;
    /* __tcc_defer_step(&answer); */
    vpush_helper_func(tok_alloc_const("__tcc_defer_step"));
    vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.step_off);
    gaddrof();
    mk_pointer(&vtop->type);
    gfunc_call(1);
    for (s = defer_state.sites; s; s = s->next) {
        int j;
        vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.step_off);
        gv(RC_INT);
        vpushi(s->id);
        gen_op(TOK_EQ);
        j = gvtst(1, 0);
        gjmp_addr(s->label->jind);
        gsym(j);
    }
    for (r = defer_state.resumes; r; r = r->next) {
        int j;
        vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.step_off);
        gv(RC_INT);
        vpushi(-r->id);
        gen_op(TOK_EQ);
        j = gvtst(1, 0);
        gjmp_addr(r->label->jind);
        gsym(j);
    }
    gsym(fall);
    c6_free_lists();
    defer_state.enabled = 0;
}
#endif /* C6_EXT */

/* ------------------------------------------------------------------------- */
/* VLA */

static void vla_restore(int loc)
{
    if (loc)
        gen_vla_sp_restore(loc);
}

static void vla_leave(struct scope *o)
{
    struct scope *c = cur_scope, *v = NULL;
    for (; c != o && c; c = c->prev)
      if (c->vla.num)
        v = c;
    if (v)
      vla_restore(v->vla.locorig);
}

/* ------------------------------------------------------------------------- */
/* local scopes */

static void new_scope(struct scope *o)
{
    /* copy and link previous scope */
    *o = *cur_scope;
    o->prev = cur_scope;
    cur_scope = o;
    cur_scope->vla.num = 0;

    /* record local declaration stack position */
    o->lstk = local_stack;
    o->llstk = local_label_stack;
    ++local_scope;
    o->depth = local_scope;
}

static void prev_scope(struct scope *o, int is_expr)
{
#ifdef C6_EXT
    if (defer_state.enabled && !nocode_wanted && o->depth > 0)
        c6_defer_begin(0, o->depth - 1);
#endif
    vla_leave(o->prev);

    if (o->cl.s != o->prev->cl.s)
        block_cleanup(o->prev);

    if (debug_modes)
        tcc_debug_end_scope(o->lstk, !is_expr);

    /* pop locally defined labels */
    label_pop(&local_label_stack, o->llstk, is_expr);

    /* In the is_expr case (a statement expression is finished here),
       vtop might refer to symbols on the local_stack.  Either via the
       type or via vtop->sym.  We can't pop those nor any that in turn
       might be referred to.  To make it easier we don't roll back
       any symbols in that case; some upper level call to block() will
       do that.  We do have to remove such symbols from the lookup
       tables, though.  sym_pop will do that.  */

    /* pop locally defined symbols */
    sym_pop(&local_stack, o->lstk, is_expr);
    cur_scope = o->prev;
    --local_scope;
}

#ifdef C6_EXT
/* ------------------------------------------------------------------------- */
/* c6 'try' / 'catch'                                                    */
/*                                                                           */
/* 'try f()'             : if the error slot is set after the call, run this  */
/*                         frame's errdefer/defer in reverse order and return */
/*                         the sentinel of the enclosing function.            */
/* 'f() catch e { ... }' : if the error slot is set, snapshot it into 'e' and */
/*                         clear the slot, then run the handler body.         */
/* Both forms require the operand to be a call to a 'T!error' function;       */
/* 'try' additionally requires the enclosing function to be 'T!error'.        */

/* emit "__tcc_err_flag_get() != 0" and return a jump that is taken when the
   error slot is empty, i.e. the success path of 'try' / 'catch' */
static int c6_err_flag_jump(void)
{
    int slot = (loc - (int)sizeof(int)) & -(int)PTR_SIZE;

    loc = slot;
    vpush_helper_func(tok_alloc_const("__tcc_err_flag_peek"));
    vset(&int_type, VT_LOCAL | VT_LVAL, slot);
    gaddrof();
    mk_pointer(&vtop->type);
    gfunc_call(1);
    vset(&int_type, VT_LOCAL | VT_LVAL, slot);
    vpushi(0);
    gen_op(TOK_NE);
    return gvtst(1, 0);
}

/* A value of a 'T!error' function may only be consumed by 'try' or by
   'catch'.  Any other use (operator, cast, assignment, discarded result)
   would drop the error silently, so it is rejected here. */
static void c6_fallible_use(void)
{
    if (!c6_value_fallible)
        return;
    c6_value_fallible = 0;
    /* 'sizeof(f())' / 'typeof(f())' never call f, so there is no error to
       lose: just drop the mark instead of rejecting the expression */
    if (NOEVAL_WANTED)
        return;
    tcc_error("the result of a 'T!error' function must be used with "
              "'try' or 'catch'");
}

/* push a void value: the expression statement parser always pops one */
static void c6_push_void(void)
{
    vpushi(0);
    vtop->type.t = VT_VOID;
}

/* Take the already-evaluated operand off the value stack, remembering its
   value for the success path (registers are flushed so that the cleanup
   calls emitted below cannot clobber it). */
static int c6_save_operand(const char *kw, SValue *sv)
{
    int has_value;

    if (!c6_value_fallible)
        tcc_error("'%s' requires a call to a function returning 'T!error'",
                  kw);
    c6_value_fallible = 0;
    has_value = (vtop->type.t & VT_BTYPE) != VT_VOID;
    if (has_value) {
        save_regs(0);
        *sv = *vtop;
        vtop--;
    } else {
        vpop();
    }
    return has_value;
}

/* 'try f()': the operand is parsed here (prefix position in unary()). */
static void c6_try_parse(void)
{
    SValue sv;
    int has_value, skip;

    if (!c6_ext_on())
        tcc_error("'try' requires -std=c6");
    if (defer_state.body_depth > 0)
        tcc_error("'try' is not allowed in a defer body");
    if (!c6_func_sym || !c6_func_sym->type.ref->f.func_fallible)
        tcc_error("'try' requires the enclosing function to return 'T!error'");
    next();                             /* skip 'try' */
    c6_value_fallible = 0;
    unary();                            /* the called function (clears the
                                           slot itself before the call) */
    has_value = c6_save_operand("try", &sv);
    skip = c6_err_flag_jump();      /* taken when no error is pending */
    c6_emit_propagate();            /* failure path: clean up + return */
    gsym(skip);
    if (has_value)
        vpushv(&sv);
    else
        c6_push_void();
}

/* 'f() catch [e] { ... }': the operand has already been parsed by
   expr_catch(), so only the handler is compiled here. */
static void c6_catch_parse(void)
{
    SValue sv;
    struct scope o;
    Sym *ev;
    int has_value, succ, saved_nocode;

    if (!c6_ext_on())
        tcc_error("'catch' requires -std=c6");
    if (defer_state.body_depth > 0)
        tcc_error("'catch' is not allowed in a defer body");
    has_value = c6_save_operand("catch", &sv);
    succ = c6_err_flag_jump();      /* taken when no error is pending */
    next();                             /* skip 'catch' */

    new_scope(&o);
    ev = NULL;
    if (tok != '{') {
        int name = 0, paren = 0;
        if (tok == '(') {
            paren = 1;
            next();
            if (tok != ')') {
                if (tok < TOK_UIDENT)
                    expect("error variable");
                name = tok;
            }
        } else {
            if (tok < TOK_UIDENT)
                expect("'{'");
            name = tok;
        }
        if (name) {
            /* declare 'struct __tcc_error NAME;' in the handler scope */
            Sym *tags = struct_find(tok_alloc_const("__tcc_error"));
            CType et;
            int align, size;
            if (!tags || !(tags->v & SYM_STRUCT))
                tcc_error("internal error: 'struct __tcc_error' is missing");
            et.t = VT_STRUCT;
            et.ref = tags;
            size = type_size(&et, &align);
            loc = (loc - size) & -align;
            ev = sym_push(name, &et, VT_LOCAL | VT_LVAL, loc);
            next();
        }
        if (paren)
            skip(')');
    }

    /* snapshot the error into 'e' and clear the slot: 'catch' always consumes
       the error, so it never leaks into the enclosing function */
    vpush_helper_func(tok_alloc_const("__tcc_error_take"));
    if (ev) {
        vset(&ev->type, VT_LOCAL | VT_LVAL, ev->c);
        gaddrof();
    } else {
        vpushi(0);
    }
    gfunc_call(1);

    saved_nocode = nocode_wanted;
    block(STMT_COMPOUND);
    /* the handler must leave on its own and continue past its end with the
       success path.  When the compiler cannot prove that the body always
       leaves, emit a trap rather than silently resuming the success path. */
    if (!(nocode_wanted & CODE_OFF_BIT) && !(saved_nocode & CODE_OFF_BIT)) {
        if (!c6_last_stmt_leaves)
            tcc_error("'catch' body must end with 'return', 'err()' or 'goto'");
        vpush_helper_func(tok_alloc_const("__tcc_catch_fallthrough"));
        gfunc_call(0);
    }
    nocode_wanted = saved_nocode;
    prev_scope(&o, 0);

    gsym(succ);
    if (has_value)
        vpushv(&sv);
    else
        c6_push_void();
}
#endif

/* leave a scope via break/continue(/goto) */
static void leave_scope(struct scope *o)
{
    if (!o)
        return;
#ifdef C6_EXT
    if (defer_state.enabled && !nocode_wanted) {
        if (o == root_scope)
            c6_defer_begin(1, 0);
        else
            c6_defer_begin(0, o->depth);
    }
#endif
    try_call_scope_cleanup(o->cl.s);
    vla_leave(o);
}

/* short versiona for scopes with 'if/do/while/switch' which can
   declare only types (of struct/union/enum) */
static void new_scope_s(struct scope *o)
{
    o->lstk = local_stack;
    ++local_scope;
}

static void prev_scope_s(struct scope *o)
{
    sym_pop(&local_stack, o->lstk, 0);
    --local_scope;
}

/* ------------------------------------------------------------------------- */
/* call block from 'for do while' loops */

static void lblock(int *bsym, int *csym)
{
    struct scope *lo = loop_scope, *co = cur_scope;
    int *b = co->bsym, *c = co->csym;
    if (csym) {
        co->csym = csym;
        loop_scope = co;
    }
    co->bsym = bsym;
    block(0);
    co->bsym = b;
    if (csym) {
        co->csym = c;
        loop_scope = lo;
    }
}

/* c2y if/switch declaration */
static void gexpr_decl(void)
{
    int v = decl(VT_JMP);
    if (v > 1 && tok != ';') {
        Sym *s = sym_find(v);
        vset(&s->type, s->r, (s->r & VT_SYM) ? 0 : s->c);
        vtop->sym = s;
    } else {
        if (v)
            skip(';');
        gexpr();
    }
}

static void block(int flags)
{
    int a, b, c, d, e, t;
#ifdef C6_EXT
    int ret_off, ret_saved;
#endif
    struct scope o;
    Sym *s;

again:
    t = tok;
#ifdef C6_EXT
    c6_last_stmt_leaves = 0;
    /* '(int q, int r) = f();' and '(a, b) = f();' are statements, so they
       may also be the unbraced body of if/while/for/switch or follow a
       label.  Those single statements do not go through decl(), which is
       where the form is otherwise recognized, so classify it here as well
       (c6_tuple_lead_kind() replays and restores the tokens by itself). */
    if (tcc_state->c6_ext && t == '(') {
        int kind = c6_tuple_lead_kind();
        if (kind == C6_TK_PATTERN || kind == C6_TK_ASSIGN) {
            /* let decl() run the statement: it pushes the element
               assignments back as tokens and parses them right away, while
               the hidden temporary is still in scope */
            decl(VT_LOCAL);
            goto c6_stmt_done;
        }
    }
#endif
    /* If the token carries a value, next() might destroy it. Only with
       invalid code such as f(){"123"4;} */
    if (TOK_HAS_VALUE(t))
        goto expr;
    next();

#ifdef C6_EXT
    if (tcc_state->c6_ext && tok >= TOK_IDENT) {
        int kw = c6_soft_kw(t);
        if (kw != C6_KW_NONE && !c6_soft_kw_shadowed(t)) {
            switch (kw) {
            case C6_KW_CO:   c6_co_parse();   break;
            case C6_KW_GO:
                if (c6_gc_in_drop)
                    tcc_error("a destructor cannot suspend: it runs while "
                              "the object is being released");
                c6_go_parse();
                break;
            default:         c6_task_decl();  break;
            }
            goto c6_stmt_done;
        }
    }
#endif
    if (debug_modes)
        tcc_tcov_check_line (tcc_state, 0), tcc_tcov_block_begin (tcc_state);

    if (t == TOK_IF) {
        new_scope_s(&o);
        skip('(');
        gexpr_decl();
        a = gvtst(1, 0);
        skip(')');
        block(0);
        if (tok == TOK_ELSE) {
            d = gjmp(0);
            gsym(a);
            next();
            block(0);
            gsym(d); /* patch else jmp */
        } else {
            gsym(a);
        }
        prev_scope_s(&o);

    } else if (t == TOK_WHILE) {
        new_scope_s(&o);
        d = gind();
        skip('(');
        gexpr();
        a = gvtst(1, 0);
        skip(')');
        b = 0;
        lblock(&a, &b);
        gjmp_addr(d);
        gsym_addr(b, d);
        gsym(a);
        prev_scope_s(&o);

    } else if (t == '{') {
        if (debug_modes)
            tcc_debug_stabn(tcc_state, N_LBRAC, ind - func_ind);
        new_scope(&o);

        /* handle local labels declarations */
        while (tok == TOK_LABEL) {
            do {
                next();
                if (tok < TOK_UIDENT)
                    expect("label identifier");
                label_push(&local_label_stack, tok, LABEL_DECLARED);
                next();
            } while (tok == ',');
            skip(';');
        }

        while (tok != '}') {
	    decl(VT_LOCAL);
            if (tok != '}') {
                block(flags | STMT_COMPOUND);
            }
        }

        prev_scope(&o, flags & STMT_EXPR);
        if (debug_modes)
            tcc_debug_stabn(tcc_state, N_RBRAC, ind - func_ind);
        if (local_scope)
            next();
        else if (!nocode_wanted
#ifdef C6_EXT
                 && !c6_async_cur     /* an async body always returns a kind */
#endif
                 )
            check_func_return();

    } else if (t == TOK_RETURN) {
#ifdef C6_EXT
        c6_last_stmt_leaves = 1;
        if (defer_state.body_depth > 0)
            tcc_error("'return' is not allowed in a defer body");
        ret_off = 0;
        ret_saved = 0;
        if (c6_async_cur) {
            /* in an async body 'return' stores the value into the task */
            c6_async_return();
            goto c6_ret_done;
        }
#endif
        b = (func_vt.t & VT_BTYPE) != VT_VOID;
        if (tok != ';') {
#ifdef C6_EXT
            if (tcc_state->c6_ext)
                c6_return_value(&func_vt);  /* also 'return {a, b};' */
            else
#endif
                gexpr();
#ifdef C6_EXT
            c6_fallible_use();  /* 'return f();' would drop the error */
#endif
            if (b) {
                gen_assign_cast(&func_vt);
#ifdef C6_EXT
                c6_gc_return_hook();    /* hand one reference to the caller */
#endif
            } else {
                if (vtop->type.t != VT_VOID)
                    tcc_warning("void function returns a value");
                vtop--;
            }
        } else if (b && func_old && (func_vt.t & VT_BTYPE) == VT_INT) {
            vpushi(0);
        } else if (b) {
            tcc_warning("'return' with no value");
            b = 0;
        }
#ifdef C6_EXT
        /* protect the return value from cleanup calls */
        if (b && defer_state.enabled && defer_state.has_sites) {
            CType rt = func_vt;
            int align, size = type_size(&func_vt, &align);
            loc = (loc - size) & -align;
            ret_off = loc;
            ret_saved = 1;
            /* a plain word: the reference handed to the caller was counted
               by c6_gc_return_hook() already */
            c6_gc_plain(&rt);
            c6_gc_no_hook = 1;
            vset(&rt, VT_LOCAL | VT_LVAL, ret_off);
            vswap();
            vstore();
            vpop();
            c6_gc_no_hook = 0;
        }
        c6_gc_in_drop = 0;
#endif
        leave_scope(root_scope);
#ifdef C6_EXT
        if (ret_saved)
            vset(&func_vt, VT_LOCAL | VT_LVAL, ret_off);
#endif
        if (b)
            gfunc_return(&func_vt);
        skip(';');
        /* jump unless last stmt in top-level block (with defer/errdefer
           active the jump is required: the function-exit dispatcher must
           not be reached by falling through) */
        if (tok != '}' || local_scope != 1
#ifdef C6_EXT
            || defer_state.enabled
#endif
            )
            rsym = gjmp(rsym);
        if (debug_modes)
	    tcc_tcov_block_end (tcc_state, -1);
        CODE_OFF();
#ifdef C6_EXT
    c6_ret_done: ;
#endif

    } else if (t == TOK_BREAK) {
#ifdef C6_EXT
        c6_last_stmt_leaves = 1;
        if (defer_state.body_depth > 0)
            tcc_error("'break' is not allowed in a defer body");
#endif
        /* compute jump */
        if (!cur_scope->bsym)
            tcc_error("cannot break");
        if (cur_switch && cur_scope->bsym == cur_switch->bsym)
            leave_scope(cur_switch->scope);
        else
            leave_scope(loop_scope);
        *cur_scope->bsym = gjmp(*cur_scope->bsym);
        skip(';');

    } else if (t == TOK_CONTINUE) {
#ifdef C6_EXT
        c6_last_stmt_leaves = 1;
        if (defer_state.body_depth > 0)
            tcc_error("'continue' is not allowed in a defer body");
#endif
        /* compute jump */
        if (!cur_scope->csym)
            tcc_error("cannot continue");
        leave_scope(loop_scope);
        *cur_scope->csym = gjmp(*cur_scope->csym);
        skip(';');

#ifdef C6_EXT
    } else if (t == TOK_C6_DEFER) {
        if (!c6_ext_on())
            tcc_error("'defer' requires -std=c6");
        else
            c6_defer_parse(0);

    } else if (t == TOK_C6_ERRDEFER) {
        if (!c6_ext_on())
            tcc_error("'errdefer' requires -std=c6");
        else
            c6_errdefer_parse();

    } else if (t == TOK_C6_ERR) {
        if (!c6_ext_on())
            tcc_error("'err' requires -std=c6");
        else {
            c6_last_stmt_leaves = 1;
            c6_err_parse();
        }

    } else if (t == TOK_C6_TRY) {
        /* 'try f();' is an ordinary expression statement */
        if (!c6_ext_on())
            tcc_error("'try' requires -std=c6");
        unget_tok(t);
        goto expr;

    } else if (t == TOK_C6_CATCH) {
        if (!c6_ext_on())
            tcc_error("'catch' requires -std=c6");
        tcc_error("'catch' has no operand");

    } else if (t == TOK_C6_FOREACH) {
        if (!c6_ext_on())
            tcc_error("'foreach' requires -std=c6");
        if (tok != '|') {
            /* not a statement: 'foreach' is used as a plain name here */
            unget_tok(t);
            goto expr;
        }
        c6_foreach_parse();
#endif

    } else if (t == TOK_FOR) {
        new_scope(&o);

        skip('(');
        if (tok != ';') {
            /* c99 for-loop init decl? */
            if (!decl(VT_JMP)) {
                /* no, regular for-loop init expr */
                gexpr();
                vpop();
            }
        }
        skip(';');
        a = b = 0;
        c = d = gind();
        if (tok != ';') {
            gexpr();
            a = gvtst(1, 0);
        }
        skip(';');
        if (tok != ')') {
            e = gjmp(0);
            d = gind();
            gexpr();
            vpop();
            gjmp_addr(c);
            gsym(e);
        }
        skip(')');
        lblock(&a, &b);
        gjmp_addr(d);
        gsym_addr(b, d);
        gsym(a);
        prev_scope(&o, 0);

    } else if (t == TOK_DO) {
        new_scope_s(&o);
        a = b = 0;
        d = gind();
        lblock(&a, &b);
        gsym(b);
        skip(TOK_WHILE);
        skip('(');
	gexpr();
        c = gvtst(0, 0);
        skip(')');
        skip(';');
	gsym_addr(c, d);
        gsym(a);
        prev_scope_s(&o);

    } else if (t == TOK_SWITCH) {
        struct switch_t *sw;

        sw = tcc_mallocz(sizeof *sw);
        sw->bsym = &a;
        sw->scope = cur_scope;
        sw->prev = cur_switch;
        sw->nocode_wanted = nocode_wanted;
        cur_switch = sw;

        new_scope_s(&o);
        skip('(');
        gexpr_decl();
        if (!is_integer_btype(vtop->type.t & VT_BTYPE))
            tcc_error("switch value not an integer");
        skip(')');
        sw->sv = *vtop--; /* save switch value */
        a = 0;
        b = gjmp(0); /* jump to first case */
        lblock(&a, NULL);
        a = gjmp(a); /* add implicit break */
        /* case lookup */
        gsym(b);
        prev_scope_s(&o);
        if (sw->nocode_wanted)
            goto skip_switch;
        case_sort(sw);
        sw->bsym = NULL; /* marker for 32bit:gen_opl() */
        vpushv(&sw->sv);
        gv(RC_INT);
        d = gcase(sw->p, sw->n, 0);
        vpop();
        if (sw->def_sym)
            gsym_addr(d, sw->def_sym);
        else
            gsym(d);
    skip_switch:
        /* break label */
        gsym(a);
        end_switch();

    } else if (t == TOK_CASE) {
        struct case_t *cr;
        if (!cur_switch)
            expect("switch");
        cr = tcc_malloc(sizeof(struct case_t));
        dynarray_add(&cur_switch->p, &cur_switch->n, cr);
        t = cur_switch->sv.type.t;
        cr->v1 = cr->v2 = value64(expr_const64(), t);
        if (tok == TOK_DOTS && gnu_ext) {
            next();
            cr->v2 = value64(expr_const64(), t);
            if (case_cmp(cr->v2, cr->v1) < 0)
                tcc_warning("empty case range");
        }
        /* case and default are unreachable from a switch under nocode_wanted */
        if (!cur_switch->nocode_wanted)
            cr->ind = gind();
        cr->line = file->line_num;
        skip(':');
        goto block_after_label;

    } else if (t == TOK_DEFAULT) {
        if (!cur_switch)
            expect("switch");
        if (cur_switch->def_sym)
            tcc_error("too many 'default'");
        cur_switch->def_sym = cur_switch->nocode_wanted ? -1 : gind();
        skip(':');
        goto block_after_label;

    } else if (t == TOK_GOTO) {
#ifdef C6_EXT
        c6_last_stmt_leaves = 1;
        if (defer_state.body_depth > 0)
            tcc_error("'goto' is not allowed in a defer body");
#endif
        vla_restore(cur_scope->vla.locorig);
        if (tok == '*' && gnu_ext) {
            /* computed goto */
            next();
            gexpr();
            if ((vtop->type.t & VT_BTYPE) != VT_PTR)
                expect("pointer");
            ggoto();

        } else if (tok >= TOK_UIDENT) {
	    s = label_find(tok);
	    /* put forward definition if needed */
            if (!s)
              s = label_push(&global_label_stack, tok, LABEL_FORWARD);
            else if (s->r == LABEL_DECLARED)
              s->r = LABEL_FORWARD;

	    if (s->r & LABEL_FORWARD) {
		/* start new goto chain for cleanups, linked via label->next */
		if (cur_scope->cl.s && !nocode_wanted) {
                    sym_push2(&pending_gotos, SYM_FIELD, 0, cur_scope->cl.n);
                    pending_gotos->cleanup_label = s;
                    s = sym_push2(&s->next, SYM_FIELD, 0, 0);
                    pending_gotos->next = s;
                }
		s->jnext = gjmp(s->jnext);
	    } else {
		try_call_cleanup_goto(s->cleanupstate);
		gjmp_addr(s->jind);
	    }
	    next();

        } else {
            expect("label identifier");
        }
        skip(';');

    } else if (t == TOK_ASM1 || t == TOK_ASM2 || t == TOK_ASM3) {
        asm_instr();

    } else {
        if (tok == ':' && t >= TOK_UIDENT) {
            /* label case */
	    next();
            s = label_find(t);
            if (s) {
                if (s->r == LABEL_DEFINED)
                    tcc_error("duplicate label '%s'", get_tok_str(s->v, NULL));
                s->r = LABEL_DEFINED;
		if (s->next) {
		    Sym *pcl; /* pending cleanup goto */
		    for (pcl = s->next; pcl; pcl = pcl->prev)
		      gsym(pcl->jnext);
		    sym_pop(&s->next, NULL, 0);
		} else
		  gsym(s->jnext);
            } else {
                s = label_push(&global_label_stack, t, LABEL_DEFINED);
            }
#ifdef C6_EXT
            s->defer_depth = local_scope;
#endif
            s->jind = gind();
            s->cleanupstate = cur_scope->cl.s;
#ifdef C6_EXT
            /* every label entry point dispatches deferred calls that belong
               to scopes being left by the goto */
            if (defer_state.enabled && !nocode_wanted)
                c6_defer_begin(0, s->defer_depth);
#endif

    block_after_label:
            /* Accept attributes after labels (e.g. 'unused') */
            parse_attribute(NULL);

            if (debug_modes)
                tcc_tcov_reset_ind(tcc_state);
            vla_restore(cur_scope->vla.loc);

            if (tok != '}') {
                if (0 == (flags & STMT_COMPOUND))
                    goto again;
                /* C23: insert implicit null-statement whithin compound statement */
            } else {
                /* we accept this, but it is a mistake */
                tcc_warning_c(warn_all)("deprecated use of label at end of compound statement");
            }
        } else {
            /* expression case */
            if (t != ';') {
                unget_tok(t);
    expr:
                if (flags & STMT_EXPR) {
                    vpop();
                    gexpr();   /* the value belongs to the enclosing
                                  expression, checked there */
                } else {
                    gexpr();
#ifdef C6_EXT
                    c6_fallible_use();
                    c6_gc_discard_hook();
#endif
                    vpop();
                }
                skip(';');
            }
        }
    }

#ifdef C6_EXT
c6_stmt_done:
#endif
    if (debug_modes)
        tcc_tcov_check_line (tcc_state, 0), tcc_tcov_block_end (tcc_state, 0);
}

/* This skips over a stream of tokens containing balanced {} and ()
   pairs, stopping at outer ',' ';' and '}' (or matching '}' if we started
   with a '{').  If STR then allocates and stores the skipped tokens
   in *STR.  This doesn't check if () and {} are nested correctly,
   i.e. "({)}" is accepted.  */
static void skip_or_save_block(TokenString **str)
{
    int braces = tok == '{';
    int level = 0;
    if (str)
      *str = tok_str_alloc();

    while (1) {
	int t = tok;
        if (level == 0
            && (t == ','
             || t == ';'
             || t == '}'
             || t == ')'
             || t == ']'))
             break;
	if (t == TOK_EOF) {
	     if (str || level > 0)
	       tcc_error("unexpected end of file");
	     else
	       break;
	}
	if (str)
	  tok_str_add_tok(*str);
	next();
	if (t == '{' || t == '(' || t == '[') {
	    level++;
	} else if (t == '}' || t == ')' || t == ']') {
	    level--;
	    if (level == 0 && braces && t == '}')
	      break;
	}
    }
    if (str)
	tok_str_add(*str, TOK_EOF);
}

#define EXPR_CONST 1
#define EXPR_ANY   2

static void parse_init_elem(int expr_type)
{
    int saved_global_expr;
    switch(expr_type) {
    case EXPR_CONST:
        /* compound literals must be allocated globally in this case */
        saved_global_expr = global_expr;
        global_expr = 1;
        expr_const1();
        global_expr = saved_global_expr;
        /* NOTE: symbols are accepted, as well as lvalue for anon symbols
	   (compound literals).  */
        if (((vtop->r & (VT_VALMASK | VT_LVAL)) != VT_CONST
             && ((vtop->r & (VT_SYM|VT_LVAL)) != (VT_SYM|VT_LVAL)
                 || vtop->sym->v < SYM_FIRST_ANOM))
#ifdef TCC_TARGET_PE
                 || ((vtop->r & VT_SYM) && vtop->sym->a.dllimport)
#endif
           )
            tcc_error("initializer element is not constant");
        break;
    case EXPR_ANY:
        expr_eq();
        break;
    }
}

#if 1
static void init_assert(init_params *p, int offset)
{
    if (p->sec ? !NODATA_WANTED && offset > p->sec->data_offset
               : !nocode_wanted && offset > p->local_offset)
        tcc_internal_error("initializer overflow");
}
#else
#define init_assert(sec, offset)
#endif

/* put zeros for variable based init */
static void init_putz(init_params *p, unsigned long c, int size)
{
    init_assert(p, c + size);
    if (p->sec) {
        /* nothing to do because globals are already set to zero */
    } else {
        vpush_helper_func(TOK_memset);
        vseti(VT_LOCAL, c);
        vpushi(0);
        vpushs(size);
#if defined TCC_TARGET_ARM && defined TCC_ARM_EABI
        vswap();  /* using __aeabi_memset(void*, size_t, int) */
#endif
        gfunc_call(3);
    }
}

#define DIF_FIRST     1
#define DIF_SIZE_ONLY 2
#define DIF_HAVE_ELEM 4
#define DIF_CLEAR     8
#define DIF_DIRTY     16

/* delete relocations for specified range c ... c + size. Unfortunatly
   in very special cases, relocations may occur unordered */
static void decl_design_delrels(Section *sec, int c, int size)
{
    ElfW_Rel *rel, *rel2, *rel_end;
    if (!sec || !sec->reloc)
        return;
    rel = rel2 = (ElfW_Rel*)sec->reloc->data;
    rel_end = (ElfW_Rel*)(sec->reloc->data + sec->reloc->data_offset);
    while (rel < rel_end) {
        if (rel->r_offset >= c && rel->r_offset < c + size) {
            sec->reloc->data_offset -= sizeof *rel;
        } else {
            if (rel2 != rel)
                memcpy(rel2, rel, sizeof *rel);
            ++rel2;
        }
        ++rel;
    }
}

static void decl_design_flex(init_params *p, Sym *ref, int index)
{
    if (ref == p->flex_array_ref) {
        if (index >= ref->c)
            ref->c = index + 1;
    } else if (ref->c < 0 && index >= 0)
        tcc_error("flexible array has zero size in this context");
}

/* t is the array or struct type. c is the array or struct
   address. cur_field is the pointer to the current
   field, for arrays the 'c' member contains the current start
   index.  'flags' is as in decl_initializer.
   'al' contains the already initialized length of the
   current container (starting at c).  This returns the new length of that.  */
static int decl_designator(init_params *p, CType *type, unsigned long c,
                           Sym **cur_field, int flags, int al)
{
    Sym *s, *f;
    int index, index_last, align, l, nb_elems, elem_size, chain = 0;
    unsigned long corig = c;

    elem_size = 0;
    nb_elems = 1;

    if (flags & DIF_HAVE_ELEM)
        goto no_designator;

    if (gnu_ext && tok >= TOK_UIDENT) {
        l = tok, next();
        if (tok == ':')
            goto struct_field;
        unget_tok(l);
    }

    /* NOTE: we only support ranges for last designator */
    while (nb_elems == 1 && (tok == '[' || tok == '.')) {
        if (tok == '[') {
            if (!(type->t & VT_ARRAY))
                expect("array type");
            next();
            index = index_last = expr_const();
            if (tok == TOK_DOTS && gnu_ext) {
                next();
                index_last = expr_const();
            }
            skip(']');
            s = type->ref;
            decl_design_flex(p, s, index_last);
            if (index < 0 || index_last >= s->c || index_last < index)
	        tcc_error("index exceeds array bounds or range is empty");
            if (cur_field)
		(*cur_field)->c = index_last;
            type = pointed_type(type);
            elem_size = type_size(type, &align);
            c += index * elem_size;
            nb_elems = index_last - index + 1;
        } else {
            int cumofs;
            next();
            l = tok;
        struct_field:
            next();
	    f = find_field(type, l, &cumofs);
            if (cur_field)
                *cur_field = f;
	    type = &f->type;
            c += cumofs;
        }
        cur_field = NULL;
        if (nb_elems == 1 && (tok == '[' || tok == '.')) {
            chain = 1;
            break;
        }
    }
    if (chain) {
        /* more designators follow: continue in the subobject as an
           elided list, so that following items fill it and then
           return to the next element of this container */
        elem_size = type_size(type, &align);
        if (!(flags & DIF_SIZE_ONLY) && c - corig < al)
            flags = (flags & ~DIF_CLEAR) | DIF_DIRTY;
        decl_initializer(p, type, c, flags & ~DIF_FIRST);
        c += elem_size;
        return c - corig > al ? c - corig : al;
    }
    if (!cur_field) {
        if (tok == '=') {
            next();
        } else if (!gnu_ext) {
	    expect("=");
        }
    } else {
    no_designator:
        if (type->t & VT_ARRAY) {
	    index = (*cur_field)->c;
            s = type->ref;
            decl_design_flex(p, s, index);
            if (index >= s->c)
                tcc_error("too many initializers");
            type = pointed_type(type);
            elem_size = type_size(type, &align);
            c += index * elem_size;
        } else {
            f = *cur_field;
	    /* Skip bitfield padding. Also with size 32 and 64. */
	    while (f && (f->v & SYM_FIRST_ANOM) &&
		   is_integer_btype(f->type.t & VT_BTYPE))
	        *cur_field = f = f->next;
            if (!f)
                tcc_error("too many initializers");
	    type = &f->type;
            c += f->c;
        }
    }

    if (!elem_size) /* for structs */
        elem_size = type_size(type, &align);

    /* Using designators the same element can be initialized more
       than once.  In that case we need to delete possibly already
       existing relocations. */
    if (!(flags & DIF_SIZE_ONLY) && (c - corig < al || (flags & DIF_DIRTY))) {
        decl_design_delrels(p->sec, c, elem_size * nb_elems);
        flags &= ~DIF_CLEAR; /* mark stack dirty too */
    }

    decl_initializer(p, type, c, flags & ~(DIF_FIRST | DIF_DIRTY));

    if (!(flags & DIF_SIZE_ONLY) && nb_elems > 1) {
        Sym aref = {0};
        CType t1;
        int i;
        if (p->sec || (type->t & VT_ARRAY)) {
            /* make init_putv/vstore believe it were a struct */
            aref.c = elem_size;
            t1.t = VT_STRUCT, t1.ref = &aref;
            type = &t1;
        }
        if (p->sec)
            vpush_ref(type, p->sec, c, elem_size);
        else
	    vset(type, VT_LOCAL|VT_LVAL, c);
        for (i = 1; i < nb_elems; i++) {
            vdup();
            init_putv(p, type, c + elem_size * i);
	}
        vpop();
    }

    c += nb_elems * elem_size;
    if (c - corig > al)
      al = c - corig;
    return al;
}

static void write_ldouble(unsigned char *d, void *s)
{
    //printf("long double %Lf\n", *(long double*)s);
#ifdef TCC_CROSS_TEST
    if (LDOUBLE_SIZE >= 10) {
        double b = *(long double*)s;
        s = &b;
#else
    if (sizeof (long double) == 8 && LDOUBLE_SIZE >= 10) {
#endif
        /* our 'long double' is a double really (_WIN32, __APPLE__) */
        uint64_t m = *(uint64_t*)s;
        int e = m >> 48;
        int f = e >> 4 & 0x7FF;
        m <<= 11;
        if (0 == f) {
            if (0 == m)
                goto set;
            for (f = 1; !(m & 1ULL<<63); --f)
                m <<= 1;
        }
        if (f == 0x7ff)
            f = 0x43FF;
        e = (e & 0x8000) | (f + 0x3C00);
        m |= 1ULL<<63;
    set:
    #if (defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64)
        /* double -> extended */
        write64le(d, m);
        write16le(d+8, e);
    #elif LDOUBLE_SIZE == 16
        /* double -> quad */
        write64le(d+6, m << 1);
        write16le(d+14, e);
    #endif
        ;
    } else {
    #if LDOUBLE_SIZE == 8
        /* long double -> double */
        double b = *(long double*)s;
        memcpy(d, &b, 8);
    #elif (__i386__ || __x86_64__) && (defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64)
        /* extended -> extended */
        memcpy(d, s, 10);
    #elif (__i386__ || __x86_64__) && (defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64)
        /* extended -> quad */
        uint64_t m = *(uint64_t*)s;
        int e = *(uint16_t*)((char*)s + 8);
        write64le(d+6, m << 1);
        write16le(d+14, e);
    #elif (__aarch64__ || __riscv) && (defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64)
        /* quad -> extended */
        uint64_t m = read64le((char*)s + 6);
        int e = read16le((char*)s + 14);
        (e & 0x7fff) && (m & 1) && 0 == ++m && ++e;
        write64le(d, m >> 1 | ((e & 0x7fff) ? 1ULL<<63 : 0));
        write16le(d+8, e);
    #else
        /* unknown */
        if (sizeof (long double) == LDOUBLE_SIZE)
            memcpy(d, s, LDOUBLE_SIZE);
    #endif
    }
}

/* store a value or an expression directly in global data or in local array */
static void init_putv(init_params *p, CType *type, unsigned long c)
{
    int bt;
    void *ptr;
    CType dtype;
    int size, align;
    Section *sec = p->sec;
    uint64_t val;

    dtype = *type;
    dtype.t &= ~VT_CONSTANT; /* need to do that to avoid false warning */

    size = type_size(type, &align);
    if (type->t & VT_BITFIELD)
        size = (BIT_POS(type->t) + BIT_SIZE(type->t) + 7) / 8;
    init_assert(p, c + size);

    if (sec) {
        /* XXX: not portable */
        /* XXX: generate error if incorrect relocation */
        gen_assign_cast(&dtype);
        bt = type->t & VT_BTYPE;

        if ((vtop->r & VT_SYM)
            && bt != VT_PTR
            && (bt != (PTR_SIZE == 8 ? VT_LLONG : VT_INT)
                || (type->t & VT_BITFIELD))
            && !((vtop->r & VT_CONST) && vtop->sym->v >= SYM_FIRST_ANOM)
            )
            tcc_error("initializer element is not computable at load time");

        if (NODATA_WANTED) {
            vtop--;
            return;
        }

        ptr = sec->data + c;
        val = vtop->c.i;

	if ((vtop->r & (VT_SYM|VT_CONST)) == (VT_SYM|VT_CONST)
            && vtop->sym->v >= SYM_FIRST_ANOM
            && ((vtop->r & VT_LVAL) /* compound literal */
                || bt == VT_STRUCT /* designator */
                )) {
	    /* memcpy stuff over.  */
	    Section *ssec;
	    ElfSym *esym;
	    ElfW_Rel *rel;
	    esym = elfsym(vtop->sym);
	    ssec = tcc_state->sections[esym->st_shndx];
	    memmove (ptr, ssec->data + esym->st_value + (int)vtop->c.i, size);
	    if (ssec->reloc) {
		/* We need to copy over all memory contents, and that
		   includes relocations.  Use the fact that relocs are
		   created it order, so look from the end of relocs
		   until we hit one before the copied region.  */
                unsigned long relofs = ssec->reloc->data_offset;
		while (relofs >= sizeof(*rel)) {
                    relofs -= sizeof(*rel);
                    rel = (ElfW_Rel*)(ssec->reloc->data + relofs);
		    if (rel->r_offset >= esym->st_value + size)
		      continue;
		    if (rel->r_offset < esym->st_value)
		      break;
		    put_elf_reloca(symtab_section, sec,
				   c + rel->r_offset - esym->st_value,
				   ELFW(R_TYPE)(rel->r_info),
				   ELFW(R_SYM)(rel->r_info),
#if PTR_SIZE == 8
				   rel->r_addend
#else
				   0
#endif
				  );
		}
	    }
	} else {
            if (type->t & VT_BITFIELD) {
                int bit_pos, bit_size, bits, n;
                unsigned char *p, v, m;
                bit_pos = BIT_POS(vtop->type.t);
                bit_size = BIT_SIZE(vtop->type.t);
                p = (unsigned char*)ptr + (bit_pos >> 3);
                bit_pos &= 7, bits = 0;
                while (bit_size) {
                    n = 8 - bit_pos;
                    if (n > bit_size)
                        n = bit_size;
                    v = val >> bits << bit_pos;
                    m = ((1 << n) - 1) << bit_pos;
                    *p = (*p & ~m) | (v & m);
                    bits += n, bit_size -= n, bit_pos = 0, ++p;
                }
            } else
            switch(bt) {
	    case VT_BOOL:
		*(char *)ptr = val != 0;
                break;
	    case VT_BYTE:
		*(char *)ptr = val;
		break;
	    case VT_SHORT:
                write16le(ptr, val);
		break;
	    case VT_FLOAT:
                write32le(ptr, val);
		break;
	    case VT_DOUBLE:
                write64le(ptr, val);
		break;
	    case VT_LDOUBLE:
                write_ldouble(ptr, &vtop->c.ld);
		break;

#if PTR_SIZE == 8
            /* intptr_t may need a reloc too, see tcctest.c:relocation_test() */
	    case VT_LLONG:
	    case VT_PTR:
	        if (vtop->r & VT_SYM)
	          greloca(sec, vtop->sym, c, R_DATA_PTR, val);
	        else
	          write64le(ptr, val);
	        break;
            case VT_INT:
                write32le(ptr, val);
                break;
#else
	    case VT_LLONG:
                write64le(ptr, val);
                break;
            case VT_PTR:
            case VT_INT:
	        if (vtop->r & VT_SYM)
	          greloc(sec, vtop->sym, c, R_DATA_PTR);
	        write32le(ptr, val);
	        break;
#endif
	    default:
                //tcc_internal_error("unexpected type");
                break;
	    }
	}
        vtop--;
    } else {
        vset(&dtype, VT_LOCAL|VT_LVAL, c);
        vswap();
#ifdef C6_EXT
        /* an initializer writes a slot that holds no value yet */
        c6_gc_init_store = 1;
#endif
        vstore();
#ifdef C6_EXT
        c6_gc_init_store = 0;
#endif
        vpop();
    }
}

/* 't' contains the type and storage info. 'c' is the offset of the
   object in section 'sec'. If 'sec' is NULL, it means stack based
   allocation. 'flags & DIF_FIRST' is true if array '{' must be read (multi
   dimension implicit array init handling). 'flags & DIF_SIZE_ONLY' is true if
   size only evaluation is wanted (only for arrays). */
static void decl_initializer(init_params *p, CType *type, unsigned long c, int flags)
{
    int len, n, no_oblock, i;
    int size1, align1;
    Sym *s, *f;
    Sym indexsym;
    CType *t1;

    /* generate line number info */
    if (debug_modes && !(flags & DIF_SIZE_ONLY) && !p->sec)
        tcc_debug_line(tcc_state), tcc_tcov_check_line (tcc_state, 1);

#ifdef C6_EXT
    /* a slice initializer is read as a single value, a string literal
       included: 'char[:] m = "text";' views the literal */
    if (!(flags & DIF_HAVE_ELEM) && tok != '{' && c6_slice_of_type(type)) {
        int ncw_prev = nocode_wanted;
        if ((flags & DIF_SIZE_ONLY) && !p->sec)
            ++nocode_wanted;
        parse_init_elem(!p->sec ? EXPR_ANY : EXPR_CONST);
        nocode_wanted = ncw_prev;
        flags |= DIF_HAVE_ELEM;
    } else
#endif
    if (!(flags & DIF_HAVE_ELEM) && tok != '{' &&
	/* In case of strings we have special handling for arrays, so
	   don't consume them as initializer value (which would commit them
	   to some anonymous symbol).  */
	tok != TOK_LSTR && tok != TOK_STR && tok != '[' && tok != '.' &&
	(!(flags & DIF_SIZE_ONLY)
            /* a struct may be initialized from a struct of same type, as in
                    struct {int x,y;} a = {1,2}, b = {3,4}, c[] = {a,b};
               In that case we need to parse the element in order to check
               it for compatibility below */
            || (type->t & VT_BTYPE) == VT_STRUCT)
        ) {
        int ncw_prev = nocode_wanted;
        if ((flags & DIF_SIZE_ONLY) && !p->sec)
            ++nocode_wanted;
	parse_init_elem(!p->sec ? EXPR_ANY : EXPR_CONST);
        nocode_wanted = ncw_prev;
        flags |= DIF_HAVE_ELEM;
    }

    if (type->t & VT_ARRAY) {
        no_oblock = 1;
        if (((flags & DIF_FIRST) && tok != TOK_LSTR && tok != TOK_STR) ||
            tok == '{') {
            skip('{');
            no_oblock = 0;
        }

        s = type->ref;
        n = s->c;
        t1 = pointed_type(type);
        size1 = type_size(t1, &align1);

        /* only parse strings here if correct type (otherwise: handle
           them as ((w)char *) expressions */
        if ((tok == TOK_LSTR && 
#ifdef TCC_TARGET_PE
             (t1->t & VT_BTYPE) == VT_SHORT && (t1->t & VT_UNSIGNED)
#else
             (t1->t & VT_BTYPE) == VT_INT
#endif
            ) || (tok == TOK_STR && (t1->t & VT_BTYPE) == VT_BYTE)) {
	    len = 0;
            cstr_reset(&initstr);
            if (size1 != (tok == TOK_STR ? 1 : sizeof(nwchar_t)))
              tcc_error("unhandled string literal merging");
            while (tok == TOK_STR || tok == TOK_LSTR) {
                if (initstr.size)
                  initstr.size -= size1;
                if (tok == TOK_STR)
                  len += tokc.str.size;
                else
                  len += tokc.str.size / sizeof(nwchar_t);
                len--;
                cstr_cat(&initstr, tokc.str.data, tokc.str.size);
                next();
            }
            if (tok != ')' && tok != '}' && tok != ',' && tok != ';'
                && tok != TOK_EOF) {
                /* Not a lone literal but part of a bigger expression.  */
                unget_tok(size1 == 1 ? TOK_STR : TOK_LSTR);
                tokc.str.size = initstr.size;
                tokc.str.data = initstr.data;
                goto do_init_array;
            }

            decl_design_flex(p, s, len);
            if (!(flags & DIF_SIZE_ONLY)) {
                int nb = n, ch;
                if (len < nb)
                    nb = len;
                if (len > nb)
                  tcc_warning("initializer-string for array is too long");
                /* in order to go faster for common case (char
                   string in global variable, we handle it
                   specifically */
                if (p->sec && size1 == 1) {
                    init_assert(p, c + nb);
                    if (!NODATA_WANTED)
                      memcpy(p->sec->data + c, initstr.data, nb);
                } else {
                    for(i=0;i<n;i++) {
                        if (i >= nb) {
                          /* only add trailing zero if enough storage (no
                             warning in this case since it is standard) */
                          if (flags & DIF_CLEAR)
                            break;
                          if (n - i >= 4) {
                            init_putz(p, c + i * size1, (n - i) * size1);
                            break;
                          }
                          ch = 0;
                        } else if (size1 == 1)
                          ch = ((unsigned char *)initstr.data)[i];
                        else
                          ch = ((nwchar_t *)initstr.data)[i];
                        vpushi(ch);
                        init_putv(p, t1, c + i * size1);
                    }
                }
            }
            if (tok == ',' && !no_oblock) /* static const char s[] = { "123", }; */
                next();
        } else {

          do_init_array:
	    indexsym.c = 0;
	    f = &indexsym;

          do_init_list:
            /* zero memory once in advance */
            if (!(flags & (DIF_CLEAR | DIF_SIZE_ONLY | DIF_DIRTY))) {
                init_putz(p, c, n*size1);
                flags |= DIF_CLEAR;
            }

	    len = 0;
            /* GNU extension: if the initializer is empty for a flex array,
               it's size is zero.  We won't enter the loop, so set the size
               now.  */
            decl_design_flex(p, s, len - 1);
	    while (tok != '}' || (flags & DIF_HAVE_ELEM)) {
		len = decl_designator(p, type, c, &f, flags, len);
		flags &= ~DIF_HAVE_ELEM;
		if (type->t & VT_ARRAY) {
		    ++indexsym.c;
		    /* special test for multi dimensional arrays (may not
		       be strictly correct if designators are used at the
		       same time) */
		    if (no_oblock && len >= n*size1)
		        break;
		} else {
		    if (s->type.t == VT_UNION)
		        f = NULL;
		    else
		        f = f->next;
		    if (no_oblock && f == NULL)
		        break;
		}

		if (tok == '}')
		    break;
		skip(',');
		if (no_oblock && (tok == '[' || tok == '.')) {
		    /* designator for an enclosing container */
		    unget_tok(',');
		    break;
		}
	    }
        }
        if (!no_oblock)
            skip('}');

    } else if ((flags & DIF_HAVE_ELEM)
        /* Use i_c_parameter_t, to strip toplevel qualifiers.
           The source type might have VT_CONSTANT set, which is
           of course assignable to non-const elements.  */
            && is_compatible_unqualified_types(type, &vtop->type)) {
        goto one_elem;

#ifdef C6_EXT
    } else if ((flags & DIF_HAVE_ELEM) && c6_slice_convert(type)) {
        /* 'int[:] s = array;': vtop now holds the slice value */
        goto one_elem;
    } else if ((flags & DIF_HAVE_ELEM) && c6_iface_convert(type)) {
        /* 'Shape s = circle;': vtop now holds the interface value */
        goto one_elem;
#endif

    } else if ((type->t & VT_BTYPE) == VT_STRUCT) {
        no_oblock = 1;
        if ((flags & DIF_FIRST) || tok == '{') {
            skip('{');
            no_oblock = 0;
        }
        s = type->ref;
        f = s->next;
        n = s->c;
        size1 = 1;
	goto do_init_list;

    } else if (tok == '{') {
        if (flags & DIF_HAVE_ELEM)
          skip(';');
        next();
        decl_initializer(p, type, c, flags & ~DIF_HAVE_ELEM);
        skip('}');

    } else one_elem: if ((flags & DIF_SIZE_ONLY)) {
	/* If we supported only ISO C we wouldn't have to accept calling
	   this on anything than an array if DIF_SIZE_ONLY (and even then
	   only on the outermost level, so no recursion would be needed),
	   because initializing a flex array member isn't supported.
	   But GNU C supports it, so we need to recurse even into
	   subfields of structs and arrays when DIF_SIZE_ONLY is set.  */
        /* just skip expression */
        if (flags & DIF_HAVE_ELEM)
            vpop();
        else
            skip_or_save_block(NULL);

    } else {
	if (!(flags & DIF_HAVE_ELEM)) {
	    /* This should happen only when we haven't parsed
	       the init element above for fear of committing a
	       string constant to memory too early.  */
	    if (tok != TOK_STR && tok != TOK_LSTR)
	      expect("string constant");
	    parse_init_elem(!p->sec ? EXPR_ANY : EXPR_CONST);
	}
        if (!p->sec && (flags & DIF_CLEAR) /* container was already zero'd */
            && (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST
            && vtop->c.i == 0
            && btype_size(type->t & VT_BTYPE) /* not for fp constants */
            )
            vpop();
        else
            init_putv(p, type, c);
    }
}

/* parse an initializer for type 't' if 'has_init' is non zero, and
   allocate space in local or global data space ('r' is either
   VT_LOCAL or VT_CONST). If 'v' is non zero, then an associated
   variable 'v' of scope 'scope' is declared before initializers
   are parsed. If 'v' is zero, then a reference to the new object
   is put in the value stack. If 'has_init' is 2, a special parsing
   is done to handle string constants. */
static void decl_initializer_alloc(CType *type, AttributeDef *ad, int r, 
                                   int has_init, int v, int scope)
{
    int size, align, addr;
    TokenString *init_str = NULL;

    Section *sec;
    Sym *flexible_array;
    Sym *sym = NULL;
    int saved_nocode_wanted = nocode_wanted;
#ifdef CONFIG_TCC_BCHECK
    int bcheck = tcc_state->do_bounds_check && !NODATA_WANTED;
#endif
    init_params p = {0};

    if (scope == VT_CONST) {
        /* see if a global symbol was already defined */
        sym = sym_find(v);
        if (sym) {
            patch_storage(sym, ad, type);
            /* we accept several definitions of the same global variable. */
            if (!has_init && sym->c && elfsym(sym)->st_shndx != SHN_UNDEF)
                return;
            type = &sym->type;
        }
    }

    /* Always allocate static or global variables */
    if (v && (r & VT_VALMASK) == VT_CONST)
        nocode_wanted |= DATA_ONLY_WANTED;

    flexible_array = NULL;
    size = type_size(type, &align);

    /* exactly one flexible array may be initialized, either the
       toplevel array or the last member of the toplevel struct */

    if (size < 0) {
        // error out except for top-level incomplete arrays
        // (arrays of incomplete types are handled in array parsing)
        if (!(type->t & VT_ARRAY))
            tcc_error("initialization of incomplete type");
        /* If the base type itself was an array type of unspecified
           size (like in 'typedef int arr[]; arr x = {1};') then
           we will overwrite the unknown size by the real one for
           this decl.  We need to unshare the ref symbol holding
           that size.  */
        if (IS_BT_ARRAY(type->t))
            type->ref = sym_push(SYM_FIELD, &type->ref->type, 0, type->ref->c);
        p.flex_array_ref = type->ref;

    } else if (has_init && (type->t & VT_BTYPE) == VT_STRUCT) {
        Sym *field = type->ref->next;
        if (field) {
            while (field->next)
                field = field->next;
            if (field->type.t & VT_ARRAY && field->type.ref->c < 0) {
                flexible_array = field;
                p.flex_array_ref = field->type.ref;
                size = -1;
            }
        }
    }

    if (size < 0) {
        /* If unknown size, do a dry-run 1st pass */
        if (!has_init)
            goto err_size;
        if (has_init == 2) {
            /* only get strings */
            init_str = tok_str_alloc();
            while (tok == TOK_STR || tok == TOK_LSTR) {
                tok_str_add_tok(init_str);
                next();
            }
            tok_str_add(init_str, TOK_EOF);
        } else
            skip_or_save_block(&init_str);
        unget_tok(0);

        /* compute size */
        begin_macro(init_str, 1);
        next();
        decl_initializer(&p, type, 0, DIF_FIRST | DIF_SIZE_ONLY);
        /* prepare second initializer parsing */
        macro_ptr = init_str->str;
        next();

        /* if still unknown size, error */
        size = type_size(type, &align);
        if (size < 0)
    err_size:
            tcc_error("unknown type size");

        /* If there's a flex member and it was used in the initializer
           adjust size.  */
        if (flexible_array && flexible_array->type.ref->c > 0)
            size += flexible_array->type.ref->c
                    * pointed_size(&flexible_array->type);
    }

    /* take into account specified alignment if bigger */
    if (ad->a.aligned) {
	int speca = 1 << (ad->a.aligned - 1);
        if (speca > align)
            align = speca;
    } else if (ad->a.packed) {
        align = 1;
    }

    if (!v && NODATA_WANTED)
        size = 0, align = 1;

    if ((r & VT_VALMASK) == VT_LOCAL) {
        sec = NULL;
#ifdef CONFIG_TCC_BCHECK
        if (bcheck && v) {
            /* add padding between stack variables for bound checking */
            loc -= align;
        }
#endif
        loc = (loc - size) & -align;
        addr = loc;
        p.local_offset = addr + size;
#ifdef CONFIG_TCC_BCHECK
        if (bcheck && v) {
            /* add padding between stack variables for bound checking */
            loc -= align;
        }
#endif
        if (v) {
            /* local variable */
#ifdef CONFIG_TCC_ASM
	    if (ad->asm_label) {
		int reg = asm_parse_regvar(ad->asm_label);
		if (reg >= 0)
		    r = (r & ~VT_VALMASK) | reg;
	    }
#endif
            sym = sym_push(v, type, r, addr);
	    if (ad->cleanup_func) {
		Sym *cls = sym_push2(&all_cleanups,
                    SYM_FIELD | ++cur_scope->cl.n, 0, 0);
		cls->cleanup_sym = sym;
		cls->cleanup_func = ad->cleanup_func;
		cls->next = cur_scope->cl.s;
		cur_scope->cl.s = cls;
	    }

            sym->a = ad->a;
        } else {
            /* push local reference */
            vset(type, r, addr);
        }
    } else {
        CType *tp = type;
        int is_const;

        while ((tp->t & (VT_BTYPE|VT_ARRAY)) == (VT_PTR|VT_ARRAY))
            tp = &tp->ref->type;
        is_const = tp->t & VT_CONSTANT;

        /* allocate symbol in corresponding section */
        sec = ad->section;
        if (!sec) {
            if (type->t & VT_TLS) {
                sec = find_section(tcc_state, has_init ? ".tdata" : ".tbss");
                sec->sh_flags = SHF_ALLOC | SHF_WRITE | SHF_TLS;
                sec->sh_type = has_init ? SHT_PROGBITS : SHT_NOBITS;
            } else if (is_const) {
		sec = rodata_section;
            } else if (has_init) {
		sec = data_section;
                /*if (g_debug & 4)
                    tcc_warning("rw data: %s", get_tok_str(v, 0));*/
            } else if (tcc_state->nocommon)
                sec = bss_section;
        } else if (ad->new_section) {
            /* XXX: DWIM logic: set section flags according to first usage */
            if (!is_const) {
                sec->sh_flags |= SHF_WRITE;
                if (!has_init)
                    sec->sh_type = SHT_NOBITS;
            }
        }

#ifdef CONFIG_TCC_BCHECK
        if (bcheck)
            size = size + 1; /* add padding */
#endif
        if (sec) {
	    addr = section_add(sec, size, align);
        } else {
            addr = align; /* SHN_COMMON is special, symbol value is align */
	    sec = common_section;
        }

        if (v) {
            if (!sym) {
                sym = sym_push(v, type, r | VT_SYM, 0);
                patch_storage(sym, ad, NULL);
            }
            /* update symbol definition */
	    put_extern_sym(sym, sec, addr, size);
        } else {
            /* push global reference */
            vpush_ref(type, sec, addr, size);
            sym = vtop->sym;
	    vtop->r |= r;
        }

#ifdef CONFIG_TCC_BCHECK
        /* handles bounds now because the symbol must be defined
           before for the relocation */
        if (bcheck) {
            addr_t *bounds_ptr;

            greloca(bounds_section, sym, bounds_section->data_offset, R_DATA_PTR, 0);
            /* then add global bound info */
            bounds_ptr = section_ptr_add(bounds_section, 2 * sizeof(addr_t));
            bounds_ptr[0] = 0; /* relocated */
            bounds_ptr[1] = size - 1;
        }
#endif
    }

    if (type->t & VT_VLA) {
        int a;

        if (has_init)
            tcc_error("variable length array cannot be initialized");

        if (NODATA_WANTED)
            goto no_alloc;

        /* save before-VLA stack pointer if needed */
        if (cur_scope->vla.num == 0) {
            if (cur_scope->prev && cur_scope->prev->vla.num) {
                cur_scope->vla.locorig = cur_scope->prev->vla.loc;
            } else {
                gen_vla_sp_save(loc -= PTR_SIZE);
                cur_scope->vla.locorig = loc;
            }
        }

        vpush_type_size(type, &a);
        gen_vla_alloc(type, a);
#if defined TCC_TARGET_PE && defined TCC_TARGET_X86_64
        /* on _WIN64, because of the function args scratch area, the
           result of alloca differs from RSP and is returned in RAX.  */
        gen_vla_result(addr), addr = (loc -= PTR_SIZE);
#endif
        gen_vla_sp_save(addr);
        cur_scope->vla.loc = addr;
        cur_scope->vla.num++;
    } else if (has_init) {
        p.sec = sec;
        decl_initializer(&p, type, addr, DIF_FIRST);
        /* patch flexible array member size back to -1, */
        /* for possible subsequent similar declarations */
        if (flexible_array)
            flexible_array->type.ref->c = -1;
    }

 no_alloc:
    /* restore parse state if needed */
    if (init_str) {
        end_macro();
        next();
    }

    nocode_wanted = saved_nocode_wanted;
}

/* generate vla code saved in post_type() */
static void func_vla_arg_code(Sym *arg)
{
    int align;
    TokenString *vla_array_tok = NULL;

    if (arg->type.ref)
        func_vla_arg_code(arg->type.ref);

    if ((arg->type.t & VT_VLA) && arg->type.ref->vla_array_str) {
	loc -= type_size(&int_type, &align);
	loc &= -align;
	arg->type.ref->c = loc;

	unget_tok(0);
	vla_array_tok = tok_str_alloc();
	vla_array_tok->str = arg->type.ref->vla_array_str;
	begin_macro(vla_array_tok, 1);
	next();
	gexpr();
	end_macro();
	next();
	vpush_type_size(&arg->type.ref->type, &align);
	gen_op('*');
	vset(&int_type, VT_LOCAL|VT_LVAL, arg->type.ref->c);
	vswap();
	vstore();
	vpop();
    }
}

static void func_vla_arg(Sym *sym)
{
    Sym *arg;

    for (arg = sym->type.ref->next; arg; arg = arg->next)
        if ((arg->type.t & VT_BTYPE) == VT_PTR && (arg->type.ref->type.t & VT_VLA))
            func_vla_arg_code(arg->type.ref);
}

/* set the local stack address for function parameter from gfunc_prolog() */
ST_FUNC Sym *gfunc_set_param(Sym *s, int c, int byref)
{
    s = sym_find(s->v);
    if (!s) /* unnamed parameters, not enabled */
        return NULL;
    s->c = c;
    if (byref)
        s->r = VT_LLOCAL | VT_LVAL; /* otherwise VT_LOCAL */
    return s;
}

/* push parameters (and their types), last first */
static void sym_push_params(Sym *ref)
{
    Sym *s = ref;
    while (s->next)
        s = s->next;
    while (s != ref) {
        if ((s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
            sym_copy(s, &local_stack);
        s = s->prev;
    }
}

/* parse a function defined by symbol 'sym' and generate its code in
   'cur_text_section' */
/* ------------------------------------------------------------------ */
/* c6: async / await (stackless coroutines)                            */
#ifdef C6_EXT
/*                                                                     */
/* 'async T f(params)' compiles to 'int __c6_body_f(void *task)'.  The  */
/* body runs on the caller's stack; a suspension mirrors the used part  */
/* of the native frame, the pending defer records and the error slot    */
/* into the task and returns to the driver, and a resume copies them    */
/* back to the same offsets and jumps to the recorded resume point.     */
/* No back end change is involved and every local keeps exactly the      */
/* semantics it has in an ordinary function.                            */
/*                                                                     */
/* 'struct f_task { struct __c6_task head; T val; P0 p0; ... };' is the  */
/* cross-TU frame prefix: it follows from the declaration alone, so the  */
/* defining and the calling translation unit agree on it.  All header    */
/* accesses go through the __c6_co_* accessors so the layout has a       */
/* single source of truth.                                              */

typedef struct C6AsyncSite {
    int state;                        /* resume point number (> 0) */
    Sym *label;
    struct C6AsyncSite *next;
} C6AsyncSite;

static int c6_async_name;             /* name of the body being compiled */
static CType c6_async_ftype;          /* its declared function type */
static C6AsyncSite *c6_async_sites;
static int c6_async_nstates;
static int c6_async_dispatch;         /* jump chain to the resume dispatcher */
static int c6_async_base_off;         /* slot holding the frame base marker */
static int c6_async_fr_off;           /* slot of the 'void *task' parameter */

#define C6_CO_KIND_DONE  0
#define C6_CO_KIND_IO    1
#define C6_CO_KIND_CHILD 2
#define C6_CO_KIND_ERR   3

/* '__c6_body_<name>', '__c6_desc_<name>' and '<name>_task' */
static int c6_async_tok(const char *prefix, int name, const char *suffix)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s%s%s", prefix,
             get_tok_str(name, NULL), suffix);
    return tok_alloc_const(buf);
}

/* the declaration of runtime helper NAME, which tcc_predefs() always emits */
static Sym *c6_rt(const char *name)
{
    Sym *s = sym_find(tok_alloc_const(name));
    if (!s)
        tcc_error("internal: runtime declaration '%s' is missing", name);
    return s;
}

/* push the helper symbol; the arguments go on top of it and c6_rt_end()
   performs the call and pushes the declared result */
static void c6_rt_begin(const char *name)
{
    Sym *s = c6_rt(name);
    vpushsym(&s->type, s);
}

static void c6_rt_end(const char *name, int nargs)
{
    Sym *s = c6_rt(name);
    CType rt = s->type.ref->type;

    gfunc_call(nargs);
    if ((rt.t & VT_BTYPE) != VT_VOID) {
        vpushi(0);
        vtop->r = REG_IRET;
        vtop->type = rt;
    }
}

/* Register a cleanup site whose body is one runtime call.  It runs on every
   exit of the function, and -- being a cleanup -- it runs before the return
   value is put back into the return registers, so a call emitted here cannot
   clobber it.  Used for the end-of-main thread join. */
static void c6_defer_rt_call(const char *name)
{
    int skip, id;
    Sym *l;

    if (!defer_state.enabled || nocode_wanted)
        return;
    id = ++defer_state.next_site;
    l = c6_new_label("dcl");
    vpush_helper_func(tok_alloc_const("__tcc_defer_push"));
    vpushi(id);
    vpushi(0);                  /* scope 0: only the function-exit run */
    vpushi(0);                  /* defer, not errdefer */
    gfunc_call(3);
    skip = gjmp(0);
    c6_def_label(l);
    c6_add_site(id, l);
    defer_state.has_sites = 1;
    c6_rt_begin(name);
    c6_rt_end(name, 0);
    defer_state.disp_jmps = gjmp(defer_state.disp_jmps);
    gsym(skip);
}

/* the parameters of an async signature, skipping the '(void)' spelling */
static Sym *c6_async_params(CType *ft)
{
    Sym *p = ft->ref->next;
    if (p && (p->type.t & VT_BTYPE) == VT_VOID)
        return NULL;
    return p;
}

/* Append an integer constant to TS (the parser's current token is saved and
   restored, since tok_str_add_tok() reads it). */
static void c6_async_add_int(TokenString *ts, int v)
{
    int save_tok = tok;
    CValue save_val = tokc;

    tok = TOK_CINT;
    tokc.i = v;
    tok_str_add_tok(ts);
    tok = save_tok;
    tokc = save_val;
}

/* Append the tokens that spell TYPE as a type specifier (no declarator).
   'async' signatures are restricted to types that can be written back out,
   because the task structure is rebuilt from the declaration. */
static int c6_async_type_tokens(TokenString *ts, CType *type)
{
    C6Slice *sl;
    int t = type->t;

    if (t & (VT_ARRAY | VT_VLA))
        return 0;
    sl = c6_slice_of_type(type);
    if (sl) {
        if (!c6_async_type_tokens(ts, &sl->elem))
            return 0;
        tok_str_add(ts, '[');
        tok_str_add(ts, ':');
        tok_str_add(ts, ']');
        return 1;
    }
    if (t & VT_CONSTANT)
        tok_str_add(ts, TOK_CONST1);
    switch (t & VT_BTYPE) {
    case VT_VOID:   tok_str_add(ts, TOK_VOID);   return 1;
    case VT_FLOAT:  tok_str_add(ts, TOK_FLOAT);  return 1;
    case VT_DOUBLE: tok_str_add(ts, TOK_DOUBLE); return 1;
    case VT_LDOUBLE:
        tok_str_add(ts, TOK_LONG);
        tok_str_add(ts, TOK_DOUBLE);
        return 1;
    case VT_BOOL:   tok_str_add(ts, TOK_BOOL);   return 1;
    case VT_BYTE:   tok_str_add(ts, TOK_CHAR);   break;
    case VT_SHORT:  tok_str_add(ts, TOK_SHORT);  break;
    case VT_INT:    tok_str_add(ts, TOK_INT);    break;
    case VT_LLONG:
        tok_str_add(ts, TOK_LONG);
        tok_str_add(ts, TOK_LONG);
        break;
    case VT_PTR:
        {
            /* a managed reference is spelled with its qualifier, so that the
               generated 'struct NAME_task' fields are managed too: the task
               owns them (the call site and the body both go through the
               store hook) and 'release' gives them back */
            int gq = c6_type_gq(type);

            if (gq & C6_GQ_QUAL) {
                tok_str_add(ts, tok_alloc_const(gq & C6_GQ_SHARED ? "shared"
                                                : gq & C6_GQ_OWN ? "own"
                                                : "weak"));
                tok_str_add(ts, '@');
                return c6_async_type_tokens(ts, &type->ref->type);
            }
        }
        if (!c6_async_type_tokens(ts, &type->ref->type))
            return 0;
        tok_str_add(ts, '*');
        return 1;
    case VT_STRUCT:
    case VT_UNION:
        {
            /* a tuple is written as '(T1, T2)': its tag is an implementation
               detail, and replaying the tag name would declare a fresh struct
               tag instead of finding the tuple */
            C6Tuple *tp = c6_tuple_of_type(type);
            if (tp) {
                int i;
                tok_str_add(ts, '(');
                for (i = 0; i < tp->n; i++) {
                    if (i)
                        tok_str_add(ts, ',');
                    if (!c6_async_type_tokens(ts, &tp->elem[i]))
                        return 0;
                }
                tok_str_add(ts, ')');
                return 1;
            }
        }
        if (!type->ref)
            return 0;
        tok_str_add(ts, (t & VT_UNION) == VT_UNION ? TOK_UNION : TOK_STRUCT);
        tok_str_add(ts, type->ref->v & ~SYM_STRUCT);
        return 1;
    default:
        return 0;
    }
    if (t & VT_UNSIGNED)
        tok_str_add(ts, TOK_UNSIGNED);
    return 1;
}

/* Declare 'struct NAME_task { struct __c6_task head; T val; P0 p0; ... };'
   Idempotent, so a repeated prototype is cheap. */
static void c6_async_declare(CType *ftype, int name)
{
    TokenString *ts;
    Sym *p;
    char buf[32];
    int tag, i;

    /* E-GC10: what a task carries crosses to whoever awaits it, so it must
       be 'shared' (only a 'shared' count is atomic) */
    if (c6_is_managed(&ftype->ref->type)
        && !(c6_type_gq(&ftype->ref->type) & (C6_GQ_SHARED | C6_GQ_WEAK)))
        tcc_error("an 'async' return value that carries a reference requires "
                  "'shared': '%s' returns a per-thread count",
                  get_tok_str(name, NULL));
    for (p = c6_async_params(ftype); p; p = p->next)
        if (c6_is_managed(&p->type)
            && !(c6_type_gq(&p->type) & (C6_GQ_SHARED | C6_GQ_WEAK)))
            tcc_error("passing a reference to 'async' requires 'shared': "
                      "'%s' is counted per thread", get_tok_str(name, NULL));

    tag = c6_async_tok("", name, "_task");
    if (struct_find(tag))
        return;
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, tag);
    tok_str_add(ts, '{');
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, tok_alloc_const("__c6_task"));
    tok_str_add(ts, tok_alloc_const("head"));
    tok_str_add(ts, ';');
    if ((ftype->ref->type.t & VT_BTYPE) != VT_VOID) {
        if (!c6_async_type_tokens(ts, &ftype->ref->type))
            goto bad;
        tok_str_add(ts, tok_alloc_const("val"));
        tok_str_add(ts, ';');
    }
    for (i = 0, p = c6_async_params(ftype); p; p = p->next, i++) {
        if (!c6_async_type_tokens(ts, &p->type))
            goto bad;
        snprintf(buf, sizeof buf, "p%d", i);
        tok_str_add(ts, tok_alloc_const(buf));
        tok_str_add(ts, ';');
    }
    tok_str_add(ts, '}');
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);
    return;
bad:
    {
        char tbuf[256];
        type_to_str(tbuf, sizeof tbuf, ftype, NULL);
        tcc_error("'async' does not support this signature: '%s'", tbuf);
    }
}

/* push the task pointer (the body's only parameter) */
static void c6_async_push_task(void)
{
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, c6_async_fr_off);
}

/* push the frame base (rbp), recovered from the marker's own address */
static void c6_async_push_base(void)
{
    vset(&char_pointer_type, VT_LOCAL, c6_async_base_off);
    vpushi(-c6_async_base_off);
    gen_op('+');
}

/* the task structure of async function NAME */
static CType c6_async_task_ctype(int name)
{
    CType t;
    Sym *s = struct_find(c6_async_tok("", name, "_task"));
    if (!s)
        tcc_error("'%s' is not an async function", get_tok_str(name, NULL));
    t.t = VT_STRUCT;
    t.ref = s;
    return t;
}

/* push the lvalue of TASK.FIELD; TASK_OFF is the slot holding the task
   pointer, or 0 for the task of the body being compiled */
static void c6_async_push_field(int name, const char *field, int task_off)
{
    CType st = c6_async_task_ctype(name);
    Sym *f;
    int ofs = 0;

    f = find_field(&st, tok_alloc_const(field), &ofs);
    if (task_off)
        vset(&char_pointer_type, VT_LOCAL | VT_LVAL, task_off);
    else
        c6_async_push_task();
    vtop->type = char_pointer_type;
    vpushi(ofs);
    gen_op('+');
    vtop->type = f->type;
    vtop->r |= VT_LVAL;        /* async signatures cannot contain arrays */
}

/* address of the descriptor of async function NAME */
static void c6_async_push_desc(int name)
{
    CType dt;
    Sym *st = struct_find(tok_alloc_const("__c6_desc"));
    AttributeDef ad;
    Sym *s;

    if (!st)
        tcc_error("internal: 'struct __c6_desc' is missing");
    dt.t = VT_STRUCT | VT_CONSTANT | VT_EXTERN;
    dt.ref = st;
    memset(&ad, 0, sizeof ad);
    s = external_sym(c6_async_tok("__c6_desc_", name, ""), &dt, 0, &ad);
    vpushsym(&dt, s);
    mk_pointer(&vtop->type);        /* the symbol's address, as '... *' */
}

/* __c6_co_finish(task, kind); return kind; -- leaves the body for good */
static void c6_async_finish(int kind)
{
    c6_rt_begin("__c6_co_finish");
    c6_async_push_task();
    vpushi(kind);
    c6_rt_end("__c6_co_finish", 2);
    vpushi(kind);
    gfunc_return(&int_type);
    rsym = gjmp(rsym);
    CODE_OFF();
}

/* Park the frame and return to the driver; execution continues at the
   freshly defined resume label when the task is stepped again. */
static void c6_async_suspend(int kind)
{
    int size = -loc;
    int state = ++c6_async_nstates;
    int vsp = vtop - vstack;
    C6AsyncSite *site;
    Sym *label;
    int i;

    /* Everything already on the value stack has to survive the suspension.
       Frame slots do, because a resume restores the frame to the same
       offsets; registers and pending jump chains do not. */
    for (i = 0; i < vsp; i++) {
        int r = vstack[i].r & VT_VALMASK;
        if (r < VT_CONST || r == VT_CMP || r == VT_JMP || r == VT_JMPI)
            tcc_error("the value pending across 'await' must live in a "
                      "variable: store it in a local first");
    }

    /* __c6_co_suspend(task, kind, state, base, size, defer_base) */
    c6_rt_begin("__c6_co_suspend");
    c6_async_push_task();
    vpushi(kind);
    vpushi(state);
    c6_async_push_base();
    vpushi(size);
    vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.sp0_off);
    c6_rt_end("__c6_co_suspend", 6);
    vpushi(kind);
    gfunc_return(&int_type);
    rsym = gjmp(rsym);
    CODE_OFF();
    /* the resume path must find the value stack exactly as the suspending
       path left it */
    vtop = vstack + vsp;

    label = c6_new_label("co_resume");
    c6_def_label(label);
    CODE_ON();                          /* the resumed code is real code */
    site = tcc_mallocz(sizeof *site);
    site->state = state;
    site->label = label;
    site->next = c6_async_sites;
    c6_async_sites = site;
}

/* 'return [expr];' inside an async body */
/* Parse the value of a 'return' of type TYPE.  Besides an ordinary
   expression, a braced initializer list is accepted for an aggregate:
   'return {a, b};' initializes a temporary and returns that. */
static void c6_return_value(CType *type)
{
    AttributeDef ad;

    if (tok != '{') {
        gexpr();
        return;
    }
    if ((type->t & VT_BTYPE) != VT_STRUCT)
        tcc_error("'return {…}' requires a struct, union or tuple "
                  "return type");
    memset(&ad, 0, sizeof ad);
    decl_initializer_alloc(type, &ad, VT_LOCAL | VT_LVAL, 1, 0, 0);
}

static void c6_async_return(void)
{
    CType vt = c6_async_ftype.ref->type;

    if (defer_state.body_depth > 0)
        tcc_error("'return' is not allowed in a defer body");
    if (tok != ';') {
        if ((vt.t & VT_BTYPE) == VT_VOID) {
            gexpr();
            c6_fallible_use();
            vpop();
        } else {
            c6_async_push_field(c6_async_name, "val", 0);
            c6_return_value(&vt);
            c6_fallible_use();
            gen_assign_cast(&vt);
            vstore();
            vpop();
        }
    }
    if (defer_state.enabled && (defer_state.has_sites || defer_state.disp_jmps))
        c6_defer_begin(1, 0);
    c6_async_finish(C6_CO_KIND_DONE);
}

/* 'err()' or a propagated 'try' inside an async body: park the sentinel as
   the task value, run the deferred cleanups and end the body with KIND_ERR so
   that the driver reports the failure and not a value. */
static void c6_async_propagate(void)
{
    CType vt = c6_async_ftype.ref->type;

    if ((vt.t & VT_BTYPE) != VT_VOID) {
        if ((vt.t & VT_BTYPE) == VT_STRUCT) {
            int align, size = type_size(&vt, &align), off;
            int saved = c6_value_fallible;
            c6_async_push_field(c6_async_name, "val", 0);
            loc = (loc - size) & -align;
            off = loc;
            c6_zero_local(off, size);
            vset(&vt, VT_LOCAL | VT_LVAL, off);
            c6_value_fallible = saved;
            vstore();
            vpop();
        } else {
            c6_async_push_field(c6_async_name, "val", 0);
            vpushi(TCC_ERR_SENTINEL);
            vtop->type = vt;        /* '(TYPE)-1', no conversion warning */
            c6_gc_no_hook = 1;      /* the sentinel is not a reference */
            vstore();
            c6_gc_no_hook = 0;
            vpop();
        }
    }
    if (defer_state.has_sites)
        c6_defer_begin(2, 0);
    c6_async_finish(C6_CO_KIND_ERR);
}

/* 'await c6_io(fd, ev)': the only leaf suspension primitive */
static void c6_async_await_io(Sym *s)
{
    CType iot = s->type.ref->next->type;   /* fd */
    CType evt = s->type.ref->next->next->type;   /* ev */

    next();                                /* '(' */
    c6_rt_begin("__c6_co_set_io");
    c6_async_push_task();
    expr_eq();
    gen_assign_cast(&iot);
    skip(',');
    expr_eq();
    gen_assign_cast(&evt);
    skip(')');
    c6_rt_end("__c6_co_set_io", 3);

    c6_async_suspend(C6_CO_KIND_IO);
    /* the value of the await is the readiness mask */
    c6_rt_begin("__c6_co_rev");
    c6_async_push_task();
    c6_rt_end("__c6_co_rev", 1);
}

/* 'await f(args)' of async function S, whose task lives in slot TASK_OFF */
static void c6_async_await_task(int task_off, Sym *s)
{
    CType ct = s->type.ref->type;
    int j, skip;

    /* __c6_co_await(child, task) is 0 when the child already finished */
    c6_rt_begin("__c6_co_await");
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, task_off);
    c6_async_push_task();
    c6_rt_end("__c6_co_await", 2);
    gen_test_zero(TOK_NE);
    j = gvtst(0, 0);                       /* jump when the child must run */
    skip = gjmp(0);
    gsym(j);
    c6_async_suspend(C6_CO_KIND_CHILD);
    gsym(skip);
    /* the value of the await is the child's completed value */
    if ((ct.t & VT_BTYPE) == VT_VOID) {
        CType vt;
        vt.t = VT_VOID;
        vt.ref = NULL;
        vpush(&vt);
    } else {
        c6_async_push_field(s->v, "val", task_off);
    }
}

/* Rewrite 'f(args)' of an async function into a task creation.  Leaves the
   handle ('struct f_task *') on the value stack, or the completed value when
   the call is awaited.  On entry the token is at '('. */
static void c6_async_call(Sym *s)
{
    CType taskt = c6_async_task_ctype(s->v);
    Sym *p;
    char buf[32];
    int task_off, i, awaited = c6_await_pending;

    c6_await_pending = 0;

    /* task = __c6_co_new(&__c6_desc_NAME) */
    c6_rt_begin("__c6_co_new");
    c6_async_push_desc(s->v);
    c6_rt_end("__c6_co_new", 1);
    loc = (loc - PTR_SIZE) & -PTR_SIZE;
    task_off = loc;
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, task_off);
    vswap();
    vstore();
    vpop();

    /* copy the arguments into the frame prefix */
    next();                                /* '(' */
    for (i = 0, p = c6_async_params(&s->type); p; p = p->next, i++) {
        if (i)
            skip(',');
        snprintf(buf, sizeof buf, "p%d", i);
        c6_async_push_field(s->v, buf, task_off);
        if (c6_is_managed(&p->type))
            /* the task outlives this expression, so its slot owns a
               reference (the descriptor's 'release' gives it back) */
            vtop->type = p->type;
        expr_eq();
        c6_fallible_use();
        gen_assign_cast(&p->type);
        vstore();
        vpop();
    }
    skip(')');

    if (awaited) {
        c6_async_await_task(task_off, s);
        return;
    }
    vset(&taskt, VT_LOCAL | VT_LVAL, task_off);
    mk_pointer(&vtop->type);
}

/* prologue of an async body: locate the frame base, then either dispatch to a
   resume point or fall through into the ordinary body code */
static void c6_async_func_begin(void)
{
    Sym *arg = sym_find(tok_alloc_const("__c6_arg"));
    Sym *p;
    int j, skip;

    if (!arg)
        tcc_error("internal: async body has no frame argument");
    /* the whole native frame of an async function is copied into the task and
       restored at a possibly different address, so a weak slot (which is a
       pointer to the slot itself) cannot be supported here */
    c6_async_in_body = 1;
    if (c6_type_gq(pointed_type(&c6_async_ftype)) & C6_GQ_WEAK)
        tcc_error("a 'weak' reference cannot be returned by an async "
                  "function: its frame is moved to the task");
    for (p = c6_async_params(&c6_async_ftype); p; p = p->next)
        if (c6_type_gq(&p->type) & C6_GQ_WEAK)
            tcc_error("a 'weak' reference cannot be a parameter of an async "
                      "function: its frame is moved to the task");
    c6_async_fr_off = arg->c;
    loc = (loc - PTR_SIZE) & -PTR_SIZE;
    c6_async_base_off = loc;

    /* if (__c6_co_state(task)) { __c6_co_restore(task, base); goto dispatch; } */
    c6_rt_begin("__c6_co_state");
    c6_async_push_task();
    c6_rt_end("__c6_co_state", 1);
    gen_test_zero(TOK_NE);
    j = gvtst(0, 0);                       /* jump when resuming */
    skip = gjmp(0);
    gsym(j);
    c6_rt_begin("__c6_co_restore");
    c6_async_push_task();
    c6_async_push_base();
    c6_rt_end("__c6_co_restore", 2);
    c6_async_dispatch = gjmp(c6_async_dispatch);
    gsym(skip);
}

/* Declare the declared parameters as ordinary locals and copy them in from
   the frame prefix.  Only the first entry runs this: a resume restores the
   whole native frame instead. */
static void c6_async_params_copy(void)
{
    Sym *p;
    int i, align, size;
    char buf[32];

    for (i = 0, p = c6_async_params(&c6_async_ftype); p; p = p->next, i++) {
        CType pt = p->type;
        Sym *ls;

        snprintf(buf, sizeof buf, "p%d", i);
        convert_parameter_type(&pt);
        size = type_size(&pt, &align);
        loc = (loc - size) & -align;
        ls = sym_push(p->v & ~SYM_FIELD, &pt, VT_LOCAL | VT_LVAL, loc);
        c6_async_push_field(c6_async_name, buf, 0);
        if (c6_is_managed(&pt)) {
            /* a parameter is a borrow: the task slot owns the reference and
               the descriptor's 'release' gives it back, so the local neither
               counts nor is released, and assigning to it is an error */
            c6_gc_no_hook = 1;
            vset(&pt, VT_LOCAL | VT_LVAL, ls->c);
            vswap();
            vstore();
            vpop();
            c6_gc_no_hook = 0;
            ls->type.gq |= C6_GQ_BORROW;
            continue;
        }
        vset(&pt, VT_LOCAL | VT_LVAL, ls->c);
        vswap();
        vstore();
        vpop();
    }
}

/* resume dispatcher, emitted after the body and after the defer dispatcher */
static void c6_async_func_end(void)
{
    C6AsyncSite *s;
    int st_off;

    c6_async_in_body = 0;
    CODE_ON();
    if (c6_async_dispatch)
        gsym(c6_async_dispatch);
    loc = (loc - (int)sizeof(int)) & -PTR_SIZE;
    st_off = loc;
    c6_rt_begin("__c6_co_state");
    c6_async_push_task();
    c6_rt_end("__c6_co_state", 1);
    vset(&int_type, VT_LOCAL | VT_LVAL, st_off);
    vswap();
    vstore();
    vpop();
    for (s = c6_async_sites; s; s = s->next) {
        int j;
        vset(&int_type, VT_LOCAL | VT_LVAL, st_off);
        gv(RC_INT);
        vpushi(s->state);
        gen_op(TOK_EQ);
        j = gvtst(1, 0);          /* fall through when it matches */
        gjmp_addr(s->label->jind);
        gsym(j);
    }
    /* nothing matched: end the task rather than run off the end */
    c6_async_finish(C6_CO_KIND_DONE);

    while (c6_async_sites) {
        C6AsyncSite *n = c6_async_sites->next;
        tcc_free(c6_async_sites);
        c6_async_sites = n;
    }
    c6_async_cur = 0;
    c6_async_nstates = 0;
    c6_async_dispatch = 0;
}

/* 'await expr': the operand must become a call of an async function or of
   'c6_io', which the call hook rewrites into a suspension. */
static void c6_await_parse(void)
{
    int save;

    if (!c6_async_cur)
        tcc_error("'await' is only allowed inside an 'async' function");
    if (defer_state.body_depth > 0)
        tcc_error("'await' is not allowed in a 'defer' body");
    if (nocode_wanted)
        tcc_error("'await' is not allowed in an unevaluated context");
    next();                             /* 'await' */
    save = c6_await_pending;
    c6_await_pending = 1;
    unary();
    if (c6_await_pending)
        tcc_error("'await' requires a call to an 'async' function");
    c6_await_pending = save;
}

/* '__c6_gc_release((void **)((char *)T + OFS), MODE);' */
static void c6_async_add_release(TokenString *ts, int arg, int ofs, int mode)
{
    tok_str_add(ts, tok_alloc_const("__c6_gc_release"));
    tok_str_add(ts, '(');
    tok_str_add(ts, '(');
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, '*');
    tok_str_add(ts, '*');
    tok_str_add(ts, ')');
    tok_str_add(ts, '(');
    tok_str_add(ts, '(');
    tok_str_add(ts, TOK_CHAR);
    tok_str_add(ts, '*');
    tok_str_add(ts, ')');
    tok_str_add(ts, arg);
    tok_str_add(ts, '+');
    c6_async_add_int(ts, ofs);
    tok_str_add(ts, ')');
    tok_str_add(ts, ',');
    c6_async_add_int(ts, mode);
    tok_str_add(ts, ')');
    tok_str_add(ts, ';');
}

/* 'static void __c6_rel_NAME(void *__c6_t) { ... }' -- the references the task
   slot owns, given back when the task is destroyed.  Returns 0 (and emits
   nothing) when the task owns none. */
static int c6_async_release_fn(int name, CType *ftype)
{
    int arg = tok_alloc_const("__c6_t");
    CType st = c6_async_task_ctype(name);
    int fn, any = 0, i, ofs;
    Sym *p, *f;
    TokenString *ts;
    char buf[32];

    if (c6_is_managed(&ftype->ref->type))
        any = 1;
    for (i = 0, p = c6_async_params(ftype); p; p = p->next, i++)
        if (c6_is_managed(&p->type))
            any = 1;
    if (!any)
        return 0;

    fn = c6_async_tok("__c6_rel_", name, "");
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_STATIC);
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, fn);
    tok_str_add(ts, '(');
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, '*');
    tok_str_add(ts, arg);
    tok_str_add(ts, ')');
    tok_str_add(ts, '{');
    /* the return value (only for a task that dies unread) */
    if (c6_is_managed(&ftype->ref->type)) {
        ofs = 0;
        f = find_field(&st, tok_alloc_const("val"), &ofs);
        if (f)
            c6_async_add_release(ts, arg, ofs, c6_gc_mode(&ftype->ref->type));
    }
    for (i = 0, p = c6_async_params(ftype); p; p = p->next, i++) {
        if (!c6_is_managed(&p->type))
            continue;
        snprintf(buf, sizeof buf, "p%d", i);
        ofs = 0;
        f = find_field(&st, tok_alloc_const(buf), &ofs);
        if (f)
            c6_async_add_release(ts, arg, ofs, c6_gc_mode(&p->type));
    }
    tok_str_add(ts, '}');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);
    return fn;
}

/* 'async' function definition: declare the task type, compile the body under
   its own name, then emit the descriptor the runtime creates tasks from. */
static void c6_async_definition(Sym *sym, CType *ftype)
{
    int name = sym->v;
    int body = c6_async_tok("__c6_body_", name, "");
    TokenString *ts;
    Sym *body_sym;
    int align, size, rel;

    c6_async_declare(ftype, name);

    /* 'int __c6_body_NAME(void *__c6_arg);' */
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_INT);
    tok_str_add(ts, body);
    tok_str_add(ts, '(');
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, '*');
    tok_str_add(ts, tok_alloc_const("__c6_arg"));
    tok_str_add(ts, ')');
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);
    body_sym = sym_find(body);
    if (!body_sym)
        tcc_error("internal: cannot declare '%s'", get_tok_str(body, NULL));
    if (ftype->ref->f.func_fallible)
        body_sym->type.ref->f.func_fallible = 1;

    c6_async_cur = 1;
    c6_async_name = name;
    c6_async_ftype = *ftype;
    c6_async_sites = NULL;
    c6_async_nstates = 0;
    c6_async_dispatch = 0;

    cur_text_section = text_section;
    gen_function(body_sym);

    /* 'const struct __c6_desc __c6_desc_NAME = { size, align, max, body };' */
    {
        CType taskt = c6_async_task_ctype(name);
        size = type_size(&taskt, &align);
    }
    rel = c6_async_release_fn(name, ftype);
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_CONST1);
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, tok_alloc_const("__c6_desc"));
    tok_str_add(ts, c6_async_tok("__c6_desc_", name, ""));
    tok_str_add(ts, '=');
    tok_str_add(ts, '{');
    c6_async_add_int(ts, size);
    tok_str_add(ts, ',');
    c6_async_add_int(ts, align);
    tok_str_add(ts, ',');
    c6_async_add_int(ts, 64);   /* defer records parked per task */
    tok_str_add(ts, ',');
    tok_str_add(ts, body);
    tok_str_add(ts, ',');
    if (rel)
        tok_str_add(ts, rel);   /* gives back the references the task owns */
    else
        c6_async_add_int(ts, 0);
    tok_str_add(ts, '}');
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);
}

/* ---- 'go f();' and 'task t = f();' ---------------------------------- */

/* Reject anything that is not the handle of an async call.  A handle is a
   pointer to one of the generated 'struct NAME_task' structures, which always
   begin with 'struct __c6_task head'. */
static void c6_task_check_handle(CType *t, const char *what)
{
    CType *inner;
    Sym *head, *rt;

    if ((t->t & VT_BTYPE) != VT_PTR || !t->ref)
        tcc_error("%s", what);
    inner = pointed_type(t);
    if ((inner->t & VT_BTYPE) != VT_STRUCT || !inner->ref)
        tcc_error("%s", what);
    head = c6_find_field_quiet(inner, tok_alloc_const("head") | SYM_FIELD);
    rt = struct_find(tok_alloc_const("__c6_task"));
    if (!head || !rt || (head->type.t & VT_BTYPE) != VT_STRUCT
        || head->type.ref != rt)
        tcc_error("%s", what);
}

/* Register 'defer { __c6_co_destroy(slot); }' so the handle is released when
   the enclosing scope exits, on every path that already runs defer bodies.
   Same registration as c6_defer_parse(), with the cleanup body emitted here
   instead of being parsed from a statement. */
static void c6_task_defer_destroy(int slot)
{
    int skip, id;
    Sym *l;

    if (!defer_state.enabled || nocode_wanted)
        return;
    defer_state.body_depth++;
    id = ++defer_state.next_site;
    l = c6_new_label("dcl");
    vpush_helper_func(tok_alloc_const("__tcc_defer_push"));
    vpushi(id);
    vpushi(local_scope);
    vpushi(0);                          /* kind: defer */
    gfunc_call(3);
    skip = gjmp(0);
    c6_def_label(l);
    CODE_ON();
    c6_add_site(id, l);
    defer_state.has_sites = 1;
    c6_rt_begin("__c6_co_destroy");
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, slot);
    c6_rt_end("__c6_co_destroy", 1);
    defer_state.disp_jmps = gjmp(defer_state.disp_jmps);
    gsym(skip);
    defer_state.body_depth--;
}

/* 'co f(args);' : start a top-level coroutine.  It is not a child of the
   current task and belongs to no scope, so nothing but completion releases
   it; see __tcc_co_spawn() in libtcc1. */
static void c6_co_parse(void)
{
    CType ht;
    int slot;

    if (tok >= TOK_UIDENT) {
        Sym *s = sym_find(tok);
        if (s && (s->type.t & VT_BTYPE) == VT_FUNC && s->type.ref
            && !s->type.ref->f.func_async)
            tcc_error("'co' requires an 'async' function: '%s' is a plain "
                      "function (use 'go')", get_tok_str(tok, NULL));
    }
    expr_eq();
    c6_fallible_use();
    ht = vtop->type;
    c6_task_check_handle(&ht,
        "the operand of 'co' must be the handle of an 'async' call");
    /* park the handle in a frame slot so the call can push its symbol below */
    loc = (loc - PTR_SIZE) & -PTR_SIZE;
    slot = loc;
    vset(&ht, VT_LOCAL | VT_LVAL, slot);
    vswap();
    vstore();
    vpop();
    c6_rt_begin("__tcc_co_spawn");
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, slot);
    c6_rt_end("__tcc_co_spawn", 1);
    skip(';');
}

/* 'task NAME = f(args), ... ;' : declare the handle and release it when the
   enclosing scope exits.  The type comes from the initializer, which makes
   the declaration independent of how the callee happens to be spelled. */
static void c6_task_decl(void)
{
    for (;;) {
        CType ht;
        Sym *ls;
        int v, align, size, slot;

        if (tok < TOK_UIDENT)
            expect("variable name");
        v = tok;
        next();
        skip('=');
        expr_eq();
        c6_fallible_use();
        ht = vtop->type;
        c6_task_check_handle(&ht,
            "the initializer of a 'task' variable must be a call of an "
            "'async' function");
        size = type_size(&ht, &align);
        loc = (loc - size) & -align;
        slot = loc;
        ls = sym_push(v, &ht, VT_LOCAL | VT_LVAL, slot);
        vset(&ht, VT_LOCAL | VT_LVAL, ls->c);
        vswap();
        vstore();
        vpop();
        c6_task_defer_destroy(slot);
        if (tok != ',')
            break;
        next();                         /* ',' */
    }
    skip(';');
}

/* ---------------------------------------------------------------------- */
/* 'foreach |i, v| iterable { ... }'                                       */
/*                                                                         */
/* The iterable is evaluated once.  A complete array or a slice becomes    */
/* the '{ elem *data; size_t len; }' frame pair that the slice runtime     */
/* already uses, a range 'lo:hi' becomes two integer slots.  The binders    */
/* are ordinary locals -- the index counts and the value is a fresh copy    */
/* of the element -- so 'break', 'continue', 'defer' and 'await' behave     */
/* exactly as they do inside a 'for'.  A user type is iterated through a    */
/* slice it exposes ('foreach |i, v| obj.items'): there is no runtime       */
/* protocol and no call into a library.                                    */

/* what the usual arithmetic conversions give for two integer operands:
   the wider type, unsigned when both are as wide and either is unsigned,
   and never narrower than 'int' (default promotions) */
static CType c6_common_int_type(CType *a, CType *b)
{
    CType r;
    int al, bl, sa, sb;

    sa = type_size(a, &al);
    sb = type_size(b, &bl);
    r = sb > sa ? *b : *a;
    if (sa == sb && ((a->t | b->t) & VT_UNSIGNED))
        r.t |= VT_UNSIGNED;
    if (type_size(&r, &al) < (int)sizeof(int))
        r.t = VT_INT;
    return r;
}

static int c6_is_int_type(CType *t)
{
    if (is_float(t->t))
        return 0;
    return is_integer_btype(t->t & VT_BTYPE);
}

/* 'foreach' keeps the 'for' semantics: the binders live in their own scope
   and 'lblock' collects the break/continue jumps of the body. */
static void c6_foreach_parse(void)
{
    C6Slice *sl;
    CType t, elem, ept, ctr;
    struct scope o;
    char buf[256];
    int n1, n2, nb, is_range;
    int i_off, v_off, pair, hi_off, size, align;
    int a, b, c, d;

    n1 = n2 = nb = i_off = v_off = pair = hi_off = 0;
    is_range = 0;

    next();                             /* '|' */
    for (;;) {
        int name;
        if (tok < TOK_UIDENT)
            tcc_error("'foreach' expects a binding name");
        if (nb == 2)
            tcc_error("'foreach' takes at most two bindings");
        name = 0;
        if (strcmp(get_tok_str(tok, NULL), "_"))
            name = tok;
        if (nb++ == 0)
            n1 = name;
        else
            n2 = name;
        next();
        if (tok != ',')
            break;
        next();
    }
    skip('|');

    new_scope(&o);

    expr_cond();
    if (tok == ':') {
        is_range = 1;
        next();
        expr_cond();
        if (nb != 1)
            tcc_error("a range yields only an index: write 'foreach |i| lo:hi'");
        if (!c6_is_int_type(&vtop[-1].type) || !c6_is_int_type(&vtop->type))
            tcc_error("range bounds must be integers");
        ctr = c6_common_int_type(&vtop[-1].type, &vtop->type);
        size = type_size(&ctr, &align);
        /* the upper bound goes to a hidden slot ... */
        loc = (loc - size) & -align;
        hi_off = loc;
        vset(&ctr, VT_LOCAL | VT_LVAL, hi_off);
        vswap();
        vstore();
        vpop();
        /* ... and the binder is the counter, so it starts at the lower
           bound (the store converts it to the common type) */
        loc = (loc - size) & -align;
        i_off = loc;
        if (n1)
            sym_push(n1, &ctr, VT_LOCAL | VT_LVAL, i_off);
        vset(&ctr, VT_LOCAL | VT_LVAL, i_off);
        vswap();
        vstore();
        vpop();
    } else {
        t = vtop->type;
        if (nb != 2)
            tcc_error("an array or slice yields an index and a value: "
                      "write 'foreach |i, v| a'");
        if (t.t & VT_VLA)
            tcc_error("cannot iterate over a variable length array");
        if (t.t & VT_ARRAY) {
            if (!t.ref || t.ref->c < 0)
                tcc_error("cannot iterate over an array of unknown length");
            elem = *pointed_type(&t);
            elem.t &= ~VT_QUAL;
            pair = c6_slice_spill(NULL);
        } else if ((sl = c6_slice_of_type(&t)) != NULL) {
            elem = sl->elem;
            pair = c6_slice_spill(sl);
        } else {
            type_to_str(buf, sizeof buf, &t, NULL);
            if ((t.t & VT_BTYPE) == VT_PTR)
                tcc_error("cannot iterate over '%s' (use a slice parameter "
                          "'T[:]', or '(T[:]){p, n}')", buf);
            if (c6_is_int_type(&t))
                tcc_error("cannot iterate over '%s' (write a range: "
                          "'foreach |i| lo:hi')", buf);
            tcc_error("cannot iterate over '%s' (expose a slice member, "
                      "e.g. 'T[:] items')", buf);
        }
        if (!n1 && !n2)
            tcc_error("'foreach' discards both binders");
        if (elem.t & VT_ARRAY) {
            type_to_str(buf, sizeof buf, &elem, NULL);
            tcc_error("'foreach' cannot copy an element of type '%s'", buf);
        }
        size = type_size(&elem, &align);
        if (size < 0) {
            type_to_str(buf, sizeof buf, &elem, NULL);
            tcc_error("cannot iterate over incomplete element type '%s'", buf);
        }
        ctr = c6_size_type();
        /* hidden slots first, then the two binders */
        loc = (loc - size) & -align;
        v_off = loc;
        loc = (loc - PTR_SIZE) & -PTR_SIZE;
        i_off = loc;
        if (n1)
            sym_push(n1, &ctr, VT_LOCAL | VT_LVAL, i_off);
        if (n2)
            sym_push(n2, &elem, VT_LOCAL | VT_LVAL, v_off);
        vset(&ctr, VT_LOCAL | VT_LVAL, i_off);       /* i = 0 */
        vpushi(0);
        vstore();
        vpop();
    }

    c = gind();
    vset(&ctr, VT_LOCAL | VT_LVAL, i_off);
    vset(&ctr, VT_LOCAL | VT_LVAL, is_range ? hi_off : pair + PTR_SIZE);
    gen_op(TOK_LT);
    a = gvtst(1, 0);

    /* the value binder is loaded from the pair on every iteration */
    if (n2) {
        ept = elem;
        mk_pointer(&ept);
        vset(&elem, VT_LOCAL | VT_LVAL, v_off);     /* destination */
        vset(&ept, VT_LOCAL | VT_LVAL, pair);       /* data */
        vset(&ctr, VT_LOCAL | VT_LVAL, i_off);      /* index */
        gen_op('+');
        indir();
        vstore();
        vpop();
    }
    if (tok != '{')
        expect("'{'");                              /* the body is a block */
    b = 0;                      /* no 'continue' jumps yet */
    lblock(&a, &b);

    /* 'continue' lands on the increment */
    d = gind();
    gsym_addr(b, d);
    vset(&ctr, VT_LOCAL | VT_LVAL, i_off);
    vpushi(1);
    gen_op('+');
    vset(&ctr, VT_LOCAL | VT_LVAL, i_off);
    vswap();
    vstore();
    vpop();
    gjmp_addr(c);
    gsym(a);

    prev_scope(&o, 0);
}

/* ---------------------------------------------------------------------- */
/* reference counted managed references '@T'                               */
/*                                                                         */
/* A managed local is released like a 'defer': the declaration registers a  */
/* cleanup site whose body is '__c6_gc_release(&local, mode)'.  Going        */
/* through the existing cleanup machinery covers every way out of a scope   */
/* (end of block, return, break, continue, goto and the 'err()' path) and,   */
/* because the body recomputes the address from the frame pointer, it also   */
/* stays correct when an 'async' body moves its own frame.                  */
/*                                                                         */
/* Everything a store or a release does is decided here and executed by the  */
/* runtime (lib/libtcc1.c): the code generator only tells it which kind of   */
/* slot it is ('weak @T' / 'own @T' / 'shared @T') and whether the value is  */
/* a fresh reference ('new T').                                            */

/* the GC qualifier of a managed reference type, 0 for anything else.  The
   trusted copy lives in the pointee symbol (see mk_pointer), so a CType whose
   'gq' byte was never initialised cannot be mistaken for a managed one. */
static int c6_type_gq(CType *type)
{
    if ((type->t & VT_BTYPE) != VT_PTR || !type->ref)
        return 0;
    return type->ref->type.gq & (C6_GQ_MANAGED | C6_GQ_QUAL);
}

static int c6_is_managed(CType *type)
{
    return (c6_type_gq(type) & C6_GQ_MANAGED) != 0;
}

/* the same pointer type, but not a managed reference: a plain word.  Used
   wherever the compiler itself handles the raw slot of a managed object. */
static void c6_gc_plain(CType *type)
{
    CType inner;

    if (!c6_is_managed(type))
        return;
    inner = *pointed_type(type);
    mk_pointer(&inner);
    *type = inner;
}

/* the runtime mode of a managed type: what kind of slot it is */
static int c6_gc_mode(CType *t)
{
    int m = 0, gq = c6_type_gq(t);

    if (gq & C6_GQ_WEAK)
        m |= C6_GCM_WEAK;
    if (gq & C6_GQ_OWN)
        m |= C6_GCM_OWN;
    if (gq & C6_GQ_SHARED)
        m |= C6_GCM_ATOMIC;
    return m;
}

/* what releasing a slot of this type does: the same mapping the field table
   uses ('C6_GCK_*' of the runtime) */
static int c6_gc_slot_kind(CType *t)
{
    int gq = c6_type_gq(t);

    if (gq & C6_GQ_WEAK)
        return C6_GCK_WEAK;
    if (gq & C6_GQ_OWN)
        return C6_GCK_OWN;
    return C6_GCK_STRONG;
}

/* A managed object with static storage is a root: it outlives every scope, so
   the runtime releases it at exit instead.  The address is handed over from a
   constructor, because 'new T' cannot appear in a static initializer. */
static void c6_gc_root_add(int tok, CType *type)
{
    CType elem;
    C6GcRoot *r;
    int count, stride;

    if (!tcc_state->c6_ext || !c6_gc_elem(type, &elem, &count, &stride))
        return;
    if (!c6_is_managed(&elem))
        return;
    r = tcc_mallocz(sizeof *r);
    r->tok = tok;
    r->kind = c6_gc_slot_kind(&elem);
    r->count = count;
    r->stride = stride;
    dynarray_add(&tcc_state->c6_gc_roots, &tcc_state->c6_nb_gc_roots, r);
}

/* 'void *', for the slot arguments of the runtime helpers */
static void c6_gc_voidptr(CType *t)
{
    t->t = VT_VOID;
    t->ref = NULL;
    t->gq = 0;
    mk_pointer(t);
}

/* register the cleanup site that releases the managed local at frame
   offset OFF */
static void c6_gc_site(int off, CType *type)
{
    CType vt;
    int skip, id, mode = c6_gc_mode(type);
    Sym *l;

    if (!defer_state.enabled || nocode_wanted || off >= 0)
        return;                     /* parameters are borrowed */
    if ((c6_type_gq(type) & C6_GQ_WEAK) && c6_async_in_body)
        tcc_error("a 'weak' reference cannot be declared in an async "
                  "function: its frame is moved to the task");
    id = ++defer_state.next_site;
    l = c6_new_label("gcr");
    vpush_helper_func(tok_alloc_const("__tcc_defer_push"));
    vpushi(id);
    vpushi(local_scope);
    vpushi(0);                      /* kind: defer, it runs on every exit */
    gfunc_call(3);
    skip = gjmp(0);                 /* registration time jumps over the body */
    c6_def_label(l);
    c6_add_site(id, l);
    defer_state.has_sites = 1;
    c6_rt_begin("__c6_gc_release");
    vt = *type;
    c6_gc_plain(&vt);               /* the slot itself, as a plain pointer */
    vset(&vt, VT_LOCAL | VT_LVAL, off);
    gaddrof();
    vpushi(mode);
    c6_rt_end("__c6_gc_release", 2);
    defer_state.disp_jmps = gjmp(defer_state.disp_jmps);
    gsym(skip);
}

/* ---------------------------------------------------------------------- */
/* managed field metadata                                                 */
/*                                                                         */
/* A struct that holds managed references needs a description of where     */
/* they are, so that the runtime can release them when the object dies.    */
/* The description is one static record per struct, emitted as ordinary    */
/* source at the point where the struct is defined - the fields and their  */
/* offsets are known there, and a 'new T' anywhere later in the unit just  */
/* passes its address, so there is no ordering problem and no fixpoint.    */
/*                                                                         */
/* Field kinds are the runtime's 'struct __c6_gc_field.kind':              */
/*   STRONG '@T', WEAK 'weak @T', OWN 'own @T', EMBED a struct by value.   */

static C6GcType *c6_gc_type_find(int tag)
{
    int i;

    if (!tcc_state->c6_ext || tag <= 0)
        return NULL;
    for (i = 0; i < tcc_state->c6_nb_gc_types; i++)
        if (tcc_state->c6_gc_types[i] && tcc_state->c6_gc_types[i]->tag == tag)
            return tcc_state->c6_gc_types[i];
    return NULL;
}

/* fill in the size of a description record from the struct it describes.
   An incomplete struct has none yet ('new T' needs the complete type, so the
   record only reaches the runtime when it is complete). */
static void c6_gc_size_of(C6GcType *gt, CType *st)
{
    int align = 0;

    if (gt->size || !st->ref || st->ref->c < 0)
        return;
    gt->size = type_size(st, &align);
    gt->align = align;
}

/* the element type of TYPE after peeling every array level, plus the number
   of elements and the stride between two of them.  0 when TYPE cannot be
   walked (flexible array member, variable length array, not an object). */
static int c6_gc_elem(CType *type, CType *elem, int *count, int *stride)
{
    CType e = *type;
    int n = 1, align = 0, size;

    while ((e.t & VT_ARRAY) && e.ref) {
        if (e.ref->c <= 0)
            return 0;
        n *= e.ref->c;
        e = e.ref->type;
    }
    if (e.t & (VT_ARRAY | VT_VLA))
        return 0;
    size = type_size(&e, &align);
    if (size <= 0)
        return 0;
    *elem = e;
    *count = n;
    *stride = size;
    return 1;
}

/* Descriptors are emitted at the end of the unit, when every 'drop' method is
   known; a 'new T' in the meantime only declares the symbol ('extern'), so
   the address it passes is resolved at the definition like any other global.
   The names carry a per-unit counter plus a hash of the source file name:
   every unit gets its own copy and no two units that include the same header
   can collide. */
static unsigned c6_gc_tu_hash(void)
{
    static unsigned h;
    const char *p;

    if (!h) {
        h = 2166136261u;
        for (p = file->filename; p && *p; p++)
            h = (h ^ (unsigned char)*p) * 16777619u;
        if (!h)
            h = 1;
    }
    return h;
}

/* '__attribute__ ((WHICH))' where WHICH is a keyword token such as
   TOK_CONSTRUCTOR1: tcc reads it before the declarator, as in
   tests/tests2/108_constructor.c */
static void c6_gc_attr(TokenString *ts, int which)
{
    tok_str_add(ts, TOK_ATTRIBUTE1);
    tok_str_add(ts, '(');
    tok_str_add(ts, '(');
    tok_str_add(ts, which);
    tok_str_add(ts, ')');
    tok_str_add(ts, ')');
}

static int c6_gc_name(const char *fmt, int n)
{
    char buf[64];

    snprintf(buf, sizeof buf, fmt, c6_gc_tu_hash(), n);
    return tok_alloc_const(buf);
}

/* 'extern struct __c6_gc_desc __c6_gc_d<hash>_<n>;' - emitted where the type
   is first used, so a 'new T' anywhere later can take its address */
static void c6_gc_desc_decl(C6GcType *gt)
{
    TokenString *ts;

    if (gt->declared)
        return;
    gt->declared = 1;
    gt->desc_tok = c6_gc_name("__c6_gc_d%08x_%d", ++tcc_state->c6_gc_seq);
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_EXTERN);
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, tok_alloc_const("__c6_gc_desc"));
    tok_str_add(ts, gt->desc_tok);
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);
}

/* the record of a struct used by 'new T': created on demand, so that a 'drop'
   method declared later still reaches the objects made here */
static int c6_gc_desc_for(CType *type)
{
    C6GcType *gt;
    int tag;

    if ((type->t & VT_BTYPE) != VT_STRUCT || !type->ref)
        return 0;                   /* no fields and no 'drop' possible */
    tag = type->ref->v & ~SYM_STRUCT;
    gt = c6_gc_type_find(tag);
    if (!gt) {
        gt = tcc_mallocz(sizeof *gt);
        gt->tag = tag;
        gt->src_file = tcc_strdup(file->filename);
        gt->src_line = file->line_num;
        dynarray_add(&tcc_state->c6_gc_types, &tcc_state->c6_nb_gc_types, gt);
    }
    c6_gc_size_of(gt, type);        /* 'new T' has the complete type */
    c6_gc_desc_decl(gt);
    return gt->desc_tok;
}

/* 'static struct __c6_gc_field __c6_gc_fH_N[] = {...};' and
   'struct __c6_gc_desc __c6_gc_dH_N = {...};' - at the end of the unit */
static void c6_gc_emit_desc(C6GcType *gt)
{
    TokenString *ts;
    int arr = 0, i;

    if (gt->emitted)
        return;
    c6_gc_desc_decl(gt);
    gt->emitted = 1;
    if (gt->nfields) {
        arr = c6_gc_name("__c6_gc_f%08x_%d", ++tcc_state->c6_gc_seq);
        ts = tok_str_alloc();
        tok_str_add(ts, TOK_STATIC);
        tok_str_add(ts, TOK_STRUCT);
        tok_str_add(ts, tok_alloc_const("__c6_gc_field"));
        tok_str_add(ts, arr);
        tok_str_add(ts, '[');
        tok_str_add(ts, ']');
        tok_str_add(ts, '=');
        tok_str_add(ts, '{');
        for (i = 0; i < gt->nfields; i++) {
            C6GcField *f = &gt->fields[i];
            tok_str_add(ts, '{');
            c6_async_add_int(ts, f->off);
            tok_str_add(ts, ',');
            c6_async_add_int(ts, f->kind);
            tok_str_add(ts, ',');
            c6_async_add_int(ts, f->count);
            tok_str_add(ts, ',');
            c6_async_add_int(ts, f->stride);
            tok_str_add(ts, ',');
            if (f->kind == C6_GCK_EMBED) {
                C6GcType *sub = c6_gc_type_find(f->sub_tag);
                if (!sub)
                    tcc_error("internal: no descriptor for the embedded struct");
                c6_gc_desc_decl(sub);   /* declared above, defined below */
                tok_str_add(ts, '&');
                tok_str_add(ts, sub->desc_tok);
            } else {
                c6_async_add_int(ts, 0);
            }
            tok_str_add(ts, '}');
            tok_str_add(ts, ',');
        }
        tok_str_add(ts, '}');
        tok_str_add(ts, ';');
        tok_str_add(ts, TOK_EOF);
        c6_decl_tokens(ts);
    }

    ts = tok_str_alloc();
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, tok_alloc_const("__c6_gc_desc"));
    tok_str_add(ts, gt->desc_tok);
    tok_str_add(ts, '=');
    tok_str_add(ts, '{');
    c6_async_add_int(ts, gt->size);
    tok_str_add(ts, ',');
    c6_async_add_int(ts, gt->align);
    tok_str_add(ts, ',');
    c6_async_add_int(ts, gt->nfields);
    tok_str_add(ts, ',');
    c6_async_add_int(ts, gt->has_weak ? C6_GCD_HAS_WEAK : 0);
    tok_str_add(ts, ',');
    if (gt->drop_tok) {
        /* the same '(void (*)(void *))sym' spelling the vtable objects use */
        tok_str_add(ts, '(');
        tok_str_add(ts, TOK_VOID);
        tok_str_add(ts, '*');
        tok_str_add(ts, ')');
        tok_str_add(ts, '(');
        tok_str_add(ts, TOK_VOID);
        tok_str_add(ts, '*');
        tok_str_add(ts, ')');
        tok_str_add(ts, gt->drop_tok);
    } else {
        c6_async_add_int(ts, 0);    /* no destructor */
    }
    tok_str_add(ts, ',');
    if (arr)
        tok_str_add(ts, arr);
    else
        c6_async_add_int(ts, 0);
    tok_str_add(ts, '}');
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);
}

/* ---- static cycle detection ---------------------------------------- */
/* A strong reference graph with a cycle can never be freed by counting: the
   objects keep each other alive.  It is rejected at compile time, with the
   path that closes the loop, and the escape hatches are 'weak' (does not
   keep the target alive) and 'own' (exclusive, freed down the chain). */

#define C6_GC_PATH_MAX 32
static C6GcType *c6_gc_cycle_path[C6_GC_PATH_MAX];
static int c6_gc_cycle_via[C6_GC_PATH_MAX];   /* the field used to get there */

static void c6_gc_cycle_report(int from, int depth);

/* 0 = no cycle found, 1 = a cycle was reported (compilation stops there) */
static int c6_gc_cycle_dfs(C6GcType *gt, int depth)
{
    int i;

    if (gt->mark == 2)
        return 0;
    if (gt->mark == 1) {
        /* back edge: the path from GT to itself */
        c6_gc_cycle_report(gt->tag, depth);
        return 1;
    }
    if (depth >= C6_GC_PATH_MAX) {
        /* a chain longer than the buffer: report it as a whole */
        c6_gc_cycle_report(gt->tag, depth);
        return 1;
    }
    gt->mark = 1;
    c6_gc_cycle_path[depth] = gt;
    for (i = 0; i < gt->nfields; i++) {
        C6GcField *f = &gt->fields[i];
        C6GcType *to;

        if (!f->to_tag)
            continue;               /* 'weak' and 'own' are not edges */
        to = c6_gc_type_find(f->to_tag);
        if (!to || !to->nfields)
            continue;
        c6_gc_cycle_via[depth] = f->name_tok;
        if (c6_gc_cycle_dfs(to, depth + 1))
            return 1;
    }
    gt->mark = 2;
    return 0;
}

/* 'reference cycle: A -> B -> A; break it with 'weak' or 'own'' */
static void c6_gc_cycle_report(int from, int depth)
{
    char buf[512];
    int i, start = 0, len = 0;

    if (depth > C6_GC_PATH_MAX)
        depth = C6_GC_PATH_MAX;
    /* print from the first occurrence of the type that closes the loop */
    for (i = 0; i < depth; i++)
        if (c6_gc_cycle_path[i]->tag == from) {
            start = i;
            break;
        }
    buf[0] = 0;
    for (i = start; i < depth; i++) {
        len += snprintf(buf + len, sizeof buf - len, "%s -%s-> ",
                        get_tok_str(c6_gc_cycle_path[i]->tag, NULL),
                        c6_gc_cycle_via[i]
                            ? get_tok_str(c6_gc_cycle_via[i], NULL) : "?");
        if (len >= (int)sizeof buf - 8)
            break;
    }
    snprintf(buf + len, sizeof buf - len, "%s", get_tok_str(from, NULL));
    {
        C6GcType *gt = c6_gc_cycle_path[start];

        tcc_error("reference cycle: %s; break it with 'weak' or 'own' "
                  "(type '%s' at %s:%d)", buf, get_tok_str(gt->tag, NULL),
                  gt->src_file ? gt->src_file : "?", gt->src_line);
    }
}

static void c6_gc_cycle_check(void)
{
    int i;

    for (i = 0; i < tcc_state->c6_nb_gc_types; i++) {
        C6GcType *gt = tcc_state->c6_gc_types[i];
        if (gt && gt->nfields && gt->mark != 2)
            c6_gc_cycle_dfs(gt, 0);
    }
}

/* emit the descriptors of the unit: once the parser has seen every struct,
   every 'drop' method and every 'static inline' body */
static void c6_gc_flush_descs(void)
{
    int i;

    c6_gc_cycle_check();
    for (i = 0; i < tcc_state->c6_nb_gc_types; i++) {
        C6GcType *gt = tcc_state->c6_gc_types[i];
        if (gt && (gt->declared || gt->nfields || gt->drop_tok))
            c6_gc_emit_desc(gt);
    }
    if (tcc_state->c6_nb_gc_roots) {
        TokenString *ts;
        int f = c6_gc_name("__c6_gc_roots%08x_%d", ++tcc_state->c6_gc_seq);

        ts = tok_str_alloc();
        tok_str_add(ts, TOK_STATIC);
        tok_str_add(ts, TOK_VOID);
        c6_gc_attr(ts, TOK_CONSTRUCTOR1);
        tok_str_add(ts, f);
        tok_str_add(ts, '(');
        tok_str_add(ts, TOK_VOID);
        tok_str_add(ts, ')');
        tok_str_add(ts, '{');
        for (i = 0; i < tcc_state->c6_nb_gc_roots; i++) {
            C6GcRoot *r = tcc_state->c6_gc_roots[i];
            tok_str_add(ts, tok_alloc_const("__c6_gc_root"));
            tok_str_add(ts, '(');
            tok_str_add(ts, '&');
            tok_str_add(ts, r->tok);
            tok_str_add(ts, ',');
            c6_async_add_int(ts, r->kind);
            tok_str_add(ts, ',');
            c6_async_add_int(ts, (int)r->count);
            tok_str_add(ts, ',');
            c6_async_add_int(ts, (int)r->stride);
            tok_str_add(ts, ')');
            tok_str_add(ts, ';');
        }
        tok_str_add(ts, '}');
        tok_str_add(ts, TOK_EOF);
        c6_decl_tokens(ts);
        /* and a destructor that releases them at exit */
        f = c6_gc_name("__c6_gc_exit%08x_%d", ++tcc_state->c6_gc_seq);
        ts = tok_str_alloc();
        tok_str_add(ts, TOK_STATIC);
        tok_str_add(ts, TOK_VOID);
        c6_gc_attr(ts, TOK_DESTRUCTOR1);
        tok_str_add(ts, f);
        tok_str_add(ts, '(');
        tok_str_add(ts, TOK_VOID);
        tok_str_add(ts, ')');
        tok_str_add(ts, '{');
        tok_str_add(ts, tok_alloc_const("__c6_gc_shutdown"));
        tok_str_add(ts, '(');
        tok_str_add(ts, ')');
        tok_str_add(ts, ';');
        tok_str_add(ts, '}');
        tok_str_add(ts, TOK_EOF);
        c6_decl_tokens(ts);
    }
}

/* called where a struct definition is complete: record where its managed
   references live and emit the descriptor */
static void c6_gc_collect(CType *type, int is_union)
{
    Sym *s, *f;
    C6GcType *gt;
    C6GcField *fields = NULL;
    int n = 0, cap = 0, tag, has_weak = 0;

    if (!tcc_state->c6_ext || !type->ref)
        return;
    /* a union is a struct with the VT_UNION flag, so VT_BTYPE is enough */
    if ((type->t & VT_BTYPE) != VT_STRUCT)
        return;
    s = type->ref;
    tag = s->v & ~SYM_STRUCT;
    if (s->c < 0 || !s->next)
        return;                     /* incomplete or empty */
    for (f = s->next; f; f = f->next) {
        CType elem;
        int count = 0, stride = 0, kind = 0, sub = 0, gq;

        if (f->type.t & VT_BITFIELD)
            continue;
        if (!c6_gc_elem(&f->type, &elem, &count, &stride)) {
            /* a flexible array member hides how many elements there are */
            if ((f->type.t & VT_ARRAY) && f->type.ref
                && (c6_type_gq(&f->type.ref->type) & C6_GQ_MANAGED)) {
                char b[160];
                type_to_str(b, sizeof b, type, NULL);
                tcc_error("'%s' has a flexible array of managed references: "
                          "allocate it with 'new T[n]' instead", b);
            }
            continue;
        }
        gq = c6_type_gq(&elem);
        if (gq & C6_GQ_MANAGED) {
            if (gq & C6_GQ_WEAK)
                kind = C6_GCK_WEAK, has_weak = 1;
            else if (gq & C6_GQ_OWN)
                kind = C6_GCK_OWN;
            else
                kind = C6_GCK_STRONG;
        } else if ((elem.t & VT_BTYPE) == VT_STRUCT && elem.ref) {
            C6GcType *sub_gt = c6_gc_type_find(elem.ref->v & ~SYM_STRUCT);
            if (sub_gt) {
                kind = C6_GCK_EMBED;
                sub = sub_gt->tag;
            }
        }
        if (!kind)
            continue;
        if (is_union) {
            char b[160];
            type_to_str(b, sizeof b, type, NULL);
            tcc_error("'%s' is a union with a managed field: which member is "
                      "alive cannot be known, use a struct", b);
        }
        if (n >= cap) {
            cap = cap ? cap * 2 : 8;
            fields = tcc_realloc(fields, cap * (int)sizeof *fields);
        }
        fields[n].off = f->c;
        fields[n].kind = kind;
        fields[n].count = count;
        fields[n].stride = stride;
        fields[n].sub_tag = sub;
        /* the graph edge: a strong reference points at its pointee, an
           embedded struct is part of this object (weak/own own neither) */
        fields[n].name_tok = f->v & ~SYM_FIELD;
        if (kind == C6_GCK_EMBED) {
            fields[n].to_tag = sub;
        } else if (kind == C6_GCK_STRONG
                   && (elem.t & VT_BTYPE) == VT_PTR && elem.ref
                   && (elem.ref->type.t & VT_BTYPE) == VT_STRUCT
                   && elem.ref->type.ref)
            /* the pointee's struct tag */
            fields[n].to_tag = elem.ref->type.ref->v & ~SYM_STRUCT;
        else
            fields[n].to_tag = 0;
        n++;
    }
    if (!n) {
        C6GcType *egt = c6_gc_type_find(tag);
        tcc_free(fields);
        /* nothing managed: no field table, but a 'drop' may still be there */
        if (egt)
            c6_gc_size_of(egt, type);
        return;
    }
    if (local_scope != 0) {
        char b[160];
        type_to_str(b, sizeof b, type, NULL);
        tcc_error("'%s' holds managed references and is defined in a block: "
                  "define it at file scope", b);
    }
    gt = c6_gc_type_find(tag);
    if (gt) {
        if (gt->nfields) {
            tcc_free(fields);
            return;                 /* already collected */
        }
        tcc_free(gt->fields);
    } else {
        gt = tcc_mallocz(sizeof *gt);
        gt->tag = tag;
        gt->src_file = tcc_strdup(file->filename);
        gt->src_line = file->line_num;
        dynarray_add(&tcc_state->c6_gc_types, &tcc_state->c6_nb_gc_types, gt);
    }
    gt->nfields = n;
    gt->fields = fields;
    gt->size = s->c;
    gt->align = s->r;
    gt->has_weak = has_weak;
}

/* A struct that holds managed references cannot be copied: the copy would
   duplicate the references and nothing would release them.  Such a struct
   only lives in the heap, made by 'new'. */
static void c6_gc_value_check(CType *type, const char *what)
{
    CType elem;
    C6GcType *gt;
    int count, stride;

    if (!tcc_state->c6_ext)
        return;
    if (!c6_gc_elem(type, &elem, &count, &stride))
        return;
    if ((elem.t & VT_BTYPE) != VT_STRUCT || !elem.ref)
        return;
    gt = c6_gc_type_find(elem.ref->v & ~SYM_STRUCT);
    if (gt && gt->nfields) {
        char buf[160];
        type_to_str(buf, sizeof buf, &elem, NULL);
        tcc_error("'%s' holds managed references: %s is not possible. "
                  "Allocate it with 'new' and hold it in '@%s'",
                  buf, what, buf);
    }
}

/* true while a store into a managed slot is being handled */
/* Rewrite 'slot = value' when SLOT is a managed reference.  The runtime has
   to increment the new value before it drops the old one, so a value that is
   the last reference to itself stays alive while it is being stored. */
static int c6_gc_store_hook(void)
{
    CType *dt, vt;
    SValue dst, val;
    const char *name;
    int mode, own, is_null;

    if (!tcc_state->c6_ext || c6_gc_no_hook)
        return 0;
    dt = &vtop[-1].type;
    if (!c6_is_managed(dt)) {
        /* a brand new reference has no other owner: storing it in a plain
           pointer would leak the object (a borrowed one is a plain escape,
           which the design allows) */
        if (vtop->gc_state == C6_GC_FRESH) {
            char b[160];
            type_to_str(b, sizeof b, dt, NULL);
            tcc_error("a new reference cannot be stored in '%s': it is not "
                      "a managed reference (declare it as '@T')", b);
        }
        return 0;
    }

    if ((dt->gq & C6_GQ_BORROW) && !c6_gc_init_store) {
        /* parameters (and 'async' parameters) borrow the caller's or the
           task's reference: overwriting the slot would release something
           this frame does not own */
        char b[160];
        type_to_str(b, sizeof b, dt, NULL);
        tcc_error("'%s' is a borrow: it owns nothing, so it cannot be "
                  "reassigned (copy it into an owned local first)", b);
    }
    is_null = (vtop->r & VT_VALMASK) == VT_CONST
        && !(vtop->r & (VT_LVAL | VT_SYM)) && vtop->c.i == 0;
    if (vtop->gc_state != C6_GC_FRESH && !c6_is_managed(&vtop->type)
        && !is_null) {
        char b1[160], b2[160];
        type_to_str(b1, sizeof b1, &vtop->type, NULL);
        type_to_str(b2, sizeof b2, dt, NULL);
        tcc_error("cannot assign a '%s' to '%s': a managed reference is "
                  "only made by 'new', by another managed reference or by "
                  "the empty value", b1, b2);
    }
    mode = c6_gc_mode(dt);
    if (vtop->gc_state == C6_GC_FRESH)
        mode |= C6_GCM_FRESH;
    if (c6_gc_init_store)
        mode |= C6_GCM_INIT;
    if ((c6_type_gq(&vtop->type) & C6_GQ_WEAK) && !(mode & C6_GCM_WEAK)) {
        /* 'q = w' is the upgrade: an atomic try-incref out of the weak slot,
           which replaces (and releases) the old content of DST */
        if (!(vtop->r & VT_LVAL)) {
            char b1[160], b2[160];
            type_to_str(b1, sizeof b1, &vtop->type, NULL);
            type_to_str(b2, sizeof b2, dt, NULL);
            tcc_error("a '%s' can only be upgraded out of a slot, not from "
                      "a '%s' expression (assign it to a 'weak @T' first)",
                      b1, b2);
        }
        dst = vtop[-1];
        val = *vtop;
        vpop();
        vpop();
        c6_rt_begin("__c6_gc_weak_store");
        vpushv(&dst);
        gaddrof();                  /* the destination slot */
        vpushv(&val);
        gaddrof();                  /* the weak slot to read */
        vpushi(mode);
        c6_rt_end("__c6_gc_weak_store", 3);
        vt = *dt;
        vtop->type = vt;
        vtop->gc_state = C6_GC_BORROW;
        return 1;
    }
    if ((mode & C6_GCM_ATOMIC) && vtop->gc_state == C6_GC_FRESH
        && !vtop->gc_atomic) {
        /* the count lives in the object, so a 'shared' reference to an
           object whose count is not atomic would race */
        tcc_error("a 'shared' slot needs an object with an atomic count: "
                  "create it with 'new shared T', not 'new T'");
    }
    own = (mode & C6_GCM_OWN) != 0;
    if (own && !(mode & C6_GCM_FRESH) && !is_null) {
        char b[160];
        type_to_str(b, sizeof b, dt, NULL);
        tcc_error("'%s' takes ownership: only a fresh reference ('new') or "
                  "the empty value can be stored in it", b);
    }
    dst = vtop[-1];
    val = *vtop;
    vpop();
    vpop();
    name = own ? "__c6_gc_own_store" : "__c6_gc_store";
    c6_rt_begin(name);
    vpushv(&dst);
    gaddrof();                      /* the address of the slot */
    vpushv(&val);
    vpushi(mode);
    c6_rt_end(name, 3);
    vt = *dt;
    vtop->type = vt;                /* the value of the assignment */
    vtop->gc_state = C6_GC_BORROW;
    return 1;
}

/* can TOKEN start a type?  Used to tell 'new T' from a variable named
   'new'. */
static int c6_gc_type_lead(int t)
{
    Sym *s;

    switch (t) {
    case '@': case TOK_STRUCT: case TOK_UNION: case TOK_ENUM:
    case TOK_VOID: case TOK_CHAR: case TOK_SHORT: case TOK_INT:
    case TOK_LONG: case TOK_FLOAT: case TOK_DOUBLE: case TOK_BOOL:
    case TOK_SIGNED1: case TOK_SIGNED2: case TOK_SIGNED3: case TOK_UNSIGNED:
    case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
    case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
    case TOK_RESTRICT1: case TOK_RESTRICT2: case TOK_RESTRICT3:
    case TOK__Atomic:
        return 1;
    }
    if (t < TOK_IDENT)
        return 0;
    s = sym_find(t);
    if (s && (s->type.t & VT_TYPEDEF))
        return 1;
    if (struct_find(t))
        return 1;
    return c6_gc_qual(t) != 0;      /* 'new shared T' */
}

/* is the current token a 'new' that starts an allocation? */
static int c6_gc_new_lead(void)
{
    int t, save;

    if (!tcc_state->c6_ext || tok < TOK_IDENT)
        return 0;
    if (c6_soft_kw_shadowed(tok) || strcmp(get_tok_str(tok, NULL), "new"))
        return 0;
    save = tok;
    next();
    t = tok;
    unget_tok(save);
    return c6_gc_type_lead(t);
}

/* 'new T', 'new T[n]', 'new shared T': a fresh reference to a zeroed
   object (or to COUNT zeroed elements) */
static void c6_gc_new_expr(void)
{
    CType type;
    AttributeDef ad;
    int flags = 0, align, size;

    next();                             /* 'new' */
    if (tok >= TOK_IDENT && !c6_soft_kw_shadowed(tok)
        && c6_gc_qual(tok) == C6_GQ_SHARED) {
        flags |= C6_GCM_ATOMIC;
        next();
    }
    parse_btype(&type, &ad, 0);
    type.t &= ~VT_QUAL;
    if ((type.t & VT_BTYPE) == VT_VOID)
        tcc_error("'new' needs an object type, not 'void'");
    if (type.t & VT_ARRAY)
        tcc_error("'new' takes the element type: write 'new T[n]'");
    align = 0;
    size = type_size(&type, &align);
    c6_rt_begin("__c6_gc_new");
    vpushs(size);                       /* the size of one element */
    if (tok == '[') {
        next();
        expr_eq();                      /* the element count */
        skip(']');
    } else {
        vpushs(1);
    }
    {
        /* the type descriptor: where the managed fields are, or a null
           pointer when the type holds none */
        int dtok = c6_gc_desc_for(&type);
        Sym *ds = dtok ? sym_find(dtok) : NULL;
        if (ds) {
            vpushsym(&ds->type, ds);
            vtop->r |= VT_LVAL;         /* the descriptor as an lvalue ... */
            gaddrof();                  /* ... and its address */
            vtop->type = char_pointer_type;
        } else {
            vpushs(0);
        }
    }
    vpushs(flags);
    c6_rt_end("__c6_gc_new", 4);
    mk_pointer(&type);
    type.gq = C6_GQ_MANAGED;
    type.ref->type.gq = type.gq;
    vtop->type = type;
    vtop->gc_state = C6_GC_FRESH;
    vtop->gc_atomic = (flags & C6_GCM_ATOMIC) != 0;
}

/* is the current token a 'move' call? */
static int c6_gc_move_lead(void)
{
    int save, r = 0;

    if (!tcc_state->c6_ext || tok < TOK_IDENT)
        return 0;
    if (c6_soft_kw_shadowed(tok) || strcmp(get_tok_str(tok, NULL), "move"))
        return 0;
    save = tok;
    next();
    if (tok == '(')
        r = 1;
    unget_tok(save);
    return r;
}

/* 'move(p)': hand over the reference held in the slot P (design §3.4).  The
   slot is emptied without a release, so the value keeps exactly the one
   reference it had and the scope exit has nothing left to do.  This is what
   makes 'own' usable: an owned chain is built from 'new' or from 'move'. */
static void c6_gc_move_expr(void)
{
    SValue slot;
    CType st;

    next();                             /* 'move' */
    skip('(');
    unary();
    if (!c6_is_managed(&vtop->type) || !(vtop->r & VT_LVAL)) {
        char b[160];
        type_to_str(b, sizeof b, &vtop->type, NULL);
        tcc_error("'move' needs a managed reference held in a slot, not "
                  "a '%s'", b);
    }
    st = vtop->type;
    slot = *vtop;
    vpop();
    c6_rt_begin("__c6_gc_move");
    vpushv(&slot);
    gaddrof();                          /* the address of the slot */
    c6_rt_end("__c6_gc_move", 1);
    vtop->type = st;
    vtop->gc_state = C6_GC_FRESH;       /* the one reference moved with it */
    vtop->gc_atomic = (c6_type_gq(&st) & C6_GQ_SHARED) != 0;
    skip(')');
}

/* Frame slots that hold fresh call arguments until the call has returned.
   The values must outlive the call but nothing owns them afterwards, so
   each one is moved out of the argument list into its own slot and the
   slots are released once the call is done. */
static int *c6_gc_temps;            /* frame offsets */
static int c6_nb_gc_temps, c6_gc_temps_cap;
static int *c6_gc_marks;            /* saved lengths, one per call */
static int c6_nb_gc_marks, c6_gc_marks_cap;

static void c6_gc_arg_mark(void)
{
    if (!tcc_state->c6_ext)
        return;
    if (c6_nb_gc_marks >= c6_gc_marks_cap) {
        c6_gc_marks_cap = c6_gc_marks_cap ? c6_gc_marks_cap * 2 : 16;
        c6_gc_marks = tcc_realloc(c6_gc_marks,
                                  c6_gc_marks_cap * (int)sizeof(int));
    }
    c6_gc_marks[c6_nb_gc_marks++] = c6_nb_gc_temps;
}

/* move a fresh argument into a slot, keeping a borrowed pointer on the
   stack as the argument itself */
static void c6_gc_arg_temp(void)
{
    CType pt, rt;
    SValue v;
    int off;

    if (!tcc_state->c6_ext || vtop->gc_state != C6_GC_FRESH)
        return;
    loc = (loc - (int)sizeof(void *)) & -(int)PTR_SIZE;
    off = loc;
    v = *vtop;
    vpop();
    c6_gc_voidptr(&pt);
    c6_rt_begin("__c6_gc_store");
    vset(&pt, VT_LOCAL | VT_LVAL, off);
    gaddrof();
    vpushv(&v);
    vpushi(C6_GCM_FRESH | C6_GCM_INIT);    /* a brand new frame slot */
    c6_rt_end("__c6_gc_store", 3);
    rt = v.type;
    vtop->type = rt;
    vtop->gc_state = C6_GC_BORROW;
    if (c6_nb_gc_temps >= c6_gc_temps_cap) {
        c6_gc_temps_cap = c6_gc_temps_cap ? c6_gc_temps_cap * 2 : 16;
        c6_gc_temps = tcc_realloc(c6_gc_temps,
                                  c6_gc_temps_cap * (int)sizeof(int));
    }
    c6_gc_temps[c6_nb_gc_temps++] = off;
}

/* the call has returned: release the fresh arguments of this call.  The
   release is a call, so the result of the call being compiled (if it has
   one) is parked in a frame slot while it runs. */
static void c6_gc_arg_release(int has_value)
{
    CType pt, vt, rt;
    int i, mark, off = 0, align = 0, size = 0;

    if (!tcc_state->c6_ext || c6_nb_gc_marks <= 0)
        return;
    mark = c6_gc_marks[--c6_nb_gc_marks];
    if (c6_nb_gc_temps <= mark)
        return;
    rt.t = VT_INT;
    if (has_value) {
        rt = vtop->type;
        vt = rt;
        c6_gc_plain(&vt);           /* a plain word, no counting */
        size = type_size(&rt, &align);
        loc = (loc - size) & -align;
        off = loc;
        c6_gc_no_hook = 1;
        vset(&vt, VT_LOCAL | VT_LVAL, off);
        vswap();
        vstore();
        vpop();
        c6_gc_no_hook = 0;
    }
    for (i = c6_nb_gc_temps - 1; i >= mark; i--) {
        c6_gc_voidptr(&pt);
        c6_rt_begin("__c6_gc_release");
        vset(&pt, VT_LOCAL | VT_LVAL, c6_gc_temps[i]);
        gaddrof();
        vpushi(0);
        c6_rt_end("__c6_gc_release", 2);
    }
    c6_nb_gc_temps = mark;
    if (has_value) {
        vset(&vt, VT_LOCAL | VT_LVAL, off);
        if (c6_is_managed(&rt))
            vtop->gc_state = C6_GC_FRESH;
    }
}

/* 'return VALUE' of a managed type hands one reference to the caller.  A
   fresh value already is that reference; anything else is counted here,
   because the local that holds it is released on the way out. */
static void c6_gc_return_hook(void)
{
    CType rt = func_vt;
    SValue v;

    if (!tcc_state->c6_ext)
        return;
    c6_gc_value_check(&rt, "a returned value");
    if (c6_type_gq(&rt) & C6_GQ_WEAK) {
        /* a weak reference is a slot address: handing it back by value would
           lose the registration and leak the count the caller gets */
        tcc_error("a weak reference cannot be returned: upgrade it first "
                  "('@T q = w;')");
    }
    if (!c6_is_managed(&rt)) {
        if (vtop->gc_state == C6_GC_FRESH) {
            char b[160];
            type_to_str(b, sizeof b, &rt, NULL);
            tcc_error("a new reference cannot be returned as '%s': the "
                      "caller has no way to release it (return '@T')", b);
        }
        return;
    }
    if (vtop->gc_state == C6_GC_FRESH)
        return;
    v = *vtop;
    vpop();
    c6_rt_begin("__c6_gc_incref");
    vpushv(&v);
    c6_rt_end("__c6_gc_incref", 1);
    vtop->type = rt;
    vtop->gc_state = C6_GC_FRESH;
}

/* a discarded fresh reference ('new T{};') is released right here: the value
   is not in a slot, so the runtime drops it directly */
static void c6_gc_discard_hook(void)
{
    SValue v;

    if (!tcc_state->c6_ext || vtop->gc_state != C6_GC_FRESH)
        return;
    v = *vtop;
    vpop();
    c6_rt_begin("__c6_gc_drop");
    vpushv(&v);
    c6_rt_end("__c6_gc_drop", 1);
}

/* ---------------------------------------------------------------------- */
/* soft keywords                                                           */
/*                                                                         */
/* 'co', 'go' and 'task' are keywords only at the start of a statement      */
/* inside a function, and only when an identifier follows: 'go f();' starts  */
/* a thread, while 'go = 1;', 'go(x);', 'typedef int go; go v;' and 'go:'    */
/* keep using the name.  The statement parser already holds the next token   */
/* in TOK when it dispatches, so the test is a string compare.              */

static int c6_soft_kw(int t)
{
    const char *s;

    if (t < TOK_IDENT)
        return C6_KW_NONE;
    s = get_tok_str(t, NULL);
    switch (*s) {
    case 'c': if (!strcmp(s, "co"))   return C6_KW_CO;   break;
    case 'g': if (!strcmp(s, "go"))   return C6_KW_GO;   break;
    case 't': if (!strcmp(s, "task")) return C6_KW_TASK; break;
    }
    return C6_KW_NONE;
}

/* A typedef of the same name keeps declaring variables: 'co v;' declares a
   variable when 'co' names a type, it never starts a coroutine. */
static int c6_soft_kw_shadowed(int t)
{
    Sym *s = sym_find(t);

    return s && (s->r & VT_TYPEDEF);
}

/* 'weak' / 'own' / 'shared' in front of '@', as a CType.gq bit */
static int c6_gc_qual(int t)
{
    const char *s;

    if (t < TOK_IDENT)
        return 0;
    s = get_tok_str(t, NULL);
    switch (*s) {
    case 'o': if (!strcmp(s, "own"))    return C6_GQ_OWN;    break;
    case 's': if (!strcmp(s, "shared")) return C6_GQ_SHARED; break;
    case 'w': if (!strcmp(s, "weak"))   return C6_GQ_WEAK;   break;
    }
    return 0;
}


/* ---------------------------------------------------------------------- */
/* 'go f(a);' : run f on an OS thread                                      */
/*                                                                         */
/* The arguments are evaluated at the 'go' statement and copied into a heap */
/* block; the runtime starts a thread on a trampoline that unpacks the block */
/* and calls f.  The trampoline is generated at the end of the unit, once per */
/* spawned function, and is hidden from other units - a header full of       */
/* prototypes costs nothing at all until something is spawned.               */
/*                                                                          */
/* Block layout: the parameters in declaration order, each aligned like a    */
/* structure member.  The same loop lays the block out here and in the       */
/* trampoline, so there is one description of the layout.                    */

static int c6_sp_name(const char *prefix, int name, const char *suffix)
{
    char buf[192];

    snprintf(buf, sizeof buf, "%s%s%s", prefix, get_tok_str(name, NULL),
             suffix);
    return tok_alloc_const(buf);
}

/* Can the signature of FT travel through an argument block? */
static int c6_sp_ok(CType *ft)
{
    if ((ft->t & VT_BTYPE) != VT_FUNC || !ft->ref)
        return 0;
    if (ft->ref->f.func_async || ft->ref->f.func_fallible)
        return 0;
    if (ft->ref->f.func_type == FUNC_OLD
        || ft->ref->f.func_type == FUNC_ELLIPSIS)
        return 0;
    return 1;
}

/* Can the type be written back as source?  Types that cannot are not
   spawnable: the trampoline has to spell every parameter out. */
static int c6_sp_type_ok(CType *t)
{
    TokenString *ts = tok_str_alloc();
    int ok = c6_async_type_tokens(ts, t);

    tok_str_free(ts);
    return ok;
}

/* the type of a trampoline, 'void *(*)(void *)', as declared for
   __tcc_th_start() in the predefs */
static CType c6_sp_fn_type(void)
{
    Sym *s = sym_find(tok_alloc_const("__tcc_th_start"));
    CType ft;

    if (!s || (s->type.t & VT_BTYPE) != VT_FUNC || !s->type.ref
        || !s->type.ref->next)
        tcc_error("internal: '__tcc_th_start' is missing");
    ft = s->type.ref->next->type;   /* 'void *(*)(void *)' */
    if ((ft.t & VT_BTYPE) != VT_PTR || !ft.ref)
        tcc_error("internal: '__tcc_th_start' takes no function pointer");
    return ft.ref->type;            /* 'void *(void *)' */
}

static void c6_sp_remember(int name, int tramp, CType *ftype)
{
    C6SpPending *p;

    for (p = c6_sp_pending; p; p = p->next)
        if (p->tramp == tramp)
            return;
    p = tcc_mallocz(sizeof *p);
    p->name = name;
    p->tramp = tramp;
    p->ftype = *ftype;
    p->next = c6_sp_pending;
    c6_sp_pending = p;
}

/* 'static void *__c6_sp_NAME(void *__c6_arg) {
       NAME(*(int *)((char *)__c6_arg + 0), ...);
       return 0;
   }' */
static void c6_sp_emit(C6SpPending *sp)
{
    TokenString *ts;
    Sym *p;
    char *save_pkg;
    int arg = tok_alloc_const("__c6_arg");
    int i, ofs = 0;

    ts = tok_str_alloc();
    tok_str_add(ts, TOK_STATIC);
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, '*');
    tok_str_add(ts, sp->tramp);
    tok_str_add(ts, '(');
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, '*');
    tok_str_add(ts, arg);
    tok_str_add(ts, ')');
    tok_str_add(ts, '{');
    tok_str_add(ts, sp->name);
    tok_str_add(ts, '(');
    for (i = 0, p = c6_async_params(&sp->ftype); p; p = p->next, i++) {
        CType pt = p->type;
        int align, size;

        if (i)
            tok_str_add(ts, ',');
        convert_parameter_type(&pt);
        size = type_size(&pt, &align);
        ofs = (ofs + align - 1) & -align;
        /* *(T *)((char *)__c6_arg + ofs) */
        tok_str_add(ts, '*');
        tok_str_add(ts, '(');
        if (!c6_async_type_tokens(ts, &pt))
            tcc_error("internal: cannot write the signature of 'go'");
        tok_str_add(ts, '*');
        tok_str_add(ts, ')');
        tok_str_add(ts, '(');
        tok_str_add(ts, '(');
        tok_str_add(ts, TOK_CHAR);
        tok_str_add(ts, '*');
        tok_str_add(ts, ')');
        tok_str_add(ts, arg);
        tok_str_add(ts, '+');
        c6_async_add_int(ts, ofs);
        tok_str_add(ts, ')');
        ofs += size;
    }
    tok_str_add(ts, ')');
    tok_str_add(ts, ';');
    /* the argument block owned one reference per managed parameter: give it
       back before the block is freed, or the object would outlive its users */
    for (i = 0, p = c6_async_params(&sp->ftype), ofs = 0; p; p = p->next, i++) {
        CType pt = p->type;
        int a2, sz;

        convert_parameter_type(&pt);
        sz = type_size(&pt, &a2);
        ofs = (ofs + a2 - 1) & -a2;
        if (c6_is_managed(&pt)) {
            tok_str_add(ts, tok_alloc_const("__c6_gc_release"));
            tok_str_add(ts, '(');
            tok_str_add(ts, '(');
            tok_str_add(ts, TOK_VOID);
            tok_str_add(ts, '*');
            tok_str_add(ts, '*');
            tok_str_add(ts, ')');
            tok_str_add(ts, '(');
            tok_str_add(ts, '(');
            tok_str_add(ts, TOK_CHAR);
            tok_str_add(ts, '*');
            tok_str_add(ts, ')');
            tok_str_add(ts, arg);
            tok_str_add(ts, '+');
            c6_async_add_int(ts, ofs);
            tok_str_add(ts, ')');
            tok_str_add(ts, ',');
            c6_async_add_int(ts, c6_gc_mode(&pt));
            tok_str_add(ts, ')');
            tok_str_add(ts, ';');
        }
        ofs += sz;
    }
    tok_str_add(ts, TOK_RETURN);
    c6_async_add_int(ts, 0);
    tok_str_add(ts, ';');
    tok_str_add(ts, '}');
    tok_str_add(ts, TOK_EOF);
    /* the callee name was mangled when it was declared: keep it as it is */
    save_pkg = tcc_state->c6_package;
    tcc_state->c6_package = NULL;
    c6_decl_tokens(ts);
    tcc_state->c6_package = save_pkg;
}

static void c6_sp_flush(void)
{
    C6SpPending *p;

    for (p = c6_sp_pending; p; p = p->next)
        c6_sp_emit(p);
}

/* 'go f(args);' : evaluate the arguments, hand them and the trampoline to
   the runtime and return at once. */
static void c6_go_parse(void)
{
    Sym *fs = sym_find(tok);
    CType ft, fnt;
    Sym *tramp_sym, *p;
    AttributeDef ad;
    int name, tramp, slot = 0, i, ofs, size;

    if (!fs || (fs->type.t & VT_BTYPE) != VT_FUNC || !fs->type.ref)
        tcc_error("the operand of 'go' must be a call of a plain function");
    name = fs->v;                   /* the symbol's own name, after mangling */
    ft = fs->type;
    if (ft.ref->f.func_async)
        tcc_error("'go' requires a plain function: '%s' is 'async' (use "
                  "'co')", get_tok_str(name, NULL));
    if (ft.ref->f.func_fallible)
        tcc_error("'go' does not support fallible functions: '%s'",
                  get_tok_str(name, NULL));
    if (ft.ref->f.func_type == FUNC_ELLIPSIS)
        tcc_error("'go' does not support variadic functions: '%s'",
                  get_tok_str(name, NULL));
    if (!c6_sp_ok(&ft))
        tcc_error("'go' does not support this signature: '%s'",
                  get_tok_str(name, NULL));
    for (p = c6_async_params(&ft); p; p = p->next) {
        if (!c6_sp_type_ok(&p->type))
            tcc_error("'go' does not support this signature: '%s'",
                      get_tok_str(name, NULL));
        if (c6_is_managed(&p->type)
            && !(c6_type_gq(&p->type) & C6_GQ_SHARED))
            tcc_error("passing a reference to 'go' requires 'shared': '%s' "
                      "is counted per thread", get_tok_str(name, NULL));
    }

    /* a forward reference to the trampoline, which c6_sp_flush() defines at
       the end of this unit */
    tramp = c6_sp_name("__c6_sp_", name, "");
    fnt = c6_sp_fn_type();
    fnt.t |= VT_STATIC | VT_EXTERN;
    memset(&ad, 0, sizeof ad);
    tramp_sym = external_sym(tramp, &fnt, 0, &ad);
    c6_sp_remember(name, tramp, &ft);

    next();                             /* the callee name */
    skip('(');
    size = 0;
    for (p = c6_async_params(&ft); p; p = p->next) {
        CType pt = p->type;
        int a2, sz;

        convert_parameter_type(&pt);
        sz = type_size(&pt, &a2);
        size = (size + a2 - 1) & -a2;
        size += sz;
    }
    if (size) {
        loc = (loc - PTR_SIZE) & -PTR_SIZE;
        slot = loc;
        /* __c6_p = __tcc_sp_args(size); */
        c6_rt_begin("__tcc_sp_args");
        vpushi(size);
        c6_rt_end("__tcc_sp_args", 1);
        vset(&char_pointer_type, VT_LOCAL | VT_LVAL, slot);
        vswap();
        vstore();
        vpop();
    }
    ofs = 0;
    for (i = 0, p = c6_async_params(&ft); p; p = p->next, i++) {
        CType pt = p->type;
        int a2, sz;

        if (i)
            skip(',');
        convert_parameter_type(&pt);
        sz = type_size(&pt, &a2);
        ofs = (ofs + a2 - 1) & -a2;
        /* __c6_p->pN = argN; */
        vset(&char_pointer_type, VT_LOCAL | VT_LVAL, slot);
        vpushi(ofs);
        gen_op('+');
        vtop->type = pt;
        vtop->r |= VT_LVAL;
        expr_eq();
        vstore();
        vpop();
        ofs += sz;
    }
    skip(')');
    /* __tcc_th_start(&__c6_sp_NAME, __c6_p); */
    c6_rt_begin("__tcc_th_start");
    fnt = c6_sp_fn_type();
    vpushsym(&fnt, tramp_sym);
    mk_pointer(&vtop->type);
    if (size)
        vset(&char_pointer_type, VT_LOCAL | VT_LVAL, slot);
    else
        vpushi(0);
    c6_rt_end("__tcc_th_start", 2);
    vpop();
    skip(';');
}
#endif /* C6_EXT */

static void gen_function(Sym *sym)
{
    struct scope f = { 0 };

    cur_scope = root_scope = &f;
    nocode_wanted = 0;

    ind = cur_text_section->data_offset;
    if (sym->a.aligned) {
	size_t newoff = section_add(cur_text_section, 0,
				    1 << (sym->a.aligned - 1));
	gen_fill_nops(newoff - ind);
    }

#ifdef C6_EXT
    /* '__func__' shows the name as written, not the mangled method symbol */
    funcname = c6_method_source_name(sym->v);
    if (!funcname)
        funcname = get_tok_str(sym->v, NULL);
#else
    funcname = get_tok_str(sym->v, NULL);
#endif
    func_ind = ind;
    func_vt = sym->type.ref->type;
    func_var = sym->type.ref->f.func_type == FUNC_ELLIPSIS;
    func_old = sym->type.ref->f.func_type == FUNC_OLD;
#ifdef C6_EXT
    c6_func_sym = sym;
    c6_value_fallible = 0;
    c6_last_stmt_leaves = 0;
    c6_gc_in_drop = c6_gc_is_drop(sym->v);
#endif

    /* NOTE: we patch the symbol size later */
    put_extern_sym(sym, cur_text_section, ind, 0);

    if (sym->type.ref->f.func_ctor)
        add_array (tcc_state, ".init_array", sym->c);
    if (sym->type.ref->f.func_dtor)
        add_array (tcc_state, ".fini_array", sym->c);

    /* put debug symbol */
    tcc_debug_funcstart(tcc_state, sym);

    /* push a dummy symbol to enable local sym storage */
    sym_push2(&local_stack, SYM_FIELD, 0, 0);
    /* push parameters */
    local_scope = 1;
    sym_push_params(sym->type.ref);
#ifdef C6_EXT
    if (tcc_state->c6_ext) {
        /* a managed parameter borrows the caller's reference: it owns
           nothing, is never released and cannot be reassigned */
        Sym *ps;
        for (ps = local_stack; ps && (ps->r & VT_LOCAL); ps = ps->prev) {
            if ((ps->r & VT_VALMASK) == VT_LOCAL && ps->c >= 0
                && c6_is_managed(&ps->type))
                ps->type.gq |= C6_GQ_BORROW;
        }
    }
#endif
    local_scope = 0;
    rsym = 0;
    nb_temp_local_vars = 0;

    gfunc_prolog(sym);
#ifdef C6_EXT
    if (tcc_state->c6_ext) {
        /* an async body first checks whether it is being resumed, then
           records its defer frame base and copies the parameters in */
        if (c6_async_cur)
            c6_async_func_begin();
        c6_defer_func_init();
        if (c6_async_cur)
            c6_async_params_copy();
        /* 'go' threads have no handler to wait for them, so the thread that
           started them reaps them when it exits.  For main that is a cleanup
           site: it must run through the same path as 'defer' so the return
           value survives the call (the return value is only reloaded after
           the cleanups have run). */
        else if (funcname && !strcmp(funcname, "main"))
            c6_defer_rt_call("__c6_th_join_all");
    }
#endif
    tcc_debug_prolog_epilog(tcc_state, 0);
    func_vla_arg(sym);
    block(0);
#ifdef C6_EXT
    if (c6_async_cur && tcc_state->c6_ext) {
        /* falling off the end finishes the task */
        if (!nocode_wanted) {
            if (defer_state.enabled)
                c6_defer_begin(1, 0);
            c6_async_finish(C6_CO_KIND_DONE);
        }
        if (defer_state.enabled)
            c6_defer_func_end();
        c6_async_func_end();
    } else if (defer_state.enabled) {
        /* fall-through function exit also runs the cleanups; the dispatcher
           itself must be emitted even if the body ended with a jump. */
        if (!nocode_wanted) {
            c6_defer_begin(1, 0);
            if (c6_implicit_ret0) {
                vpushi(0);
                gfunc_return(&func_vt);
            } else {
                rsym = gjmp(rsym); /* after the cleanups: function epilogue */
            }
        }
        c6_defer_func_end();
    } else if (c6_implicit_ret0 && !nocode_wanted) {
        vpushi(0);
        gfunc_return(&func_vt);
    }
    c6_implicit_ret0 = 0;
#endif
    gsym(rsym);
    nocode_wanted = 0;
    tcc_debug_end_scope(NULL, !func_var);
    tcc_debug_prolog_epilog(tcc_state, 1);
    gfunc_epilog();

    /* end of function */
    tcc_debug_funcend(tcc_state, ind - func_ind);

    /* patch symbol size */
    elfsym(sym)->st_size = ind - func_ind;
    cur_text_section->data_offset = ind;

    sym_pop(&local_stack, NULL, 0);
    label_pop(&global_label_stack, NULL, 0);
    sym_pop(&all_cleanups, NULL, 0);
    local_scope = 0;

    /* It's better to crash than to generate wrong code */
    cur_text_section = NULL;
    funcname = ""; /* for safety */
    func_vt.t = VT_VOID; /* for safety */
    func_var = 0; /* for safety */
    ind = 0; /* for safety */
    func_ind = -1;
    nocode_wanted = DATA_ONLY_WANTED;
    check_vstack();

    /* do this after funcend debug info */
    next();
}

static void gen_inline_functions(TCCState *s)
{
    Sym *sym;
    int inline_generated, i;
    struct InlineFunc *fn;

    tcc_open_bf(s, ":inline:", 0);
    /* iterate while inline function are referenced */
    do {
        inline_generated = 0;
        for (i = 0; i < s->nb_inline_fns; ++i) {
            fn = s->inline_fns[i];
            sym = fn->sym;
            if (sym && (sym->c || !(sym->type.t & VT_INLINE))) {
                /* the function was used or forced (and then not internal):
                   generate its code and convert it to a normal function */
                fn->sym = NULL;
                tccpp_putfile(fn->filename);
                begin_macro(fn->func_str, 1);
                next();
                cur_text_section = text_section;
                gen_function(sym);
                end_macro();

                inline_generated = 1;
            }
        }
    } while (inline_generated);
    tcc_close();
}

static void free_inline_functions(TCCState *s)
{
    int i;
    /* free tokens of unused inline functions */
    for (i = 0; i < s->nb_inline_fns; ++i) {
        struct InlineFunc *fn = s->inline_fns[i];
        if (fn->sym)
            tok_str_free(fn->func_str);
    }
    dynarray_reset(&s->inline_fns, &s->nb_inline_fns);
}

static void do_Static_assert(void)
{
    int c;
    const char *msg;

    next();
    skip('(');
    c = expr_const();
    msg = "_Static_assert fail";
    if (tok == ',') {
        next();
        msg = parse_mult_str("string constant")->data;
    }
    skip(')');
    if (c == 0)
        tcc_error("%s", msg);
    skip(';');
}

#ifdef TCC_TARGET_PE
static void pe_check_linkage(CType *type, AttributeDef *ad)
{
    if (!ad->a.dllimport && !ad->a.dllexport)
        return;
    if (type->t & VT_STATIC)
        tcc_error("cannot have dll linkage with static");
    if (type->t & VT_TYPEDEF) {
        const char *m = ad->a.dllimport ? "im" : "ex";
        tcc_warning("'dll%sport' attribute ignored for typedef", m);
        ad->a.dllimport = 0;
        ad->a.dllexport = 0;
    } else if (ad->a.dllimport) {
        if ((type->t & VT_BTYPE) == VT_FUNC)
            ad->a.dllimport = 0;
        else
            type->t |= VT_EXTERN;
    }
}
#endif

#ifdef C6_EXT
/* Consume the c6 '!error' suffix when present: 'int!error f(void)' and
   'int f(void)!error' mean the same, namely that f propagates errors. */
static int c6_parse_fallible_suffix(void)
{
    if (tok != '!')
        return 0;
    next();
    if (tok < TOK_UIDENT || strcmp(get_tok_str(tok, NULL), "error") != 0)
        expect("'error'");
    next();
    return 1;
}

/* 'error' only ever leaves a function: it is the payload of the 'T!error'
   return channel and is never passed as an argument.  Reject it in parameter
   lists ('error*' and 'error[]' included, they would be out-parameters). */
static void c6_check_error_param(CType *type)
{
    CType *t = type;
    Sym *tag;

    if (!tcc_state->c6_ext)
        return;
    if ((t->t & VT_BTYPE) == VT_PTR && t->ref)
        t = pointed_type(t);
    if ((t->t & VT_BTYPE) != VT_STRUCT || !t->ref)
        return;
    tag = struct_find(tok_alloc_const("__tcc_error"));
    if (tag && t->ref == tag)
        tcc_error("'error' can only be a return type, not a parameter");
}

/* Every variable definition must come with an initializer.  Declarations
   ('extern' objects, function prototypes, typedefs), parameters, struct
   members and alias objects are not definitions and are exempt; so are VLAs,
   because C forbids initializing a variable length array.  The check runs
   before TCC turns a tentative global definition into an 'extern' one, and
   before a 'package' mangles the name, so the diagnostic shows the name as
   written. */
static void c6_check_var_initialized(CType *type, int has_init, int v,
                                         AttributeDef *ad)
{
    if (!tcc_state->c6_ext || has_init)
        return;
    if (type->t & (VT_EXTERN | VT_TYPEDEF | VT_VLA))
        return;
    if ((type->t & VT_BTYPE) == VT_FUNC)
        return;
    if (ad->alias_target)
        return;
    tcc_error("variable '%s' must be initialized", get_tok_str(v, NULL));
}

/* Turn the '!error' annotation of TYPE into a property of the function. */
static void c6_apply_fallible(CType *type)
{
    CType *ft = type;
    int bt;

    if ((ft->t & VT_BTYPE) == VT_PTR && ft->ref
        && (ft->ref->type.t & VT_BTYPE) == VT_FUNC)
        ft = &ft->ref->type;
    if ((ft->t & VT_BTYPE) != VT_FUNC || !ft->ref)
        tcc_error("'!error' requires a function declarator");
    bt = ft->ref->type.t & VT_BTYPE;
    /* a scalar carries the '-1' sentinel, an aggregate is zeroed: what a
       'T!error' function reports is the error slot, never the value, so the
       error value of an aggregate is never observed.  Only types that cannot
       be built at all are rejected. */
    if (!(bt == VT_VOID || bt == VT_PTR || bt == VT_BOOL
          || (bt >= VT_BYTE && bt <= VT_LLONG)
          || bt == VT_STRUCT
          || IS_ENUM(ft->ref->type.t))) {
        char ebuf[256];
        type_to_str(ebuf, sizeof ebuf, &ft->ref->type, NULL);
        tcc_error("'!error' does not support a '%s' return type", ebuf);
    }
    ft->ref->f.func_fallible = 1;
}
#endif

/* 'l' is VT_LOCAL or VT_CONST to define default storage type
   or VT_CMP if parsing old style parameter list
   or VT_JMP if parsing c99 for decl: for (int i = 0, ...) */
static int decl(int l)
{
    int v, has_init, r, oldint;
    CType type, btype;
    Sym *sym, *sa;
    AttributeDef ad, adbase;
    ElfSym *esym;
#ifdef C6_EXT
    int fallible, decl_name, method_tok;
#endif

    while (1) {

        oldint = 0;
#ifdef C6_EXT
        fallible = 0;
        method_tok = 0;
        if (l == VT_CONST && tcc_state->c6_ext) {
            if (c6_is_test_kw(tok)) {
                c6_test_parse();
                continue;
            }
            if (tok == TOK_C6_PACKAGE) {
                c6_package_parse();
                continue;
            }
            if (tok == TOK_C6_IMPORT) {
                c6_import_parse();
                continue;
            }
            if (tok == TOK_C6_INTERFACE) {
                c6_interface_parse();
                continue;
            }
            /* 'module NAME;' is a soft keyword: a visible identifier named
               'module' keeps its ordinary C meaning */
            if (tok >= TOK_UIDENT
                && !strcmp(get_tok_str(tok, NULL), "module")
                && !sym_find_raw(tok)) {
                int c6_kw = tok;
                next();
                if (tok >= TOK_UIDENT || tok == '.') {
                    unget_tok(c6_kw);
                    c6_module_parse();
                    continue;
                }
                unget_tok(c6_kw);
            }
        }
        /* 'co', 'go' and 'task' are soft keywords: at file scope they stay
           plain identifiers, so 'go f();' only fails when the declaration
           does.  Say what is wrong instead of reporting an implicit int. */
        if (tcc_state->c6_ext && l == VT_CONST) {
            int c6_kw = c6_soft_kw(tok);
            if (c6_kw != C6_KW_NONE && !c6_soft_kw_shadowed(tok)) {
                int c6_name = tok;
                next();
                if (tok >= TOK_IDENT) {
                    unget_tok(c6_name);
                    tcc_error("'%s' can only be used inside a function",
                              get_tok_str(c6_name, NULL));
                }
                unget_tok(c6_name);
            }
        }
        /* 'foreach |i, v| a { ... }' is a statement: at file scope it is
           always a mistake, not a declaration with an implicit int */
        if (tcc_state->c6_ext && l == VT_CONST && tok == TOK_C6_FOREACH) {
            int c6_name = tok;
            next();
            if (tok == '|')
                tcc_error("'foreach' can only be used inside a function");
            unget_tok(c6_name);
        }
#endif
#ifdef C6_EXT
        /* a block statement that starts with a parenthesised group may be
           the destructuring of a tuple: '(int q, int r) = f();' declares,
           '(q, r) = f();' assigns.  Anything else ('(void)f();', '(int)x;',
           '(a, b);') falls through untouched. */
        if (tcc_state->c6_ext && tok == '(' && l == VT_LOCAL) {
            int c6_kind = c6_tuple_lead_kind();
            if (c6_kind == C6_TK_PATTERN || c6_kind == C6_TK_ASSIGN) {
                c6_tuple_destructure(c6_kind == C6_TK_PATTERN);
                /* the rewritten element statements are declarations or
                   assignments: let the ordinary code parse them */
                continue;
            }
        }
#endif
#ifdef C6_EXT
        /* remember that this file already had a declaration: 'module' is
           only meaningful before the first one */
        if (l == VT_CONST)
            file->c6_decl_seen = 1;
#endif
        if (!parse_btype(&btype, &adbase, l == VT_LOCAL)) {
            if (l == VT_JMP)
                return 0;
            /* skip redundant ';' if not in old parameter decl scope */
            if (tok == ';' && l != VT_CMP) {
                next();
                continue;
            }
            if (tok == TOK_STATIC_ASSERT) {
                do_Static_assert();
                continue;
            }
            if (l != VT_CONST)
                break;
            if (tok == TOK_ASM1 || tok == TOK_ASM2 || tok == TOK_ASM3) {
                /* global asm block */
                asm_global_instr();
                continue;
            }
            if (tok >= TOK_UIDENT) {
               /* special test for old K&R protos without explicit int
                  type. Only accepted when defining global data */
                btype.t = VT_INT;
                oldint = 1;
            } else {
#ifdef C6_EXT
                /* end of a 'package NAME { ... }' body */
                if (tok == '}' && tcc_state->c6_package)
                    break;
#endif
                if (tok != TOK_EOF)
                    expect("declaration");
                break;
            }
        }
#ifdef C6_EXT
        /* 'T!error ...' : the annotation comes before the declarator */
        if (tcc_state->c6_ext && c6_parse_fallible_suffix())
            fallible = 1;
#endif

        if (tok == ';') {
            if ((btype.t & VT_BTYPE) == VT_STRUCT
                && (btype.ref->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
                ; /* struct decl with named tag */
            else if (IS_ENUM(btype.t))
                ; /* enum decl */
            else
                tcc_warning("useless type defines no instances");
            if (l == VT_JMP)
                return 1;
            next();
            continue;
        }

        while (1) { /* iterate thru each declaration */
            type = btype;
	    ad = adbase;
#ifdef C6_EXT
            c6_decl_fallible = 0;
#endif
            type_decl(&type, &ad, &v, l == VT_CMP ? TYPE_DIRECT | TYPE_PARAM : TYPE_DIRECT);
            /*ptype("decl", &type, v);*/
#ifdef C6_EXT
            /* an old-style parameter declaration list ('void f(e) error e;') */
            if (l == VT_CMP)
                c6_check_error_param(&type);
            /* 'T f(...)!error' : the same annotation after the declarator */
            if (tcc_state->c6_ext && c6_parse_fallible_suffix())
                fallible = 1;
            if (c6_decl_fallible)
                fallible = 1, c6_decl_fallible = 0;
            if (fallible)
                c6_apply_fallible(&type);
#endif
#ifdef C6_EXT
            /* keep the name as written: a 'package' mangles it below, but
               diagnostics must talk about the identifier the user wrote */
            decl_name = v;
#ifdef C6_EXT
            /* a module owns the program entry point of its own TU, not of
               the importer: 'main' is not an exportable module name */
            if (l == VT_CONST && file->c6_module && decl_name >= TOK_UIDENT
                && !strcmp(get_tok_str(decl_name, NULL), "main"))
                tcc_error("'main' cannot be declared in a module");
#endif
            /* symbols declared inside a package are name-mangled; the
               program entry point always keeps its name */
            if (l == VT_CONST && tcc_state->c6_package
                && v >= TOK_UIDENT
                && strcmp(get_tok_str(v, NULL), "main") != 0)
                v = c6_mangle_tok(tcc_state->c6_package, v);
            /* ... and a module prefixes the result once more, so that
               'package p { int f() }' in module m becomes 'm__p__f' */
            if (l == VT_CONST && file->c6_module && v >= TOK_UIDENT)
                v = c6_mod_name(v);
#endif
            if ((type.t & VT_BTYPE) == VT_FUNC) {
                if ((type.t & VT_STATIC) && (l != VT_CONST))
                    tcc_error("function without file scope cannot be static");
#ifdef C6_EXT
                if (tcc_state->c6_ext && type.ref->f.func_async) {
                    if (type.t & VT_INLINE)
                        tcc_error("'async' cannot be combined with 'inline'");
                    if (l != VT_CONST)
                        tcc_error("'async' functions must have file scope");
                    if (v >= TOK_UIDENT
                        && !strcmp(get_tok_str(v, NULL), "main"))
                        tcc_error("'main' cannot be async");
                    c6_async_declare(&type, v);
                }
#endif
                /* if old style function prototype, we accept a
                   declaration list */
                sym = type.ref;
                if (sym->f.func_type == FUNC_OLD && l == VT_CONST) {
                    func_vt = type;
		    ++local_scope;
                    decl(VT_CMP);
		    --local_scope;
                }
                if ((type.t & (VT_EXTERN|VT_INLINE)) == (VT_EXTERN|VT_INLINE)) {
                    /* always_inline functions must be handled as if they
                       don't generate multiple global defs, even if extern
                       inline, i.e. GNU inline semantics for those.  Rewrite
                       them into static inline.  */
                    if (tcc_state->gnu89_inline || sym->f.func_alwinl)
                        type.t = (type.t & ~VT_EXTERN) | VT_STATIC;
                    else
                        type.t &= ~VT_INLINE; /* always compile otherwise */
                }

            } else if (oldint) {
                tcc_warning("type defaults to int");
            }

            if (gnu_ext && (tok == TOK_ASM1 || tok == TOK_ASM2 || tok == TOK_ASM3)) {
                ad.asm_label = asm_label_instr();
                /* parse one last attribute list, after asm label */
                parse_attribute(&ad);
            #if 0
                /* gcc does not allow __asm__("label") with function definition,
                   but why not ... */
                if (tok == '{')
                    expect(";");
            #endif
            }

#ifdef TCC_TARGET_PE
            pe_check_linkage(&type, &ad);
#endif
            if (tok == '{') {
                if (l != VT_CONST)
                    tcc_error("cannot use local functions");
                if ((type.t & VT_BTYPE) != VT_FUNC)
                    expect("function definition");

                /* apply post-declaraton attributes */
                merge_funcattr(&type.ref->f, &ad.f);
#ifdef C6_EXT
                /* a prototype marked '!error' also marks the definition */
                if (tcc_state->c6_ext) {
                    Sym *ps = sym_find(v);
                    if (ps && (ps->type.t & VT_BTYPE) == VT_FUNC
                        && ps->type.ref->f.func_fallible)
                        type.ref->f.func_fallible = 1;
                    if (ps && (ps->type.t & VT_BTYPE) == VT_FUNC
                        && ps->type.ref->f.func_async)
                        type.ref->f.func_async = 1;
                }
#endif
                /* put function symbol */
                type.t &= ~VT_EXTERN;
#ifdef C6_EXT
                /* a method is emitted as '<Type>_<name>' so that different
                   structures can share a method name (c6_method_symbol) */
                if (!tcc_state->c6_package || file->c6_module)
                    method_tok = c6_method_symbol(decl_name, &type);
                if (method_tok)
                    v = method_tok;
                /* importing a module: register the prototype, skip the body
                   (the module's own object contains the code) */
                if (c6_importing) {
                    if (type.t & VT_STATIC) {
                        skip_or_save_block(NULL);
                        break;
                    }
                    type.t |= VT_EXTERN;
                    external_sym(v, &type, 0, &ad);
                    c6_method_register(decl_name, v, &type);
                    if (method_tok)
                        c6_method_plain_decl(decl_name, &type, &ad);
                    c6_header_record(C6_HD_FUNC, v, &type, NULL);
                    skip_or_save_block(NULL);
                    break;
                }
#endif
                sym = external_sym(v, &type, 0, &ad);
#ifdef C6_EXT
                /* 'ret f(struct T *self, ...)' becomes callable as p->f() */
                c6_method_register(decl_name, v, &type);
                /* the source name stays declared for plain 'f(p)' calls */
                if (method_tok)
                    c6_method_plain_decl(decl_name, &type, &ad);
                c6_header_record(C6_HD_FUNC, v, &type, NULL);
#endif

                /* reject abstract declarators in function definition
                   make old-style float params double */
                for (sa = sym->type.ref; (sa = sa->next) != NULL;) {
                    if (!(sa->v & ~SYM_FIELD))
                        expect("identifier");
                    if (sa->type.t == VT_FLOAT
                        && sym->type.ref->f.func_type == FUNC_OLD) {
                        sa->type.t = VT_DOUBLE;
                    }
                }

                /* static inline functions are just recorded as a kind
                   of macro. Their code will be emitted at the end of
                   the compilation unit only if they are used */
                if (sym->type.t & VT_INLINE) {
                    struct InlineFunc *fn;
                    fn = tcc_malloc(sizeof *fn + strlen(file->filename));
                    strcpy(fn->filename, file->filename);
                    fn->sym = sym;
                    dynarray_add(&tcc_state->inline_fns,
				 &tcc_state->nb_inline_fns, fn);
                    skip_or_save_block(&fn->func_str);
                } else {
                    /* compute text section */
                    cur_text_section = text_section;
                    if (ad.section) {
                        cur_text_section = ad.section;
                        if (ad.new_section)
                            ad.section->sh_flags = text_section->sh_flags;
                    }
#ifdef C6_EXT
                    if (tcc_state->c6_ext && type.ref->f.func_async) {
                        if (c6_async_cur)
                            tcc_error("'async' functions cannot be nested");
                        c6_async_definition(sym, &type);
                        break;
                    }
#endif
                    gen_function(sym);
#ifdef C6_EXT
                    /* the source name stays available as an alias */
                    if (method_tok)
                        c6_method_alias(decl_name, sym);
#endif
                }
                break;
            } else {
                has_init = 0;
		if (l == VT_CMP) {
		    /* find parameter in function parameter list */
		    for (sym = func_vt.ref->next; sym; sym = sym->next)
			if ((sym->v & ~SYM_FIELD) == v)
			    goto found;
		    tcc_error("declaration for parameter '%s' but no such parameter",
			      get_tok_str(v, NULL));
                found:
		    if (type.t & VT_STORAGE) /* 'register' is okay */
		        tcc_error("storage class specified for '%s'",
				  get_tok_str(v, NULL));
		    if (!(sym->type.t & VT_EXTERN))
		        tcc_error("redefinition of parameter '%s'",
				  get_tok_str(v, NULL));
		    convert_parameter_type(&type);
		    sym->type = type;
		} else if (type.t & VT_TYPEDEF) {
                    /* save typedefed type  */
                    /* XXX: test storage specifiers ? */
                    sym = sym_find(v);
                    if (sym && sym->sym_scope == local_scope) {
                        if (!is_compatible_types(&sym->type, &type)
                            || !(sym->type.t & VT_TYPEDEF))
                            tcc_error("incompatible redefinition of '%s'",
                                get_tok_str(v, NULL));
                        sym->type = type;
                    } else {
                        sym = sym_push(v, &type, 0, 0);
                    }
                    sym->a = ad.a;
                    if ((type.t & VT_BTYPE) == VT_FUNC)
                      merge_funcattr(&sym->type.ref->f, &ad.f);
#ifdef C6_EXT
                    if (l == VT_CONST)
                        c6_header_record(C6_HD_TYPEDEF, v, &type, NULL);
#endif
                    if (debug_modes)
                        tcc_debug_typedef (tcc_state, sym);
		} else if ((type.t & VT_BTYPE) == VT_VOID
			   && !(type.t & VT_EXTERN)) {
		    tcc_error("declaration of void object");
                } else {
                    r = 0;
                    if ((type.t & VT_BTYPE) == VT_FUNC) {
                        /* external function definition */
                        /* specific case for func_call attribute */
                        merge_funcattr(&type.ref->f, &ad.f);
                    } else if (!(type.t & VT_ARRAY)) {
                        /* not lvalue if array */
                        r |= VT_LVAL;
                    }

                    if (tok == '=')
                        has_init = 1;
#ifdef C6_EXT
                    if (c6_importing && l == VT_CONST) {
                        if (type.t & VT_STATIC) {
                            /* file local: nothing to import */
                            c6_skip_decl_tail();
                            if (tok == ',') {
                                next();
                                continue;
                            }
                            skip(';');
                            break;
                        }
                        /* a definition becomes a declaration here; its data
                           belongs to the module's own object file */
                        type.t |= VT_EXTERN;
                        if (has_init) {
                            has_init = 0;
                            c6_skip_decl_tail();
                        }
                    }
                    c6_check_var_initialized(&type, has_init, decl_name,
                                                 &ad);
#endif

                    if (((type.t & VT_EXTERN) && (!has_init || l != VT_CONST))
		        || (type.t & VT_BTYPE) == VT_FUNC
                        /* as with GCC, uninitialized global arrays with no size
                           are considered extern: */
                        || ((type.t & VT_ARRAY) && !has_init
                            && l == VT_CONST && type.ref->c < 0)
                        ) {
                        /* external variable or function */
                        type.t |= VT_EXTERN;
#ifdef C6_EXT
                        if (!tcc_state->c6_package || file->c6_module)
                            method_tok = c6_method_symbol(decl_name, &type);
                        if (method_tok)
                            v = method_tok;
#endif
                        sym = external_sym(v, &type, r, &ad);
#ifdef C6_EXT
                        /* a prototype is enough to call it as p->f(); the
                           source name stays declared for plain 'f(p)' calls */
                        if ((type.t & VT_BTYPE) == VT_FUNC && l == VT_CONST)
                            c6_method_register(decl_name, v, &type);
                        if (method_tok)
                            c6_method_plain_decl(decl_name, &type, &ad);
                        if (l == VT_CONST)
                            c6_header_record((type.t & VT_BTYPE) == VT_FUNC
                                             ? C6_HD_FUNC : C6_HD_VAR,
                                             v, &type, NULL);
#endif
                    } else {
                        if (l == VT_CONST || (type.t & VT_STATIC))
                            r |= VT_CONST;
                        else
                            r |= VT_LOCAL;
                        type.t &= ~VT_EXTERN;
                        if (has_init)
                            next();
                        else if (l == VT_CONST)
                            /* uninitialized global variables may be overridden */
                            type.t |= VT_EXTERN;
                        decl_initializer_alloc(&type, &ad, r, has_init, v, l);
#ifdef C6_EXT
                        if (tcc_state->c6_ext)
                            c6_gc_value_check(&type, "a declared object");
                        if (tcc_state->c6_ext
                            && (l == VT_CONST || (type.t & VT_STATIC)))
                            c6_gc_root_add(v, &type);
                        if (l == VT_LOCAL && tcc_state->c6_ext
                            && !(type.t & (VT_STATIC | VT_ARRAY))
                            && c6_is_managed(&type)) {
                            /* released like a 'defer': the site runs at the
                               end of the scope or on return/break/err */
                            Sym *gs = sym_find(v);
                            if (gs && (gs->r & VT_LOCAL))
                                c6_gc_site(gs->c, &type);
                        }
                        if (l == VT_CONST)
                            c6_header_record(C6_HD_VAR, v, &type, NULL);
#endif
                    }

                    if (ad.alias_target && l == VT_CONST) {
                        /* Aliases need to be emitted when their target symbol
                           is emitted, even if perhaps unreferenced.
                           We only support the case where the base is already
                           defined, otherwise we would need deferring to emit
                           the aliases until the end of the compile unit.  */
                        esym = elfsym(sym_find(ad.alias_target));
                        if (!esym)
                            tcc_error("unsupported forward __alias__ attribute");
                        put_extern_sym2(sym_find(v), esym->st_shndx,
                                        esym->st_value, esym->st_size, 1);
                    }
                }
                if (tok != ',') {
                    if (l == VT_JMP)
                        return has_init ? v : 1;
                    skip(';');
                    break;
                }
                next();
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
#undef gjmp_addr
#undef gjmp
/* ------------------------------------------------------------------------- */
