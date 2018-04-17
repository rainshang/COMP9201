/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>

struct file
{
    char f_mode;
    struct vnode *f_vnode;
    struct lock *f_lock;
    unsigned f_ref_count;
};

struct file_table
{
    struct file *opened_files[OPEN_MAX];
};

int open(const userptr_t filename, int flags, mode_t mode);
ssize_t read(int fd, userptr_t buf, size_t buflen);
ssize_t write(int fd, const userptr_t buf, size_t nbytes);
off_t lseek(int fd, off_t pos, int whence);
int close(int fd);
int dup2(int oldfd, int newfd);

#endif /* _FILE_H_ */
