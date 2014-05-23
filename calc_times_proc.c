#include "calc_times_proc.h"
#include <xbt.h>

XBT_LOG_NEW_DEFAULT_SUBCATEGORY(CALC_TIMES_PROC, ST, "calc times proc log");


/*
 * Generic macros for dealing with netlink sockets. Might be duplicated
 * elsewhere. It is recommended that commercial grade applications use
 * libnl or libnetlink and use the interfaces provided by the library
 */
#define GENLMSG_DATA(glh)	((void *)(NLMSG_DATA(glh) + GENL_HDRLEN))
#define GENLMSG_PAYLOAD(glh)	(NLMSG_PAYLOAD(glh, 0) - GENL_HDRLEN)
#define NLA_DATA(na)		((void *)((char*)(na) + NLA_HDRLEN))
#define NLA_PAYLOAD(len)	(len - NLA_HDRLEN)

/* Maximum size of response requested or message sent */
#define MAX_MSG_SIZE	1024
/* Maximum number of cpus expected to be specified in a cpumask */
#define MAX_CPUS	32


struct msgtemplate {
  struct nlmsghdr n;
  struct genlmsghdr g;
  char buf[MAX_MSG_SIZE];
} msg;

pid_t pid;

/*
 * Create a raw netlink socket and bind
 */
int create_nl_socket(int protocol)
{
  int fd;
  struct sockaddr_nl local;

  fd = socket(AF_NETLINK, SOCK_RAW, protocol);
  if (fd < 0)
    return -1;

  memset(&local, 0, sizeof(local));
  local.nl_family = AF_NETLINK;

  if (bind(fd, (struct sockaddr *) &local, sizeof(local)) < 0)
    goto error;

  return fd;
error:
  close(fd);
  return -1;
}


int send_cmd(int sd, __u16 nlmsg_type, __u32 nlmsg_pid, __u8 genl_cmd, __u16 nla_type, void *nla_data, int nla_len)
{
  struct nlattr *na;
  struct sockaddr_nl nladdr;
  int r, buflen;
  char *buf;


  msg.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
  msg.n.nlmsg_type = nlmsg_type;
  msg.n.nlmsg_flags = NLM_F_REQUEST;
  msg.n.nlmsg_seq = 0;
  msg.n.nlmsg_pid = nlmsg_pid;
  msg.g.cmd = genl_cmd;
  msg.g.version = 0x1;
  na = (struct nlattr *) GENLMSG_DATA(&msg);
  na->nla_type = nla_type;
  na->nla_len = nla_len + 1 + NLA_HDRLEN;
  memcpy(NLA_DATA(na), nla_data, nla_len);
  msg.n.nlmsg_len += NLMSG_ALIGN(na->nla_len);

  buf = (char *) &msg;
  buflen = msg.n.nlmsg_len;
  memset(&nladdr, 0, sizeof(nladdr));
  nladdr.nl_family = AF_NETLINK;
  while ((r = sendto(sd, buf, buflen, 0, (struct sockaddr *) &nladdr, sizeof(nladdr))) < buflen) {
    if (r > 0) {
      buf += r;
      buflen -= r;
    } else if (errno != EAGAIN)
      return -1;
  }
  return 0;
}


/*
 * Probe the controller in genetlink to find the family id
 * for the TASKSTATS family
 */
int get_family_id(int sd)
{
  struct {
    struct nlmsghdr n;
    struct genlmsghdr g;
    char buf[256];
  } ans;

  int id = 0 /*, rc */ ;
  struct nlattr *na;
  int rep_len;
  char name[100];

  strcpy(name, TASKSTATS_GENL_NAME);
  /*rc = */ send_cmd(sd, GENL_ID_CTRL, getpid(), CTRL_CMD_GETFAMILY,
                     CTRL_ATTR_FAMILY_NAME, (void *) name, strlen(TASKSTATS_GENL_NAME) + 1);

  rep_len = recv(sd, &ans, sizeof(ans), 0);
  if (ans.n.nlmsg_type == NLMSG_ERROR || (rep_len < 0) || !NLMSG_OK((&ans.n), rep_len))
    return 0;

  na = (struct nlattr *) GENLMSG_DATA(&ans);
  na = (struct nlattr *) ((char *) na + NLA_ALIGN(na->nla_len));
  if (na->nla_type == CTRL_ATTR_FAMILY_ID) {
    id = *(__u16 *) NLA_DATA(na);
  }
  return id;
}

static int _nl_sd;
static __u16 _id;

/* initialization */
void cputimer_init()
{
  _nl_sd = create_nl_socket(NETLINK_GENERIC);
  xbt_assert(_nl_sd >=0, "error creating Netlink socket to retrieve the time");

  _id = get_family_id(_nl_sd);
  xbt_assert(_id, "Error getting family id, errno %d\n", errno);

  memset(&msg, 0, sizeof(struct msgtemplate));
}

/* cleaning up */
void cputimer_exit()
{
  close(_nl_sd);
}

void cputimer_get(int tid, long long int *times)
{
  struct msgtemplate msg;
  int rep_len;
  struct nlattr *na;
  struct taskstats *stats;

  int rc = send_cmd(_nl_sd, _id, pid, TASKSTATS_CMD_GET,  TASKSTATS_CMD_ATTR_PID, &tid, sizeof(__u32));
  xbt_assert(rc>=0, "Error while sending a command to the netlink socket: %s", strerror(errno));

  rep_len = recv(_nl_sd, &msg, sizeof(msg), 0);
  xbt_assert(rep_len>=0, "error while receiving the answer from netlink socket: %s\n", strerror(errno));

  xbt_assert( (msg.n.nlmsg_type != NLMSG_ERROR && NLMSG_OK((&msg.n), rep_len)) ,
    	"received a fatal error from the netlink socket: %s", strerror(errno));

  rep_len = GENLMSG_PAYLOAD(&msg.n);

  na = (struct nlattr *) GENLMSG_DATA(&msg);
  int len = 0;
  int aggr_len, len2;
  while (len < rep_len) {
    len += NLA_ALIGN(na->nla_len);
    switch (na->nla_type) {
    case TASKSTATS_TYPE_AGGR_PID:
      aggr_len = NLA_PAYLOAD(na->nla_len);
      len2 = 0;
      /* For nested attributes, na follows */
      na = (struct nlattr *) NLA_DATA(na);
      while (len2 < aggr_len) {
        switch (na->nla_type) {
        case TASKSTATS_TYPE_PID:
          break;
        case TASKSTATS_TYPE_STATS:
          /* here we collect info */
          stats = (struct taskstats *) NLA_DATA(na);
          //times[0] = (long long int)stats->ac_etime;
          times[1] = (long long int) stats->ac_utime;
          times[2] = (long long int) stats->ac_stime;
          break;
        default:
          XBT_ERROR("Unknown nested" " nla_type %d\n", na->nla_type);
          break;
        }
        len2 += NLA_ALIGN(na->nla_len);
        na = (struct nlattr *) ((char *) na + len2);
      }
      break;
    default:
      XBT_ERROR("Unknown nla_type %d\n", na->nla_type);
      break;
    }
    na = (struct nlattr *) (GENLMSG_DATA(&msg) + len);
  }
}
