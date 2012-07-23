#include "syscall_process.h"
#include "syscall_data.h"
#include "insert_trace.h"
#include "sockets.h"
#include "run_trace.h"
#include "data_utils.h"
#include "ptrace_utils.h"
#include "process_descriptor.h"
#include "args_trace.h"
#include "task.h"
#include "xbt.h"
#include "simdag/simdag.h"
#include "xbt/log.h"
#include "communication.h"
#include "print_syscall.h"
#include "syscall_list.h"

#include <linux/futex.h>

XBT_LOG_NEW_DEFAULT_SUBCATEGORY(SYSCALL_PROCESS, SIMTERPOSE, "Syscall process log");

int process_accept_in_call(pid_t pid, syscall_arg_u* sysarg);
int process_recv_in_call(int pid, int fd);


//TODO test the possibility to remove incomplete checking
//There is no need to return value because send always bring a task
int process_send_call(int pid, syscall_arg_u* sysarg)
{
  send_arg_t arg = &(sysarg->send);
  if (socket_registered(pid,arg->sockfd) != -1) {
    if (!socket_netlink(pid,arg->sockfd))
    {
//       printf("%d This is not a netlink socket\n", arg->sockfd);
      calculate_computation_time(pid);
      struct infos_socket *is = get_infos_socket(pid,arg->sockfd);
      struct infos_socket *s = comm_get_peer(is);
      if(2222 == is->port_local)
        fprintf(stderr, "[%d] tracker respond to %d (%d)\n", pid, s->proc->pid, arg->ret);

//       printf("Sending data(%d) to %d on socket %d\n", arg->ret, s->proc->pid, s->fd);
      int peer_stat = process_get_state(s->proc->pid);
      if(peer_stat == PROC_SELECT || peer_stat == PROC_POLL || peer_stat == PROC_RECV_IN)
        add_to_sched_list(s->proc->pid);
      
      handle_new_send(is,  sysarg);

      SD_task_t task = create_send_communication_task(pid, is, arg->ret);

      schedule_comm_task(is->proc->station, s->proc->station, task);
      is->proc->on_simulation = 1;
      return 1;
    }
    return 0;
  }
  else 
    THROW_IMPOSSIBLE;
}

int process_recv_call(int pid, syscall_arg_u* sysarg)
{
  recv_arg_t arg = &(sysarg->recv);
  if (socket_registered(pid,arg->sockfd) != -1) {
    if (!socket_netlink(pid,arg->sockfd))
    {
      calculate_computation_time(pid);
      
      //if handle_new_receive return 1, there is a task found
      if(handle_new_receive(pid, arg->sockfd, arg->ret))
        return PROCESS_TASK_FOUND;
      else
        return PROCESS_NO_TASK_FOUND;
    }
  }
  else
    THROW_IMPOSSIBLE;
  
  return 0;
}

int process_fork_call(int pid)
{
  THROW_UNIMPLEMENTED;
  return 1;
}


