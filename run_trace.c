#include <unistd.h>

#include "args_trace.h"
#include "peek_data.h"
#include "sysdep.h"
#include "calc_times_proc.h"
#include "times_proc.h"
#include "sockets.h"
#include "insert_trace.h"
#include "syscalls_io.h"
#include "run_trace.h"
#include "benchmark.h"
#include "syscall_process.h"
#include "xbt/fifo.h"
#include "replay.h"

#define BUFFER_SIZE 512
#define ROUND_ARRAY_SIZE 1024
#define ROUND_ARRAY_MASK 1023


struct time_process all_procs[MAX_PROCS]; 
int nb_procs = 0;
process_descriptor process_desc[MAX_PID];
float flops_per_second;
float micro_s_per_flop;
xbt_fifo_t sig_info_fifo;


// pid_t round_pid_array[ROUND_ARRAY_SIZE];
// pid_t round_status_array[ROUND_ARRAY_SIZE];

// struct info_child{
//   pid_t pid;
//   int status;
// };

// void sig_child(int sig, siginfo_t* info, void* context)
// {
//   static int indice =0;
//   printf("New sigchild receive %d .... ", info->si_pid);
//   round_pid_array[indice]=info->si_pid;
//   waitpid(info->si_pid, &(round_status_array[indice]), 0);
//   //round_status_array[indice]=info->si_status;
//   ++indice;
//   //TODO do something more performant than comparison to 1024
//   if(indice > ROUND_ARRAY_SIZE)
//     indice = 0;
//   printf("Process done %d\n", indice-1);
//   
// }


void usage(char* progName) {
  printf("usage : %s platform_file.xml deployment_file.xml [-fp flops_power]\n", progName);
}

void print_trace_header(FILE* trace)
{
  fprintf(trace,"%8s %12s %8s %10s %10s %21s %21s\n","pidX", "syscall", "pidY", "return","diff_cpu","local_addr:port", "remote_addr:port");
}


