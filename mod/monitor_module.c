#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/kthread.h>

static unsigned long log_sec = 1;
static unsigned long log_nsec = 0;
static ktime_t kt;
static struct hrtimer timer;
static struct task_struct * my_task = 0;
 
module_param(log_sec, ulong, 0);
module_param(log_nsec, ulong, 0);

static int my_thread_func(void * data){
    int iteration;
    iteration = 0;
    while(1){    
	printk(KERN_ALERT "current->nvcsw:%lu\ncurrent->nivcsw:%lu\niteration:%d", current->nvcsw, current->nivcsw, iteration);
	set_current_state(TASK_INTERRUPTIBLE);
	schedule();
	if(kthread_should_stop()){
	    break;
	}
	iteration++;
    }
    printk(KERN_ALERT "Exiting the loop, Terminating thread\n");
    return 0;
}

/* timer function */
static enum hrtimer_restart expiration_func(struct hrtimer * tim){
    wake_up_process(my_task);
    hrtimer_forward_now(tim, kt);
    printk(KERN_ALERT "timer expired\n");
    return HRTIMER_RESTART;
}
/* init function - logs that initialization happened, returns success */
static int monitor_init (void) {
    printk(KERN_ALERT "monitor module initialized\n");
    kt = ktime_set(log_sec, log_nsec);
    hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    timer.function = expiration_func;
    hrtimer_start(&timer, kt, HRTIMER_MODE_REL);
    my_task = kthread_run(my_thread_func,0,"my_thread");/*launch thread*/
    return 0;
}

/* exit function - logs that the module is being removed */
static void monitor_exit (void) {
    int ret;
    printk(KERN_ALERT "monitor module is being unloaded\n");
    hrtimer_cancel(&timer);
    ret = kthread_stop(my_task);
    if(ret){
    	printk(KERN_ALERT "could not kill thread with return value:%d\n", ret);
    }
}

module_init (monitor_init);
module_exit (monitor_exit);

MODULE_LICENSE ("GPL");
MODULE_AUTHOR ("Daniel Zahka, Anish Naik");
MODULE_DESCRIPTION ("Lab 1");
