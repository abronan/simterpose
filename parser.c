#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "parser.h"
#include <xbt.h>
#include "surf/surfxml_parse.h"
#include "simgrid/platf.h"

launcher_procdesc *proc;
int proc_amount = 0;

void destruct_process_descriptor(launcher_procdesc * proc)
{
  free(proc->process_name);
  int i;
  for (i = 0; i < proc->argument_nbr - 1; ++i)
    free(proc->command_line_argument[i]);
  free(proc->command_line_argument);
  free(proc);
  //We don't free executable because it is already free when freeing command_line member
}

static int compare_time(const void *proc1, const void *proc2)
{
  int time1 = (*((launcher_procdesc **) proc1))->launching_time;
  int time2 = (*((launcher_procdesc **) proc2))->launching_time;

  if (time1 < time2)
    return -1;
  else if (time1 == time2)
    return 0;
  else
    return 1;
}

// FIXME: dupplicated code
static double parse_double(const char *string)
{
  char *endptr;
  double value = strtod(string, &endptr);
  xbt_assert(*endptr == '\0', "%s is not a double", string);
  return value;
}

// FIXME: Don't use flexml data but args
static void parse_processes(sg_platf_process_cbarg_t args)
{
  // init
  proc = malloc(sizeof(launcher_procdesc));
  proc->process_name = strdup(A_surfxml_process_host);
  proc->executable = strdup(A_surfxml_process_function);
  proc->launching_time = parse_double(A_surfxml_process_start___time);
  proc->argument_nbr = 1;
  proc->command_line_argument = malloc(sizeof(char *));
  proc->command_line_argument[0] = proc->executable;

  // argument
  ++(proc->argument_nbr);
  proc->command_line_argument = realloc(proc->command_line_argument, proc->argument_nbr * sizeof(char *));
  proc->command_line_argument[proc->argument_nbr - 1] = strdup(A_surfxml_argument_value);

  // finalize
//Starting with add NULL termination to command line
  ++(proc->argument_nbr);
  proc->command_line_argument = realloc(proc->command_line_argument, proc->argument_nbr * sizeof(char *));
  proc->command_line_argument[proc->argument_nbr - 1] = NULL;

  ++proc_amount;
  proc_list = realloc(proc_list, sizeof(launcher_procdesc *) * proc_amount);
  proc_list[proc_amount - 1] = proc;
}

void parse_deployment_file(const char *filename)
{
  xbt_ex_t e;

  surf_parse_reset_callbacks();
  sg_platf_process_add_cb(parse_processes);

  surf_parse_open(filename);
  TRY {
    int parse_status = surf_parse();
    surf_parse_close();
    xbt_assert(!parse_status, "Parse error at %s", filename);
  } CATCH(e) {
  }

  qsort(proc_list, proc_amount, sizeof(launcher_procdesc *), compare_time);
}

char **parser_get_commandline(int numero)
{
  return proc_list[numero]->command_line_argument;
}

char *parser_get_workstation(int numero)
{
  return proc_list[numero]->process_name;
}

int parser_get_amount()
{
  return proc_amount;
}

double parser_get_start_time(int numero)
{
  return proc_list[numero]->launching_time;
}


void parser_free_all()
{
  int i;
  for (i = 0; i < proc_amount; ++i) {
    destruct_process_descriptor(proc_list[i]);
  }
  free(proc_list);
}
