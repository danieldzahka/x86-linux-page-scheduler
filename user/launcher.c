/* Daniel Zahka ~ dzahka3@gatech.edu */
/* Adapted from invrun code from Brian Kocoloski ~ brian.kocoloski@wustl.edu */

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/ioctl.h>

#include <pg_sched.h>
#include "hashtable.h"

#define PG_SCHED_NULL_PID 0

struct program_data {
    char * this_exe;
    int dev_fd;
    
    /* target data */
    char * exe;
    pid_t pid;

    /* target argc/argv/envp */
    int argc;
    char ** argv;
    int envc;
    char ** envp;

    /* pipes for dup()ing target's stdin/stderr/stdout */
    int target_infd[2];
    int target_outfd[2];
    int target_errfd[2];
};

typedef int (*fd_handler_fn)(int fd, void * priv_data);
struct fd_handler {
    int           fd;
    fd_handler_fn fn;
    void        * priv_data;
};

/* a set of globals for handling signals and event processing */
static int sigchld_pipe[2];
static int max_fd;
static fd_set fdset;
static bool terminate;
static struct hashtable * fd_to_handler_table;

/*
 * utility to read up to 'data_len' bytes from a non-blocking fd
 */
static size_t
read_data(int     fd,
          char  * data,
          size_t  data_len)
{
    size_t bytes_read;
    ssize_t cur_bytes;

    for (bytes_read = 0; bytes_read < data_len; bytes_read += cur_bytes) {
        cur_bytes = read(fd, data+bytes_read, data_len-bytes_read);
        
        if (cur_bytes < 0) {
            if (errno == EINTR) {
                cur_bytes = 0;
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            
            fprintf(stderr, "Failed to read from fd: %s\n", strerror(errno));
            return -1;
        }

        if (cur_bytes == 0)
            break;
    }

    return bytes_read;
}

/*
 * utility to write 'data_len' bytes to a non-blocking fd
 */
static ssize_t
write_data(int          fd,
           const char * data,
           size_t       data_len)
{
    size_t bytes_written;
    ssize_t cur_bytes;

    for (bytes_written = 0; bytes_written < data_len; bytes_written += cur_bytes) {
        cur_bytes = write(fd, data+bytes_written, data_len-bytes_written);
        
        if (cur_bytes < 0) {
            if (errno == EINTR) {
                cur_bytes = 0;
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            
            fprintf(stderr, "Failed to write to fd: %s\n", strerror(errno));
            return -1;
        }

        if (cur_bytes == 0)
            break;
    }

    return bytes_written;
}

/*
 * Data structures and callback routines for handling event processing
 * in the invrun process
 */

static unsigned int
fd_hash_fn(uintptr_t key)
{
    return invirt_hash_ptr(key);
}

static int
fd_eq_fn(uintptr_t key1,
         uintptr_t key2)
{
    return (key1 == key2);
}

static int
remember_fd(int           fd,
            fd_handler_fn fn,
            void        * priv_data)
{
    struct fd_handler * handler;
    int status;

    status = invirt_htable_search(fd_to_handler_table, fd);
    if (status != 0) {
        fprintf(stderr, "Atttempted to register duplicate FD handler"
            " (fd=%d)\n", fd); 
        return -1;
    }

    handler = malloc(sizeof(struct fd_handler));
    if (handler == NULL) {
        fprintf(stderr, "Could not malloc fd_handler\n");
        return -1;
    }

    handler->fd = fd;
    handler->fn = fn;
    handler->priv_data = priv_data;

    status = invirt_htable_insert(fd_to_handler_table, (uintptr_t)fd, 
        (uintptr_t)handler);
    if (status == 0) {
        fprintf(stderr, "Could not update fd_to_handler_table?\n");
        free(handler);
        return -1;
    }

    if (fd > max_fd)
        max_fd = fd;

    FD_SET(fd, &fdset);

    return 0;
}

static void
forget_fd(int fd)
{
    struct fd_handler * handler;

    handler = (struct fd_handler *)invirt_htable_remove(fd_to_handler_table, 
        (uintptr_t)fd, 0);
    assert(handler != NULL);

    FD_CLR(fd, &fdset);
    free(handler);
}

static struct fd_handler *
handler_of_fd(int fd)
{
    return (struct fd_handler *)invirt_htable_search(fd_to_handler_table, fd);
}


/*
 * data available on shadow's stdout 
 */
/* static int */
/* handle_shadow_stdout(int    fd, */
/*                      void * priv_data) */
/* { */
/*     char data[PAGE_SIZE]; */
/*     ssize_t bytes; */
/*     size_t total_bytes; */

/*     for (total_bytes = 0; ; total_bytes += bytes) { */
/*         /\* read out the data *\/ */
/*         bytes = read_data(fd, data, PAGE_SIZE-1); */
/*         if (bytes <= 0) */
/*             return bytes; */

/*         data[bytes] = '\0'; */

/*         printf("%s", data); */
/*         fflush(stdout); */
/*     } */

/*     assert(1 == 0); */
/*     return -1; */
/* } */

/*
 * data avilable on shadow's stderr
 */
/* static int */
/* handle_shadow_stderr(int    fd, */
/*                      void * priv_data) */
/* { */
/*     char data[PAGE_SIZE]; */
/*     ssize_t bytes; */
/*     size_t total_bytes; */

/*     for (total_bytes = 0; ; total_bytes += bytes) { */
/*         /\* read out the data *\/ */
/*         bytes = read_data(fd, data, PAGE_SIZE-1); */
/*         if (bytes <= 0) */
/*             return bytes; */

/*         data[bytes] = '\0'; */

/*         printf("%s", data); */
/*         fflush(stdout); */
/*     } */

/*     assert(1 == 0); */
/*     return -1; */
/* } */

/*
 * data available on target's stdout
 */
static int
handle_target_stdout(int    fd,
                     void * priv_data)
{
    char data[PAGE_SIZE];
    ssize_t bytes, bytes_written;
    size_t total_bytes;

    for (total_bytes = 0; ; total_bytes += bytes) {
        /* read out the data */
        bytes = read_data(fd, data, PAGE_SIZE-1);
        if (bytes <= 0)
            return bytes;

        data[bytes] = '\0';
        bytes_written = write_data(STDOUT_FILENO, data, bytes);
        assert(bytes_written == bytes);
    }

    assert(1 == 0);
    return -1;
}

/*
 * data available on target's stderr
 */
static int
handle_target_stderr(int    fd,
                     void * priv_data)
{
    char data[PAGE_SIZE];
    ssize_t bytes, bytes_written;
    size_t total_bytes;

    for (total_bytes = 0; ; total_bytes += bytes) {
        /* read out the data */
        bytes = read_data(fd, data, PAGE_SIZE-1);
        if (bytes <= 0)
            return bytes;

        data[bytes] = '\0';
        bytes_written = write_data(STDERR_FILENO, data, bytes);
        assert(bytes_written == bytes);
    }

    assert(1 == 0);
    return -1;
}

/*
 * data available on stdin 
 */
static int
handle_stdin(int    fd,
             void * priv_data)
{
    char data[PAGE_SIZE];
    ssize_t bytes, bytes_written;
    size_t total_bytes;

    struct program_data * p_data = (struct program_data *)priv_data;

    for (total_bytes = 0; ; total_bytes += bytes) {
        /* read out the data */
        bytes = read_data(fd, data, PAGE_SIZE-1);
        if (bytes <= 0)
            return bytes;

        data[bytes] = '\0';
        bytes_written = write_data(p_data->target_infd[1], data, bytes);
        assert(bytes_written == bytes);
    }

    assert(1 == 0);
    return -1;
}

static int
pg_sched_untrack_pid(struct program_data * data)
{
    int status;
    struct untrack_pid_arg arg = {
        .pid = data->pid
    };

    status = ioctl(data->dev_fd, PG_SCHED_UNTRACK_PID, &arg);
    if (status == -1){
        puts("IOCTL ERROR\n");
        return status;
    }
    
    status = close(data->dev_fd);
    if (status){
        puts("Error closing " PG_SCHED_DEVICE_PATH "\n");
        return status;
    }

    return 0;
}

static int
pg_sched_track_pid(struct program_data * data)
{

    int status;
    int dev_open_flags = 0;
    struct track_pid_arg arg = {
        .pid = data->pid
    };

    data->dev_fd = open(PG_SCHED_DEVICE_PATH, dev_open_flags);
    if (data->dev_fd == -1){
        fprintf(stderr, "Couldn't open " PG_SCHED_DEVICE_PATH "\n");
        return -1;
    }

    status = ioctl(data->dev_fd, PG_SCHED_TRACK_PID, &arg);
    if (status == -1){
        fprintf(stderr, "IOCTL ERROR\n");
        return status;
    }
    
    return 0;
}

/*
 * teardown pipes and other state associated with
 * the target
 */
static void
teardown_target(struct program_data * data)
{

    close(data->target_infd[1]);
    close(data->target_outfd[0]);
    close(data->target_errfd[0]);

    forget_fd(STDIN_FILENO);
    forget_fd(data->target_outfd[0]);
    forget_fd(data->target_errfd[0]);
}

/*
 * kill a running target process
 */
static void
kill_and_teardown_target(struct program_data * data)
{
    int status;
    
    /* Tell module to release mm struct, close dev */
    status = pg_sched_untrack_pid(data);
    if (status){
        fprintf(stderr, "Error: Could not untrack target pid\n");
    }
    
    kill(data->pid, SIGKILL);
    waitpid(data->pid, NULL, 0);

    teardown_target(data);
}

/*
 * teardown pipes and other state associated with
 * the shadow
 */
/* static void */
/* teardown_shadow(struct program_data * data) */
/* { */
/*     close(data->shadow_outfd[0]); */
/*     close(data->shadow_errfd[0]); */

/*     forget_fd(data->shadow_outfd[0]); */
/*     forget_fd(data->shadow_errfd[0]); */

/*     unlink(data->shadow_exe); */
/* } */

/*
 * kill a running shadow process
 */
/* static void */
/* kill_and_teardown_shadow(struct program_data * data) */
/* { */
/*     kill(data->shadow_pid, SIGTERM); */
/*     waitpid(data->shadow_pid, NULL, 0); */

/*     teardown_shadow(data); */
/* } */

/*
 * we received sigchld 
 */
static int
handle_sigchld(int    fd,
               void * priv_data)
{
    struct program_data * p_data = (struct program_data *)priv_data;
    pid_t pid;
    char byte;
    int ex_status;
    int status;

    /* consume byte from self pipe */
    assert(read(fd, &byte, sizeof(char)) == sizeof(char));
    assert(byte == 'c');


    /* Tell module to release mm struct, close dev */
    status = pg_sched_untrack_pid(p_data);
    if (status){
        fprintf(stderr, "Error: Could not untrack target pid\n");
    }
    
    /* see if target is down */
    pid = waitpid(p_data->pid, &ex_status, WNOHANG);
    if (pid == p_data->pid) {

        /* printf("target died\n"); */
        fflush(stdout);
        
        if (WIFEXITED(ex_status)) {
            printf("Target exited with status %d\n", WEXITSTATUS(ex_status));

            teardown_target(p_data);
            terminate = true;
        }

        p_data->pid = PG_SCHED_NULL_PID;
    }

    /* see if shadow is down */
    /* pid = waitpid(p_data->shadow_pid, &ex_status, WNOHANG); */
    /* if (pid == p_data->shadow_pid) { */

    /*     if (WIFEXITED(ex_status)) { */
    /*         if (p_data->pid != PG_SCHED_NULL_PID) */
    /*             fprintf(stderr, "Shadow exited while target still running " */
    /*                 " -- tearing down target\n"); */

    /*         teardown_shadow(p_data); */
    /*         terminate = true; */
    /*     } */

    /*     p_data->shadow_pid = PG_SCHED_NULL_PID; */
    /* } */

    return 0;
}

/*
 * signal handlers 
 */
static void
handle_sigchld_signal(int         sig,
                      siginfo_t * siginfo,
                      void      * ucontext)
{
    /* write to self pipe to defer the heavy processing we can't do here */
    assert(write(sigchld_pipe[1], "c", sizeof(char)) == sizeof(char));
}

static void
handle_sigint_signal(int sig)
{
    terminate = true;
}

static int
mark_fd_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (!(flags & O_NONBLOCK))
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return 0;
}

static int
mark_fds_nonblocking(struct program_data * data)
{
    return (
        /* (mark_fd_nonblocking(data->shadow_outfd[0]) != 0) || */
        /* (mark_fd_nonblocking(data->shadow_errfd[0]) != 0) || */
        (mark_fd_nonblocking(data->target_outfd[0]) != 0) ||
        (mark_fd_nonblocking(data->target_errfd[0]) != 0) ||
        (mark_fd_nonblocking(STDIN_FILENO)          != 0) ||
        (mark_fd_nonblocking(sigchld_pipe[0])       != 0)
    );

}
static int
monitor_fds(struct program_data * data)
{
    int status;

    /* first, mark our read-side pipe fds as non-blocking */
    status = mark_fds_nonblocking(data);
    if (status != 0) {
        fprintf(stderr, "Failed to mark file descriptors with O_NONBLOCK\n");
        return -1;
    }
    
    /* status = remember_fd(data->shadow_outfd[0], handle_shadow_stdout, data); */
    /* if (status != 0) { */
    /*     fprintf(stderr, "Failed to track fd\n"); */
    /*     goto out_1; */
    /* } */
    
    /* status = remember_fd(data->shadow_errfd[0], handle_shadow_stderr, data); */
    /* if (status != 0) { */
    /*     fprintf(stderr, "Failed to track fd\n"); */
    /*     goto out_2; */
    /* } */

    status = remember_fd(data->target_outfd[0], handle_target_stdout, data);
    if (status != 0) {
        fprintf(stderr, "Failed to track fd\n");
        goto out_3;
    }

    status = remember_fd(data->target_errfd[0], handle_target_stderr, data);
    if (status != 0) {
        fprintf(stderr, "Failed to track fd\n");
        goto out_4;
    }

    status = remember_fd(STDIN_FILENO, handle_stdin, data);
    if (status != 0) {
        fprintf(stderr, "Failed to track fd\n");
        goto out_5;
    }

    status = remember_fd(sigchld_pipe[0], handle_sigchld, data);
    if (status != 0) {
        fprintf(stderr, "Failed to track fd\n");
        goto out_6;
    }

    return 0;

out_6:
    forget_fd(STDIN_FILENO);

out_5:
    forget_fd(data->target_errfd[0]);

out_4:
    forget_fd(data->target_outfd[0]);

out_3:
/*     forget_fd(data->shadow_errfd[0]); */

/* out_2: */
/*     forget_fd(data->shadow_outfd[0]); */

/* out_1: */
    return -1;
}

static int
setup_sigchld_handler(void)
{
    struct sigaction sa;
    int status;

    /* first, setup a self pipe */
    status = pipe(sigchld_pipe);
    if (status != 0) {
        fprintf(stderr, "Failed to create pipe: %s\n", strerror(errno));
        return -1;
    }

    sa.sa_sigaction = &handle_sigchld_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDSTOP + SA_SIGINFO;

    status = sigaction(SIGCHLD, &sa, NULL);
    if (status != 0) {
        fprintf(stderr, "sigaction failed: %s\n", strerror(errno));

        close(sigchld_pipe[0]);
        close(sigchld_pipe[1]);

        return -1;
    }

    return 0;
}

static int
setup_sigint_handler(void)
{
    struct sigaction sa;
    int status;

    sa.sa_handler = &handle_sigint_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDSTOP;

    status = sigaction(SIGINT, &sa, NULL);
    if (status != 0) {
        fprintf(stderr, "sigaction failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static void
usage(FILE  * out_stream,
      char ** argv)
{
    fprintf(out_stream, 
        "Usage: %s [OPTIONS] <exe> <arguments ...>\n"
        "    -h (--help)        : print help and exit\n"
        "    -x (--exclude-env) : do not inherit current environment in"
                " target process\n",
        *argv
    );
    fflush(out_stream);
}

static int
setup_env(struct program_data * data,
          char               ** envp)
{
    char ** new_envp;
    int i, envc;
    
    /* determine number of entries in new envp */
    if (envp == NULL) {
        envc = 1;
    } else {
        for (i = 0; ; i++) {
            if (envp[i] == NULL)
                break;
        }

        envc = i + 1;
    }

    new_envp = malloc(sizeof(char *) * (envc + 1));
    if (new_envp == NULL) {
        fprintf(stderr, "Could not allocate envp: %s\n", strerror(errno));
        return -1;
    }

    /* copy in source env, if requested */
    for (i = 0; i < envc - 1; i++) {
        asprintf(&(new_envp[i]), "%s", envp[i]);
    }

    /* 
     * Add in LD_BIND_NOW to ensure dynamically linked libraries are pulled in
     * at program startup time, to force as much overhead forward as possible
     */
    asprintf(&(new_envp[i]), "LD_BIND_NOW=1");

    /* add null truncator */
    new_envp[i+1] = NULL;

    data->envp = new_envp;
    return 0;
}

static int
parse_cmd_line(int                   argc,
               char               ** argv,
               char               ** envp,
               struct program_data * data)
{
    int status, opt_off, opt_index, i;
    bool inherit_env = true;

    struct option long_options[] =
        {
         {"help",        no_argument,        0,  'h'},
         {"exclude-env", no_argument,        0,  'x'},
         {0,             0,                  0,  0}
        };

    opterr = 0; // quell error messages

    /* memset(data, 0, sizeof(struct program_data)); */

    while (1) {
        int c;
        
        c = getopt_long_only(argc, argv, "+hx", long_options, &opt_index);
        if (c == -1)
            break;

        switch (c) {
        case 'h':
            usage(stdout, argv);
            exit(EXIT_SUCCESS);

        case 'x':
            inherit_env = false;
            break;

        case '?':
        default:
            fprintf(stderr, "Error: invalid command line option\n");
            usage(stderr, argv);
            return -1;
        }
    }

    /* optind is the location in argv of the first non-option argument */
    opt_off = optind;
    if ((argc - opt_off) < 1) {
        usage(stderr, argv);
        return -1;
    }


    /* setup argc/argv/envp */
    {
        data->this_exe = argv[0];
        data->exe = argv[opt_off];
        data->argc = argc - opt_off;

        assert(data->argc >= 1);

        data->argv = malloc(sizeof(char *) * (data->argc + 1));
        if (data->argv == NULL) {
            fprintf(stderr, "Out of memory\n");
            data->argc = 0;
            return -1;
        }

        for (i = 0; opt_off < argc; i++, opt_off++) {
            data->argv[i] = argv[opt_off];
        }
        data->argv[i] = NULL;

        status = setup_env(data, (inherit_env) ? envp : NULL);
        if (status != 0) {
            fprintf(stderr, "Error: could not initialize target environment\n");
            free(data->argv);
            data->argv = NULL;
            data->argc = 0;
            return -1;
        }
    }

    return 0;
}




static int
setup_target_pipes(struct program_data * data)
{
    int status;

    /* create pipes */
    status = pipe(data->target_infd);
    if (status != 0) {
        fprintf(stderr, "Could not create pipe: %s\n", strerror(errno));
        goto out_in;
    }

    status = pipe(data->target_outfd);
    if (status != 0) {
        fprintf(stderr, "Could not create pipe: %s\n", strerror(errno));
        goto out_out;
    }

    status = pipe(data->target_errfd);
    if (status != 0) {
        fprintf(stderr, "Could not create pipe: %s\n", strerror(errno));
        goto out_err;
    }

out_err:
    if (status != 0) {
        close(data->target_outfd[0]);
        close(data->target_outfd[1]);
    }

out_out:
    if (status != 0) {
        close(data->target_infd[0]);
        close(data->target_infd[1]);
    }

out_in:
    return status; 
}

static int
target(struct program_data * data)
{
    int status;

    /* allow self to be ptraced */
    status = ptrace(PTRACE_TRACEME, 0, NULL, NULL);
    if (status != 0) {
        fprintf(stderr, "Failed to mark self as PTRACE-able: %s\n",
            strerror(errno));
        exit(-1);
    }

    /* Running speed trials, still standing in place... */
    return execve(data->exe, data->argv, data->envp);
}


/*
 * waitpid() will return once the tracee invokes exec
 */
static int
attach_to_pid_at_entry_point(struct program_data * data)
{
    int t_status, status;

    waitpid(data->pid, &t_status, 0);
    if (WIFEXITED(t_status)) {
        fprintf(stderr, "Target process exited with status %d\n", 
                WEXITSTATUS(t_status));
        return -1;
    }

    if (!WIFSTOPPED(t_status)) {
        fprintf(stderr, "Target did not stop on exec?\n");
        return -1;
    }

    printf("Target (%s) stopped at entry point\n", 
        data->exe
    );

    /* Latch onto the stopped process */
    status = pg_sched_track_pid(data);
    if (status){
        fprintf(stderr, "Error: Could not track target pid\n");
        return status;
    }
    
    /* Tell the target to resume */
    errno = 0;
    ptrace(PTRACE_CONT, data->pid, NULL, NULL);
    if (errno){
        fprintf(stderr, "Error: Ptrace continue failed\n");
        return -1;
    }

    return 0;
}

static int
setup_target(struct program_data * data)
{
    int status;

    /* create the pipes */
    status = setup_target_pipes(data);
    if (status != 0)
        return status;

    /* fork/exec the target */
    switch (data->pid = fork()) {
        case -1:
            fprintf(stderr, "Failed to fork shadow process: %s\n", 
                strerror(errno));
            return -1;

        case 0:
            close(data->target_infd[1]);
            close(data->target_outfd[0]);
            close(data->target_errfd[0]);

            /* dup2() to re-direct stdin/stdout/stderr to invrun */
            status = dup2(data->target_infd[0], STDIN_FILENO);
            if (status < 0) {
                fprintf(stderr, "Failed to dup2() STDIN_FILENO: %s\n", 
                    strerror(errno));
                return status;
            }

            status = dup2(data->target_outfd[1], STDOUT_FILENO);
            if (status < 0) {
                fprintf(stderr, "Failed to dup2() STDOUT_FILENO: %s\n", 
                    strerror(errno));
                return status;
            }

            status = dup2(data->target_errfd[1], STDERR_FILENO);
            if (status < 0) {
                fprintf(stderr, "Failed to dup2() STDERR_FILENO: %s\n", 
                    strerror(errno));
                return status;
            }

            close(data->target_infd[0]);
            close(data->target_outfd[1]);
            close(data->target_errfd[1]);

            /* run target application */
            exit(target(data));
            assert(1 == 0);

        default:
            close(data->target_infd[0]);
            close(data->target_outfd[1]);
            close(data->target_errfd[1]);
            break;
    }

    /* ptrace the child, installing a breakpoint at its entry point before
     * transfering control to the shadow
     */
    status = attach_to_pid_at_entry_point(data);
    if (status != 0) {
        fprintf(stderr, "Failed to run target to its entry point\n");
        return 0;
    }

    return 0;
}

int main(int     argc,
         char ** argv,
         char ** envp)
{
    int status;
    struct program_data * data;

    data = malloc(sizeof(struct program_data));
    if (data == NULL) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }

    memset(data, 0, sizeof(struct program_data));
    data->pid = PG_SCHED_NULL_PID;

    /* parse cmd line to generate program data */
    status = parse_cmd_line(argc, argv, envp, data);
    if (status != 0)
        goto out;

    /* start the target and intercept it at its entry point */
    status = setup_target(data);
    if (status != 0) {
        fprintf(stderr, "Could not setup target process\n");
        goto out;
    }

        /* do some other miscellaneous post-processing */
    {
        /* create hashtable of select'able fds */
        fd_to_handler_table = invirt_create_htable(0, fd_hash_fn, fd_eq_fn);
        if (fd_to_handler_table == NULL) {
            fprintf(stderr, "Could not create hashtable\n");
            goto out;
        }

        /* catch SICHLD */
        status = setup_sigchld_handler();
        if (status != 0) {
            fprintf(stderr, "Could not setup SIGCHLD handler\n");
            goto out;
        }

        /* catch SIGINT */
        status = setup_sigint_handler();
        if (status != 0) {
            fprintf(stderr, "Could not setup SIGINT handler\n");
            goto out;
        }

        /* track various fds that signal events */
        status = monitor_fds(data);
        if (status != 0) {
            fprintf(stderr, "Could not monitor file descriptors\n");
            goto out;
        }
    }

        /* core event processing loop */
    terminate = false;
    while (!terminate) {
        struct fd_handler * handler;
        int i, active_fds;
        fd_set rdset;

        rdset = fdset;
        active_fds = select(max_fd + 1, &rdset, NULL, NULL, NULL);
        if (active_fds < 0) {
            if (errno == EINTR)
                continue;

            fprintf(stderr, "Failed to select: %s\n", strerror(errno));
            status = -1;
            break;
        }

        for (i = 0; i <= max_fd && active_fds > 0; i++) {
            if (FD_ISSET(i, &rdset)) {
                --active_fds;

                handler = handler_of_fd(i);
                assert(handler != NULL);
                assert(handler->fd == i);

                status = handler->fn(i, handler->priv_data);
                if (status != 0)
                    fprintf(stderr, "Error in fd handler (fd=%d)\n", i);

                /* if we handled a sigchld, don't keep processing */
                if (i == sigchld_pipe[0]) {
                    terminate = true;
                    break;
                }
            }
        }
    }
    
 out:
    /* teardown target/shadow if either is still running */
    if (data->pid != PG_SCHED_NULL_PID)
        kill_and_teardown_target(data);

    
    
    return status;
}