int process_select_call(pid_t pid)
{
//   printf("Entering select\n");
  process_descriptor *proc= process_get_descriptor(pid);
  select_arg_t arg = &(proc->sysarg.select);
  int i;
  
  fd_set fd_rd, fd_wr, fd_ex;
  
  fd_rd = arg->fd_read;
  fd_wr = arg->fd_write;
  fd_ex = arg->fd_except;
  
  int match = 0;
  
  for(i=0 ; i < arg->maxfd ; ++i)
  {
    struct infos_socket* is = process_get_fd(pid, i);
    //if i is NULL that means that i is not a socket
    if(is == NULL)
    {
      FD_CLR(i, &(fd_rd));
      FD_CLR(i, &(fd_wr));
      continue;
    }

    int sock_status = socket_get_state(is);
    if(FD_ISSET(i, &(fd_rd)))
    {
      if(sock_status & SOCKET_READ_OK || sock_status & SOCKET_CLOSED || sock_status & SOCKET_SHUT)
        ++match;
      else
        FD_CLR(i, &(fd_rd));
    }
    if(FD_ISSET(i, &(fd_wr)))
    {
      if(sock_status & SOCKET_WR_NBLK && !(sock_status & SOCKET_CLOSED) && !(sock_status & SOCKET_SHUT))
        ++match;
      else
        FD_CLR(i, &(fd_wr));
    }
    if(FD_ISSET(i, &(fd_ex)))
    {
      XBT_WARN("Mediation for exception states on socket are not support yet\n");
    }
  }
  if(match > 0)
  {
//     printf("match for select\n");
    arg->fd_read = fd_rd;
    arg->fd_write = fd_wr;
    arg->fd_except = fd_ex;
    sys_build_select(pid, match);
//     print_select_syscall(pid, &(proc->sysarg));
    return match;
  }
  
  if(proc->in_timeout == PROC_TIMEOUT_EXPIRE)
  {
//     printf("Timeout for select\n");
    
    FD_ZERO(&fd_rd);
    FD_ZERO(&fd_wr);
    FD_ZERO(&fd_ex);
    arg->ret=0;
    arg->fd_read = fd_rd;
    arg->fd_write = fd_wr;
    arg->fd_except = fd_ex;
    sys_build_select(pid, 0);
//     print_select_syscall(pid, &(proc->sysarg));
    proc->in_timeout = PROC_NO_TIMEOUT;
    return 1;
  }
  return 0;
}


int process_poll_call(pid_t pid)
{
  process_descriptor *proc = process_get_descriptor(pid);
  
//   printf("Entering poll %lf %p\n", SD_get_clock(), proc->timeout);
  poll_arg_t arg = (poll_arg_t)&(proc->sysarg.poll);
  
  int match=0;
  int i;
  
  for(i=0; i < arg->nbfd; ++i)
  {
    struct pollfd *temp = &(arg->fd_list[i]);
    
    struct infos_socket *is = get_infos_socket(pid, temp->fd);
    if(is == NULL)
      continue;
    else
    {
      int sock_status = socket_get_state(is);
      if(temp->events & POLLIN)
      {
        if(sock_status & SOCKET_READ_OK || sock_status & SOCKET_CLOSED)
        {
          temp->revents = temp->revents | POLLIN;
          ++match;
        }
        else
        {
          temp->revents = temp->revents & ~POLLIN;
        }
      }
      else if(temp->events & POLLOUT)
      {
        if(sock_status & SOCKET_WR_NBLK)
        {
          temp->revents = temp->revents | POLLOUT;
          ++match;
        }
        else
        {
          temp->revents = temp->revents & ~POLLOUT;
        }
      }
      else
        XBT_WARN("Mediation different than POLLIN are not handle for poll\n");
    }
  }
  if(match > 0)
  {
//     printf("Result for poll\n");
    sys_build_poll(pid, match);
//     print_poll_syscall(pid, &(proc->sysarg));
    free(proc->sysarg.poll.fd_list);
    return match;
  }
  if(proc->in_timeout == PROC_TIMEOUT_EXPIRE)
  {
//     printf("Time out on poll\n");
    sys_build_poll(pid, 0);
//     print_poll_syscall(pid, &(proc->sysarg));
    free(proc->sysarg.poll.fd_list);
    proc->in_timeout = PROC_NO_TIMEOUT;
    return 1;
  }
  return match;
}


