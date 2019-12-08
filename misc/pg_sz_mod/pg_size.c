#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mm_types.h>

static int __init 
pg_size_init(void)
{
    printk(KERN_ALERT "struct page: %lu B\n", sizeof(struct page));
    return 0;
}

static void __exit
pg_size_exit(void ) 
{
}

module_init(pg_size_init);
module_exit(pg_size_exit);

MODULE_LICENSE("GPL");
