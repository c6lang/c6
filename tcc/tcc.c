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

#ifndef ONE_SOURCE
# define ONE_SOURCE 1
#endif

#include "tcc.h"
#if ONE_SOURCE
# include "libtcc.c"
#endif
#include "tcctools.c"

#if defined(TCC_TARGET_C99) || defined(CONFIG_EMBED_C99)
# ifndef _WIN32
#  include <sys/stat.h>
# endif
#endif

static const char help[] =
    "Tiny C Compiler "TCC_VERSION" - Copyright (C) 2001-2006 Fabrice Bellard\n"
    "Usage: tcc [options...] [-o outfile] [-c] infile(s)...\n"
    "       tcc [options...] -run infile (or --) [arguments...]\n"
    "General options:\n"
#ifdef TCC_TARGET_C99
    "  -c           one generated C file per input file (with -o dir)\n"
    "  -o outfile   set output file, or directory for one .c per input\n"
    "  -c99         generate portable C source (default in this build)\n"
#else
    "  -c           compile only - generate an object file\n"
    "  -o outfile   set output filename\n"
#endif
#ifdef CONFIG_EMBED_C99
    "  -c99         generate portable C source (also -o out.c / -o dir)\n"
#endif
    "  -run         run compiled source\n"
    "  -fflag       set or reset (with 'no-' prefix) 'flag' (see tcc -hh)\n"
    "  -Wwarning    set or reset (with 'no-' prefix) 'warning' (see tcc -hh)\n"
    "  -w           disable all warnings\n"
    "  -v --version show version\n"
    "  -vv          show search paths or loaded files\n"
    "  -h -hh       show this, show more help\n"
    "  -bench       show compilation statistics\n"
    "  -            use stdin pipe as infile\n"
    "  @listfile    read arguments from listfile\n"
    "Preprocessor options:\n"
    "  -Idir        add include path 'dir'\n"
    "  -Dsym[=val]  define 'sym' with value 'val'\n"
    "  -Usym        undefine 'sym'\n"
    "  -E           preprocess only\n"
    "  -nostdinc    do not use standard system include paths\n"
    "Linker options:\n"
    "  -Ldir        add library path 'dir'\n"
    "  -llib        link with dynamic or static library 'lib'\n"
    "  -nostdlib    do not link with standard crt and libraries\n"
    "  -r           generate (relocatable) object file\n"
    "  -rdynamic    export all global symbols to dynamic linker\n"
    "  -shared      generate a shared library/dll\n"
    "  -soname      set name for shared library to be used at runtime\n"
    "  -Wl,-opt[=val]  set linker option (see tcc -hh)\n"
    "Debugger options:\n"
    "  -g           generate stab runtime debug info\n"
    "  -gdwarf[-x]  generate dwarf runtime debug info\n"
#ifdef TCC_TARGET_PE
    "  -g.pdb       create .pdb debug database\n"
#endif
#ifdef CONFIG_TCC_BCHECK
    "  -b           compile with built-in memory and bounds checker (implies -g)\n"
#endif
#ifdef CONFIG_TCC_BACKTRACE
    "  -bt[N]       link with backtrace (stack dump) support [show max N callers]\n"
#endif
    "Misc. options:\n"
#ifdef C6_EXT
    "  -std=version define __STDC_VERSION__ according to version (c11/gnu11/c6)\n"
#else
    "  -std=version define __STDC_VERSION__ according to version (c11/gnu11)\n"
#endif
    "  -x[c|a|b|n]  specify type of the next infile (C,ASM,BIN,NONE)\n"
    "  -Bdir        set tcc's private include/library dir\n"
    "  -M[M]D       generate make dependency file [ignore system files]\n"
    "  -M[M]        as above but no other output\n"
    "  -MF file     specify dependency file name\n"
#if defined(TCC_TARGET_I386) || defined(TCC_TARGET_X86_64)
    "  -m32/64      defer to i386/x86_64 cross compiler\n"
#endif
    "Tools:\n"
    "  create library  : tcc -ar [crstvx] lib [files]\n"
#ifdef TCC_TARGET_PE
    "  create def file : tcc -impdef lib.dll [-v] [-o lib.def]\n"
#endif
    "Discussion & bug reports:\n"
    "  https://lists.nongnu.org/mailman/listinfo/tinycc-devel\n"
    ;

