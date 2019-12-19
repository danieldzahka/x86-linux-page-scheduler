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
#include <linux/pid.h>

#include <linux/sched/mm.h>

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

static LIST_HEAD(pg_sched_tracked_pids);

struct page_desc {
    int accesses;
    int last_touched;
    int node;
};

struct vma_desc {
    struct vm_area_struct * vma; /*Use as key*/
    unsigned long           vm_start;
    unsigned long           vm_end;
    struct page_desc *      page_accesses;
    int                     num_pages;

    struct list_head linkage; /* struct tracked_process -> vma_list */
};

struct tracked_process {
    pid_t pid;
    int migration_enabled;
    struct {
        int scans_to_be_idle;
        struct {
            unsigned long sec;
            unsigned long nsec;
        } period;
    } policy;

    struct mm_struct * mm;

    struct kref refcount;
    struct list_head linkage; /* List: (static global) pg_sched_tracked_pids */
    struct list_head vma_desc_list; /* VMA's that we are watching */
    int vma_desc_list_length; /* For debugging merged or disappearing vma's */

    struct {
        ktime_t kt;
        struct hrtimer timer;
        struct task_struct* scanner_thread;
    } scanner_thread_struct;
    
    void (*release) (struct kref * refc);
};

static int
allocate_track_vma(struct tracked_process * this,
                   struct vm_area_struct  * vma,
                   struct vma_desc desc   * res)
{
    res = kzalloc(sizeof(struct vma_desc desc), GFP_KERNEL);

    res->vma           = vma;
    res->vm_start      = vma->vm_start;
    res->vm_end        = vma->vm_end;
    res->page_accesses = NULL;
    res->num_pages     = (vma->vm_end - vma->vm_start) & ((1<<12) - 1) ?
	((vma->vm_end - vma->vm_start) >> 12) + 1 : (vma->vm_end - vma->vm_start) >> 12;;
    INIT_LIST_HEAD(&res->linkage);
    res->page_accesses = kzalloc(num_pages * sizeof(struct page_desc), GFP_KERNEL);

    if (IS_ERR(res->page_accesses)){
	printk(KERN_EMERG "Error Allocating page access vector\n");
	return -1;
    }

    list_add(&res->linkage, &this->vma_desc_list);
    this->vma_desc_list_length++;
    
    return 0;
}

/*This also handles merged or resized vmas*/
static int
get_vma_desc_add_if_absent(struct tracked_process * this,
                           struct vm_area_struct * vma,
                           struct vma_desc * res)
{
    int status;
    struct vma_desc * p;
    int stale = 0;

    list_for_each(p, &this->vma_desc_list, linkage){
        if (vma == p->vma){
            if (vma->vm_start != p->vm_start ||
		vma->vm_end != p->vm_end){
                stale = 1;
                break;
	    } else {
                res = p;
                return 0;
            }
        }
    }

    if (stale){
        list_del(&p->linkage);
        this->vma_desc_list_length--;
        WARN_ON(this->vma_desc_list_length < 0);
        free_vma_desc(p);
    }

    status = allocate_track_vma(this, vma, res);
    if (status){
        printk(KERN_EMERG "COULD NOT ALLOCATE VMA DESC\n");
        return status;
    }
    
    return 0;
}

static struct vma_desc *
get_vma_desc(struct tracked_process * this, struct vm_area_struct * vma)
{
    int i;
    i = index_of_vma(vma);
    return &(new_vmas[i]);
}

static void
free_vma_desc(struct vma_desc * desc)
{
    kfree(desc->page_accesses);
    kfree(desc);
}

static void
free_vma_desc_list(struct list_head * list)
{
    struct vma_desc * desc;
    
    while (!list_empty(list)){
        desc = list_first_entry(list, struct vma_desc, linkage);
        free_vma_desc(desc);
    }    
}