int process_handle_active(pid_t pid)
{
//   printf("Handle active process\n");
  int status;
  
  int proc_state = process_get_state(pid);
  if(proc_state & PROC_SELECT)
  {
    //if the select match changment we have to run the child
    if(process_select_call(pid))
    {
      process_descriptor* proc = process_get_descriptor(pid);
      if(proc->timeout != NULL)
        remove_timeout(pid);
      process_set_state(pid, PROC_NO_STATE);
    }
    else
      return PROCESS_ON_MEDIATION;
  }
  else if(proc_state & PROC_POLL)
  {
    if(process_poll_call(pid))
    {
      process_descriptor* proc = process_get_descriptor(pid);
      if(proc->timeout != NULL)
        remove_timeout(pid);
      process_set_state(pid, PROC_NO_STATE);
    }
    else
      return PROCESS_ON_MEDIATION;
  }
  else if(proc_state & PROC_CONNECT)
  {
    return PROCESS_ON_MEDIATION;
  }
  else if(proc_state & PROC_CONNECT_DONE)
  {
    waitpid(pid, &status, 0);
    return process_handle(pid, status);
  }
  else if(proc_state & PROC_ACCEPT_IN)
  {
    process_descriptor* proc = process_get_descriptor(pid);
    pid_t conn_pid = process_accept_in_call(pid, &proc->sysarg);
    if(conn_pid)
      add_to_sched_list(conn_pid); //We have to add conn_pid to the schedule list
    else
      return PROCESS_ON_MEDIATION;
  }
  else if(proc_state & PROC_RECV_IN)
  {
    process_descriptor* proc = process_get_descriptor(pid);
    if(process_recv_in_call(pid, proc->sysarg.recv.sockfd))
      process_set_state(pid, PROC_NO_STATE);
    else
      return PROCESS_ON_MEDIATION;
  }
  
  ptrace_resume_process(pid);
  
  if(waitpid(pid, &status, 0) < 0)
  {
    fprintf(stderr, " [%d] waitpid %s %d\n", pid, strerror(errno), errno);
    exit(1);
  }
  
  return process_handle( pid, status);
}


int process_recv_in_call(int pid, int fd)
{
  process_descriptor *proc = process_get_descriptor(pid);
//   fprintf(stderr, "[%d]Try to see if socket %d recv something\n", pid, fd);
  if(proc->fd_list[fd]==NULL)
    return 0;
  
  if(!socket_network(pid, fd))
    return 1;

  int status = comm_get_socket_state(get_infos_socket(pid, fd));
//   printf("socket status %d %d\n", status, status & SOCKET_READ_OK || status & SOCKET_CLOSED);
  return (status & SOCKET_READ_OK || status & SOCKET_CLOSED || status & SOCKET_SHUT);
}


//Return 0 if nobody wait or the pid of the one who wait
int process_accept_in_call(pid_t pid, syscall_arg_u* sysarg)
{
  accept_arg_t acc = &(sysarg->accept);
  //We try to find here if there's a connection to accept
  if(comm_has_connect_waiting(get_infos_socket(pid, acc->sockfd)))
  {
    pid_t conn_pid = comm_accept_connect(get_infos_socket(pid, acc->sockfd));
    
    int conn_state = process_get_state(conn_pid);
    if(conn_state == PROC_CONNECT)
    {
      ptrace_resume_process(conn_pid);
      add_to_sched_list(conn_pid);
      process_set_state(conn_pid, PROC_CONNECT_DONE);
    }
    return conn_pid;
  }
  else
  {
//     printf("Communication wait\n");
    process_set_state(pid, PROC_ACCEPT_IN);
    process_descriptor* proc = process_get_descriptor(pid);
    proc->sysarg.accept = sysarg->accept;
    return 0;
  }
}

void process_accept_out_call(pid_t pid, syscall_arg_u* sysarg)
{
  accept_arg_t arg = &(sysarg->accept);
  
  if(arg->ret >= 0)
  {
    int domain = get_domain_socket(pid, arg->sockfd);
    int protocol=get_protocol_socket(pid, arg->sockfd);
    
    struct infos_socket* is = register_socket(pid, arg->ret, domain, protocol);
    update_socket(pid, arg->ret);
    comm_join_on_accept(is, pid, arg->sockfd);
  }
  process_set_state(pid, PROC_NO_STATE);
}

void process_shutdown_call(pid_t pid, syscall_arg_u* sysarg)
{
  shutdown_arg_t arg = &(sysarg->shutdown);
  struct infos_socket *is = get_infos_socket(pid, arg->fd);
  if(is == NULL)
    return;
  comm_shutdown(is);
}


