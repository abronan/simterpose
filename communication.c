#include "sockets.h"
#include "communication.h"
#include "xbt.h"

#include <sys/types.h>
#include <stdlib.h>

xbt_dynar_t comm_list;

void init_comm()
{
  comm_list = xbt_dynar_new(sizeof(comm_t), NULL); 
}

comm_t comm_new(struct infos_socket* socket, unsigned int remote_ip, int remote_port)
{
//   printf("Creating new communication with peer %d %d\n", remote_ip, remote_port);
  comm_t res = malloc(sizeof(comm_s));
  
  socket->comm=res;
  
  res->info[0].socket = socket;
  res->info[0].recv = recv_information_new();
  res->info[1].socket = NULL;
  res->info[1].recv = recv_information_new();
  
  res->remote_port = remote_port;
  res->remote_ip = remote_ip;
  
  xbt_dynar_push(comm_list, &res);
  
  return res;
}


comm_t comm_find_incomplete(unsigned int ip, int port, struct infos_socket* is)
{
  comm_t temp;
  unsigned int cpt = 0;
  
  xbt_dynar_foreach(comm_list, cpt, temp)
  {
    struct infos_socket* socket = temp->info[0].socket;
    if(socket->ip_local == ip &&  socket->port_local == port)
    {
      //Now verify if it's the good one we want
      if(is->ip_local == temp->remote_ip && is->port_local == temp->remote_port) 
        return temp;
    }
  }
//   printf("No communication found\n");
  return NULL;
}

void comm_join(comm_t comm, struct infos_socket* socket)
{
  socket->comm = comm;
  comm->info[1].socket = socket;
}

struct infos_socket* comm_get_peer(struct infos_socket* is)
{
  comm_t comm = is->comm;
  if( comm->info[0].socket == is)
    return comm->info[1].socket;
  else
    return comm->info[0].socket;
}

recv_information* comm_get_own_recv(struct infos_socket* is)
{
  comm_t comm = is->comm;
  if( comm->info[0].socket == is)
    return comm->info[0].recv;
  else
    return comm->info[1].recv;
}

