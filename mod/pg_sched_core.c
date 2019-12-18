#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kallsyms.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/kref.h>
#include <linux/list.h>

#include <pg_sched.h>
#include <pg_sched_priv.h>
#include <pg_sched_mem.h>

/* debugging */
int pg_sched_debug = 0;
module_param(pg_sched_debug, int, 0644);

static unsigned long log_sec = 1;
static unsigned long log_nsec = 0; 
module_param(log_sec, ulong, 0);
module_param(log_nsec, ulong, 0);

//maybe export?
static LIST_HEAD(pg_sched_tracked_pids);

struct tracked_process {
    pid_t pid;
    struct {
        int scans_to_be_idle;
        struct {
            unsigned long sec;
            unsigned long nsec;
        } period;
    } policy;

    struct kref refcount;
    struct list_head linkage;
    void (*release) (struct kref * refc);
};

static void
tracked_process_destructor(struct tracked_process * this)
{
    printk(KERN_INFO "Destructor called for pid %d\n", this->pid);
    //recursively free any nested objects
    kfree(this);
}

static void
tracked_process_release(struct kref * refc)
{
    struct tracked_process * this;

    this = container_of(refc, struct tracked_process, refcount);
    tracked_process_destructor(this);
}

static void
init_tracked_process(struct tracked_process * this,
                     pid_t pid)
{
    this->pid = pid;
    this->policy.scans_to_be_idle = 10;
    this->policy.period.sec  = 1;
    this->policy.period.nsec = 0;
    this->release = tracked_process_release;

    kref_init(&this->refcount); /* +1 for global linked list */
    INIT_LIST_HEAD(&this->linkage);
}

static int
allocate_tracker_and_add_to_list(pid_t pid)
{
    struct tracked_process * tracked_pid;

    tracked_pid = kzalloc(sizeof(*tracked_pid), GFP_KERNEL);
    if (!tracked_pid){
        printk(KERN_ALERT "Could not allocate tracker struct\n");
        return -1;
    }
    
    init_tracked_process(tracked_pid, pid);
    list_add(&tracked_pid->linkage, &pg_sched_tracked_pids);

    return 0;
}

static void
remove_tracker_from_list(pid_t pid)
{
    struct list_head * target, * pos;
    struct tracked_process * obj;

    obj = NULL;
    target = NULL;
    
    list_for_each(pos, &pg_sched_tracked_pids){
        obj = list_entry(pos, struct tracked_process, linkage);
        if (obj->pid == pid){
            target = pos;
            break;
        }
    }

    WARN_ON(obj == NULL || target == NULL);
    
    if (target != NULL) list_del(target); /* Remove from List */
    if (obj != NULL) kref_put(&obj->refcount, obj->release); //should probably call d'tor
}

/* seq_file stuff */
static int
pg_sched_data_show(struct seq_file *m,
		   void *v)
{

    if (print_page_access_data(m)){
	printk(KERN_EMERG "Seq File has overflowed\n");
    }
    return 0;
}

DEFINE_SHOW_ATTRIBUTE(pg_sched_data);

static int
pg_sched_open(struct inode * inodep,
	      struct file  * filp)
{
    int status;

    if (pg_sched_debug) printk(KERN_DEBUG "pg_sched device opened\n");

    /*To Do: Grab onto the mm struct and bump the refcount*/
    /*To Do: Launch the page scheduler thread*/
    
    /* register_init_vmas(current->mm); */
    /* status = launch_scanner_kthread(current->mm, log_sec, log_nsec); */

    status = 0;
    return status;
}

static int
pg_sched_release(struct inode * inodep,
		 struct file  * filp)
{
    int status;
    if (pg_sched_debug) printk(KERN_DEBUG "pg_sched device released\n");

    //Though for now, the blocking means that the user mm exists
    /*To Do: Forget the mm struct and decrement the refcount*/
    /*To Do: Kill the page scheduler thread*/

    /* status = stop_scanner_thread(); */
    /* if (status){ */
    /*     return -1; */
    /* } */

    //print out data
    /* print_page_access_data(); */
    
    //free memory
    /* free_page_access_arrays(); */

    status = 0;
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
        /* printk(KERN_INFO "Requested Page Table Scan\n"); */
        /* NEED TO BUMP THE MM REFCOUNT PROBABLY!! */
        /* count_vmas(current->mm); */
        status = 0;
        break;

    case PG_SCHED_TRACK_PID:
        {
            struct track_pid_arg my_arg;
            int n;
            
            n = copy_from_user(&my_arg, (void *) arg, sizeof(struct track_pid_arg));
            if (n){
                printk(KERN_ALERT "Could not read IOCTL ARG\n");
                status = -1;
                break;
            }
            printk(KERN_INFO "tracking pid: %d\n", my_arg.pid);
            status = allocate_tracker_and_add_to_list(my_arg.pid);
            if (status){
                printk(KERN_ALERT "Error Allocating tracker\n");
                break;
            }
        }
        status = 0;
        break;

    case PG_SCHED_UNTRACK_PID:
        {
            struct untrack_pid_arg my_arg;
            int n;
            
            n = copy_from_user(&my_arg, (void *) arg, sizeof(struct untrack_pid_arg));
            if (n){
                printk(KERN_ALERT "Could not read IOCTL ARG\n");
                status = -1;
                break;
            }
            printk(KERN_INFO "untracking pid: %d\n", my_arg.pid);
            remove_tracker_from_list(my_arg.pid);
        }
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

static int
get_nonexported_symbols(void)
{
    unsigned long sym;
    
    /* Grab non-exported funtion symbols */
    sym = kallsyms_lookup_name("vma_is_stack_for_current");
    if (sym == 0){
	printk(KERN_ALERT "func not found!\n");
	return -1;
    }

    my_vma_is_stack_for_current = (fake_vma_is_stack_for_current) sym;

    sym = kallsyms_lookup_name("isolate_lru_page");
    if (sym == 0){
	printk(KERN_ALERT "func not found!\n");
	return -1;
    }

    my_isolate_lru_page = (fake_isolate_lru_page) sym;

    sym = kallsyms_lookup_name("migrate_pages");
    if (sym == 0){
	printk(KERN_ALERT "func not found!\n");
	return -1;
    }

    my_migrate_pages = (fake_migrate_pages) sym;

    return 0;
}

static struct proc_dir_entry * entry;

static int __init 
pg_sched_init(void)
{
    int status;

    status = get_nonexported_symbols();
    if (status){
        printk(KERN_ALERT "Failed to parse non exported functions\n");
        return status;
    }
    
    status = misc_register(&dev_handle);
    if (status != 0) {
        return status;
    }

    entry = proc_create("pg_sched_data", 0, NULL, &pg_sched_data_fops);

    printk(KERN_INFO "Initialized page_scheduler module\n");

    return 0;
}

static void __exit
pg_sched_exit(void ) 
{
    /* free_page_access_arrays(); //remove later */
    misc_deregister(&dev_handle);
    remove_proc_entry("pg_sched_data",NULL);
    printk(KERN_INFO "Unloaded page_scheduler module\n");
}


module_init(pg_sched_init);
module_exit(pg_sched_exit);

MODULE_LICENSE("GPL");
