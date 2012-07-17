#ifndef __ARGS_TRACE_H 
#define __ARGS_TRACE_H


#include "syscall_data.h"
#include "ptrace_utils.h"
#include "sockets.h"

extern int nb_procs;

void get_args_socket(pid_t child, reg_s *arg, syscall_arg_u* sysarg);

void get_args_bind_connect(pid_t child, int syscall, reg_s *reg, syscall_arg_u *arg);

void get_args_accept(pid_t child, reg_s *reg, syscall_arg_u *arg);

void get_args_listen(pid_t child, reg_s *reg, syscall_arg_u *sysarg);

void get_args_send_recv(pid_t child, int syscall, reg_s *reg, syscall_arg_u *arg);

void get_args_select(pid_t child, reg_s *r, syscall_arg_u *sysarg);

//There's no need to pass more argument because process_desc already contain argument
void sys_build_select(pid_t pid, int match);

void sys_build_poll(pid_t pid, int match);

void get_args_poll(pid_t child, reg_s* arg, syscall_arg_u* sysarg);

void get_args_get_setsockopt(pid_t child, int syscall, reg_s* reg, syscall_arg_u *sysarg);

void get_args_sendto_recvfrom(pid_t child, int syscall, reg_s* reg, syscall_arg_u* sysarg);

void get_args_send_recvmsg(pid_t child, reg_s* reg, syscall_arg_u* sysarg);

void get_args_fcntl(pid_t pid, reg_s* reg, syscall_arg_u* sysarg);

void get_args_read(pid_t pid, reg_s* reg, syscall_arg_u* sysarg);

void get_args_write(pid_t pid, reg_s* reg, syscall_arg_u* sysarg);

void get_args_shutdown(pid_t pid, reg_s* reg, syscall_arg_u* sysarg);

#endif