static const char help2[] =
    "Tiny C Compiler "TCC_VERSION" - More Options\n"
    "Special options:\n"
    "  -P -P1                        with -E: no/alternative #line output\n"
    "  -dD -dM                       with -E: output #define directives\n"
    "  -pthread                      same as -D_REENTRANT and -lpthread\n"
    "  -On                           same as -D__OPTIMIZE__ for n > 0\n"
    "  -Wp,-opt                      same as -opt\n"
    "  -include file                 include 'file' above each input file\n"
    "  -nostdlib                     do not link with standard crt/libs\n"
    "  -isystem dir                  add 'dir' to system include path\n"
    "  -static                       link to static libraries (not recommended)\n"
    "  -dumpversion                  print version\n"
    "  -print-search-dirs            print search paths\n"
    "  -rstdin file                  with -run: use 'file' as custom stdin\n"
    "  -dt                           with -run/-E: auto-define 'test_...' macros\n"
    "Ignored options:\n"
    "  -arch -C --param -pedantic -pipe -s -traditional\n"
    "-W[no-]... warnings:\n"
    "  all                           turn on some (*) warnings\n"
    "  error[=warning]               stop after warning (any or specified)\n"
    "  write-strings                 strings are const\n"
    "  unsupported                   warn about ignored options, pragmas, etc.\n"
    "  implicit-function-declaration warn for missing prototype (*)\n"
    "  discarded-qualifiers          warn when const is dropped (*)\n"
    "-f[no-]... flags:\n"
    "  unsigned-char                 default char is unsigned\n"
    "  signed-char                   default char is signed\n"
    "  common                        use common section instead of bss\n"
    "  leading-underscore            decorate extern symbols\n"
    "  ms-extensions                 allow anonymous struct in struct\n"
    "  dollars-in-identifiers        allow '$' in C symbols\n"
    "  reverse-funcargs              evaluate function arguments right to left\n"
    "  gnu89-inline                  'extern inline' is like 'static inline'\n"
    "  asynchronous-unwind-tables    create eh_frame section [on]\n"
    "  test-coverage                 create code coverage code\n"
    "-m... target specific options:\n"
    "  ms-bitfields                  use MSVC bitfield layout\n"
#ifdef TCC_TARGET_ARM
    "  float-abi                     hard/softfp on arm\n"
#endif
#ifdef TCC_TARGET_X86_64
    "  no-sse                        disable floats on x86_64\n"
#endif
    "-Wl,... linker options:\n"
    "  -nostdlib                     do not search standard library paths\n"
    "  -[no-]whole-archive           load lib(s) fully/only as needed\n"
    "  -export-all-symbols           same as -rdynamic\n"
    "  -export-dynamic               same as -rdynamic\n"
    "  -image-base= -Ttext=          set base address of executable\n"
    "  -section-alignment=           set section alignment in executable\n"
#ifdef TCC_TARGET_PE
    "  -file-alignment=              set PE file alignment\n"
    "  -stack=                       set PE stack reserve\n"
    "  -large-address-aware          set related PE option\n"
    "  -subsystem=[console/windows]  set PE subsystem\n"
    "  -oformat=[pe-* binary]        set executable output format\n"
    "Predefined macros:\n"
    "  tcc -E -dM - < nul\n"
#else
    "  -rpath=                       set dynamic library search path\n"
    "  -enable-new-dtags             set DT_RUNPATH instead of DT_RPATH\n"
    "  -soname=                      set DT_SONAME elf tag\n"
#if defined(TCC_TARGET_MACHO)
    "  -install_name=                set DT_SONAME elf tag (soname macOS alias)\n"
#else
    "  -Ipath, -dynamic-linker=path  set ELF interpreter to path\n"
#endif
    "  -Bsymbolic                    set DT_SYMBOLIC elf tag\n"
    "  -oformat=[elf32/64-* binary]  set executable output format\n"
    "  -init= -fini= -Map= -as-needed -O -z= (ignored)\n"
    "Predefined macros:\n"
    "  tcc -E -dM - < /dev/null\n"
#endif
    "See also the manual for more details.\n"
    ;

static const char version[] =
    "tcc version "TCC_VERSION
#ifdef TCC_GITHASH
    " "TCC_GITHASH
#endif
    " ("
#ifdef TCC_TARGET_I386
        "i386"
#elif defined TCC_TARGET_X86_64
        "x86_64"
#elif defined TCC_TARGET_C67
        "C67"
#elif defined TCC_TARGET_ARM
        "ARM"
# ifdef TCC_ARM_EABI
        " eabi"
#  ifdef TCC_ARM_HARDFLOAT
        "hf"
