#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdbool.h>
#include <debug.h>
#include <stddef.h>

void syscall_init (void);

struct lock filesys_lock;

#endif /* userprog/syscall.h */
