#include "simterpose.h"
#include "data_utils.h"
#include "process_descriptor.h"
#include "xbt.h"
#include "sockets.h"
#include "simdag/simdag.h"      /* For SD_get_clock() */

struct simterpose_globals {
  xbt_dynar_t future_events_set;
  process_descriptor_t *process_desc[MAX_PID];
  xbt_dict_t list_station;
  xbt_dict_t list_ip;
  xbt_dict_t list_translate;
  time_t init_time;
  int child_amount;
  float msec_per_flop;
};
typedef struct simterpose_globals simterpose_data_t;
simterpose_data_t *global_data;

void simterpose_globals_init(float msec_per_flop)
{
  global_data = malloc(sizeof(simterpose_data_t));
  global_data->msec_per_flop = msec_per_flop;

  global_data->child_amount = 0;
  global_data->future_events_set = xbt_dynar_new(sizeof(time_desc_t *), NULL);
  global_data->list_station = xbt_dict_new_homogeneous(&destroy_simterpose_station);
  global_data->list_ip = xbt_dict_new_homogeneous(&free);
  global_data->list_translate = xbt_dict_new_homogeneous(&free);
  global_data->init_time = time(NULL);

  int i;
  for (i = 0; i < MAX_PID; ++i) {
    global_data->process_desc[i] = NULL;
  }
}

void simterpose_globals_exit()
{
  xbt_dynar_free(&(global_data->future_events_set));
  xbt_dict_free(&(global_data->list_station));
  xbt_dict_free(&(global_data->list_ip));
  xbt_dict_free(&(global_data->list_translate));
  free(global_data);
}

void destroy_simterpose_station(void *data)
{
  simterpose_station_t *station = (simterpose_station_t *) data;
  xbt_dict_free(&(station->port));
  free(station);
}

double FES_peek_next_date()
{
  if (xbt_dynar_is_empty(global_data->future_events_set))
    return -1;

  time_desc_t **t = (time_desc_t **) xbt_dynar_get_ptr(global_data->future_events_set, 0);
//   printf("Next start_time %lf\n", (*t)->start_time);
  return (*t)->start_time;
}

pid_t FES_pop_next_pid()
{
  time_desc_t *t = NULL;
  xbt_dynar_shift(global_data->future_events_set, &t);
  int res = t->pid;

  process_descriptor_t *proc = process_get_descriptor(res);
  if (proc->in_timeout == PROC_IN_TIMEOUT)
    proc->in_timeout = PROC_TIMEOUT_EXPIRE;
  proc->timeout = NULL;

  free(t);
  return res;
}

void FES_schedule_at(pid_t pid, double start_time)
{
  time_desc_t *t = malloc(sizeof(time_desc_t));
  t->pid = pid;
  t->start_time = start_time;

  process_descriptor_t *proc = process_get_descriptor(pid);
  proc->timeout = t;

  xbt_dynar_push(global_data->future_events_set, &t);
}

void FES_schedule_now(pid_t pid)
{
  time_desc_t *t = malloc(sizeof(time_desc_t));
  t->pid = pid;
  t->start_time = SD_get_clock();

  process_descriptor_t *proc = process_get_descriptor(pid);
  proc->timeout = t;

  xbt_dynar_unshift(global_data->future_events_set, &t);
}

int FES_contains_events()
{
  return !xbt_dynar_is_empty(global_data->future_events_set);
}

void FES_push_timeout(pid_t pid, double start_time)
{

  if (start_time == SD_get_clock())
    start_time += 0.0001;
//   printf("Add new timeout of %lf for %d\n", start_time, pid);
  time_desc_t *t = malloc(sizeof(time_desc_t));
  t->pid = pid;
  t->start_time = start_time;

  process_descriptor_t *proc = process_get_descriptor(pid);
  proc->timeout = t;
  proc->in_timeout = PROC_IN_TIMEOUT;

  int i = 0;
  while (i < xbt_dynar_length(global_data->future_events_set)) {
    time_desc_t **t = xbt_dynar_get_ptr(global_data->future_events_set, i);
    if (start_time < (*t)->start_time)
      break;
    ++i;
  }
  xbt_dynar_insert_at(global_data->future_events_set, i, &t);
}

