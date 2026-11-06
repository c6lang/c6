/* c6 extension: multi-level package names are purely lexical.
   'acme.util' is just the prefix 'acme__util'; nothing here looks at
   directories or file names. */
#include <stdio.h>

package acme.util {
    int counter = 0;
    int add(int a, int b) { return a + b; }
    /* inside the package the qualified spelling needs no import */
    int twice(int a) { return acme.util.add(a, a); }
}

/* nested blocks compose their prefixes: 'acme' + 'util' */
package acme {
    package util {
        int len(const char *s)
        {
            int n = 0;
            while (s[n])
                n++;
            return n;
        }
    }
}

/* the file-level form names the rest of the file: 'acme.crc' */
package acme.crc;
int sum(int a, int b) { return a + b; }

import acme.util;
import acme.crc;

int main(void)
{
    printf("add=%d twice=%d\n", acme.util.add(2, 3), acme.util.twice(21));
    printf("len=%d\n", acme.util.len("hello"));
    printf("sum=%d\n", acme.crc.sum(20, 22));

    /* the mangled names are the very same ordinary symbols */
    printf("mangled=%d %d\n", acme__util__add(1, 2), acme__crc__sum(3, 4));

    acme.util.counter = 7;
    printf("counter=%d\n", acme.util.counter);
    return 0;
}
