#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

#include <pg_sched.h>
#include <pg_sched_priv.h>

static struct file_operations 
pg_sched_fops = 
{
    /* .open           = pg_sched_open, */
    /* .flush          = pg_sched_flush, */
    /* .unlocked_ioctl = pg_sched_ioctl, */
    /* .mmap           = pg_sched_mmap, */
};

static struct miscdevice
dev_handle =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name  = PG_SCHED_MODULE_NAME,
    .fops  = &pg_sched_fops
};

static int __init 
pg_sched_init(void)
{
    int status;

    status = misc_register(&dev_handle);
    if (status != 0) {
        return status;
    }

    printk(KERN_INFO "Initialized page_scheduler module\n");

    return 0;
}

static void __exit
pg_sched_exit(void ) 
{
    misc_deregister(&dev_handle);

    printk(KERN_INFO "Unloaded page_scheduler module\n");
}


module_init(pg_sched_init);
module_exit(pg_sched_exit);

MODULE_LICENSE("GPL");
