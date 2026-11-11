/*
 *  llvm-link.c -- "linker" side of the LLVM IR back-end.
 *
 *  Nothing is ever linked and no object file is produced: the back-end
 *  writes LLVM assembly.  These entry points exist only because the rest of
 *  TinyCC (tccelf.c) refers to them, and they all refuse to run.
 */

#ifdef TARGET_DEFS_ONLY

#define EM_TCC_TARGET EM_X86_64

#define R_DATA_32   R_X86_64_32
#define R_DATA_PTR  R_X86_64_64
#define R_JMP_SLOT  R_X86_64_JUMP_SLOT
#define R_GLOB_DAT  R_X86_64_GLOB_DAT
#define R_COPY      R_X86_64_COPY
#define R_RELATIVE  R_X86_64_RELATIVE

#define R_NUM       R_X86_64_NUM

#define ELF_START_ADDR 0x00400000
#define ELF_PAGE_SIZE  0x1000

#define PCRELATIVE_DLLPLT 1
#define RELOCATE_DLLPLT 1

#else /* !TARGET_DEFS_ONLY */

#define USING_GLOBALS
#include "tcc.h"

/* Unlike the code generator, these entry points are called from
   tcc_output_file(), i.e. after tcc_compile() has already left the global
   state.  error1() works on tcc_state, so it has to be entered again before
   raising the error; otherwise it would dereference a NULL TCCState and the
   compiler would crash instead of printing the diagnostic.  There is nothing
   to recover here, so the error is fatal. */
static void llvm_no_link(TCCState *s1, const char *fmt)
{
    tcc_enter_state(s1);
    tcc_error("%s", fmt);
}

ST_FUNC int code_reloc(int reloc_type)
{
    (void)reloc_type;
    return 0;
}

ST_FUNC int gotplt_entry_type(int reloc_type)
{
    (void)reloc_type;
    return 0;
}

ST_FUNC unsigned create_plt_entry(TCCState *s1, unsigned got_offset,
                                  struct sym_attr *attr)
{
    (void)got_offset; (void)attr;
    llvm_no_link(s1, "llvm output: no dynamic linking");
    return 0;
}

ST_FUNC void relocate_plt(TCCState *s1)
{
    llvm_no_link(s1, "llvm output: no dynamic linking");
}

ST_FUNC void relocate(TCCState *s1, ElfW_Rel *rel, int type,
                      unsigned char *ptr, addr_t addr, addr_t val)
{
    (void)rel; (void)type; (void)ptr; (void)addr; (void)val;
    llvm_no_link(s1, "llvm output: no machine code relocation");
}

#endif /* !TARGET_DEFS_ONLY */
