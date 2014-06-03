#ifndef INCLUDED_SYSCALL_PROCESS
#define INCLUDED_SYSCALL_PROCESS

#include <sys/types.h>          //for pid_t
#include "ptrace_utils.h"
#include "syscall_data.h"
#include "process_descriptor.h"

enum { PROCESS_DEAD, PROCESS_GROUP_DEAD, PROCESS_IDLE_STATE, PROCESS_TASK_FOUND, PROCESS_NO_TASK_FOUND,
      PROCESS_ON_MEDIATION, PROCESS_ON_COMPUTATION, PROCESS_CONTINUE };
extern const char *state_names[8];

#define RECV_CLOSE              10

int process_handle(process_descriptor_t * proc, int status);

int process_handle_active(process_descriptor_t * proc);

int process_handle_idle(process_descriptor_t * proc);

int process_handle_mediate(process_descriptor_t * proc);

#endif
