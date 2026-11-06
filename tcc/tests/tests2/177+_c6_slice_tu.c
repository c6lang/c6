/* c6 extension: definitions for 177_c6_slice_tu.c */
int sum(int[:] s)
{
    int i = 0, total = 0;
    for (i = 0; i < (int)s.len; i++)
        total += s[i];
    return total;
}

int[:] tail(int[:] s)
{
    return s[1:];
}

int first(int[:] s)
{
    return s.len > 0 ? s[0] : -1;
}
