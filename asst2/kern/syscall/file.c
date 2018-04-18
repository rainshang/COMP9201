#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>
#include <proc.h>

static int _sys_open(char *sys_filename, int flags, mode_t mode, int *fd)
{
    struct vnode *vnode = NULL;
    int err = vfs_open(sys_filename, flags, mode, &vnode);
    if (err)
    {
        return err;
    }

    struct file *file = kmalloc(sizeof(struct file));
    if (!file)
    {
        vfs_close(vnode);
        return ENOMEM;
    }

    file->f_vnode = vnode;
    file->f_lock = lock_create("f_lock");
    if (!file->f_lock)
    {
        vfs_close(vnode);
        kfree(file);
        return ENOMEM;
    }

    lock_acquire(curproc->f_table->ft_lock);
    *fd = -1;
    for (unsigned i = 0; i < OPEN_MAX; ++i)
    {
        if (!curproc->f_table->opened_files[i])
        {
            curproc->f_table->opened_files[i] = file;
            *fd = i;
            break;
        }
    }
    lock_release(curproc->f_table->ft_lock);
    if (*fd == -1)
    {
        vfs_close(vnode);
        lock_release(file->f_lock);
        kfree(file);
        return EMFILE;
    }
    return 0;
}

int sys_open(const_userptr_t filename, int flags, mode_t mode, int *fd)
{
    // copy data from user space to system space
    char *sys_filename = kmalloc(PATH_MAX);
    if (!sys_filename)
    {
        return ENOMEM;
    }
    size_t len_sys_filename = 0;
    int err = copyinstr(filename, sys_filename, PATH_MAX - 1, &len_sys_filename);
    if (err)
    {
        return err;
    }

    err = _sys_open(sys_filename, flags, mode, fd);
    if (err)
    {
        return err;
    }

    return 0;
}

ssize_t sys_read(int fd, userptr_t buf, size_t buflen)
{
    (void)fd;
    (void)buf;
    (void)buflen;
    kprintf("DDDDDebug-----sys_read------delete this when implemented\n");
    return 0;
}

ssize_t sys_write(int fd, const_userptr_t buf, size_t nbytes)
{
    (void)fd;
    (void)buf;
    (void)nbytes;
    kprintf("DDDDDebug-----sys_write------delete this when implemented\n");
    return 0;
}

off_t sys_lseek(int fd, off_t pos, int whence)
{
    (void)fd;
    (void)pos;
    (void)whence;
    kprintf("DDDDDebug-----sys_lseek------delete this when implemented\n");
    return 0;
}

int sys_close(int fd)
{
    // out of range
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }

    int err;
    lock_acquire(curproc->f_table->ft_lock);
    if (curproc->f_table->opened_files[fd])
    {
        lock_acquire(curproc->f_table->opened_files[fd]->f_lock);
        vfs_close(curproc->f_table->opened_files[fd]->f_vnode); //don't know whether need to kfree this vnode. I reckon not
        lock_release(curproc->f_table->opened_files[fd]->f_lock);
        lock_destroy(curproc->f_table->opened_files[fd]->f_lock);
        kfree(curproc->f_table->opened_files[fd]);
        curproc->f_table->opened_files[fd] = NULL;
        err = 0;
    }
    else // not open
    {
        err = ENXIO;
    }
    lock_release(curproc->f_table->ft_lock);

    return err;
}

int sys_dup2(int oldfd, int newfd)
{
    (void)oldfd;
    (void)newfd;
    kprintf("DDDDDebug-----sys_dup2------delete this when implemented\n");
    return 0;
}

int init_process_file_table(struct proc *proc)
{
    proc->f_table = kmalloc(sizeof(struct file_table));
    if (!proc->f_table)
    {
        return ENOMEM;
    }

    for (unsigned i = 0; i < OPEN_MAX; ++i)
    {
        proc->f_table->opened_files[i] = NULL;
    }
    proc->f_table->ft_lock = lock_create("ft_lock");
    if (!proc->f_table->ft_lock)
    {
        kfree(proc->f_table);
        return ENOMEM;
    }

    // create fds for standard input (stdin), standard output (stdout), and standard error (stderr)
    char console_device[5];
    for (unsigned i = 0; i < 3; ++i)
    {
        int fd = i;                     // fd should equal to i
        strcpy(console_device, "con:"); // vfs_open() could alter the value of console_device...
        int err = _sys_open(console_device, O_RDWR, 0, &fd);
        if (err)
        {
            return err;
        }
    }

    return 0;
}
