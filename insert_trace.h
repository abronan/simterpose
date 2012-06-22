#ifndef __INSERT_TRACE_H_ 
#define __INSERT_TRACE_H_

#include <sys/types.h>

extern int nb_procs;

void insert_trace_comm(pid_t pid, int sockfd, char *syscall, int res);

void insert_trace_fork_exit(pid_t pid, char *syscall, int res);

void insert_init_trace(pid_t pid);

int calculate_computation_time(int pid);

#endif
