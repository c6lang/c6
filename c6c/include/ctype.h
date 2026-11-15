/* c6c 自带 <ctype.h>（只做 ASCII） */
#ifndef _C6C_CTYPE_H
#define _C6C_CTYPE_H

int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isprint(int c);
int isxdigit(int c);
int toupper(int c);
int tolower(int c);

#endif
