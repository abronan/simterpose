#include "syscall_process.h"
#include "syscalls_io.h"
#include "insert_trace.h"
#include "sockets.h"
#include "run_trace.h"
#include "data_utils.h"
#include "task.h"
#include "xbt.h"

//TODO test the possibility to remove incomplete checking
int process_send_call(int pid, int sockfd, int ret)
{
  printf("Entering process_send_call\n");
  if (socket_registered(pid,sockfd) != -1) {
    if (socket_incomplete(pid,sockfd))
    {
      update_socket(pid,sockfd);
    }
    if (!socket_netlink(pid,sockfd))
    {
      int result;
      calculate_computation_time(pid);
      struct infos_socket *is = get_infos_socket(pid,sockfd);
      struct infos_socket *s = getSocketInfoFromContext(is->ip_local, is->port_local, is->ip_remote, is->port_remote);
      if(s->communication_receive == -1)
      {
	launch_process_idling(s->proc->pid);
	socket_communication_receive(s);
      }
      socket_communication_receive(s);
      if(s!=NULL)
	result = handle_new_send(s,  ret);
      else
	THROW_IMPOSSIBLE;
      insert_trace_comm(pid,sockfd,"send",ret);
      create_send_communication_task(pid, s, ret);
	
      //if result is not null, we have give a task to another processus so one more are assigned
      if(result)
      {
	//--(global_data->not_assigned);
	return 1;
      }
    }
  }
  return 1;
}

int process_recv_call(int pid, int sockfd, int ret)
{
  printf("Entering process_recv_call %d\n", global_data->not_assigned);
  if (socket_registered(pid,sockfd) != -1) {
    if (socket_incomplete(pid,sockfd)) 
      update_socket(pid,sockfd);
    if (!socket_netlink(pid,sockfd))
    {
      calculate_computation_time(pid);
      //if handle_new_receive return 1, we have assigned a new task so one processus are assigned
      if(handle_new_receive(pid, sockfd, ret))
      {
// 	if(!process_descriptor_get_idle(pid))
	return 1;
      }
    }
  }
  return 0;
}

int process_fork_call(int pid)
{
  printf("New fork\n");
  unsigned long new_pid;
  if (ptrace(PTRACE_GETEVENTMSG, pid, 0, &new_pid)==-1) {
    perror("ptrace geteventmsg");
    exit(1);
  }
  if(pid == global_data->launcherpid)
  {
    global_data->last_pid_create = new_pid;
    printf("Creation of pid %lud\n", new_pid);
    char buff[256];
    char* tmp= buff;
    int got;
    while ((got = read(global_data->launcher_com,tmp,1))>0) {
      if(*tmp=='\n')
      {
	*tmp='\0';
	break;
      }
      else
	++tmp;
    }
    if(got <0)
    {
      perror("read");
      exit(1);
    }
    char name[256];
    double *next = malloc(sizeof(double));
    sscanf(buff, "%s %lf", name, next);

    xbt_fifo_push(global_data->time_to_next, next);
    global_data->process_desc[new_pid] = process_descriptor_new(name, new_pid);
    
    #if defined(DEBUG)
    print_trace_header(global_data->process_desc[new_pid]->trace);
    #endif
    printf("New application launch\n");
    insert_init_trace(new_pid);


    printf("new pid with (v)fork %lu by processus %d\n",new_pid, pid);
    if(pid != global_data->launcherpid)
      insert_trace_fork_exit(pid, "(v)fork", (int)new_pid);
    ++global_data->child_amount;
    return 1;
  }
  else//This is an application which fork
  {
    global_data->process_desc[new_pid] = process_descriptor_fork(nex_pid, pid);
    insert_trace_fork_exit(pid, "fork", (int)new_pid);
    ++global_data->child_amount;
    return 1;
  }
}