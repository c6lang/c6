/*
 *  llvm-link.c -- "linker" side of the LLVM IR back-end.
 *
 *  Nothing is ever linked and no object file is produced: the back-end
 *  writes LLVM assembly.  These entry points exist only because the rest of
 *  TinyCC (tccelf.c) refers to them, and they all refuse to run.
 */

#include <sys/wait.h>

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

/* ---------------------------------------------------------------------- */
/* '--target=<triple>': driving the host compiler                           */

/* The IR is portable, the toolchain around it is not: clang knows which
   linker and which startup files a triple needs, so the last step of a
   '--target' compile is a plain exec of clang.  The command line below is the
   same one tests/run-tests.sh uses for the LLVM back-end (see
   docs/llvm-backend.md), plus what the triple itself asks for:

     - a wasm module never links a libc; it wants '--no-entry', everything
       exported, and no startup files (-nostdlib)
     - ELF targets link with lld, because libtcc1.a's .eh_frame makes GNU ld
       report overlapping FDEs
     - the runtime comes from the -B directory: 'libtcc1-<arch>.a' when the
       target is not the host, 'libtcc1.a' otherwise

   TCC_CC selects the compiler (--cc= wins), TCC_CCFLAGS adds flags. */
static const char *l_cc(TCCState *s1)
{
    const char *p;

    if (s1->llvm_cc && *s1->llvm_cc)
        return s1->llvm_cc;
    p = getenv("TCC_CC");
    return p && *p ? p : "clang";
}

/* "<prefix><s>" in fresh memory */
static char *l_join(const char *prefix, const char *s)
{
    char *r = tcc_malloc(strlen(prefix) + strlen(s) + 1);

    sprintf(r, "%s%s", prefix, s);
    return r;
}

static int l_file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/* the arch component of the triple: 'wasm32-unknown-unknown' -> 'wasm32' */
static void l_arch(char *buf, int size, const char *triple)
{
    int i = 0;

    if (!triple)
        triple = "";
    while (triple[i] && triple[i] != '-' && i < size - 1) {
        buf[i] = triple[i];
        i++;
    }
    buf[i] = 0;
}

/* Run the C compiler and wait for it.  This cannot be a bare execvp(): the
   parent still has the IR file to remove (or to keep, when the build failed)
   and has to hand the child's exit status back. */
static int l_spawn(char **av)
{
    char *argv0 = av[0];
    int status;
    pid_t pid;

    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "tcc: could not run '%s': %s\n", argv0, strerror(errno));
        return 1;
    }
    if (pid == 0) {
        execvp(argv0, av);
        /* distros that keep clang out of $PATH install it under /usr/lib/llvm-N;
           the same fallback as tests/run-tests.sh, so '-cc=clang' works there */
        if (!strchr(argv0, '/')) {
            static char alt[256];
            int v;

            for (v = 24; v >= 10; v--) {
                sprintf(alt, "/usr/lib/llvm-%d/bin/%s", v, argv0);
                if (!l_file_exists(alt))
                    continue;
                av[0] = alt;
                execvp(alt, av);
            }
        }
        fprintf(stderr, "tcc: could not run '%s': %s\n", argv0, strerror(errno));
        _exit(127);                 /* the parent turns this into 1 */
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            fprintf(stderr, "tcc: waitpid: %s\n", strerror(errno));
            return 1;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) == 127)
        return 1;
    return WEXITSTATUS(status);
}

ST_FUNC int llvm_tool_cc(TCCState *s1, const char *irfile, const char *outfile)
{
    char **av = tcc_malloc(64 * sizeof *av);
    int n = 0, i;
    char *buf;
    char arch[64];
    const char *triple = s1->target_triple;
    int wasm, obj = s1->llvm_compile_only;

    l_arch(arch, sizeof arch, triple);
    wasm = !strcmp(arch, "wasm32") || !strcmp(arch, "wasm64");

    av[n++] = (char *)l_cc(s1);
    if (triple && *triple) {
        buf = tcc_malloc(strlen(triple) + 10);
        sprintf(buf, "--target=%s", triple);
        av[n++] = buf;
    }
    if (!wasm && !obj)
        av[n++] = "-fuse-ld=lld";
    else if (wasm && !obj) {
        av[n++] = "-nostdlib";
        av[n++] = "-Wl,--no-entry";
        av[n++] = "-Wl,--export-all";
    }
    if (obj)
        av[n++] = "-c";
    {
        const char *fl = getenv("TCC_CCFLAGS");

        if (fl && *fl) {
            char *copy = tcc_strdup(fl), *p = copy, *q;

            while (*p && n < 58) {
                while (*p == ' ' || *p == '\t')
                    p++;
                if (!*p)
                    break;
                q = p;
                while (*q && *q != ' ' && *q != '\t')
                    q++;
                if (*q)
                    *q++ = 0;
                av[n++] = p;
                p = q;
            }
        }
    }
    av[n++] = "-o";
    av[n++] = (char *)outfile;
    av[n++] = (char *)irfile;

    /* The runtime: libtcc1 for the host, libtcc1-<arch> for another target.
       The host archive cannot be used for another target (it is machine code
       for this one), so a missing target library is an error instead of a
       link that silently pulls in the wrong objects. */
    if (!obj && s1->tcc_lib_path && *s1->tcc_lib_path) {
        char lib[4096];
        int host = !*arch || !strcmp(arch, "x86_64"), have;

        av[n++] = l_join("-L", s1->tcc_lib_path);
        snprintf(lib, sizeof lib, "%s/libtcc1-%s.a", s1->tcc_lib_path, arch);
        have = l_file_exists(lib);
        if (host) {
            char hostlib[4096];

            snprintf(hostlib, sizeof hostlib, "%s/libtcc1.a", s1->tcc_lib_path);
            if (l_file_exists(hostlib))
                av[n++] = "-ltcc1";
        } else if (have) {
            av[n++] = l_join("-ltcc1-", arch);
        } else {
            fprintf(stderr,
                    "tcc: no runtime for target '%s': %s is missing\n"
                    "tcc: build it with scripts/build-wasm-runtime.sh %s\n",
                    triple, lib, triple);
            return 1;
        }
    }
    /* '-L' / '-l' given on the command line are for the same link */
    for (i = 0; !obj && i < s1->nb_library_paths && n < 58; i++) {
        buf = tcc_malloc(strlen(s1->library_paths[i]) + 4);
        sprintf(buf, "-L%s", s1->library_paths[i]);
        av[n++] = buf;
    }
    for (i = 0; !obj && i < s1->nb_files && n < 58; i++) {
        struct filespec *f = s1->files[i];

        if (f->type & AFF_TYPE_LIB) {
            buf = tcc_malloc(strlen(f->name) + 4);
            sprintf(buf, "-l%s", f->name);
            av[n++] = buf;
        }
    }
    av[n] = NULL;

    if (s1->verbose) {
        fprintf(stderr, "tcc: %s", av[0]);
        for (i = 1; i < n; i++)
            fprintf(stderr, " %s", av[i]);
        fprintf(stderr, "\n");
    }
    return l_spawn(av);
}

#endif /* !TARGET_DEFS_ONLY */
