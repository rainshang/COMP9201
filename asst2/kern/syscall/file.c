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

static int _update_offset_with_filesize(struct file *file)
{
    struct stat *f_stat = kmalloc(sizeof(struct stat));
    int err = VOP_STAT(file->f_vnode, f_stat);
    if (!err)
    {
        file->f_offset = f_stat->st_size;
    }
    kfree(f_stat);
    return err;
}

static int _sys_open(char *sys_filename, int flags, mode_t mode, int *ret)
{
    *ret = -1;

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

    file->f_flag = flags;
    file->f_vnode = vnode;
    file->f_ref_count = 1;
    if (flags & O_APPEND)
    {
        err = _update_offset_with_filesize(file);
        if (err)
        {
            vfs_close(vnode);
            kfree(file);
            return err;
        }
    }
    else
    {
        file->f_offset = 0;
    }

    file->f_lock = lock_create("f_lock");
    if (!file->f_lock)
    {
        vfs_close(vnode);
        kfree(file);
        return ENOMEM;
    }

    lock_acquire(curproc->f_table->ft_lock);
    for (unsigned i = 0; i < OPEN_MAX; ++i)
    {
        if (!curproc->f_table->opened_files[i])
        {
            curproc->f_table->opened_files[i] = file;
            *ret = i;
            break;
        }
    }
    lock_release(curproc->f_table->ft_lock);
    if (*ret == -1)
    {
        vfs_close(vnode);
        lock_release(file->f_lock);
        kfree(file);
        return EMFILE;
    }
    return 0;
}

int sys_open(const_userptr_t filename, int flags, mode_t mode, int *ret)
{
    *ret = -1;

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

    return _sys_open(sys_filename, flags, mode, ret);
}

int sys_read(int fd, userptr_t buf, size_t buflen, ssize_t *ret)
{
    *ret = -1;

    struct uio u_io;
    struct iovec u_iovec;
    struct file *file;

    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }

    lock_acquire(curproc->f_table->ft_lock);
    file = curproc->f_table->opened_files[fd];
    lock_release(curproc->f_table->ft_lock);

    if (file == NULL)
    {
        return EBADF;
    }
    if ((file->f_flag & O_ACCMODE) == O_WRONLY)
    {
        return EBADF;
    }
    lock_acquire(file->f_lock);
    u_iovec.iov_ubase = (userptr_t)buf;
    u_iovec.iov_len = buflen;
    u_io.uio_iov = &u_iovec;
    u_io.uio_iovcnt = 1;
    u_io.uio_offset = file->f_offset;
    u_io.uio_resid = buflen;
    u_io.uio_segflg = UIO_USERSPACE;
    u_io.uio_rw = UIO_READ;
    u_io.uio_space = curproc->p_addrspace;

    int err = VOP_READ(file->f_vnode, &u_io);
    if (err)
    {
        lock_release(file->f_lock);
        return err;
    }
    *ret = u_io.uio_offset - file->f_offset;
    file->f_offset = u_io.uio_offset;
    lock_release(file->f_lock);

    return 0;
}

int sys_write(int fd, const_userptr_t buf, size_t nbytes, ssize_t *ret)
{
    *ret = -1;

    struct uio u_io;
    struct iovec u_iovec;
    struct file *file;

    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }

    lock_acquire(curproc->f_table->ft_lock);
    file = curproc->f_table->opened_files[fd];
    lock_release(curproc->f_table->ft_lock);

    if (file == NULL)
    {
        return EBADF;
    }
    if ((file->f_flag & O_ACCMODE) == O_RDONLY)
    {
        return EBADF;
    }

    lock_acquire(file->f_lock);

    u_iovec.iov_ubase = (userptr_t)buf;
    u_iovec.iov_len = nbytes;
    u_io.uio_iov = &u_iovec;
    u_io.uio_iovcnt = 1;
    u_io.uio_offset = file->f_offset;
    u_io.uio_resid = nbytes;
    u_io.uio_segflg = UIO_USERSPACE;
    u_io.uio_rw = UIO_WRITE;
    u_io.uio_space = curproc->p_addrspace;

    int err = VOP_WRITE(file->f_vnode, &u_io);
    if (err)
    {
        lock_release(file->f_lock);
        return err;
    }
    *ret = u_io.uio_offset - file->f_offset;
    file->f_offset = u_io.uio_offset;
    lock_release(file->f_lock);

    return 0;
}

int sys_lseek(int fd, off_t pos, int whence, off_t *ret)
{
    *ret = -1;

    // out of range
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
    {
        return EINVAL;
    }

    int err = 0;
    lock_acquire(curproc->f_table->ft_lock);
    struct file *file = curproc->f_table->opened_files[fd];
    lock_release(curproc->f_table->ft_lock);
    if (file)
    {
        lock_acquire(file->f_lock);

        // is seekable
        if (VOP_ISSEEKABLE(file->f_vnode))
        {
            switch (whence)
            {
            case SEEK_SET:
                if (pos < 0)
                {
                    err = EINVAL;
                }
                else
                {
                    file->f_offset = pos;
                    *ret = file->f_offset;
                }
                break;
            case SEEK_CUR:
                if (file->f_offset + pos < 0)
                {
                    err = EINVAL;
                }
                else
                {
                    file->f_offset += pos;
                    *ret = file->f_offset;
                }
                break;
            case SEEK_END:
                err = _update_offset_with_filesize(file);
                if (!err)
                {
                    if (file->f_offset + pos < 0)
                    {
                        err = EINVAL;
                    }
                    else
                    {
                        file->f_offset += pos;
                        *ret = file->f_offset;
                    }
                }
                break;
            }
        }
        else
        {
            err = ESPIPE;
        }
        lock_release(file->f_lock);
    }
    else // not opened
    {
        err = EBADF;
    }
    return err;
}

int sys_close(int fd, int *ret)
{
    *ret = -1;

    // out of range
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }

    int err;
    lock_acquire(curproc->f_table->ft_lock);
    struct file *file = curproc->f_table->opened_files[fd];
    if (file)
    {
        lock_acquire(file->f_lock);
        if (file->f_ref_count == 1) // only itself
        {
            vfs_close(file->f_vnode); //don't know whether need to kfree this vnode. I reckon not
            lock_release(file->f_lock);
            lock_destroy(file->f_lock);
            kfree(file);
        }
        else // some dup exits
        {
            file->f_ref_count--;
            lock_release(file->f_lock);
        }
        curproc->f_table->opened_files[fd] = NULL;
        *ret = 0;
        err = 0;
    }
    else // not opened
    {
        err = EBADF;
    }
    lock_release(curproc->f_table->ft_lock);
    return err;
}

int sys_dup2(int oldfd, int newfd, int *ret)
{
    *ret = -1;

    // out of range
    if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX)
    {
        return EBADF;
    }

    int err = 0;
    lock_acquire(curproc->f_table->ft_lock);
    struct file *file = curproc->f_table->opened_files[oldfd];
    if (file)
    {
        if (oldfd == newfd)
        {
            *ret = newfd;
            err = 0;
        }
        else
        {
            struct file *new_file = curproc->f_table->opened_files[newfd];
            if (new_file)
            {
                err = sys_close(newfd, ret);
            }
            if (!err)
            {
                lock_acquire(file->f_lock);
                file->f_ref_count++;
                lock_release(file->f_lock);
                curproc->f_table->opened_files[newfd] = file;
                *ret = newfd;
            }
        }
    }
    else // oldret not opened
    {
        err = EBADF;
    }
    lock_release(curproc->f_table->ft_lock);
    return err;
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
