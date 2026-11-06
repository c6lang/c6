/* c6 extension: an extension unit calling into a unit built for plain C.
   The package symbols stay ordinary C symbols, so a plain unit can provide
   them and can call the exported ones. */
#include <stdio.h>

package math {
    int add(int a, int b);
}

import math;

/* provided by 172+_c6_plain_interop.c, compiled without -std=c6 */
int plain_double(int x);

int main(void)
{
    printf("add=%d\n", math.add(2, 3));
    printf("plain_double=%d\n", plain_double(21));
    return 0;
}

package math {
    int add(int a, int b) { return a + b; }
}