void FES_remove_timeout(pid_t pid)
{
  process_descriptor_t *proc = process_get_descriptor(pid);
  time_desc_t *t = proc->timeout;
  proc->timeout = NULL;
  proc->in_timeout = PROC_NO_TIMEOUT;

  xbt_ex_t e;
  TRY {
    int i = xbt_dynar_search(global_data->future_events_set, &t);
    xbt_dynar_remove_at(global_data->future_events_set, i, NULL);
  }
  CATCH(e) {
    printf("Timeout not found %d\n", xbt_dynar_is_empty(global_data->future_events_set));
  }
  free(t);
}

int is_port_in_use(SD_workstation_t station, int port)
{
  simterpose_station_t *temp =
      (simterpose_station_t *) xbt_dict_get(global_data->list_station, SD_workstation_get_name(station));
  char buff[6];
  sprintf(buff, "%d", port);
  return (xbt_dict_get_or_null(temp->port, buff) != NULL);
}

void register_port(SD_workstation_t station, int port)
{
  //try to see if port isn't already use.
  simterpose_station_t *station_desc =
      (simterpose_station_t *) xbt_dict_get(global_data->list_station, SD_workstation_get_name(station));
  char buff[6];
  sprintf(buff, "%d", port);
  port_desc_t *temp = NULL;


  if ((temp = (port_desc_t *) xbt_dict_get_or_null(station_desc->port, buff))) {
    ++(temp->amount_socket);
  } else {
    temp = malloc(sizeof(port_desc_t));
    temp->port_num = port;
    temp->option = 0;
    temp->amount_socket = 1;
    temp->bind_socket = NULL;

    xbt_dict_set(station_desc->port, buff, temp, NULL);
  }


}

int get_port_option(SD_workstation_t station, int port)
{
  simterpose_station_t *temp =
      (simterpose_station_t *) xbt_dict_get(global_data->list_station, SD_workstation_get_name(station));
  char buff[6];
  sprintf(buff, "%d", port);

  port_desc_t *desc = xbt_dict_get_or_null(temp->port, buff);
  if (!desc)
    return 0;
  else
    return desc->option;
}

void set_port_option(SD_workstation_t station, int port, int option)
{
  simterpose_station_t *temp =
      (simterpose_station_t *) xbt_dict_get(global_data->list_station, SD_workstation_get_name(station));
  char buff[6];
  sprintf(buff, "%d", port);

  port_desc_t *desc = xbt_dict_get_or_null(temp->port, buff);
  if (desc)
    desc->option = option;
}

void set_port_on_binding(SD_workstation_t station, int port, struct infos_socket *is, int device)
{
  simterpose_station_t *temp =
      (simterpose_station_t *) xbt_dict_get(global_data->list_station, SD_workstation_get_name(station));
  char buff[6];
  sprintf(buff, "%d", port);

  port_desc_t *desc = xbt_dict_get_or_null(temp->port, buff);
  if (!desc)
    return;
  desc->option = desc->option | PORT_BIND | device;
  desc->bind_socket = is;
}

struct infos_socket *get_binding_socket(unsigned int ip, int port, int nature)
{
  struct in_addr in = { ip };
  char *ip_dot = inet_ntoa(in);

  char *station_name = xbt_dict_get(global_data->list_ip, ip_dot);
  simterpose_station_t *temp = (simterpose_station_t *) xbt_dict_get(global_data->list_station, station_name);
  char buff[6];
  sprintf(buff, "%d", port);
  port_desc_t *desc = xbt_dict_get_or_null(temp->port, buff);


  if (desc == NULL || !(desc->option & PORT_BIND))
    return NULL;

  if (!(nature & desc->option))
    return NULL;

  return desc->bind_socket;
}

struct infos_socket *get_binding_socket_workstation(SD_workstation_t station, int port, int device)
{
  simterpose_station_t *temp =
      (simterpose_station_t *) xbt_dict_get(global_data->list_station, SD_workstation_get_name(station));
  char buff[6];
  sprintf(buff, "%d", port);
  port_desc_t *desc = xbt_dict_get_or_null(temp->port, buff);

  if (desc == NULL || !(desc->option & PORT_BIND))
    return NULL;

  if (!(device & desc->option))
    return NULL;

  return desc->bind_socket;
}

