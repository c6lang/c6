/* c6 编译期诊断：托管引用的类型说明符
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过。
 */
struct Node { int v; };

// EXPECT qual_without_at: error: 'weak', 'own' and 'shared' are only valid in front of '@'
// EXPECT at_void: error: '@' needs an object type, not 'void'

#if defined qual_without_at
weak int x;
int main(void) { return 0; }
#elif defined at_void
@void p;
int main(void) { return 0; }
#else
/* 正常路径：限定符与 '@' 一起用 */
static weak @Node g = NULL;

int main(void)
{
    @Node p = 0;
    own @Node o = 0;
    shared @Node s = 0;
    return (p == 0 && o == 0 && s == 0 && g == 0) ? 0 : 1;
}
#endif
