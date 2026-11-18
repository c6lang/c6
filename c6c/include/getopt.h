/* c6c 自带 <getopt.h>：getopt_long 的最小实现，够 host tools（kallsyms 等）用。
 * 支持长选项 --name / --name=value（no_argument / required_argument），
 * 短选项原样留给后续扩展；不认识的长选项返回 '?'。 */
#ifndef _C6C_GETOPT_H
#define _C6C_GETOPT_H

struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

#define no_argument       0
#define required_argument 1
#define optional_argument 2

extern char *optarg;
extern int optind;

int getopt(int argc, char **argv, const char *shortopts);
int getopt_long(int argc, char **argv, const char *shortopts,
                const struct option *longopts, int *longindex);

#endif
