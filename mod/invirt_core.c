/*
 * This file is part of the invirt project developed at Washington
 * University in St. Louis
 *
 * This is free software.  You are permitted to use, redistribute, and
 * modify it as specified in the file "LICENSE.md".
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

#include <linux/sched/mm.h>
#include <linux/sched/task.h>

#include <invirt.h>
#include <invirt_priv.h>

struct invirt_hashlist * gbl_tg_hashtable;

/* debugging */
int invirt_debug = 0;
module_param(invirt_debug, int, 0644);

static int
invirt_attach_target_tgid(struct invirt_thread_group * tg,
                          pid_t                        target_tgid)
{
    struct pid * pid;
    struct task_struct * task;
    struct mm_struct * mm;
    int status;

    mutex_lock(&(tg->mutex));

    if (tg->target_tgid != 0) {
        status = -EBUSY;
        goto out;
    }

    pid = find_get_pid(target_tgid);
    if (IS_ERR(pid)) {
        status = PTR_ERR(pid);
        goto out;
    }

    task = get_pid_task(pid, PIDTYPE_PID);
    if (IS_ERR(task)) {
        put_pid(pid);
        status = PTR_ERR(task);
        goto out;
    }

    mm = get_task_mm(task);
    if (IS_ERR(mm)) {
        put_task_struct(task);
        put_pid(pid);
        status = PTR_ERR(mm);
        goto out;
    }

    tg->target_mm   = mm;
    tg->target_task = task;
    tg->target_tgid = target_tgid;

    status = 0;

out:
    mutex_unlock(&(tg->mutex));
    return status;
}

static int
invirt_detach_target_tgid(struct invirt_thread_group * tg)
{
    int status;

    mutex_lock(&(tg->mutex));

    if (tg->target_tgid != 0) {
        status = -EINVAL;
        goto out;
    }

    mmput(tg->target_mm);
    put_task_struct(tg->target_task);
    put_pid(task_pid(tg->target_task));

    tg->target_tgid = 0;

    status = 0;

out:
    mutex_unlock(&(tg->mutex));
    return status;
}

/*
 * user open of the invirt driver
 */
static int
invirt_open(struct inode * inodep,
            struct file  * filp)
{
    struct invirt_thread_group * tg;
    int index;
    unsigned long flags;

    /* if this has already been done, return silently */
    tg = invirt_tg_ref_by_tgid(current->tgid);
    if (!IS_ERR(tg)) {
        invirt_tg_deref(tg);
        return 0;
    }

    /* create tg */
    tg = kzalloc(sizeof(struct invirt_thread_group), GFP_KERNEL);
    if (tg == NULL)
        return -ENOMEM;

    tg->tgid = current->tgid;
    INIT_LIST_HEAD(&(tg->tg_hashnode));
    spin_lock_init(&(tg->lock));
    mutex_init(&(tg->mutex));

    invirt_tg_not_destroyable(tg);

    /* add tg to its hashlist */
    index = invirt_tg_hashtable_index(tg->tgid);
    write_lock_irqsave(&(gbl_tg_hashtable[index].lock), flags);
    list_add_tail(&(tg->tg_hashnode), &(gbl_tg_hashtable[index].list));
    write_unlock_irqrestore(&(gbl_tg_hashtable[index].lock), flags);

    return 0;
}

/*
 * Destroy an invirt_thread_group
 */
static void
invirt_destroy_tg(struct invirt_thread_group * tg)
{
    invirt_tg_destroyable(tg);
    //invirt_tg_deref(tg);
}

void
invirt_teardown_tg(struct invirt_thread_group * tg)
{
    unsigned long flags;
    int index;

    spin_lock_irqsave(&(tg->lock), flags);
    WARN_ON(tg->flags & INVIRT_FLAG_DESTROYING);
    tg->flags |= INVIRT_FLAG_DESTROYING;
    spin_unlock_irqrestore(&(tg->lock), flags);

    /* Remove tg structure from its hash list */
    index = invirt_tg_hashtable_index(tg->tgid);
    write_lock_irqsave(&(gbl_tg_hashtable[index].lock), flags);
    list_del_init(&tg->tg_hashnode);
    write_unlock_irqrestore(&(gbl_tg_hashtable[index].lock), flags);

    spin_lock_irqsave(&(tg->lock), flags);
    WARN_ON(tg->flags & INVIRT_FLAG_DESTROYED);
    tg->flags |= INVIRT_FLAG_DESTROYED;
    spin_unlock_irqrestore(&(tg->lock), flags);

    if (tg->target_tgid != 0)
        invirt_detach_target_tgid(tg);

#if 0
    /*
     * We don't call invirt_destroy_tg() here. We can't call
     * mmu_notifier_unregister() when the stack started with a
     * mmu_notifier_release() callout or we'll deadlock in the kernel
     * MMU notifier code.  invirt_destroy_tg() will be called when the
     * close of /dev/invirt occurs as deadlocks are not possible then.
     */
    invirt_tg_deref(tg);
#endif
    invirt_destroy_tg(tg);
}

