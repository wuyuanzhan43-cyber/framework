/*
 * libc_hooks_gcc.c - 裸机 GCC 链接使用的空 newlib init/fini hook。
 *
 * 层级：project/工具链适配（仅 GCC）
 * 职责：满足 __libc_init_array()，同时不引入 hosted crt0。
 * 上下文：仅启动阶段；应用代码不会调用。
 * 依赖：无。
 */
/* CMSIS Reset_Handler 初始化 .data/.bss 后调用 __libc_init_array()。
 * 固件自带启动代码，因此链接时使用 -nostartfiles，而不是再引入
 * hosted 风格的 crt0 和 exit 路径。这个 C 固件不需要额外初始化/
 * 反初始化 hook。 */
void _init(void) {}
void _fini(void) {}