static void
tracked_process_destructor(struct tracked_process * this)
{
    int status;
    status = 0;

    printk(KERN_INFO "Destructor called for pid %d\n", this->pid);

    /*Cancel Timer if active*/
    if (hrtimer_active(&this->scanner_thread_struct.timer))
        hrtimer_cancel(&this->scanner_thread_struct.timer);

    /*Cancel Thread*/
    if (this->scanner_thread_struct.scanner_thread)
        status = kthread_stop(this->scanner_thread_struct.scanner_thread);

    if (status) printk(KERN_EMERG "ERROR: Could not kill kthread\n");
    
    free_vma_desc_list(&this->vma_desc_list);
    mmdrop(this->mm);// -1 for release
    kfree(this);
}

static void
tracked_process_release(struct kref * refc)
{
    struct tracked_process * this;

    this = container_of(refc, struct tracked_process, refcount);
    tracked_process_destructor(this);
}

static int
init_tracked_process(struct tracked_process * this,
                     pid_t pid,
                     unsigned long log_sec,
                     unsigned long log_nsec)
{
    struct pid* p;
    struct task_struct * tsk;
    
    this->pid = pid;
    this->migration_enabled = 0;
    this->policy.scans_to_be_idle = 10;
    this->policy.period.sec  = log_sec;
    this->policy.period.nsec = log_nsec;
    this->release = tracked_process_release;

    p = find_get_pid(pid); /* +1 temp*/
    if (!p){
        printk(KERN_ALERT "Error: pid struct not found!!");
        return -1;
    }

    tsk = pid_task(p, PIDTYPE_PID); /* Does not elevate the refcount */
    if (!tsk){
        printk(KERN_ALERT "Error: task struct not found!!");
        put_pid(p); /* -1 temp */
        return -1;
    }
    
    this->mm = tsk->mm;
    mmgrab(this->mm); // +1 for ref

    put_pid(p); /* -1 temp */
    
    kref_init(&this->refcount); /* +1 for global linked list */
    INIT_LIST_HEAD(&this->linkage);
    INIT_LIST_HEAD(&this->vma_desc_list);
    this->vma_desc_list_length = 0;

    /* Launch Thread from Here? */
    this->scanner_thread_struct.kt = ktime_set(log_sec, log_nsec);
    hrtimer_init(&this->scanner_thread_struct.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    this->scanner_thread_struct.timer.function = expiration_func;
    scanner_thread = kthread_run(scanner_func, this, "pg_sched_scanner");
    if (scanner_thread)
        hrtimer_start(&this->scanner_thread_struct.timer, kt, HRTIMER_MODE_REL);
    else
        return -2;

    
    return 0;
}

static int
allocate_tracker_and_add_to_list(pid_t pid)
{
    struct tracked_process * tracked_pid;
    int status;

    tracked_pid = kzalloc(sizeof(*tracked_pid), GFP_KERNEL);
    if (!tracked_pid){
        printk(KERN_ALERT "Could not allocate tracker struct\n");
        return -1;
    }
    
    status = init_tracked_process(tracked_pid, pid);
    if (status){
        printk(KERN_ALERT "struct track process init failed\n");
        if (status == - 2) kfref_put(&tracked_pid->refcount);
        return status;
    }
    
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
    if (obj != NULL) kref_put(&obj->refcount, obj->release); //should call d'tor, unless the other thread still has a ref
}

/* seq_file stuff */
int
print_page_access_data(struct seq_file * m)
{
    int i;
    int j;

    seq_puts(m, "vma,pfn,accesses,last_touch,node\n");
    for (i = 0; i < new_vmas_size; ++i){
	for (j = 0; j < new_vmas[i].num_pages; ++j){
	    if (seq_has_overflowed(m)){
		return 1;
	    } else {
		seq_printf(m, "%d,%d,%d,%d,%d\n", i, j,
			   new_vmas[i].page_accesses[j].accesses,
			   new_vmas[i].page_accesses[j].last_touched,
			   new_vmas[i].page_accesses[j].node);
	    }
	}
    }
    return 0;
}

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

            status = stop_scanner_thread();
            if (status){
                printk("ERROR: Couldn't stop scanner thread\n");
                break;
            }

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