int process_handle_idle(pid_t pid)
{
//   printf("Handle idling process %d\n", pid);
  int status;
  if(waitpid(pid, &status, WNOHANG))
    return process_handle( pid, status);
  else
    return PROCESS_IDLE_STATE;
}

int process_clone_call(pid_t pid, reg_s *arg)
{
  unsigned long tid = arg->ret;
  unsigned long flags = arg->arg1;
  
  //Now create new process in model
  process_clone(tid, pid, flags);
  
  //Now add it to the launching time table to be the next process to be launch
  set_next_launchment(tid);
  
  int status;
  
  //wait for clone
  waitpid(tid, &status, 0);
  ptrace_resume_process(tid);
  //place process to te first call after clone
  waitpid(tid, &status, 0);
  process_set_in_syscall(tid);
  
  return 0;
}

//This function check if the communication already have been set. Means that 
int process_connect_in_call(pid_t pid, syscall_arg_u *sysarg)
{
  connect_arg_t arg = &(sysarg->connect);
  int domain = get_domain_socket(pid, arg->sockfd);
  
  if(domain == 2)//PF_INET
  {
    struct sockaddr_in *sai = &(arg->sai);
    
    //We ask for a connection on the socket
    int acc_pid = comm_ask_connect(sai->sin_addr.s_addr, ntohs(sai->sin_port), pid, arg->sockfd);
    
    //if someone waiting for connection we add him to shedule list
    if(acc_pid)
    {
      int status = process_get_state(acc_pid);
      if(status == PROC_ACCEPT_IN || status == PROC_SELECT || status == PROC_POLL)
        add_to_sched_list(acc_pid);
    }
    else
      THROW_IMPOSSIBLE;
    
    //Now we try to see if the socket is blocking of not
    int flags = socket_get_flags(pid, arg->sockfd);
    if(flags & O_NONBLOCK)
      return 0;
    
    //now mark the process as waiting for conn
    process_set_state(pid, PROC_CONNECT);
    return 1;
  }
  else
    return 0;
}

void process_connect_out_call(pid_t pid, syscall_arg_u *sysarg)
{
  connect_arg_t conn = &(sysarg->connect);
  
  int domain = get_domain_socket(pid, conn->sockfd);
  if(domain ==2 && conn->ret >= 0)
  {
    update_socket(pid, conn->sockfd);
  }
  process_set_state(pid, PROC_NO_STATE);
}

int process_bind_call(pid_t pid, syscall_arg_u *arg)
{
  connect_arg_t conn = &(arg->connect);
  set_localaddr_port_socket(pid,conn->sockfd,inet_ntoa(conn->sai.sin_addr),ntohs(conn->sai.sin_port));
  return 0;
}

int process_socket_call(pid_t pid, syscall_arg_u *arg)
{
  socket_arg_t sock = &(arg->socket);
  if (sock->ret>0) 
    register_socket(pid,sock->ret,sock->domain,sock->protocol);
  return 0;
}


int process_listen_call(pid_t pid, syscall_arg_u* sysarg)
{
  listen_arg_t list = &(sysarg->listen);
  struct infos_socket* is = get_infos_socket(pid, list->sockfd);
  comm_t comm = comm_new(is);
  comm_set_listen(comm);
  
  return 0;
}

void process_fcntl_call(pid_t pid, syscall_arg_u* sysarg)
{
  fcntl_arg_t arg = &(sysarg->fcntl);
  
  switch(arg->cmd)
  {
    case F_SETFL:
      socket_set_flags(pid, arg->fd , arg->arg);
      return;
      break;
    
    default:
      return;
      break;
  }
}