#  endif
# endif
#elif defined TCC_TARGET_ARM64
        "AArch64"
#elif defined TCC_TARGET_RISCV64
        "riscv64"
#endif
#ifdef TCC_TARGET_PE
        " Windows"
#elif defined(TCC_TARGET_MACHO)
        " Darwin"
#elif TARGETOS_FreeBSD || TARGETOS_FreeBSD_kernel
        " FreeBSD"
#elif TARGETOS_OpenBSD
        " OpenBSD"
#elif TARGETOS_NetBSD
        " NetBSD"
#else
        " Linux"
#endif
    ")\n"
    ;

static void print_dirs(const char *msg, char **paths, int nb_paths)
{
    int i;
    printf("%s:\n%s", msg, nb_paths ? "" : "  -\n");
    for(i = 0; i < nb_paths; i++)
        printf("  %s\n", paths[i]);
}

static void print_search_dirs(TCCState *s)
{
    printf("install: %s\n", s->tcc_lib_path);
    /* print_dirs("programs", NULL, 0); */
    print_dirs("include", s->sysinclude_paths, s->nb_sysinclude_paths);
    print_dirs("libraries", s->library_paths, s->nb_library_paths);
    printf("libtcc1:\n  %s/%s\n", s->library_paths[0], CONFIG_TCC_CROSSPREFIX TCC_LIBTCC1);
#ifdef TCC_TARGET_UNIX
    print_dirs("crt", s->crt_paths, s->nb_crt_paths);
    printf("elfinterp:\n  %s\n",  s->elfint);
#endif
}

static void set_environment(TCCState *s)
{
    char * path;

    path = getenv("C_INCLUDE_PATH");
    if(path != NULL) {
        tcc_add_sysinclude_path(s, path);
    }
    path = getenv("CPATH");
    if(path != NULL) {
        tcc_add_include_path(s, path);
    }
    path = getenv("LIBRARY_PATH");
    if(path != NULL) {
        tcc_add_library_path(s, path);
    }
}

static char *default_outputfile(TCCState *s, const char *first_file)
{
    char buf[1024];
    char *ext;
    const char *name = "a";

    if (first_file && strcmp(first_file, "-"))
        name = tcc_basename(first_file);
    if (strlen(name) + 4 >= sizeof buf)
        name = "a";
    strcpy(buf, name);
    ext = tcc_fileextension(buf);
#ifdef TCC_TARGET_C99
    /* same convention as a.out, but the result is C source; never derive the
       name from the input, that would overwrite the source itself */
    if (s->output_type == TCC_OUTPUT_C99)
        strcpy(buf, "a.c");
    else
#endif
#ifdef TCC_TARGET_PE
    if (s->output_type == TCC_OUTPUT_DLL)
        strcpy(ext, ".dll");
    else
    if (s->output_type == TCC_OUTPUT_EXE)
        strcpy(ext, ".exe");
    else
#endif
    if ((s->just_deps || s->output_type == TCC_OUTPUT_OBJ) && !s->option_r && *ext)
        strcpy(ext, ".o");
    else
        strcpy(buf, "a.out");
    return tcc_strdup(buf);
}

static unsigned getclock_ms(void)
{
#ifdef _WIN32
    return GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec*1000 + (tv.tv_usec+500)/1000;
#endif
}

#if defined(TCC_TARGET_C99) || defined(CONFIG_EMBED_C99)
/* -o may name a directory: each translation unit then gets its own
   generated C file, named after its input file. */