static int
invirt_flush(struct file  * filp,
             fl_owner_t     owner)
{
    struct invirt_thread_group * tg;
    unsigned long flags;

    /* During a call to fork() there is a check for whether the parent process
     * has any pending signals. If there are pending signals, then the fork
     * aborts, and the child process is removed before delivering the signal
     * and starting the fork again. In that case, we can end up here, but since
     * we're mid-fork, current is pointing to the parent's task_struct and not
     * the child's. This would cause us to remove the parent's invirt mappings
     * by accident. We check here whether the owner pointer we have is the same
     * as the current->files pointer. If it is, or if current->files is NULL,
     * then this flush really does belong to the current process. If they don't
     * match, then we return without doing anything since the child shouldn't
     * have a valid invirt_thread_group struct yet.
     */
    if (current->files && current->files != owner)
        return 0;

    /*
     * invirt_flush() can get called twice for thread groups which inherited
     * /dev/invirt: once for the inherited fd, once for the first explicit use
     * of /dev/invirt. If we don't find the tg via invirt_tg_ref_by_tgid() we
     * assume we are in this type of scenario and return silently.
     *
     * Note the use of _all: we want to get this even if it's
     * destroying/destroyed.
     */
    tg = invirt_tg_ref_by_tgid_all(current->tgid);
    if (IS_ERR(tg))
        return 0;

    /* 
     * Two threads could have called invirt_flush at about the same time, and
     * thus invirt_tg_ref_by_tgid could return the same tg in both threads.
     * Guard against this.
     */
    spin_lock_irqsave(&tg->lock, flags);
    if (tg->flags & INVIRT_FLAG_FLUSHING) {
        spin_unlock_irqrestore(&tg->lock, flags);
        return 0;
    }
    tg->flags |= INVIRT_FLAG_FLUSHING;
    spin_unlock_irqrestore(&tg->lock, flags);

    invirt_teardown_tg(tg);

    invirt_tg_deref(tg);

#if 0
    /* BJK: we can't remove the tg from the lookup list yet, because of 
     * this call stack is possible:
     *  invirt_flush() ->
     *    invirt_destroy_tg() ->
     *      invirt_mmu_notifier_unlink() ->
     *        invirt_mmu_release() 
     *          invirt_mmu_release() will need to query the tg from the
     *          lookup list.
     *
     * We need to take an extra ref of the tg, so that we can pull it from the
     * hashlist after we call invirt_destroy_tg() below.
     */      

    invirt_tg_ref(tg);

    invirt_destroy_tg(tg);

    invirt_tg_deref(tg);
#endif
    return 0;
}

static long
invirt_ioctl(struct file * filp,
             unsigned int  cmd,
             unsigned long arg)
{
    struct invirt_thread_group * tg;
    int status;

    tg = invirt_tg_ref_by_tgid(current->tgid);
    if (IS_ERR(tg))
        return PTR_ERR(tg);

    switch (cmd) {
        case INV_IOC_SET_TARGET_TID:
            if (arg == 0) {
                status = -EINVAL;
                break;
            }

            status = invirt_attach_target_tgid(tg, (pid_t)arg);
            break;

        case INV_IOC_CLEAR_TARGET_TID:
            if (arg != 0) {
                status = -EINVAL;
                break;
            }

            status = invirt_detach_target_tgid(tg);
            break;

        default:
            status = -ENOIOCTLCMD;
            break;
    }
            
    invirt_tg_deref(tg);
    return status;
}

static int
invirt_mmap(struct file           * filp,
            struct vm_area_struct * vma)
{
    struct invirt_thread_group * tg;

    tg = invirt_tg_ref_by_tgid(current->tgid);
    if (IS_ERR(tg))
        return PTR_ERR(tg);

    return invirt_setup_vma(tg, vma);
}

static struct file_operations 
invirt_fops = 
{
    .open           = invirt_open,
    .flush          = invirt_flush,
    .unlocked_ioctl = invirt_ioctl,
    .mmap           = invirt_mmap,
};

static struct miscdevice
dev_handle =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = INVIRT_MODULE_NAME,
    .fops = &invirt_fops
};

static int __init 
invirt_init(void)
{
    int status, i;

    gbl_tg_hashtable = kzalloc(
        sizeof(struct invirt_hashlist) * INVIRT_TG_HASHTABLE_SIZE,
        GFP_KERNEL
    );
    if (gbl_tg_hashtable == NULL)
        return -ENOMEM;

    for (i = 0; i < INVIRT_TG_HASHTABLE_SIZE; i++) {
        rwlock_init(&(gbl_tg_hashtable[i].lock));
        INIT_LIST_HEAD(&(gbl_tg_hashtable[i].list));
    }

    status = misc_register(&dev_handle);
    if (status != 0) {
        kfree(gbl_tg_hashtable);
        return status;
    }

    printk(KERN_INFO "Initialized invirt module\n");

    return 0;
}

static void __exit
invirt_exit(void ) 
{
    misc_deregister(&dev_handle);

    kfree(gbl_tg_hashtable);

    printk(KERN_INFO "Unloaded invirt module\n");
}

module_init(invirt_init);
module_exit(invirt_exit);

MODULE_LICENSE("GPL");
