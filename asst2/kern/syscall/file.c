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

int open(const userptr_t filename, int flags, mode_t mode, int *fd)
{
    (void) filename;
    (void) flags;
    (void) mode;
    (void) fd;
    kprintf("opening\n");
    return 1;
}

ssize_t read(int fd, userptr_t buf, size_t buflen)
{
    (void) fd;
    (void) buf;
    (void) buflen;
    kprintf("reading\n");
    return 1;
}

ssize_t write(int fd, const userptr_t buf, size_t nbytes)
{
    (void) fd;
    (void) buf;
    (void) nbytes;
    kprintf("writing\n");
    return 1;
}

off_t lseek(int fd, off_t pos, int whence)
{
    (void) fd;
    (void) pos;
    (void) whence;
    kprintf("lseeking\n");
    return 1;
}

int close(int fd)
{
    (void) fd;
    kprintf("closing\n");
    return 1;
}

int dup2(int oldfd, int newfd)
{
    (void) oldfd;
    (void) newfd;
    kprintf("dup2ing\n");
    return 1;
}