static int is_dir_path(const char *path)
{
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES
        && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static int has_trailing_dirsep(const char *path)
{
    int n = strlen(path);
    return n > 0 && IS_DIRSEP(path[n - 1]);
}
#endif

#ifdef TCC_TARGET_C99
static int same_file(const char *a, const char *b)
{
    if (!strcmp(a, b))
        return 1;
#ifndef _WIN32
    {
        struct stat sa, sb;
        if (stat(a, &sa) == 0 && stat(b, &sb) == 0
            && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino)
            return 1;
    }
#endif
    return 0;
}

/* "<dir>/<input basename with a .c extension>" */
static char *c99_output_in_dir(const char *dir, const char *infile)
{
    const char *base = tcc_basename(infile);
    int dlen = strlen(dir);
    int blen = strlen(base);
    int sep = dlen > 0 && !IS_DIRSEP(dir[dlen - 1]);
    char *path = tcc_malloc(dlen + sep + blen + 3);
    char *ext;

    memcpy(path, dir, dlen);
    if (sep)
        path[dlen++] = '/';
    memcpy(path + dlen, base, blen + 1);
    ext = tcc_fileextension(path);
    if (*ext)
        strcpy(ext, ".c");
    else
        strcpy(path + dlen + blen, ".c");
    return path;
}
#endif /* TCC_TARGET_C99 */

#ifdef CONFIG_EMBED_C99
/* The native tcc carries the C99 output back-end as a second, embedded
   compiler (see tcc/Makefile).  This decides when that one should run:
   -c99 asks explicitly; otherwise a .c output file, or an output directory,
   is what the C back-end can produce.  Preprocessing and linking options
   keep their native meaning. */
static int has_c99_suffix(const char *path)
{
    int n = strlen(path);
    return n >= 2 && path[n - 2] == '.' && path[n - 1] == 'c';
}

static int wants_c99_output(int argc, char **argv)
{
    const char *out = NULL;
    int i;

    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-c99"))
            return 1;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        /* preprocessing, dependency output and linking are native jobs */
        if (!strcmp(a, "-E") || !strcmp(a, "-shared")
            || !strncmp(a, "-r", 2) || !strncmp(a, "-M", 2))
            return 0;
        if (!strcmp(a, "-o")) {
            if (i + 1 < argc)
                out = argv[++i];
        } else if (!strncmp(a, "-o", 2) && a[2]) {
            out = a + 2;
        }
    }
    if (!out)
        return 0;
    return has_c99_suffix(out) || is_dir_path(out)
        || has_trailing_dirsep(out);
}

extern int c99_main(int argc, char **argv);
#endif /* CONFIG_EMBED_C99 */

