/*
 *  c99-link.c -- "linker" side of the portable C99 back-end.
 *
 *  Nothing is ever linked and no object file is produced: the back-end
 *  writes C source.  These entry points exist only because the rest of
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
    (void)s1; (void)got_offset; (void)attr;
    tcc_error("c99 output: no dynamic linking");
    return 0;
}

ST_FUNC void relocate_plt(TCCState *s1)
{
    (void)s1;
    tcc_error("c99 output: no dynamic linking");
}

ST_FUNC void relocate(TCCState *s1, ElfW_Rel *rel, int type,
                      unsigned char *ptr, addr_t addr, addr_t val)
{
    (void)s1; (void)rel; (void)type; (void)ptr; (void)addr; (void)val;
    tcc_error("c99 output: no machine code relocation");
}

#endif /* !TARGET_DEFS_ONLY */
