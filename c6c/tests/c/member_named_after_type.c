/* expect: 42 */
/* `obj.member = …` 里成员的**名字恰好是某个类型名**时，不能被当成声明。
 *
 * c6 有个便利写法：模块限定的类型名 `tu.module.Point p;` —— 单 TU 文本导入
 * 下前面的段只是命名空间装饰，看最后一段是不是类型就行。pa_at_typename()
 * 为此对所有 `IDENT . IDENT` 都去试，于是
 *
 *     key.folio = folio;
 *
 * 这种普通赋值也被判成声明（因为 folio 也是 struct folio），
 * 语句开头就走 gen_local_decl()，报"这里要 ';'（当前是 '='）"。
 *
 * 内核里"成员名和它自己的类型同名"极常见：struct folio 的 folio、
 * struct pid 的 pid、struct cfs_rq 的 cfs_rq…… 实测 mm/、fs/、kernel/、
 * lib/、arch/x86/kernel/ 一共 20 个文件卡在这条。
 *
 * 判据：前缀在符号表里已经是变量/函数（非 typedef）就不可能是模块前缀。
 */
struct folio { int x; };
struct pid { int nr; };
struct cfs_rq { int load; };

struct wait_page_key {
	struct folio *folio;
	struct pid *pid;
	int bit_nr;
};

struct task_group {
	struct cfs_rq *cfs_rq;
};

int main(void)
{
	struct folio fo;
	struct pid id;
	struct wait_page_key key;
	struct task_group root_task_group;
	struct cfs_rq runq;

	fo.x = 7;
	id.nr = 11;
	runq.load = 24;

	/* 下面三行就是内核里那三种形态 */
	key.folio = &fo;
	key.pid = &id;
	key.bit_nr = 1;
	root_task_group.cfs_rq = &runq;

	if (key.folio->x != 7)
		return 1;
	if (key.pid->nr != 11)
		return 2;
	if (key.bit_nr != 1)
		return 3;
	if (root_task_group.cfs_rq->load != 24)
		return 4;

	/* 复合赋值、自增同理 */
	key.bit_nr += 41;
	if (key.bit_nr != 42)
		return 5;
	key.pid->nr++;
	if (id.nr != 12)
		return 6;
	return 42;
}
