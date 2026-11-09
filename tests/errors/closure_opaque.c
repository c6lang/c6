/* c6 闭包不透明操作：C97/C98 */

// EXPECT test_arrow: error: 'f' is a closure: it can only be called, compared with NULL, copied, moved or released
// EXPECT test_deref: error: 'f' is a closure: it can only be called, compared with NULL, copied, moved or released
// EXPECT test_self: is captured by the closure being assigned to it: the closure would see the old value (use a named function for recursion)

#if defined test_arrow
int main(void)
{
    int n = 1;
    closure int() f = int() { return n; };
    return f->__c6_code != NULL;
}
#elif defined test_deref
int main(void)
{
    int n = 1;
    closure int() f = int() { return n; };
    return (*f)();
}
#elif defined test_self
int main(void)
{
    closure int() f = int() { return f != NULL; };
    f = NULL;
    return 0;
}
#else
int main(void) { return 0; }
#endif