int main(int argc, char *argv[]) { 
  
//   sig_info_fifo = xbt_fifo_new();
 
//   struct sigaction nvt, old;
//   memset(&nvt, 0, sizeof(nvt));
//   nvt.sa_sigaction = &sig_child;
//   nvt.sa_flags = SA_SIGINFO;
  
  
  char buff[256];
  pid_t launcherpid;
  int status;
  int stoppedpid;
  int child_amount=0;
  int manual_flop =0;
  
//   int indice_pid_array = 0;

  int sockfd; 

  int i;
//   for(i=0; i<ROUND_ARRAY_SIZE ; ++i)
//   {
//     round_pid_array[i]=-1; 
//   }
  for(i=0; i<MAX_PID; ++i)
  {
    process_desc[i].name=NULL;
    process_desc[i].trace=NULL;
  }
  
  //TODO mettre un vrai gestionnaire d'option et gérer les extensions des fichiers passés en paramètre
  if(argc>1)
  {
    for(i=1; i<argc; ++i)
    {
      if(!strcmp(argv[i], "-fp"))
      {
	if(argv[i+1] == NULL)
	{
	  usage(argv[0]); 
	}
	else
	{
	  char* endptr = argv[i+1]+strlen(argv[i+1])-1;
	  flops_per_second = strtod(argv[i+1], &endptr);
	  if(endptr == argv[i+1])
	    usage(argv[0]);
	  else
	  {
	    micro_s_per_flop  = 1000000/flops_per_second;
	    manual_flop = 1;
	  }
	}
      }
    }
  }
  
  if(!manual_flop)
    benchmark_matrix_product(&flops_per_second, &micro_s_per_flop);
  
  struct user_regs_struct regs;
  

  char ret_trace[SIZE_PARAM_TRACE];

  unsigned long reg_orig;
  unsigned long ret;
  unsigned long arg1;
  unsigned long arg2;
  unsigned long arg3;
  
  int comm_launcher[2];
  pipe(comm_launcher);

  init_syscalls_in();
  init_socket_gestion();
  launcherpid = fork();
  
  if (launcherpid == 0) {
    
    close(comm_launcher[0]);
    dup2(comm_launcher[1],3);
    close(comm_launcher[1]);
    if (ptrace(PTRACE_TRACEME, 0, NULL, NULL)==-1) {
      perror("ptrace traceme");
      exit(1);
    }
    if (execl("launcher", "launcher", argv[2], NULL)==-1) {
      perror("execl 1");
      exit(1);
    }
  
  } else {
    //We enter name of processus in array
    printf("launcher pid %d\n", launcherpid);
    process_desc[launcherpid].name=strdup("launcher");
    
    // We wait for the child to be blocked by ptrace in the first exec()
    wait(&status);
    
    printf("Starting following of %d\n", launcherpid);
    if (ptrace(PTRACE_SETOPTIONS,launcherpid,NULL,PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK | PTRACE_O_TRACEVFORKDONE)==-1) {
      perror("Error setoptions 1");
      exit(1);
    }

    //We set the signal handler here.
    //sigaction(SIGCHLD, &nvt, &old);
    ++child_amount;
    // Resume the child
    if (ptrace(PTRACE_SYSCALL, launcherpid, 0, 0)==-1) {
      perror("ptrace syscall 1");
      exit(1);
    }

    //insert_walltime_procs(launcherpid);
    insert_cputime_procs(launcherpid);
      nb_procs++;

   

    if (init_cputime()) {
      perror("Error init cputime");
      exit(1);
    }
    
	  
    while(child_amount) {
      // __WALL to follow all children
      //TODO parcour de tous les pid dans l'ordre en traitant l'appel système s'il y en a ou en passant à un autre sinon option WNOHANG
      
      stoppedpid = waitpid(-1, &status, __WALL);

      
      if (WIFEXITED(status)) {
        printf("[%d] Child is dead\n",stoppedpid);
	if(stoppedpid != launcherpid)	
	  finish_all_communication(stoppedpid);
	--child_amount;
	printf("Left %d child\n", child_amount);
	continue;
      }
      
      
      if (ptrace(PTRACE_GETREGS, stoppedpid,NULL, &regs)==-1) {
	perror("ptrace getregs");
	exit(1);
      }
      //TODO simplify handling of syscall sens
      
      int stat16=status >> 16;
//       printf("Handling signal %d\n", stat16);
      if (stat16== PTRACE_EVENT_FORK || stat16 == PTRACE_EVENT_VFORK) {
 	printf("Fork found\n");
	unsigned long new_pid;
	if (ptrace(PTRACE_GETEVENTMSG, stoppedpid, 0, &new_pid)==-1) {
	  perror("ptrace geteventmsg");
	  exit(1);
	}
	if(stoppedpid == launcherpid)
	{
	  char* tmp= buff;
	  int got;
	  while ((got = read(comm_launcher[0],tmp,1))>0) {
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
	  int time_before_next;
	  sscanf(buff, "%s %d", name, &time_before_next);
	  process_desc[new_pid].name = strdup(name);
	  strcat(name, ".txt");
	  process_desc[new_pid].trace = fopen(name, "w");
	  process_desc[new_pid].fd_list = malloc(sizeof(struct infos_socket*)*MAX_FD);
	  process_desc[new_pid].pid=new_pid;
	  int i=0;
	  for(i=0; i<MAX_FD ; ++i)
	    process_desc[new_pid].fd_list[i]=NULL;
#if defined(DEBUG)
	  print_trace_header(process_desc[new_pid].trace);
#endif
	  printf("New application launch\n");
	  insert_init_trace(new_pid);
	}
	
	
	//insert_walltime_procs(new_pid);
	insert_cputime_procs(new_pid);
	nb_procs++;
	printf("new pid with (v)fork %lu by processus %d\n",new_pid, stoppedpid);
	if(stoppedpid != launcherpid)
	  insert_trace_fork_exit(stoppedpid, "(v)fork", (int)new_pid);
	++child_amount;
      } else if (stat16== PTRACE_EVENT_CLONE) {
	unsigned long new_pid;
	if (ptrace(PTRACE_GETEVENTMSG, stoppedpid, 0, &new_pid)==-1) {
	  perror("ptrace geteventmsg");
	  exit(1);
	}
	//insert_walltime_procs(new_pid);
	insert_cputime_procs(new_pid);
	nb_procs++;
	printf("new pid with clone %lu\n",new_pid);
	insert_trace_fork_exit(stoppedpid, "clone", (int)new_pid);
	++child_amount;
      } 
      else if(stoppedpid != launcherpid){
	/*If this is the interrupt of the syscall and not the return, we print computation time */
	if (in_syscall(stoppedpid)==0) {
	  set_in_syscall(stoppedpid);
	  //calculate_computation_time(stoppedpid);
	}
	else
	{
	  /* ---- test archi for registers ---- */

	  #if defined(__x86_64) || defined(amd64)
	    reg_orig=regs.orig_rax;
	    ret=regs.rax;
	    arg1=regs.rdi;
	    arg2=regs.rsi;
	    arg3=regs.rdx;
	  #elif defined(i386)
	    reg_orig=regs.orig_eax;
	    ret=regs.eax;
	    arg1=regs.ebx;
	    arg2=regs.ecx;
	    arg3=regs.edx;
	  #endif

	  /*------------------*/

	  switch (reg_orig) {
	  case SYS_write:
	    printf("[%d] write(%ld, ... , %d) = %ld\n",stoppedpid,arg1,(int)arg3, ret);
	    if (socket_registered(stoppedpid,arg1) != -1) {
	      if ((int)ret>0 && socket_incomplete(stoppedpid,arg1)) 
		update_socket(stoppedpid,(int)arg1);
	      insert_trace_comm(stoppedpid,(int)arg1,"write",(int)ret);
	    }
	    break;

	  case SYS_read:
	    printf("[%d] read(%ld, ..., %ld) = %ld\n",stoppedpid, arg1,arg3, ret);
	    if (socket_registered(stoppedpid,arg1) != -1) {
	      if ((int)ret>0 && socket_incomplete(stoppedpid,arg1)) 
		update_socket(stoppedpid,(int)arg1);
	      insert_trace_comm(stoppedpid,(int)arg1,"read",(int)ret);
	    }
	    break;

	  case SYS_fork: 
	    printf("[%d] fork = %ld\n", stoppedpid,ret);
	    break;
	  
	  case SYS_poll:
	    get_args_poll(stoppedpid,(void *)arg1, (nfds_t)arg2);
	    printf(" = %d \n",(int)ret);
	    break;

	  case SYS_open:
	  {
	    char *flags = malloc(9);
	    switch (arg2) {
	    case 0: strcpy(flags,"O_RDONLY"); break;
	    case 1: strcpy(flags,"O_WRONLY"); break;
	    case 2: strcpy(flags,"O_RDWR"); break;
	    }
	    if (strlen(flags)>0)
	      printf("[%d] open(\"...\", %s) = %ld\n",stoppedpid,flags, ret);
	    else
	      printf("[%d] open(\"...\", no_flags) = %ld\n",stoppedpid, ret);
	  }
	  break;

	  case SYS_clone:
	    printf("[%d] clone() ?= %ld\n",stoppedpid,ret);
	    break;

	  case SYS_close: 
	    printf("[%d] close(%ld) = %ld\n",stoppedpid,arg1,ret);
	    close_sockfd(stoppedpid,(int)arg1);
	    break;

	  case SYS_dup:
	    printf("[%d] dup(%ld) = %ld\n",stoppedpid,arg1,ret);
	    break;

	  case SYS_dup2:
	    printf("[%d] dup2(%ld, %ld) = %ld\n",stoppedpid,arg1,arg2,ret);
	    break;

	  case SYS_exit_group:
	    printf("[%d] exit_group(%ld) called \n",stoppedpid,arg1);
	    insert_trace_fork_exit(stoppedpid,"exit_group",(int)arg1);
	    break;

	  case SYS_exit:
	    printf("[%d] exit(%ld) called \n",stoppedpid,arg1);
	    insert_trace_fork_exit(stoppedpid,"exit",(int)arg1);
	    break;

	  case SYS_execve:
	    printf("[%d] execve called\n",stoppedpid);
	    break;
	    
	    
  #if defined(__x86_64)  

	  case SYS_select: 
	    get_args_select(stoppedpid,&regs);
	    break;

	  case SYS_socket: 
	    printf("[%d] socket( ",stoppedpid);
	    get_args_socket(stoppedpid,(int)ret, &regs);
	    printf(" ) = %ld\n",ret);
	    break;

	  case SYS_bind:
	    printf("[%d] bind( ",stoppedpid);
	    get_args_bind_connect(stoppedpid,(int)ret,0,&regs);
	    printf(" ) = %ld\n",ret);
	    break;

	  case SYS_connect:
	    printf("[%d] connect( ",stoppedpid);
	    get_args_bind_connect(stoppedpid,(int)ret,1,&regs);
	    printf(" ) = %ld\n",ret);
	    break;

	  case SYS_accept:
	    printf("[%d] accept( ",stoppedpid);
	    get_args_accept(stoppedpid,(int)ret,&regs);
	    printf(" ) = %ld\n",ret);
	    break;

	  case SYS_listen:
	    printf("[%d] listen( ", stoppedpid); 
	    get_args_listen(stoppedpid,&regs);
	    printf(" ) = %ld\n", ret);
	    break;

	  case SYS_sendto:
	    printf("[%d] sendto( ",stoppedpid);
	    sockfd=get_args_sendto_recvfrom(stoppedpid,1,ret_trace,&regs);
	    printf(" ) = %ld\n",ret);
	    process_send_call(stoppedpid,sockfd,(int)ret);   
	    break;

	  case SYS_recvfrom:
	    printf("[%d] recvfrom( ",stoppedpid);
	    sockfd=get_args_sendto_recvfrom(stoppedpid,2,ret_trace,&regs);
	    printf(" ) = %ld\n",ret);
	    process_recv_call(stoppedpid,sockfd,(int)ret);
	    break;
	  
	  case SYS_sendmsg:
	    printf("[%d] sendmsg( ",stoppedpid);
	    sockfd=get_args_send_recvmsg(stoppedpid,1,ret_trace,&regs);
	    printf(" ) = %ld\n",ret); 
	    process_send_call(stoppedpid,sockfd,(int)ret);
	    break;

	  case SYS_recvmsg:
	    printf("[%d] recvmsg( ",stoppedpid);
	    sockfd=get_args_send_recvmsg(stoppedpid,2,ret_trace,&regs);
	    printf(" ) = %ld\n",ret);
	    process_recv_call(stoppedpid,sockfd,(int)ret);
	    break;

	  case SYS_shutdown:
	    printf("[%d] shutdown( %ld, ",stoppedpid, arg1);
	    char *how=malloc(10);;
	    switch(arg2){
	    case 0: strcpy(how,"SHUT_RD"); break;
	    case 1: strcpy(how,"SHUT_WR"); break;
	    case 2: strcpy(how,"SHUT_RDWR"); break;
	    }
	    printf("%s) = %ld\n",how,ret);;
	    break;

	  case SYS_getsockopt:
	    printf("[%d] getsockopt(",stoppedpid);
	    get_args_get_setsockopt(stoppedpid, 1, &regs);
	    printf("%d\n",(int)ret);
	    break;

	  case SYS_setsockopt:
	    printf("[%d] setsockopt(",stoppedpid);
	    get_args_get_setsockopt(stoppedpid, 1, &regs);
	    printf("%d\n",(int)ret);
	    break;

  #else

	  case SYS__newselect:
	    get_args_select(stoppedpid,&regs);
	    break;

	  case SYS_socketcall:
	    switch (arg1) {
	    
	    case 1:
	      printf("[%d] socket( ",stoppedpid);
	      get_args_socket(stoppedpid, (int)ret, (void *)arg2,NULL);
	      printf(" ) = %ld\n",ret);
	      break;

	    case 2:
	      printf("[%d] bind( ",stoppedpid);
	      get_args_bind_connect(stoppedpid,(int)ret,0,(void *)arg2);
	      printf(" ) = %ld\n",ret);
	      break;

	    case 3:
	      printf("[%d] connect( ",stoppedpid);
	      get_args_bind_connect(stoppedpid,(int)ret,1,(void *)arg2);
	      printf(" ) = %ld\n",ret);
	      if (ret<0)
		printf("%s\n",strerror(-ret));
	      break;

	    case 4: 
	      printf("[%d] listen( ", stoppedpid); 
	      get_args_listen(stoppedpid,(void *)arg2);
	      printf(" ) = %ld\n", ret);
	      break;

	    case 5:
	      printf("[%d] accept( ",stoppedpid);
	      get_args_accept(stoppedpid,(int)ret, (void *)arg2);
	      printf(" ) = %ld\n",ret);
	      break;

	    case 9:
	      printf("[%d] send( ",stoppedpid);
	      sockfd=get_args_send_recv(stoppedpid,1,ret_trace,(void *)arg2);
	      printf(" ) = %ld\n",ret);
	      process_send_call(stoppedpid,sockfd,(int)ret);
	      break;

	    case 10:
	      printf("[%d] recv( ",stoppedpid);
	      sockfd=get_args_send_recv(stoppedpid,2,ret_trace,(void *)arg2);
	      printf(" ) = %ld\n",ret);
	      process_recv_call(stoppedpid,sockfd,(int)ret);
	      break;

	    case 11:
	      printf("[%d] sendto(",stoppedpid);
	      sockfd=get_args_sendto_recvfrom(stoppedpid,1,ret_trace, (void *)arg2);
	      printf(" ) = %ld\n", ret); 
	      process_send_call(stoppedpid,sockfd,(int)ret);
	      break;

	    case 12:
	      printf("[%d] recvfrom(",stoppedpid);
	      sockfd=get_args_sendto_recvfrom(stoppedpid,2,ret_trace,(void *)arg2);
	      printf(" ) = %ld\n", ret);
	      process_recv_call(stoppedpid,sockfd,(int)ret);
	      break;

	    case 13:
	      printf("shutdown\n");
	      break;

	    case 14:
	      printf("[%d] setsockopt(",stoppedpid);
	      get_args_get_setsockopt(stoppedpid, 2, (void *)arg2);
	      printf("%d\n",(int)ret);
	      break;

	    case 15:
	      printf("[%d] getsockopt(",stoppedpid);
	      get_args_get_setsockopt(stoppedpid, 1, (void *)arg2);
	      printf("%d\n",(int)ret);
	      break;

	    case 16:
	      printf("[%d] sendmsg(",stoppedpid);
	      sockfd=get_args_send_recvmsg(stoppedpid,1,ret_trace,(void *)arg2);
	      printf(" ) = %ld\n", ret);
	      process_send_call(stoppedpid,sockfd,(int)ret);
	      break;

	    case 17:
	      printf("[%d] recvmsg(",stoppedpid);
	      sockfd=get_args_send_recvmsg(stoppedpid,2,ret_trace,(void *)arg2);
	      printf(" ) = %ld\n", ret);
	      process_recv_call(stoppedpid,sockfd,(int)ret);
	      break;

	  
	    }
	    break;

  #endif

	  default :
	      printf("[%d] Unknown syscall %ld ?= %ld\n", stoppedpid,reg_orig,ret);
	      break;

	  }
	  set_out_syscall(stoppedpid);
	
	}
      }
      else
      {
	if(in_syscall(stoppedpid))
	  set_out_syscall(stoppedpid);
	else
	  set_in_syscall(stoppedpid);
// 	printf("\t\t\t\t launcher syscall\n");
      }
      
      
//       printf("Resume child %d\n", stoppedpid);
      if (ptrace(PTRACE_SYSCALL, stoppedpid, NULL, NULL)==-1) {
	perror("ptrace syscall");
	exit(1);
      }

  
   
    }

  }
  return 0;

}
