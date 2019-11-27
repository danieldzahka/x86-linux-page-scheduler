#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

#include <pg_sched.h>
#include <pg_sched_priv.h>
#include <pg_sched_mem.h>

/* debugging */
int pg_sched_debug = 0;
module_param(pg_sched_debug, int, 0644);

static int
pg_sched_open(struct inode * inodep,
	      struct file  * filp)
{
  if (pg_sched_debug) printk(KERN_DEBUG "pg_sched device opened\n");

  return 0;
}

static int
pg_sched_release(struct inode * inodep,
		 struct file  * filp)
{
  if (pg_sched_debug) printk(KERN_DEBUG "pg_sched device released\n");

  return 0;
}

static long
pg_sched_ioctl(struct file * filp,
	       unsigned int  cmd,
	       unsigned long arg)
{
    int status;

    switch (cmd) {
    case PG_SCHED_SCAN_PT:
      printk(KERN_INFO "Requested Page Table Scan\n");
      status = 0;
      break;

    default:
      status = -ENOIOCTLCMD;
      break;
    }
            
    return status;
}

static struct file_operations 
pg_sched_fops = 
{
    .open           = pg_sched_open,
    .release        = pg_sched_release,
    .unlocked_ioctl = pg_sched_ioctl,
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
