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
    int f_flag;
    int f_ref_count;
    off_t f_offset;
    struct vnode *f_vnode;
    struct lock *f_lock;
};

struct file_table
{
    struct file *opened_files[OPEN_MAX];
    struct lock *ft_lock;
};

int sys_open(const_userptr_t filename, int flags, mode_t mode, int *ret);
int sys_read(int fd, userptr_t buf, size_t buflen, ssize_t *ret);
int sys_write(int fd, const_userptr_t buf, size_t nbytes, ssize_t *ret);
int sys_lseek(int fd, off_t pos, int whence, off_t *ret);
int sys_close(int fd, int *ret);
int sys_dup2(int oldfd, int newfd, int *ret);

int init_process_file_table(struct proc *proc);

#endif /* _FILE_H_ */