void set_real_port(SD_workstation_t station, int port, int real_port)
{

  simterpose_station_t *temp =
      (simterpose_station_t *) xbt_dict_get(global_data->list_station, SD_workstation_get_name(station));
  char buff[6];
  sprintf(buff, "%d", port);
  port_desc_t *desc = xbt_dict_get_or_null(temp->port, buff);

  if (desc == NULL)
    return;
//   printf("Set correspondance %d <-> %d (real) for %s\n",port, real_port, SD_workstation_get_name(station));
  desc->real_port = real_port;
}

unsigned int get_ip_of_station(SD_workstation_t station)
{
  simterpose_station_t *temp =
      (simterpose_station_t *) xbt_dict_get(global_data->list_station, SD_workstation_get_name(station));
  return temp->ip;
}

SD_workstation_t get_station_by_ip(unsigned int ip)
{
  struct in_addr in = { ip };
  char *name = xbt_dict_get_or_null(global_data->list_ip, inet_ntoa(in));
  if (!name)
    return NULL;

  return SD_workstation_get_by_name(name);
}

int get_random_port(SD_workstation_t station)
{
  simterpose_station_t *temp =
      (simterpose_station_t *) xbt_dict_get(global_data->list_station, SD_workstation_get_name(station));
  unsigned short port = 0;
  --port;

  while (1) {
    char buff[6];
    sprintf(buff, "%d", port);
    if (xbt_dict_get_or_null(temp->port, buff))
      --port;
    else
      break;
  }

  return port;
}

void unset_socket(pid_t pid, struct infos_socket *is)
{
  SD_workstation_t station = is->station;
  simterpose_station_t *temp =
      (simterpose_station_t *) xbt_dict_get(global_data->list_station, SD_workstation_get_name(station));

  char buff[6];
  sprintf(buff, "%d", is->port_local);

  port_desc_t *desc = xbt_dict_get_or_null(temp->port, buff);
  if (!desc) {
    fprintf(stderr, "No descriptor found for port %s\n", buff);
    return;
  }

  if (is == desc->bind_socket)
    desc->bind_socket = NULL;

  --desc->amount_socket;
  if (desc->amount_socket)
    return;

  //if this is the last socket to use te port, we have to remove it from dict
  xbt_dict_remove(temp->port, buff);
}


time_t get_simulated_timestamp()
{
  return global_data->init_time + SD_get_clock();
}

void add_new_translation(int real_port, int translated_port, unsigned int translated_ip)
{
//   printf("Add new translation %d->%d\n", real_port, translated_port);
  translate_desc_t *temp = malloc(sizeof(translate_desc_t));
  temp->port_num = translated_port;
  temp->ip = translated_ip;

  char buff[6];
  sprintf(buff, "%d", real_port);

  xbt_dict_set(global_data->list_translate, buff, temp, NULL);
}


translate_desc_t *get_translation(int real_port)
{
//   printf("Get translation for port %d\n", real_port);
  char buff[6];
  sprintf(buff, "%d", real_port);

  return xbt_dict_get_or_null(global_data->list_translate, buff);
}

int get_real_port(pid_t pid, unsigned int ip, int port)
{
//   printf("Searching for ral port of %s:%d\n", inet_ntoa(in), port);
  simterpose_station_t *temp = NULL;
  if (ip == inet_addr("127.0.0.1")) {
//     printf("We are on local network %d\n",port);
    process_descriptor_t *proc = process_get_descriptor(pid);
    temp = (simterpose_station_t *) xbt_dict_get(global_data->list_station, SD_workstation_get_name(proc->station));
  } else
    temp =
        (simterpose_station_t *) xbt_dict_get(global_data->list_station, SD_workstation_get_name(get_station_by_ip(ip)));
  char buff[6];
  sprintf(buff, "%d", port);
  port_desc_t *desc = xbt_dict_get_or_null(temp->port, buff);

  if (desc == NULL)
    return -1;

//   printf("Return %d\n", desc->real_port);
  return desc->real_port;
}

process_descriptor_t *process_get_descriptor(pid_t pid)
{
  return global_data->process_desc[pid];
}

void process_set_descriptor(pid_t pid, process_descriptor_t * proc)
{
  global_data->process_desc[pid] = proc;
}

double simterpose_get_msec_per_flop()
{
  return global_data->msec_per_flop;
}

xbt_dict_t simterpose_get_station_list()
{
  return global_data->list_station;
}

xbt_dict_t simterpose_get_ip_list()
{
  return global_data->list_ip;
}
