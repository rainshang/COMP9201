/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>
#include <proc.h>

struct file
{
    struct vnode *f_vnode;
    struct stat *f_stat;
    struct lock *f_lock;
};

struct file_table
{
    struct file *opened_files[OPEN_MAX];
    struct lock *ft_lock;
};

int sys_open(const_userptr_t filename, int flags, mode_t mode, int *fd);
ssize_t sys_read(int fd, userptr_t buf, size_t buflen);
ssize_t sys_write(int fd, const_userptr_t buf, size_t nbytes);
off_t sys_lseek(int fd, off_t pos, int whence);
int sys_close(int fd);
int sys_dup2(int oldfd, int newfd);

int init_process_file_table(struct proc *proc);

#endif /* _FILE_H_ */
