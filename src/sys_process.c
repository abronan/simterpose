/* sys_mem -- handlers of all memory-related syscalls                        */

/* Copyright (c) 2010-2014. The SimGrid Team. All rights reserved.           */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU GPLv2) which comes with this package. */

#include <linux/sched.h>   /* For clone flags */

#include "simterpose.h"
#include "syscall_process.h"
#include "print_syscall.h"
#include <xbt.h>

XBT_LOG_EXTERNAL_DEFAULT_CATEGORY(SYSCALL_PROCESS);

/** @brief retrieve the arguments of clone syscall */
static void get_args_clone(process_descriptor_t * proc, reg_s * reg, syscall_arg_u * sysarg)
{
	clone_arg_t arg = &(sysarg->clone);
	arg->ret = reg->ret;
	arg->clone_flags = reg->arg[0];
	arg->newsp = reg->arg[1];
	arg->parent_tid = (void *) reg->arg[2];
	arg->child_tid = (void *) reg->arg[3];
}

static int clone_number = 0;


/* At the exit of the syscall, we can be either in the parent or the clone.
 * If we are in the clone we actually create a new MSG process which inherits the file descriptors from the parent.
 */
void syscall_clone(reg_s * reg, syscall_arg_u * sysarg, process_descriptor_t * proc) {
	if (proc_entering(proc)) {
		proc_inside(proc);
		/* nothing to do at entrance */
	} else {

		int ret = reg->ret;
		proc_outside(proc);

		if (ret > 0) {
			// we are in the parent
			int pid_clone = ptrace_get_pid_clone(proc->pid);
			XBT_INFO("clone returning in parent, ret = %d, pid_clone = %d", ret, pid_clone);
		} else {
			// we are in the clone

			// The first time we come here, ret=-38, that is, -ENOSYS as if the syscall were not implemented.
			// According to the ptrace manpage, that means that we are actually entering the syscall, not exiting.
			// It must be the case that having 2 processes leaving the syscall fools our proc_inside() / proc_outside() mechanism
			// I guess that we should rely on PTRACE_GETSIGINFO instead of our mechanism here

			int pid_clone = ptrace_get_pid_clone(proc->pid);
			XBT_INFO("clone returning in child, ret = %d, pid_clone = %d", ret, pid_clone);
			proc_outside(proc);

			get_args_clone(proc, reg, sysarg);
			clone_arg_t arg = &(sysarg->clone);

			process_descriptor_t *clone = process_descriptor_new(proc->name, bprintf("cloned_%d",pid_clone), pid_clone);

			// the clone inherits the fd_list but subsequent actions on fd
			// do NOT affect the parent unless CLONE_FILES is set
			int i;
			for (i = 0; i < MAX_FD; ++i) {
				clone->fd_list[i] = malloc(sizeof(fd_descriptor_t));
				clone->fd_list[i]->proc = clone;
				clone->fd_list[i]->refcount = 0;
				if (proc->fd_list[i] != NULL) {
					clone->fd_list[i]->fd = proc->fd_list[i]->fd;
					clone->fd_list[i]->flags = proc->fd_list[i]->flags;
					clone->fd_list[i]->pipe = proc->fd_list[i]->pipe;
					clone->fd_list[i]->stream = proc->fd_list[i]->stream;
					clone->fd_list[i]->type = proc->fd_list[i]->type;
				}
				// deal with pipes
				if (clone->fd_list[i]->type == FD_PIPE) {
					pipe_t *pipe = clone->fd_list[i]->pipe;
					xbt_assert(pipe != NULL);

					// copy all the fds in the read end of the pipe
					unsigned int cpt_in;
					pipe_end_t end_in;
					xbt_dynar_t read_end = pipe->read_end;
					xbt_dynar_foreach(read_end, cpt_in, end_in) {
						// we have to make sure we don't add endlessly the fd from the clone
						//FIXME we still add pipes twice sometimes
						if (end_in->proc != clone && end_in->proc->pid != clone->pid) {
							xbt_assert(end_in != NULL);
							pipe_end_t clone_end = malloc(sizeof(pipe_end_s));
							clone_end->fd = end_in->fd;
							clone_end->proc = clone;
							xbt_dynar_push(read_end, &clone_end);
						}
					}

					// copy all the fds in the write end of the pipe
					xbt_dynar_t write_end = pipe->write_end;
					unsigned int cpt_out;
					pipe_end_t end_out;
					xbt_dynar_foreach(write_end, cpt_out, end_out) {
						// we have to make sure we don't add endlessly the fd from the clone
						if (end_out->proc != clone && end_out->proc->pid != clone->pid) {
							xbt_assert(end_out != NULL);
							pipe_end_t clone_end = malloc(sizeof(pipe_end_s));
							clone_end->fd = end_out->fd;
							clone_end->proc = clone;
							xbt_dynar_push(write_end, &clone_end);
						}
					}

				}
			}

			unsigned long flags = arg->clone_flags;

			//if (flags & CLONE_VM) // Nothing to do: we don't care if they share the memory
			//if (flags & CLONE_FS) // Nothing to do: we don't care if they share the file system
			if (flags & CLONE_FILES)
				XBT_WARN("CLONE_FILES unhandled");
			if (flags & CLONE_SIGHAND)
				XBT_WARN("CLONE_SIGHAND unhandled");
			if (flags & CLONE_PTRACE)
				XBT_WARN("CLONE_PTRACE ignored");
			if (flags & CLONE_VFORK)
				XBT_WARN("CLONE_VFORK unhandled");
			if (flags & CLONE_PARENT)
				XBT_WARN("CLONE_PARENT unhandled");
			if (flags & CLONE_THREAD)
				XBT_WARN("CLONE_THREAD unhandled");
			if (flags & CLONE_NEWNS)
				XBT_WARN("CLONE_NEWNS unhandled");
			if (flags & CLONE_SYSVSEM)
				XBT_WARN("CLONE_SYSVSEM unhandled");
			if (flags & CLONE_SETTLS)
				XBT_WARN("CLONE_SETTLS unhandled");
			if (flags & CLONE_UNTRACED)
				XBT_WARN("CLONE_UNTRACED unhandled");
			if (flags & CLONE_NEWUTS)
				XBT_WARN("CLONE_NEWUTS unhandled");
			if (flags & CLONE_NEWIPC)
				XBT_WARN("CLONE_NEWIPC unhandled");
			if (flags & CLONE_NEWUSER)
				XBT_WARN("CLONE_NEWUSER unhandled");
			if (flags & CLONE_NEWPID)
				XBT_WARN("CLONE_NEWPID unhandled");
			if (flags & CLONE_NEWNET)
				XBT_WARN("CLONE_NEWNET unhandled");
			if (flags & CLONE_IO)
				XBT_WARN("CLONE_IO unhandled");
			if (flags & CLONE_PARENT_SETTID)
				XBT_WARN("CLONE_PARENT_SETTID unhandled");
			if (flags & CLONE_CHILD_CLEARTID)
				XBT_WARN("CLONE_CHILD_CLEARTID unhandled");
			if (flags & CLONE_CHILD_SETTID)
				XBT_WARN("CLONE_CHILD_SETTID unhandled");

			arg->ret = clone->pid;
			ptrace_restore_syscall(proc->pid, SYS_clone, arg->ret);

			if (strace_option)
				print_clone_syscall(proc, sysarg);

			char name[256];
			sprintf(name, "clone #%d of %s", ++clone_number, MSG_process_get_name(MSG_process_self()));
			XBT_DEBUG("Creating %s, pid = %d", name, clone->pid);
			// FIXME: the current MSG process should not continue past that point:
			//        the clone is started, current can go back to its own considerations.
			// but maybe we want to start the clone in a different manner?
			MSG_process_create(name, main_loop, clone, MSG_host_self());
			proc_inside(proc);
		}
	}
}