int process_handle(pid_t pid, int stat)
{  
  int status = stat;
  reg_s arg;
  syscall_arg_u sysarg;
  while(1)
  {
//     printf("New trap\n");
    if (process_in_syscall(pid)==0) {
      process_set_in_syscall(pid);

      ptrace_get_register(pid, &arg);
      
//       printf("[%d] Syscall %s_in(%lu \n", pid, syscall_list[arg.reg_orig], arg.arg1);
      int state = -1;
      if(arg.reg_orig == SYS_read)
      {
        get_args_read(pid, &arg, &sysarg);
        print_read_syscall(pid, &sysarg);
        if (socket_registered(pid, arg.arg1) != -1) {
          if(!process_recv_in_call(pid, arg.arg1))
          {
            int flags = socket_get_flags(pid, arg.arg1);
            if(!(flags & O_NONBLOCK))
            {
              process_set_state(pid, PROC_RECV_IN);
              process_descriptor *proc = process_get_descriptor(pid);
              proc->sysarg.read = sysarg.read;
              state =  PROCESS_ON_MEDIATION;
            }
          }
        }
      }
      
      if(arg.reg_orig == SYS_poll)
      {
        get_args_poll(pid, &arg, &sysarg);
        print_poll_syscall(pid, &sysarg);
        process_descriptor* proc = process_get_descriptor(pid);
        if(sysarg.poll.timeout >=0)
          add_timeout(pid, sysarg.poll.timeout + SD_get_clock());
        else
          proc->in_timeout = 1;
        ptrace_neutralize_syscall(pid);
        ptrace_resume_process(pid);
        waitpid(pid, &status, 0);
        process_set_out_syscall(pid);
        process_set_state(pid, PROC_POLL);
        proc->sysarg.poll = sysarg.poll;
        state =  PROCESS_ON_MEDIATION;
      }
      
      if(arg.reg_orig == SYS_exit_group)
      {
        printf("[%d] exit_group(%ld) called \n",pid, arg.arg1);
        ptrace_detach_process(pid);
        return PROCESS_DEAD;
      }
      if(arg.reg_orig == SYS_exit)
      {
        printf("[%d] exit(%ld) called \n", pid, arg.arg1);
        ptrace_detach_process(pid);
        return PROCESS_DEAD;
      }
      
      if(arg.reg_orig == SYS_futex)
      {
        printf("[%d] futex_in %p %d\n", pid, (void*)arg.arg4, arg.arg2 == FUTEX_WAIT);
        //TODO add real gestion of timeout
        if(arg.arg2 == FUTEX_WAIT)
        {
          ptrace_resume_process(pid);
          return PROCESS_IDLE_STATE;
        }
      }
      
      if(arg.reg_orig == SYS_connect)
      {
//         fprintf(stderr, "New connection\n");
        printf("[%d] connect_in\n", pid);
        get_args_bind_connect(pid, 0, &arg, &sysarg);
        print_connect_syscall(pid, &sysarg);
        if(process_connect_in_call(pid, &sysarg))
          state = PROCESS_ON_MEDIATION;
      }
       
      if(arg.reg_orig == SYS_accept)
      {
        printf("[%d] accept_in\n", pid);
        get_args_accept(pid, &arg, &sysarg);
        print_accept_syscall(pid, &sysarg);
        pid_t conn_pid = process_accept_in_call(pid, &sysarg);
        if(!conn_pid)
          state =  PROCESS_ON_MEDIATION;
      }
      
      else if(arg.reg_orig == SYS_select)
      {
        get_args_select(pid,&arg, &sysarg);
        print_select_syscall(pid, &sysarg);
        process_descriptor* proc = process_get_descriptor(pid);
        if(sysarg.select.timeout >=0)
          add_timeout(pid, sysarg.select.timeout + SD_get_clock());
        else
          proc->in_timeout = 1;
        ptrace_neutralize_syscall(pid);
        ptrace_resume_process(pid);
        waitpid(pid, &status, 0);
        process_set_out_syscall(pid);
        process_set_state(pid, PROC_SELECT);
        proc->sysarg.select = sysarg.select;
        state =  PROCESS_ON_MEDIATION;
      }
      
      else if(arg.reg_orig == SYS_recvfrom)
      {
        printf("[%d] recvfrom_in\n",pid);
        get_args_recvfrom(pid, &arg, &sysarg);
        fprintf(stderr, "[%d] Seeing if %d receive something\n", pid, (int)arg.arg1);
        if(!process_recv_in_call(pid, sysarg.recvfrom.sockfd))
        {
          int flags = socket_get_flags(pid, arg.arg1);
          if(!(flags & O_NONBLOCK))
          {
            process_set_state(pid, PROC_RECV_IN);
            process_descriptor *proc = process_get_descriptor(pid);
            proc->sysarg.recvfrom = sysarg.recvfrom;
            fprintf(stderr, "[%d] save socket %d\n", pid, proc->sysarg.recv.sockfd);
            state = PROCESS_ON_MEDIATION;
          }
        }
      }
      
      else if(arg.reg_orig == SYS_recvmsg)
      {
        printf("[%d] recvmsg_in\n",pid);
        get_args_send_recvmsg(pid, &arg, &sysarg);
        if(!process_recv_in_call(pid, sysarg.recvmsg.sockfd))
        {
          int flags = socket_get_flags(pid, arg.arg1);
          if(!(flags & O_NONBLOCK))
          {
            process_set_state(pid, PROC_RECV_IN);
            process_descriptor *proc = process_get_descriptor(pid);
            proc->sysarg.recvmsg = sysarg.recvmsg;
            state = PROCESS_ON_MEDIATION;
          }
        }
      }
      //No verify if we have compuation task to simulate.
      if(calculate_computation_time(pid))
      {
        //if we have computation to simulate
        schedule_computation_task(pid);
        state = PROCESS_ON_COMPUTATION;
      }
      if(state >= 0)
        return state;
    }
    else
    {
      process_set_out_syscall(pid);
      ptrace_get_register(pid, &arg);
      switch (arg.reg_orig) {
        
        case SYS_write:
          get_args_write(pid, &arg, &sysarg);
          print_write_syscall(pid, &sysarg);
          if (socket_registered(pid, sysarg.write.fd) != -1) {
            if(process_send_call(pid, &sysarg))
              return PROCESS_TASK_FOUND;
          }
          break;

        case SYS_read:
          get_args_read(pid, &arg, &sysarg);
          print_read_syscall(pid, &sysarg);
          if (socket_registered(pid, sysarg.read.fd) != -1) {
            if(process_recv_call(pid, &sysarg) == PROCESS_TASK_FOUND)
              return PROCESS_TASK_FOUND;
          }
          break;

        case SYS_fork: 
          THROW_UNIMPLEMENTED;//Fork are not handle yet
          break;
          
        case SYS_poll:
          THROW_IMPOSSIBLE;
          break;
          
        case SYS_open:
        {
//           char *flags = malloc(9);
//           switch (arg.arg2) {
//             case 0: strcpy(flags,"O_RDONLY"); break;
//             case 1: strcpy(flags,"O_WRONLY"); break;
//             case 2: strcpy(flags,"O_RDWR"); break;
//           }
//           if (strlen(flags)>0)
//             printf("[%d] open(\"...\", %s) = %ld\n",pid, flags, arg.ret);
//           else
//             printf("[%d] open(\"...\", no_flags) = %ld\n",pid, arg.ret);
//           free(flags);
        }
        break;
        
        case SYS_clone:
          THROW_UNIMPLEMENTED;
          if(arg.ret < MAX_PID)
          {
            process_clone_call(pid, &arg);
            return PROCESS_IDLE_STATE;
          }
          else
            process_set_in_syscall(pid);
          break;
          
        case SYS_close: 
          printf("[%d] close(%ld) = %ld\n",pid, arg.arg1,arg.ret);
          socket_close(pid,(int)arg.arg1);
          break;
          
        case SYS_dup:
          printf("[%d] dup(%ld) = %ld\n",pid,arg.arg1,arg.ret);
//           THROW_UNIMPLEMENTED; //Dup are not handle yet
          break;
          
        case SYS_dup2:
          printf("[%d] dup2(%ld, %ld) = %ld\n", pid, arg.arg1, arg.arg2, arg.ret);
          THROW_UNIMPLEMENTED; //Dup are not handle yet
          break;
          
        case SYS_execve:
          printf("[%d] execve called\n", pid);
          THROW_UNIMPLEMENTED; //
          break;
              
              
          #if defined(__x86_64)  
        case SYS_fcntl:
          get_args_fcntl(pid, &arg, &sysarg);
          print_fcntl_syscall(pid, &sysarg);
          process_fcntl_call(pid, &sysarg);
          break;
          
          
        case SYS_select: 
          THROW_IMPOSSIBLE;
          break;
          
        case SYS_socket: 
          get_args_socket(pid, &arg, &sysarg);
          print_socket_syscall(pid, &sysarg);
          process_socket_call(pid, &sysarg);
          break;
          
        case SYS_bind:
          get_args_bind_connect(pid, 0, &arg, &sysarg);
          print_bind_syscall(pid, &sysarg);
          process_bind_call(pid, &sysarg);
          break;
          
        case SYS_connect:
          get_args_bind_connect(pid, 1, &arg, &sysarg);
          print_connect_syscall(pid, &sysarg);
          process_connect_out_call(pid, &sysarg);
          process_set_state(pid, PROC_NO_STATE);
          break;
          
        case SYS_accept:
          get_args_accept(pid, &arg, &sysarg);
          print_accept_syscall(pid, &sysarg);
          process_accept_out_call(pid, &sysarg);
          add_to_sched_list(pid);
          return PROCESS_NO_TASK_FOUND;
          break;
          
        case SYS_listen:
          get_args_listen(pid, &arg, &sysarg);
          print_listen_syscall(pid, &sysarg);
          process_listen_call(pid, &sysarg);
          break;
              
        case SYS_sendto:
          get_args_sendto(pid, &arg, &sysarg);
          print_sendto_syscall(pid, &sysarg);
          if(process_send_call(pid, &sysarg))
            return PROCESS_TASK_FOUND;
          break;
          
        case SYS_recvfrom:
//           printf("lalal\n");
          get_args_recvfrom(pid, &arg, &sysarg);
          print_recvfrom_syscall(pid, &sysarg);
          if(process_recv_call(pid, &sysarg) == PROCESS_TASK_FOUND)
            return PROCESS_TASK_FOUND;
          break;
          
        case SYS_sendmsg:
          get_args_send_recvmsg(pid, &arg, &sysarg);
          print_sendmsg_syscall(pid, &sysarg);
          if(process_send_call(pid, &sysarg))
            return PROCESS_TASK_FOUND;
          break;
          
        case SYS_recvmsg:
          get_args_send_recvmsg(pid, &arg, &sysarg);
          print_recvmsg_syscall(pid, &sysarg);
          if(process_recv_call(pid, &sysarg) == PROCESS_TASK_FOUND)
            return PROCESS_TASK_FOUND;
          break;
          
        case SYS_shutdown:
          get_args_shutdown(pid, &arg, &sysarg);
          print_shutdown_syscall(pid, &sysarg);
          process_shutdown_call(pid, &sysarg);
          break;
              
        case SYS_getsockopt:
          get_args_get_setsockopt(pid, 1, &arg, &sysarg);
          print_getsockopt_syscall(pid, &sysarg);
          break;
          
        case SYS_setsockopt:
          get_args_get_setsockopt(pid, 0, &arg, &sysarg);
          print_setsockopt_syscall(pid, &sysarg);
          break;
                
          #else
          
        case SYS__newselect:
          THROW_IMPOSSIBLE;
          break;
          
        case SYS_socketcall:
          switch (arg.arg1) {
            
            case SYS_socket_32:
              printf("[%d] socket( ", pid);
              get_args_socket(pid, &arg);
              printf(" ) = %ld\n", arg.ret);
              break;
              
            case SYS_bind_32:
              printf("[%d] bind( ", pid);
              get_args_bind_connect(pid, 0, &arg, &sysarg);
              printf(" ) = %ld\n",arg.ret);
              break;
              
            case SYS_connect_32:
              printf("[%d] connect( ", pid);
              get_args_bind_connect(pid, 1, &arg, &sysarg);
              printf(" ) = %ld\n", arg.ret);
              if (ret<0)
                printf("%s\n",strerror(-arg.ret));
              ptrace_resume_process(pid);
              return PROCESS_IDLE_STATE;
              break;
                    
            case SYS_listen_32: 
              //TODO add listen mark
              printf("[%d] listen( ", pid); 
              get_args_listen(pid, &arg;
              printf(" ) = %ld\n", arg.ret);
              break;
              
            case SYS_accept_32:
              printf("[%d] accept( ", pid);
              get_args_accept(pid, &arg);
              printf(" ) = %ld\n", arg.ret);
              ptrace_resume_process(pid);
              return PROCESS_IDLE_STATE;
              break;
              
            case SYS_send_32:
              printf("[%d] send( ", pid);
              sockfd=get_args_send_recv(pid, 1, &arg);
              printf(" ) = %ld\n", arg.ret);
              process_send_call(pid, sockfd, arg.ret);
              return PROCESS_TASK_FOUND;
              break;
              
            case SYS_recv_32:
              printf("[%d] recv( ", pid);
              sockfd=get_args_send_recv(pid, 2, &arg);
              printf(" ) = %ld\n", arg.ret);
              if(process_recv_call(pid, sockfd, arg.ret) == PROCESS_TASK_FOUND)
                return PROCESS_TASK_FOUND;
              break;
              
            case SYS_sendto_32:
              printf("[%d] sendto(", pid);
              sockfd=get_args_sendto_recvfrom(pid, 1, &arg);
              printf(" ) = %ld\n", arg.ret); 
              process_send_call(pid, sockfd, arg.ret);
              return PROCESS_TASK_FOUND;
              break;
                    
            case SYS_recvfrom_32:
              printf("[%d] recvfrom(", pid);
              sockfd=get_args_sendto_recvfrom(pid, 2, &arg);
              printf(" ) = %ld\n", arg.ret);
              if(process_recv_call(pid, sockfd, arg.ret) == PROCESS_TASK_FOUND)
                return PROCESS_TASK_FOUND;
              break;
              
            case SYS_shutdown_32:
              printf("shutdown\n");
              break;
              
            case SYS_setsockopt_32:
              printf("[%d] setsockopt(", pid);
              get_args_get_setsockopt(pid, 2, &arg, &sysarg);
              printf("%d\n", (int)arg.ret);
              break;
              
            case SYS_getsockopt_32:
              printf("[%d] getsockopt(", pid);
              get_args_get_setsockopt(pid, 1, &arg, &sysarg);
              printf("%d\n", (int)arg.ret);
              break;
                    
            case SYS_sendmsg_32:
              printf("[%d] sendmsg(", pid);
              sockfd=get_args_send_recvmsg(pid, 1, &arg);
              printf(" ) = %ld\n", ret);
              process_send_call(pid, sockfd, arg.ret);
              return PROCESS_TASK_FOUND;
              break;
              
            case SYS_recvmsg_32:
              printf("[%d] recvmsg(", pid);
              sockfd=get_args_send_recvmsg(pid, 2, &arg);
              printf(" ) = %ld\n", ret);
              if(process_recv_call(pid, sockfd, arg.ret) == PROCESS_TASK_FOUND)
                return PROCESS_TASK_FOUND;
              break;
          }
        break;
                
        #endif
        
        default :
          printf("[%d] Unhandle syscall %s = %ld\n", pid, syscall_list[arg.reg_orig], arg.ret);
          break;
            
      }
    }
    ptrace_resume_process(pid);
    
    //waitpid sur le fils
    waitpid(pid, &status, 0);
  }
  
  THROW_IMPOSSIBLE; //There's no way to quit the loop
  
  return 0;
}