int main(int argc, char **argv)
{
    TCCState *s, *s1;
    int ret, opt, n = 0, t = 0, done;
#ifdef TCC_TARGET_C99
    int out_dir = 0;
#endif
    unsigned start_time = 0, end_time = 0;
    const char *first_file;
    int argc0 = argc;
    char **argv0 = argv;
    FILE *ppfp = NULL;

#ifdef CONFIG_EMBED_C99
    if (wants_c99_output(argc, argv))
        return c99_main(argc, argv);
#endif

redo:
    argc = argc0, argv = argv0;
    s = s1 = tcc_new();
    opt = tcc_parse_args(s, &argc, &argv);

    if (n == 0) {
        ret = 0;
        if (opt == OPT_HELP) {
            fputs(help, stdout);
            if (s->verbose)
                goto help2;
        } else if (opt == OPT_HELP2) {
            help2: fputs(help2, stdout);
        } else if (opt == OPT_M32 || opt == OPT_M64) {
            ret = tcc_tool_cross(argv, opt);
        } else if (s->verbose)
            printf("%s", version);

        if (opt == OPT_AR)
            ret = tcc_tool_ar(argc, argv);
#ifdef TCC_TARGET_PE
        if (opt == OPT_IMPDEF)
            ret = tcc_tool_impdef(argc, argv);
#endif
        if (opt == OPT_PRINT_DIRS) {
            /* initialize search dirs */
            set_environment(s);
            tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
            print_search_dirs(s);
        }
        if (opt) {
            if (opt < 0) err:
                ret = 1;
            tcc_delete(s);
            return ret;
        }
        if (s->nb_files == 0) {
            tcc_error_noabort("no input files");
#ifdef TCC_TARGET_C99
        } else if (s->output_type && s->output_type != TCC_OUTPUT_C99
                   && s->output_type != TCC_OUTPUT_PREPROCESS) {
            /* no machine code, no linker: say so instead of pretending */
            tcc_error_noabort("the c99 back-end only produces C source; use -o out.c");
#endif
        } else if (s->output_type == TCC_OUTPUT_PREPROCESS) {
            if (s->outfile && 0!=strcmp("-",s->outfile)) {
                ppfp = tcc_fopen(s->outfile, "wb");
                if (!ppfp)
                    tcc_error_noabort("could not write '%s'", s->outfile);
            }
        } else if (s->output_type == TCC_OUTPUT_OBJ && !s->option_r) {
            if (s->nb_libraries)
                tcc_error_noabort("cannot specify libraries with -c");
            else if (s->nb_files > 1 && s->outfile)
                tcc_error_noabort("cannot specify output file with -c many files");
        }
#ifdef TCC_TARGET_C99
        /* -o may name a directory (or end with a separator) to ask for one
           generated C file per translation unit.  The output type is still
           0 when no action was given, and that means C source here. */
        if (!s->nb_errors && s->outfile
            && (s->output_type == 0 || s->output_type == TCC_OUTPUT_C99)) {
            if (has_trailing_dirsep(s->outfile)) {
                if (!is_dir_path(s->outfile))
                    tcc_error_noabort("output directory '%s' does not exist",
                                      s->outfile);
                else
                    out_dir = 1;
            } else if (is_dir_path(s->outfile)) {
                out_dir = 1;
            }
            if (out_dir && s->nb_libraries)
                tcc_error_noabort("cannot specify libraries with an output directory");
        }
        if (!s->nb_errors && s->c99_compile_only && s->nb_libraries)
            tcc_error_noabort("cannot specify libraries with -c");
        if (!s->nb_errors && s->c99_compile_only && !out_dir
            && s->nb_files > 1)
            tcc_error_noabort("cannot compile multiple files with -c to one output file; use -o <dir>");
#endif
        if (s->nb_errors)
            goto err;
        if (s->do_bench)
            start_time = getclock_ms();
    }

    set_environment(s);
    if (s->output_type == 0)
#ifdef TCC_TARGET_C99
        /* the only thing this build can generate is C source */
        s->output_type = TCC_OUTPUT_C99;
#else
        s->output_type = TCC_OUTPUT_EXE;
#endif
    ret = tcc_set_output_type(s, s->output_type);
    if (ppfp)
        s->ppfp = ppfp;

    if ((s->output_type == TCC_OUTPUT_MEMORY
      || s->output_type == TCC_OUTPUT_PREPROCESS)
        && (s->dflag & 16)) { /* -dt option */
        if (t)
            s->dflag |= 32;
        s->run_test = ++t;
        if (n)
            --n;
    }

    /* compile or add each files or library */
    first_file = NULL;
    while (0 == ret) {
        struct filespec *f = s->files[n];
        s->filetype = f->type;
        if (f->type & AFF_TYPE_LIB) {
            ret = tcc_add_library(s, f->name);
        } else {
            if (1 == s->verbose)
                printf("-> %s\n", f->name);
            if (!first_file)
                first_file = f->name;
            ret = tcc_add_file(s, f->name);
        }
        if (++n == s->nb_files)
            break;
        if (s->output_type == TCC_OUTPUT_OBJ && !s->option_r)
            break;
#ifdef TCC_TARGET_C99
        if (out_dir)
            break;
#endif
    }

    if (s->do_bench)
        end_time = getclock_ms();

    if (s->run_test) {
        t = 0;
    } else if (s->output_type == TCC_OUTPUT_PREPROCESS) {
        ;
    } else if (0 == ret) {
        if (s->output_type == TCC_OUTPUT_MEMORY) {
#ifdef TCC_IS_NATIVE
            ret = tcc_run(s, argc, argv);
#endif
        } else {
#ifdef TCC_TARGET_C99
            int clash = -1;
#endif
            if (!s->outfile)
                s->outfile = default_outputfile(s, first_file);
#ifdef TCC_TARGET_C99
            else if (out_dir) {
                char *path = c99_output_in_dir(s->outfile, first_file);
                tcc_free(s->outfile);
                s->outfile = path;
            }
            {
                int i;
                for (i = 0; i < s->nb_files; i++)
                    if (!(s->files[i]->type & AFF_TYPE_LIB)
                        && same_file(s->outfile, s->files[i]->name)) {
                        clash = i;
                        break;
                    }
            }
#endif
            if (!s->just_deps) {
#ifdef TCC_TARGET_C99
                if (clash >= 0) {
                    fprintf(stderr, "tcc: error: output file '%s' would "
                            "overwrite input file '%s'\n",
                            s->outfile, s->files[clash]->name);
                    ret = 1;
                } else
#endif
                    ret = tcc_output_file(s, s->outfile);
            }
            if (!ret && s->gen_deps)
                gen_makedeps(s, s->outfile, s->deps_outfile);
        }
    }

    done = 1;
    if (t)
        done = 0; /* run more tests with -dt -run */
    else if (ret) {
        if (s->nb_errors)
            ret = 1;
        /* else keep the original exit code from tcc_run() */
    } else if (n < s->nb_files)
        done = 0; /* compile more files with -c */
    else if (s->do_bench)
        tcc_print_stats(s, end_time - start_time);

    tcc_delete(s);
    if (!done)
        goto redo;
    if (ppfp)
        tcc_fclose(ppfp);
    return ret;
}
