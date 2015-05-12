/*
   Copyright (c) 2003 University of Chicago.  All rights reserved.
   The Web100 Network Diagnostic Tool (NDT) is distributed subject to
   the following license conditions:
   SOFTWARE LICENSE AGREEMENT
Software: Web100 Network Diagnostic Tool (NDT)

1. The "Software", below, refers to the Web100 Network Diagnostic Tool (NDT)
(in either source code, or binary form and accompanying documentation). Each
licensee is addressed as "you" or "Licensee."

2. The copyright holder shown above hereby grants Licensee a royalty-free
nonexclusive license, subject to the limitations stated herein and U.S.
Government license rights.

3. You may modify and make a copy or copies of the Software for use within your
organization, if you meet the following conditions:
a. Copies in source code must include the copyright notice and this Software
License Agreement.
b. Copies in binary form must include the copyright notice and this Software
License Agreement in the documentation and/or other materials provided with the
copy.

4. You may make a copy, or modify a copy or copies of the Software or any
portion of it, thus forming a work based on the Software, and distribute copies
outside your organization, if you meet all of the following conditions:
a. Copies in source code must include the copyright notice and this
Software License Agreement;
b. Copies in binary form must include the copyright notice and this
Software License Agreement in the documentation and/or other materials
provided with the copy;
c. Modified copies and works based on the Software must carry prominent
notices stating that you changed specified portions of the Software.

5. Portions of the Software resulted from work developed under a U.S. Government
contract and are subject to the following license: the Government is granted
for itself and others acting on its behalf a paid-up, nonexclusive, irrevocable
worldwide license in this computer software to reproduce, prepare derivative
works, and perform publicly and display publicly.

6. WARRANTY DISCLAIMER. THE SOFTWARE IS SUPPLIED "AS IS" WITHOUT WARRANTY
OF ANY KIND. THE COPYRIGHT HOLDER, THE UNITED STATES, THE UNITED STATES
DEPARTMENT OF ENERGY, AND THEIR EMPLOYEES: (1) DISCLAIM ANY WARRANTIES,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY IMPLIED WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE OR NON-INFRINGEMENT,
(2) DO NOT ASSUME ANY LEGAL LIABILITY OR RESPONSIBILITY FOR THE ACCURACY,
COMPLETENESS, OR USEFULNESS OF THE SOFTWARE, (3) DO NOT REPRESENT THAT USE
OF THE SOFTWARE WOULD NOT INFRINGE PRIVATELY OWNED RIGHTS, (4) DO NOT WARRANT
THAT THE SOFTWARE WILL FUNCTION UNINTERRUPTED, THAT IT IS ERROR-FREE OR THAT
ANY ERRORS WILL BE CORRECTED.

7. LIMITATION OF LIABILITY. IN NO EVENT WILL THE COPYRIGHT HOLDER, THE
UNITED STATES, THE UNITED STATES DEPARTMENT OF ENERGY, OR THEIR EMPLOYEES:
BE LIABLE FOR ANY INDIRECT, INCIDENTAL, CONSEQUENTIAL, SPECIAL OR PUNITIVE
DAMAGES OF ANY KIND OR NATURE, INCLUDING BUT NOT LIMITED TO LOSS OF PROFITS
OR LOSS OF DATA, FOR ANY REASON WHATSOEVER, WHETHER SUCH LIABILITY IS ASSERTED
ON THE BASIS OF CONTRACT, TORT (INCLUDING NEGLIGENCE OR STRICT LIABILITY), OR
OTHERWISE, EVEN IF ANY OF SAID PARTIES HAS BEEN WARNED OF THE POSSIBILITY OF
SUCH LOSS OR DAMAGES.
The Software was developed at least in part by the University of Chicago,
as Operator of Argonne National Laboratory (http://miranda.ctd.anl.gov:7123/).
*/

#include "../config.h"

#include <time.h>
#include <ctype.h>
#include <getopt.h>
#include <math.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#define SYSLOG_NAMES
#include <pthread.h>
#include <syslog.h>
#include <sys/times.h>

#include "web100srv.h"
#include "network.h"
#include "usage.h"
#include "utils.h"
#include "mrange.h"
#include "logging.h"
#include "testoptions.h"
#include "protocol.h"
#include "web100-admin.h"
#include "test_sfw.h"
#include "ndt_odbc.h"
#include "runningtest.h"
#include "strlutils.h"
#include "heuristics.h"
#include "tests_srv.h"
#include "jsonutils.h"
#include "websocket.h"

static char lgfn[FILENAME_SIZE];  // log file name
static char wvfn[FILENAME_SIZE];  // file name of web100-variables list
static char apfn[FILENAME_SIZE];  // admin file name
static char slfa[256];            // syslog facility
static char logd[256];            // log dir name
static char portbuf[10];          // port number user option store
static char devicebuf[100];       // device name buf (seems unused)
static char dbDSNbuf[256];        // DB datasource name
static char dbUIDbuf[256];        // DB UID
static char dbPWDbuf[256];        // DB Password

// list of global variables used throughout this program.
static int window = 64000;  // TCP buffer size
static int count_vars = 0;
int dumptrace = 0;
static int usesyslog = 0;
static int multiple = 0;
static int compress = 1;
static int max_clients = 50;
static int set_buff = 0;
static int admin_view = 0;
static int queue = 1;
static int record_reverse = 0;
static int testing;   // a test is currently being performed.
static int waiting;   // # of many tests pending
static int mclients;  // multiple client mode client count
static int refresh = 30;
static int old_mismatch = 0;  // use the old duplex mismatch detection heuristic
/* int sig1, sig2, sig17; */

static Options options;
static CwndPeaks peaks;
static int cputime = 0;
static char cputimelog[256];
static pthread_t workerThreadId;
static int cputimeworkerLoop = 1, zombie_check = 0;

static int useDB = 0;
static char *dbDSN = NULL;
static char *dbUID = NULL;
static char *dbPWD = NULL;

static char *VarFileName = NULL;
static char *AdminFileName = NULL;
static char *SysLogFacility = NULL;
static int syslogfacility = LOG_FACILITY;
static char *ConfigFileName = NULL;
static char buff[BUFFSIZE + 1];
static char rmt_host[256];
static char rmt_addr[256];
static char *device = NULL;
static char *port = PORT;
static TestOptions testopt;

static int conn_options = 0;
static struct ndtchild *head_ptr;
static int ndtpid;
static int testPort;
static char testName[256];

/* create semaphore to allow only 1 process to modify the wait queue */
#include <semaphore.h>
sem_t ndtq;

static struct option long_options[] = {{"adminview", 0, 0, 'a'},
                                       {"debug", 0, 0, 'd'},
                                       {"help", 0, 0, 'h'},
                                       {"multiple", 0, 0, 'm'},
                                       {"max_clients", 1, 0, 'x'},
                                       {"mrange", 1, 0, 301},
                                       {"old", 0, 0, 'o'},
                                       {"disable-queue", 0, 0, 'q'},
                                       {"record", 0, 0, 'r'},
                                       {"syslog", 0, 0, 's'},
                                       {"tcpdump", 0, 0, 't'},
                                       {"version", 0, 0, 'v'},
                                       {"gzip", 0, 0, 'z'},
                                       {"config", 1, 0, 'c'},
#ifdef EXPERIMENTAL_ENABLED
                                       {"avoidsndblockup", 0, 0, 306},
                                       {"snaplog", 0, 0, 307},
                                       {"snapdelay", 1, 0, 305},
                                       {"cwnddecrease", 0, 0, 308},
                                       {"cputime", 0, 0, 309},
                                       {"limit", 1, 0, 'y'},
#endif
                                       {"buffer", 1, 0, 'b'},
                                       {"file", 1, 0, 'f'},
                                       {"interface", 1, 0, 'i'},
                                       {"log", 1, 0, 'l'},
                                       {"protolog_dir", 1, 0, 'u'},
                                       {"enableprotolog", 0, 0, 'e'},
                                       {"port", 1, 0, 'p'},
                                       {"midport", 1, 0, 302},
                                       {"c2sport", 1, 0, 303},
                                       {"s2cport", 1, 0, 304},
                                       {"refresh", 1, 0, 'T'},
                                       {"adminfile", 1, 0, 'A'},
                                       {"log_dir", 1, 0, 'L'},
                                       {"logfacility", 1, 0, 'S'},
#if defined(HAVE_ODBC) && defined(DATABASE_ENABLED) && defined(HAVE_SQL_H)
                                       {"enableDBlogging", 0, 0, 310},
                                       {"dbDSN", 1, 0, 311},
                                       {"dbUID", 1, 0, 312},
                                       {"dbPWD", 1, 0, 313},
#endif
#ifdef AF_INET6
                                       {"ipv4", 0, 0, '4'},
                                       {"ipv6", 0, 0, '6'},
#endif
                                       {"tls", 0, 0, 314},
                                       {"private_key", 1, 0, 315},
                                       {"certificate", 1, 0, 316},
                                       {0, 0, 0, 0}};

/**
 * Process a SIGCHLD signal.
 * @param pid_t Process id to be processed
 * */
void child_sig(pid_t chld_pid) {
  int pid, status, retcode;
  struct ndtchild *child_proc1, *child_proc2;

  child_proc1 = head_ptr;
  log_println(
      2,
      "Processing SIGCHLD signal for active web100srv process [%d], sig17=%d",
      chld_pid, sig17);

  /* this routine cleans up after a child process has terminated.  There are 3
   * types of child processes.  The pkt-pair timing children are type 1 and the
   * spawned children to run the test are type 0.  For the pkt-pair children,
   * just acknowledge their signal to keep them from becoming defunct.  For the
   * type 0 children, use wait3() to figure out which child terminated and then
   * clean up the FIFO queue.
   *
   * The -1 type indicates a child has terminated due to a communications fault.
   * For some reason the child is stuck in the FIFO queue and it needs to be
   * removed.  In this case, the calling function will grab the head pointer's
   * PID value, call this function with a -1 to remove the entry from the FIFO,
   * issue a SIGTERM signal and call this function with the childs PID.  This
   * should prevent the code from entering into a loop.
   */
  if (chld_pid > 0) {
    while ((pid = wait4(chld_pid, &status, WNOHANG, (struct rusage *)0)) > -1) {
      log_println(3, "wait4() returned %d for PID=%d", status, pid);
      if (WIFEXITED(status) != 0) {
        log_println(3, "wexitstatus = '%d'", WEXITSTATUS(status));
        break;
      }
      if (WIFSIGNALED(status) == 1) {
        log_println(3, "wtermsig = %d", WTERMSIG(status));
      }
      if (WIFSTOPPED(status) == 1) {
        log_println(3, "wstopsig = %d", WSTOPSIG(status));
      }
      if (status != 0) {
        log_println(
            4, "child_sig() routine, wait4() non-zero status (%d) returned",
            status);
        return;
      }
    }
    return;
  }

  if (chld_pid == 0) {
    while ((pid = wait3(&status, WNOHANG, (struct rusage *)0)) > 0) {
      log_println(3, "wait3() returned status=%d for PID=%d", status, pid);
      if (WIFEXITED(status) != 0) {
        log_println(3, "wexitstatus = '%d'", WEXITSTATUS(status));
        break;
      }
      if (WIFSIGNALED(status) == 1) {
        log_println(3, "wtermsig = %d", WTERMSIG(status));
      }
      if (WIFSTOPPED(status) == 1) {
        log_println(3, "wstopsig = %d", WSTOPSIG(status));
      }
      if (status != 0) {
        log_println(
            4, "child_sig() routine, wait3() non-zero status (%d) returned",
            status);
        return;
      }
    }
    log_println(6,
                "child_sig() called pid=%d, wait returned child=%d - status=%d",
                chld_pid, pid, status);
    if (pid == 0) {
      log_println(6, "wait3() failed to return non-zero PID, ignore it");
      if (sig17 > 0) sig17--;
      return;
    }
  } else {
    // chld_pid must be -1, the error condition
    pid = head_ptr->pid;
    log_println(
        6, "Stuck child at head of queue, set pid=%d and remove it from queue",
        pid);
  }

  /* the pid variable now holds the PID of the child that exited.  Find out if
   * this is one of the queued children, and if so remove it from the queue and
   * relink everything.  At the end of this, update the sig17 counter.
   */

  if (head_ptr == NULL) return;
  log_println(5, "checking for pktpair timing children, skip them");
  child_proc1 = head_ptr;
  while (child_proc1 != NULL) {
    log_println(
        5, "\tLooking for %d, curent queue Child %d, host: %s [%s], next=0x%x",
        pid, child_proc1->pid, child_proc1->host, child_proc1->addr,
        (uintptr_t)child_proc1->next);
    if (child_proc1->pid == pid) {
      log_println(4, "Main test process %d terminated, remove from queue", pid);
      break;
    }
    child_proc1 = child_proc1->next;
  }
  if (child_proc1 == NULL) return;

  if (multiple == 1) {
    log_println(5, "mclient child '%d' (%d) has finished its test", mclients,
                pid);
    mclients--;
  }

  log_println(4, "Attempting to clean up child %d, head pid = %d", pid,
              head_ptr->pid);
  if (head_ptr->pid == pid) {
    if (get_debuglvl() > 5) {
      log_println(5, "Walkingqueue");
      child_proc1 = head_ptr;
      while (child_proc1 != NULL) {
        log_println(5, "\tChild %d, host: %s [%s], next=0x%x", child_proc1->pid,
                    child_proc1->host, child_proc1->addr,
                    (uintptr_t)child_proc1->next);
        if (child_proc1->next == NULL) break;
        child_proc1 = child_proc1->next;
      }
    }

    while ((retcode = sem_wait(&ndtq)) == -1 && errno == EINTR) {
      log_println(6, "Waiting for ndtq semaphore to free - 1");
      continue;
    }
    log_println(5,
                "Child process %d causing head pointer modification, "
                "semaphore locked",
                pid);
    if (head_ptr != NULL) {
      child_proc1 = head_ptr;
      log_println(6, "modifying queue child_proc1=0x%x, head_ptr=0x%x",
                  child_proc1, head_ptr);
      head_ptr = head_ptr->next;
      log_println(6, "free child_proc1=0x%x", child_proc1);
      free(child_proc1);
    }
    if (head_ptr == NULL) testing = 0;
    if (multiple == 0) testing = 0;
    waiting--;
    log_println(3,
                "Removing Child from head, decremented waiting/mclients %d/%d",
                waiting, mclients);
    sem_post(&ndtq);
    log_println(6, "Free'd ndtq semaphore lock - 3");
    if (sig17 > 0) sig17--;
    return;
  } else {
    child_proc1 = head_ptr;
    while (child_proc1->next != NULL) {
      if (child_proc1->next->pid == pid) {
        while ((retcode = sem_wait(&ndtq)) == -1 && errno == EINTR) {
          log_println(6, "Waiting for ndtq semaphore to free - 2");
          continue;
        }
        log_println(
            4,
            "Child process %d causing task list modification, semaphore locked",
            chld_pid);
        child_proc2 = child_proc1->next;
        child_proc1->next = child_proc2->next;
        log_println(6, "free child_proc2=0x%x", child_proc2);
        free(child_proc2);
        /* testing = 0; */
        waiting--;
        log_println(
            3, "Removing Child from list, decremented waiting/mclients %d/%d",
            waiting, mclients);
        sem_post(&ndtq);
        log_println(6, "Free'd ndtq semaphore lock - 4");
        if (sig17 > 0) sig17--;
        return;
      }
      child_proc1 = child_proc1->next;
      log_println(6, "Looping through service queue ptr = 0x%x",
                  (uintptr_t)child_proc1);
    }
  }
  if (sig17 > 0) sig17--;
  log_println(3, "SIGCHLD routine finished!, decremented sig17 counter now =%d",
              sig17);
  return;
}

/**
 * Catch termination signal(s) and print message in log file
 * @param signo Signal number
 * */
void cleanup(int signo) {
  FILE *fp;

  if (signo != SIGINT && signo != SIGPIPE) {
    log_println(1, "Signal %d received by process %d", signo, getpid());
    if (get_debuglvl() > 0) {
      fp = fopen(get_logfile(), "a");
      if (fp != NULL) {
        fprintf(fp, "Signal %d received by process %d\n", signo, getpid());
        fclose(fp);
      }
    }
  }
  switch (signo) {
    default:
      fp = fopen(get_logfile(), "a");
      if (fp != NULL) {
        fprintf(fp,
                "Unexpected signal (%d) received, process (%d) may terminate\n",
                signo, getpid());
        fclose(fp);
      }
      break;
    case SIGSEGV:
      log_println(6, "DEBUG, caught SIGSEGV signal and terminated process (%d)",
                  getpid());
      if (getpid() != ndtpid) exit(-2);
      break;
    case SIGINT:
      exit(0);
    case SIGTERM:
      if (getpid() == ndtpid) {
        log_println(
            6,
            "DEBUG, SIGTERM signal received for parent process (%d), ignore it",
            ndtpid);
        break;
      }
      exit(0);
    case SIGUSR1:
      /* SIGUSR1 is used exclusively by C2S, to interrupt the pcap capture*/
      log_println(6,
                  "DEBUG, caught SIGUSR1, setting sig1 flag and calling "
                  "force_breakloop");
      force_breakloop();
      break;

    case SIGUSR2:
      /* SIGUSR2 is used exclusively by S2C, to interrupt the pcap capture*/
      log_println(6,
                  "DEBUG, caught SIGUSR2, setting sig2 flag and calling "
                  "force_breakloop");
      force_breakloop();
      break;

    case SIGALRM:
      switch (getCurrentTest()) {
        case TEST_MID:
          log_println(6, "Received SIGALRM signal [Middlebox test]");
          break;
        case TEST_C2S:
          log_println(6, "Received SIGALRM signal [C2S throughput test] pid=%d",
                      getpid());
          break;
        case TEST_S2C:
          log_println(6, "Received SIGALRM signal [S2C throughput test] pid=%d",
                      getpid());
          break;
        case TEST_SFW:
          log_println(6, "Received SIGALRM signal [Simple firewall test]");
          break;
        case TEST_META:
          log_println(6, "Received SIGALRM signal [META test]");
          break;
      }
      fp = fopen(get_logfile(), "a");
      if (fp != NULL) {
        if (get_debuglvl() > 4)
          fprintf(
              fp,
              "Received SIGALRM signal: terminating active web100srv process "
              "[%d]",
              getpid());
        switch (getCurrentTest()) {
          case TEST_MID:
            fprintf(fp, " [Middlebox test]\n");
            break;
          case TEST_C2S:
            fprintf(fp, " [C2S throughput test]\n");
            /* break; */
            if (wait_sig == 1) return;
            break;
          case TEST_S2C:
            fprintf(fp, " [S2C throughput test]\n");
            /* break; */
            if (wait_sig == 1) return;
            break;
          case TEST_SFW:
            fprintf(fp, " [Simple firewall test]\n");
            break;
          case TEST_META:
            fprintf(fp, " [META test]\n");
            break;
          default:
            fprintf(fp, "\n");
        }
        fclose(fp);
      }
      exit(0);
    case SIGPIPE:
      // SIGPIPE is an expected signal due to race conditions regarding the
      // possibility of writing a message to an already-terminated child.  Do
      // not let it kill the process.  The SIGCHLD handler will take care of
      // child process cleanup.
      fp = fopen(get_logfile(), "a");
      if ((fp != NULL) && (get_debuglvl() > 4)) {
        fprintf(fp, "Received SIGPIPE: a child has terminated early.\n");
        fclose(fp);
      }
      break;

    case SIGHUP:
      // Initialize Web100 structures
      count_vars = tcp_stat_init(VarFileName);

      // The administrator view automatically generates a usage page for the
      // NDT server.  This page is then accessable to the general public.
      // At this point read the existing log file and generate the necessary
      // data.  This data is then updated at the end of each test.
      if (admin_view == 1) view_init(refresh);
      break;

    case SIGCHLD:
      // Set a flag. The flag is checked near the top of the main wait loop, so
      // it will only be accessed once and only the testing proces will attempt
      // to do something with it.
      if (ndtpid != getppid()) {
        sig17++;
        log_println(5, "Signal 17 (SIGCHLD) received - completed tests = %d",
                    sig17);
      }
      break;
  }
}

/** LoadConfig routine copied from Internet2.
 * @param *name Pointer to Application name
 * @param **lbuf line buffer
 * @param *lbuf_max line buffer max - both help keep track of a dynamically
 *                       grown "line" buffer.
 */
static void LoadConfig(char *name, char **lbuf, size_t *lbuf_max) {
  FILE *conf;
  char keybuf[256], valbuf[256];
  char *key = keybuf, *val = valbuf;
  int retcode = 0;

  if (!(conf = fopen(ConfigFileName, "r"))) return;

  log_println(1, " Reading config file %s to obtain options", ConfigFileName);

  // Read the values for various keys and store them in appropriate variables
  while ((retcode = I2ReadConfVar(conf, retcode, key, val, 256, lbuf,
                                  lbuf_max)) > 0) {
    if (strncasecmp(key, "administrator_view", 5) == 0) {
      admin_view = 1;
      continue;
    } else if (strncasecmp(key, "multiple_clients", 5) == 0) {
      multiple = 1;
      continue;
    } else if (strncasecmp(key, "max_clients", 5) == 0) {
      max_clients = atoi(val);
      continue;
    } else if (strncasecmp(key, "record_reverse", 6) == 0) {
      record_reverse = 1;
      continue;
    } else if (strncasecmp(key, "write_trace", 5) == 0) {
      dumptrace = 1;
      continue;
    } else if (strncasecmp(key, "TCP_Buffer_size", 3) == 0) {
      if (check_int(val, &window)) {
        char tmpText[200];
        snprintf(tmpText, sizeof(tmpText), "Invalid window size: %s", val);
        short_usage(name, tmpText);
      }
      set_buff = 1;
      continue;
    } else if (strncasecmp(key, "debug", 5) == 0) {
      set_debuglvl(atoi(val));
      continue;
    } else if (strncasecmp(key, "variable_file", 6) == 0) {
#if USE_WEB100
      snprintf(wvfn, sizeof(wvfn), "%s", val);
      VarFileName = wvfn;
#elif USE_WEB10G
      log_println(0, "Web10G does not require variable file. Ignoring");
#endif
      continue;
    } else if (strncasecmp(key, "log_file", 3) == 0) {
      snprintf(lgfn, sizeof(lgfn), "%s", val);
      set_logfile(lgfn);
      snprintf(lgfn, sizeof(lgfn), "%s", val);
      continue;
    } else if (strncasecmp(key, "protolog_dir", 12) == 0) {
      snprintf(lgfn, sizeof(lgfn), "%s", val);
      set_protologdir(lgfn);
      continue;
    } else if (strncasecmp(key, "enableprotolog", 11) == 0) {
      enableprotocollogging();
      continue;
    } else if (strncasecmp(key, "admin_file", 10) == 0) {
      snprintf(apfn, sizeof(apfn), "%s", val);
      AdminFileName = apfn;
      continue;
    } else if (strncasecmp(key, "logfacility", 11) == 0) {
      snprintf(slfa, sizeof(slfa), "%s", val);
      SysLogFacility = slfa;
      continue;
    } else if (strncasecmp(key, "device", 5) == 0) {
      snprintf(devicebuf, sizeof(devicebuf), "%s", val);
      device = devicebuf;
      continue;
    } else if (strncasecmp(key, "port", 4) == 0) {
      snprintf(portbuf, sizeof(portbuf), "%s", val);
      port = portbuf;
      continue;
    } else if (strncasecmp(key, "syslog", 6) == 0) {
      usesyslog = 1;
      continue;
    } else if (strncasecmp(key, "disable_FIFO", 5) == 0) {
      queue = 0;
      continue;
    } else if (strncasecmp(key, "old_mismatch", 3) == 0) {
      old_mismatch = 1;
      continue;
    } else if (strncasecmp(key, "cputime", 3) == 0) {
      cputime = 1;
      continue;
    } else if (strncasecmp(key, "enableDBlogging", 8) == 0) {
      useDB = 1;
      continue;
    } else if (strncasecmp(key, "dbDSN", 5) == 0) {
      snprintf(dbDSNbuf, sizeof(dbDSNbuf), "%s", val);
      dbDSN = dbDSNbuf;
      continue;
    } else if (strncasecmp(key, "dbUID", 5) == 0) {
      snprintf(dbUIDbuf, sizeof(dbUIDbuf), "%s", val);
      dbUID = dbUIDbuf;
      continue;
    } else if (strncasecmp(key, "dbPWD", 5) == 0) {
      snprintf(dbPWDbuf, sizeof(dbPWDbuf), "%s", val);
      dbPWD = dbPWDbuf;
      continue;
    } else if (strncasecmp(key, "cwnddecrease", 5) == 0) {
      options.cwndDecrease = 1;
      options.snaplog = 1;
      continue;
    } else if (strncasecmp(key, "snaplog", 5) == 0) {
      options.snaplog = 1;
      continue;
    } else if (strncasecmp(key, "avoidsndblockup", 5) == 0) {
      options.avoidSndBlockUp = 1;
      continue;
    } else if (strncasecmp(key, "snapdelay", 5) == 0) {
      options.snapDelay = atoi(val);
      continue;
    } else if (strncasecmp(key, "limit", 5) == 0) {
      options.limit = atoi(val);
      continue;
    } else if (strncasecmp(key, "refresh", 5) == 0) {
      refresh = atoi(val);
      continue;
    } else if (strncasecmp(key, "mrange", 6) == 0) {
      if (mrange_parse(val)) {
        char tmpText[300];
        snprintf(tmpText, sizeof(tmpText), "Invalid range: %s", val);
        short_usage(name, tmpText);
      }
      continue;
    } else if (strncasecmp(key, "midport", 7) == 0) {
      if (check_int(val, &testopt.midsockport)) {
        char tmpText[200];
        snprintf(tmpText, sizeof(tmpText),
                 "Invalid Middlebox test port number: %s", val);
        short_usage(name, tmpText);
      }
      continue;
    } else if (strncasecmp(key, "c2sport", 7) == 0) {
      if (check_int(val, &testopt.c2ssockport)) {
        char tmpText[200];
        snprintf(tmpText, sizeof(tmpText),
                 "Invalid C2S throughput test port number: %s", val);
        short_usage(name, tmpText);
      }
      continue;
    } else if (strncasecmp(key, "s2cport", 7) == 0) {
      if (check_int(val, &testopt.s2csockport)) {
        char tmpText[200];
        snprintf(tmpText, sizeof(tmpText),
                 "Invalid S2C throughput test port number: %s", val);
        short_usage(name, tmpText);
      }
      continue;
    } else {
      log_println(0, "Unrecognized config option: %s", key);
      exit(1);
    }
  }
  if (retcode < 0) {
    log_println(0, "Syntax error in line %d of the config file %s", (-retcode),
                ConfigFileName);
    exit(1);
  }
  fclose(conf);
}


/**
 * Capture CPU time details
 *
 * @param arg* void pointer to the log file used to record CPU time details
 */
void *cputimeWorker(void *arg) {
  char *logname = (char *)arg;
  FILE *file = fopen(logname, "w");
  struct tms buf;
  double start = secs();

  if (!file) return NULL;

  while (1) {
    if (!cputimeworkerLoop) {
      break;
    }
    times(&buf);
    fprintf(file, "%.2f %ld %ld %ld %ld\n", secs() - start, buf.tms_utime,
            buf.tms_stime, buf.tms_cutime, buf.tms_cstime);
    fflush(file);
    mysleep(0.1);
  }

  fclose(file);

  return NULL;
}

/**
 * Run all tests, process results, record them into relevant log files
 *
 * @param agent pointer to tcp_stat agent
 * @param ctl Connection used for server->client communication
 * @param testopt TestOptions *
 * @param test_suite pointer to string indicating tests to be run
 * @param ssl_context pointer to the SSL context (may be null)
 * */

int run_test(tcp_stat_agent *agent, Connection *ctl, TestOptions *testopt,
             char *test_suite, SSL_CTX *ssl_context) {
#if USE_WEB100
  tcp_stat_connection conn = NULL;
#elif USE_WEB10G
  tcp_stat_connection conn = -1;
#endif
  char date[32];      // date indicator
  char spds[4][256];  // speed "bin" array containing counters for speeds
  char logstr1[4096], logstr2[1024];  // log
  char tmpstr[256];
  char isoTime[64];

  // int n;  // temporary iterator variable --// commented out -> calc_linkspeed
  struct tcp_vars vars;

  int link = CANNOT_DETERMINE_LINK;  // local temporary variable indicative of
  // link speed. Transmitted but unused at client end , which has a similar
  // link speed variable
  int mismatch = 0, bad_cable = 0;
  int half_duplex = NO_HALF_DUPLEX;
  int congestion = 0, totaltime;
  int ret, spd_index;
  // int index;  // commented out -> calc_linkspeed
  // int links[16];  // commented out -> calc_linkspeed
  // int max;  // commented out -> calc_linkspeed
  // int total;// commented out -> calc_linkspeed
  // C->S data link speed indicator determined using results
  int c2s_linkspeed_data = 0;
  int c2s_linkspeed_ack = 0;
  // S->C data link speed indicator determined using results
  int s2c_linkspeed_data = 0;
  int s2c_linkspeed_ack = 0;
  // int j;        // commented out -> calc_linkspeed
  int totalcnt;
  int autotune;
  // values collected from the speed tests
  u_int32_t dec_cnt, same_cnt, inc_cnt;
  int timeout, dupack;
  // int ifspeed;

  time_t stime;

  double rttsec;      // average round trip time
  double swin, cwin;  // send, congestion window sizes respectively
  double rwin;        // max window size advertisement rcvd
  double rwintime;    // time spent being limited due to rcvr end
  double cwndtime;    // time spent being limited due to congestion
  double sendtime;    // time spent being limited due to sender's own fault

  double oo_order;  // out-of-order packet ratio
  double waitsec;
  double bw_theortcl;     // theoretical bandwidth
  double avgrtt;          // Average round-trip time
  double timesec;         // Total test time in microseconds
  double packetloss_s2c;  // Packet loss as calculated from S->c tests.
  double RTOidle;         // Proportion of idle time spent waiting for packets
  double s2cspd;          // average throughput as calculated by S->C test
  double c2sspd;          // average throughput as calculated by C->S test
  double s2c2spd;         // average throughput as calculated by midbox test
  double realthruput;     // total send throughput in S->C
  double acksratio;       // ratio of acks over packets sent
  double aspd = 0;
  double tmoutsratio;  // timeouts fraction
  // ratio of retransmissions and duplicate acks over packets sent
  double rtranratio, dackratio;
  float runave[4];

  FILE *fp;

  // start with a clean slate of currently running test and direction
  setCurrentTest(TEST_NONE);
  log_println(7, "Remote host= %s", get_remotehostaddress());

  stime = time(0);
  log_println(4, "Child process %d started", getpid());
  testopt->child0 = getpid();

  // initialize speeds array
  for (spd_index = 0; spd_index < 4; spd_index++)
    for (ret = 0; ret < 256; ret++)
      spds[spd_index][ret] = 0x00;
  spd_index = 0;

  // obtain web100 connection and check auto-tune status
  conn = tcp_stat_connection_from_socket(agent, ctl->socket);
  autotune = tcp_stat_autotune(ctl->socket, agent, conn);

  // client needs to be version compatible. Send current version
  snprintf(buff, sizeof(buff), "v%s", VERSION "-" TCP_STAT_NAME);
  send_json_message_any(ctl, MSG_LOGIN, buff, strlen(buff),
                        testopt->connection_flags, JSON_SINGLE_VALUE);

  // initiate test with MSG_LOGIN message.
  log_println(3, "run_test() routine, asking for test_suite = %s",
              test_suite);
  send_json_message_any(ctl, MSG_LOGIN, test_suite, strlen(test_suite),
                        testopt->connection_flags, JSON_SINGLE_VALUE);

  log_println(1, "Starting test suite:");
  if (testopt->midopt) {
    log_println(1, " > Middlebox test");
  }
  if (testopt->sfwopt) {
    log_println(1, " > Simple firewall test");
  }
  if (testopt->c2sopt) {
    log_println(1, " > C2S throughput test");
  }
  if (testopt->s2copt) {
    log_println(1, " > S2C throughput test");
  }
  if (testopt->metaopt) {
    log_println(1, " > META test");
  }

  /*  alarm(15); */
  // Run scheduled test. Log error code if necessary
  log_println(6, "Starting middlebox test");
  if ((ret = test_mid(ctl, agent, &*testopt, conn_options, &s2c2spd)) != 0) {
    if (ret < 0)
      log_println(6, "Middlebox test failed with rc=%d", ret);
    log_println(0, "Middlebox test FAILED!, rc=%d", ret);
    testopt->midopt = TOPT_DISABLED;
    return ret;
  }

  /*  alarm(20); */
  log_println(6, "Starting simple firewall test");
  if ((ret = test_sfw_srv(ctl, agent, &*testopt, conn_options)) != 0) {
    if (ret < 0)
      log_println(6, "SFW test failed with rc=%d", ret);
  }

  /*  alarm(25); */
  log_println(6, "Starting c2s throughput test");
  if ((ret = test_c2s(ctl, agent, &*testopt, conn_options, &c2sspd, set_buff,
                      window, autotune, device, &options, record_reverse,
                      count_vars, spds, &spd_index, ssl_context)) != 0) {
    if (ret < 0)
      log_println(6, "C2S test failed with rc=%d", ret);
    log_println(0, "C2S throughput test FAILED!, rc=%d", ret);
    testopt->c2sopt = TOPT_DISABLED;
    return ret;
  }

  /*  alarm(25); */
  log_println(6, "Starting s2c throughput test");
  if ((ret = test_s2c(ctl, agent, &*testopt, conn_options, &s2cspd,
                      set_buff, window, autotune, device, &options, spds,
                      &spd_index, count_vars, &peaks, ssl_context)) != 0) {
    if (ret < 0)
      log_println(6, "S2C test failed with rc=%d", ret);
    log_println(0, "S2C throughput test FAILED!, rc=%d", ret);
    testopt->s2copt = TOPT_DISABLED;
    return ret;
  }

  log_println(6, "Starting META test");
  if ((ret = test_meta_srv(ctl, agent, &*testopt, conn_options)) != 0) {
    if (ret < 0) {
      log_println(6, "META test failed with rc=%d", ret);
    }
  }

  // Compute variable values from test results and deduce results
  log_println(4, "Finished testing C2S = %0.2f Mbps, S2C = %0.2f Mbps",
              c2sspd / 1000, s2cspd / 1000);

  // Determine link speed
  calc_linkspeed(spds, spd_index, &c2s_linkspeed_data, &c2s_linkspeed_ack,
                 &s2c_linkspeed_data, &s2c_linkspeed_ack, runave, &dec_cnt,
                 &same_cnt, &inc_cnt, &timeout, &dupack, testopt->c2sopt);
  // Get web100 vars

  // ...determine number of times congestion window has been changed
  if (options.cwndDecrease) {
    dec_cnt = inc_cnt = same_cnt = 0;
    CwndDecrease(options.s2c_logname, &dec_cnt, &same_cnt, &inc_cnt);
    log_println(2, "####### decreases = %d, increases = %d, no change = %d",
                dec_cnt, inc_cnt, same_cnt);
  }

  // ...other variables
  memset(&vars, 0xFF, sizeof(vars));
  tcp_stat_logvars(&vars, count_vars);

  // end getting web100 variable values

  // section to calculate duplex mismatch
  // Calculate average round trip time and convert to seconds
  rttsec = calc_avg_rtt(vars.SumRTT, vars.CountRTT, &avgrtt);
  // Calculate packet loss
  packetloss_s2c = calc_packetloss(vars.CongestionSignals, vars.PktsOut,
                                   c2s_linkspeed_data);

  // Calculate ratio of packets arriving out of order
  oo_order = calc_packets_outoforder(vars.DupAcksIn, vars.AckPktsIn);

  // calculate theoretical maximum goodput in bits
  bw_theortcl = calc_max_theoretical_throughput(vars.CurrentMSS, rttsec,
                                                packetloss_s2c);

  // get window sizes
  calc_window_sizes(&vars.SndWinScale, &vars.RcvWinScale, vars.Sndbuf,
                    vars.MaxRwinRcvd, vars.MaxCwnd, &rwin, &swin, &cwin);

  // Total test time
  totaltime = calc_totaltesttime(vars.SndLimTimeRwin, vars.SndLimTimeCwnd,
                                 vars.SndLimTimeSender);

  // time spent being send-limited due to client's recv window
  rwintime = calc_sendlimited_rcvrfault(vars.SndLimTimeRwin, totaltime);

  // time spent in being send-limited due to congestion window
  cwndtime = calc_sendlimited_cong(vars.SndLimTimeCwnd, totaltime);

  // time spent in being send-limited due to own fault
  sendtime = calc_sendlimited_sndrfault(vars.SndLimTimeSender, totaltime);

  timesec = totaltime / MEGA;  // total time in microsecs

  // get fraction of total test time waiting for packets to arrive
  RTOidle = calc_RTOIdle(vars.Timeouts, vars.CurrentRTO, timesec);

  // get timeout, retransmission, acks and dup acks ratios.
  tmoutsratio = (double) vars.Timeouts / vars.PktsOut;
  rtranratio = (double) vars.PktsRetrans / vars.PktsOut;
  acksratio = (double) vars.AckPktsIn / vars.PktsOut;
  dackratio = (double) vars.DupAcksIn / (double) vars.AckPktsIn;

  // get actual throughput in Mbps (totaltime is in microseconds)
  realthruput = calc_real_throughput(vars.DataBytesOut, totaltime);

  // total time spent waiting
  waitsec = cal_totalwaittime(vars.CurrentRTO, vars.Timeouts);

  log_println(2, "CWND limited test = %0.2f while unlimited = %0.2f", s2c2spd,
              s2cspd);

  // Is thruput measured with limited cwnd(midbox test) > as reported S->C test
  if (is_limited_cwnd_throughput_better(s2c2spd, s2cspd) &&
      isNotMultipleTestMode(multiple)) {
    log_println(
        2,
        "Better throughput when CWND is limited, may be duplex mismatch");
  } else {
    log_println(2,
                "Better throughput without CWND limits - normal operation");
  }

  // remove the following line when the new detection code is ready for release
  // retaining old comment above
  // client link duplex mismatch detection heuristic
  old_mismatch = 1;

  if (old_mismatch == 1) {
    if (detect_duplexmismatch(cwndtime, bw_theortcl, vars.PktsRetrans, timesec,
                              vars.MaxSsthresh, RTOidle, link, s2cspd, s2c2spd,
                              multiple)) {
      if (is_c2s_throughputbetter(c2sspd, s2cspd)) {
        // also, S->C throughput is lesser than C->S throughput
        mismatch = DUPLEX_OLD_ALGO_INDICATOR;
        // possible duplex, from Old Duplex-Mismatch logic
      } else {
        mismatch = DUPLEX_SWITCH_FULL_HOST_HALF;
        // switch full, host half
      }
      link = LINK_ALGO_FAILED;
    }

    // test for uplink with duplex mismatch condition
    if (detect_internal_duplexmismatch(
        (s2cspd / 1000), realthruput, rwintime, packetloss_s2c)) {
      mismatch = DUPLEX_SWITCH_FULL_HOST_HALF;  // switch full, host half
      link = LINK_ALGO_FAILED;
    }
  } else {
    /* This section of code is being held up for non-technical reasons.
     * Once those issues are resolved a new mismatch detection algorim
     * will be placed here.
     *  RAC 5-11-06
     */
  }

  // end section calculating duplex mismatch

  // Section to deduce if there is faulty hardware links

  // estimate is less than actual throughput, something is wrong
  if (bw_theortcl < realthruput)
    link = LINK_ALGO_FAILED;

  // Faulty hardware link heuristic.
  if (detect_faultyhardwarelink(packetloss_s2c, cwndtime, timesec,
                                vars.MaxSsthresh))
    bad_cable = POSSIBLE_BAD_CABLE;

  // test for Ethernet link (assume Fast E.)
  if (detect_ethernetlink(realthruput, s2cspd, packetloss_s2c, oo_order,
                          link))
    link = LINK_ETHERNET;

  // test for wireless link
  if (detect_wirelesslink(sendtime, realthruput, bw_theortcl,
                          vars.SndLimTransRwin, vars.SndLimTransCwnd, rwintime,
                          link)) {
    link = LINK_WIRELESS;
  }

  // test for DSL/Cable modem link
  if (detect_DSLCablelink(vars.SndLimTimeSender, vars.SndLimTransSender,
                          realthruput, bw_theortcl, link)) {
    link = LINK_DSLORCABLE;
  }

  // full/half link duplex setting heuristic:
  // receiver-limited- time > 95%,
  //  .. number of transitions into the 'Receiver Limited' state is greater
  //  than 30 ps
  //  ...and the number of transitions into the 'Sender Limited' state is
  //  greater than 30 per second

  if (detect_halfduplex(rwintime, vars.SndLimTransRwin, vars.SndLimTransSender,
                        timesec))
    half_duplex = POSSIBLE_HALF_DUPLEX;

  // congestion detection heuristic
  if (detect_congestionwindow(cwndtime, mismatch, cwin, rwin, rttsec))
    congestion = POSSIBLE_CONGESTION;

  // Send results and variable values to clients
  snprintf(buff, sizeof(buff), "c2sData: %d\nc2sAck: %d\ns2cData: %d\n"
           "s2cAck: %d\n", c2s_linkspeed_data, c2s_linkspeed_ack,
           s2c_linkspeed_data, s2c_linkspeed_ack);
  send_json_message_any(ctl, MSG_RESULTS, buff, strlen(buff),
                        testopt->connection_flags, JSON_SINGLE_VALUE);

  snprintf(buff, sizeof(buff),
           "half_duplex: %d\nlink: %d\ncongestion: %d\nbad_cable: %d\n"
           "mismatch: %d\nspd: %0.2f\n", half_duplex, link, congestion,
           bad_cable, mismatch, realthruput);
  send_json_message_any(ctl, MSG_RESULTS, buff, strlen(buff),
                        testopt->connection_flags, JSON_SINGLE_VALUE);

  snprintf(buff, sizeof(buff),
           "bw: %0.2f\nloss: %0.9f\navgrtt: %0.2f\nwaitsec: %0.2f\n"
           "timesec: %0.2f\norder: %0.4f\n", bw_theortcl, packetloss_s2c,
           avgrtt, waitsec, timesec, oo_order);
  send_json_message_any(ctl, MSG_RESULTS, buff, strlen(buff),
                        testopt->connection_flags, JSON_SINGLE_VALUE);

  snprintf(buff, sizeof(buff),
           "rwintime: %0.4f\nsendtime: %0.4f\ncwndtime: %0.4f\n"
           "rwin: %0.4f\nswin: %0.4f\n", rwintime, sendtime, cwndtime, rwin,
           swin);
  send_json_message_any(ctl, MSG_RESULTS, buff, strlen(buff),
                        testopt->connection_flags, JSON_SINGLE_VALUE);

  snprintf(buff, sizeof(buff),
           "cwin: %0.4f\nrttsec: %0.6f\nSndbuf: %"VARtype"\naspd: %0.5f\n"
           "CWND-Limited: %0.2f\n", cwin, rttsec, vars.Sndbuf, aspd, s2c2spd);
  send_json_message_any(ctl, MSG_RESULTS, buff, strlen(buff),
                        testopt->connection_flags, JSON_SINGLE_VALUE);

  snprintf(buff, sizeof(buff),
           "minCWNDpeak: %d\nmaxCWNDpeak: %d\nCWNDpeaks: %d\n",
           peaks.min, peaks.max, peaks.amount);
  send_json_message_any(ctl, MSG_RESULTS, buff, strlen(buff),
                        testopt->connection_flags, JSON_SINGLE_VALUE);

  // Signal end of test results to client
  send_json_message_any(ctl, MSG_LOGOUT, "", 0, testopt->connection_flags,
                        JSON_SINGLE_VALUE);

  // Copy collected values into the meta data structures. This section
  // seems most readable, easy to debug here.
  snprintf(meta.date, sizeof(meta.date), "%s",
           get_ISOtime(isoTime, sizeof(isoTime)));

  log_println(9, "meta.date=%s, meta.clientip =%s:%s:%d", meta.date,
              meta.client_ip, rmt_addr, strlen(rmt_addr));
  memcpy(meta.client_ip, rmt_addr, strlen(rmt_addr));
  log_println(9, "2. meta.clientip =%s:%s:%d", meta.client_ip, rmt_addr);

  memset(tmpstr, 0, sizeof(tmpstr));
  snprintf(tmpstr, sizeof(tmpstr), "%d,%d,%d,%"VARtype",%"VARtype",%"
           VARtype",%"VARtype",%"VARtype",%"VARtype",%"VARtype",%"
           VARtype",%"VARtype",%"VARtype",",
           (int) s2c2spd, (int) s2cspd, (int) c2sspd, vars.Timeouts,
           vars.SumRTT, vars.CountRTT, vars.PktsRetrans, vars.FastRetran,
           vars.DataPktsOut, vars.AckPktsOut, vars.CurrentMSS, vars.DupAcksIn,
           vars.AckPktsIn);
  memcpy(meta.summary, tmpstr, strlen(tmpstr));
  memset(tmpstr, 0, sizeof(tmpstr));
  snprintf(tmpstr, sizeof(tmpstr), "%"VARtype",%"VARtype",%"VARtype",%"
           VARtype",%"VARtype",%"VARtype",%"VARtype",%"VARtype
           ",%"VARtype",%"VARtype",%"VARtype",%"VARtype",%"VARtype",",
           vars.MaxRwinRcvd, vars.Sndbuf, vars.MaxCwnd, vars.SndLimTimeRwin,
           vars.SndLimTimeCwnd, vars.SndLimTimeSender, vars.DataBytesOut,
           vars.SndLimTransRwin, vars.SndLimTransCwnd, vars.SndLimTransSender,
           vars.MaxSsthresh, vars.CurrentRTO, vars.CurrentRwinRcvd);

  strlcat(meta.summary, tmpstr, sizeof(meta.summary));
  memset(tmpstr, 0, sizeof(tmpstr));
  snprintf(tmpstr, sizeof(tmpstr), "%d,%d,%d,%d,%d", link, mismatch, bad_cable,
           half_duplex, congestion);

  strlcat(meta.summary, tmpstr, sizeof(meta.summary));
  memset(tmpstr, 0, sizeof(tmpstr));
  snprintf(tmpstr, sizeof(tmpstr), ",%d,%d,%d,%d,%"VARtype",%"VARtype
           ",%"VARtype",%"VARtype",%d",
           c2s_linkspeed_data, c2s_linkspeed_ack, s2c_linkspeed_data,
           s2c_linkspeed_ack, vars.CongestionSignals, vars.PktsOut, vars.MinRTT,
           vars.RcvWinScale, autotune);

  strlcat(meta.summary, tmpstr, sizeof(meta.summary));
  memset(tmpstr, 0, sizeof(tmpstr));
  snprintf(tmpstr, sizeof(tmpstr), ",%"VARtype",%"VARtype",%"VARtype",%"
           VARtype",%"VARtype",%"VARtype",%"VARtype",%"VARtype",%"
           VARtype",%"VARtype,
           vars.CongAvoid, vars.CongestionOverCount, vars.MaxRTT,
           vars.OtherReductions, vars.CurTimeoutCount, vars.AbruptTimeouts,
           vars.SendStall, vars.SlowStart, vars.SubsequentTimeouts,
           vars.ThruBytesAcked);

  strlcat(meta.summary, tmpstr, sizeof(meta.summary));
  memset(tmpstr, 0, sizeof(tmpstr));
  snprintf(tmpstr, sizeof(tmpstr), ",%d,%d,%d", peaks.min, peaks.max,
           peaks.amount);

  strlcat(meta.summary, tmpstr, sizeof(meta.summary));
  writeMeta(options.compress, cputime, options.snaplog, dumptrace);

  // Write into log files, DB
  fp = fopen(get_logfile(), "a");
  if (fp == NULL) {
    log_println(0,
                "Unable to open log file '%s', continuing on without logging",
                get_logfile());
  } else {
    snprintf(date, sizeof(date), "%15.15s", ctime(&stime) + 4);
    fprintf(fp, "%s,", date);
    fprintf(fp, "%s,%d,%d,%d,%"VARtype",%"VARtype",%"VARtype",%"
            VARtype",%"VARtype",%"VARtype",%"VARtype",%"VARtype",%"
            VARtype",%"VARtype",", rmt_addr,
            (int) s2c2spd, (int) s2cspd, (int) c2sspd, vars.Timeouts,
            vars.SumRTT, vars.CountRTT, vars.PktsRetrans, vars.FastRetran,
            vars.DataPktsOut, vars.AckPktsOut, vars.CurrentMSS, vars.DupAcksIn,
            vars.AckPktsIn);
    fprintf(fp, "%"VARtype",%"VARtype",%"VARtype",%"VARtype",%"VARtype","
            "%"VARtype",%"VARtype",%"VARtype",%"VARtype",%"VARtype",%"
            VARtype",%"VARtype",%"VARtype",", vars.MaxRwinRcvd,
            vars.Sndbuf, vars.MaxCwnd, vars.SndLimTimeRwin, vars.SndLimTimeCwnd,
            vars.SndLimTimeSender, vars.DataBytesOut, vars.SndLimTransRwin,
            vars.SndLimTransCwnd, vars.SndLimTransSender, vars.MaxSsthresh,
            vars.CurrentRTO, vars.CurrentRwinRcvd);
    fprintf(fp, "%d,%d,%d,%d,%d", link, mismatch, bad_cable, half_duplex,
            congestion);
    fprintf(fp, ",%d,%d,%d,%d,%"VARtype",%"VARtype",%"VARtype",%"VARtype",%d",
            c2s_linkspeed_data,
            c2s_linkspeed_ack, s2c_linkspeed_data, s2c_linkspeed_ack,
            vars.CongestionSignals, vars.PktsOut, vars.MinRTT, vars.RcvWinScale,
            autotune);
    fprintf(fp, ",%"VARtype",%"VARtype",%"VARtype",%"VARtype",%"VARtype
            ",%"VARtype",%"VARtype",%"VARtype",%"VARtype",%"VARtype,
            vars.CongAvoid,
            vars.CongestionOverCount, vars.MaxRTT, vars.OtherReductions,
            vars.CurTimeoutCount, vars.AbruptTimeouts, vars.SendStall,
            vars.SlowStart, vars.SubsequentTimeouts, vars.ThruBytesAcked);
    fprintf(fp, ",%d,%d,%d\n", peaks.min, peaks.max, peaks.amount);
    fclose(fp);
  }
  db_insert(spds, runave, cputimelog, options.s2c_logname,
            options.c2s_logname, testName, testPort, date, rmt_addr, s2c2spd,
            s2cspd, c2sspd, vars.Timeouts, vars.SumRTT, vars.CountRTT,
            vars.PktsRetrans, vars.FastRetran, vars.DataPktsOut,
            vars.AckPktsOut, vars.CurrentMSS, vars.DupAcksIn, vars.AckPktsIn,
            vars.MaxRwinRcvd, vars.Sndbuf, vars.MaxCwnd, vars.SndLimTimeRwin,
            vars.SndLimTimeCwnd, vars.SndLimTimeSender, vars.DataBytesOut,
            vars.SndLimTransRwin, vars.SndLimTransCwnd, vars.SndLimTransSender,
            vars.MaxSsthresh, vars.CurrentRTO, vars.CurrentRwinRcvd, link,
            mismatch, bad_cable, half_duplex, congestion, c2s_linkspeed_data,
            c2s_linkspeed_ack, s2c_linkspeed_data, s2c_linkspeed_ack,
            vars.CongestionSignals, vars.PktsOut, vars.MinRTT, vars.RcvWinScale,
            autotune, vars.CongAvoid, vars.CongestionOverCount, vars.MaxRTT,
            vars.OtherReductions, vars.CurTimeoutCount, vars.AbruptTimeouts,
            vars.SendStall, vars.SlowStart, vars.SubsequentTimeouts,
            vars.ThruBytesAcked, peaks.min, peaks.max, peaks.amount);
  if (usesyslog == 1) {
    snprintf(
        logstr1, sizeof(logstr1),
        "client_IP=%s,c2s_spd=%2.0f,s2c_spd=%2.0f,Timeouts=%"VARtype","
        "SumRTT=%"VARtype","
        "CountRTT=%"VARtype",PktsRetrans=%"VARtype","
        "FastRetran=%"VARtype",DataPktsOut=%"VARtype","
        "AckPktsOut=%"VARtype","
        "CurrentMSS=%"VARtype",DupAcksIn=%"VARtype","
        "AckPktsIn=%"VARtype",",
        rmt_addr, c2sspd, s2cspd, vars.Timeouts, vars.SumRTT, vars.CountRTT,
        vars.PktsRetrans, vars.FastRetran, vars.DataPktsOut, vars.AckPktsOut,
        vars.CurrentMSS, vars.DupAcksIn, vars.AckPktsIn);
    snprintf(
        logstr2, sizeof(logstr2),
        "MaxRwinRcvd=%"VARtype",Sndbuf=%"VARtype","
        "MaxCwnd=%"VARtype",SndLimTimeRwin=%"VARtype","
        "SndLimTimeCwnd=%"VARtype",SndLimTimeSender=%"VARtype","
        "DataBytesOut=%"VARtype","
        "SndLimTransRwin=%"VARtype",SndLimTransCwnd=%"VARtype","
        "SndLimTransSender=%"VARtype","
        "MaxSsthresh=%"VARtype",CurrentRTO=%"VARtype","
        "CurrentRwinRcvd=%"VARtype",",
        vars.MaxRwinRcvd, vars.Sndbuf, vars.MaxCwnd, vars.SndLimTimeRwin,
        vars.SndLimTimeCwnd, vars.SndLimTimeSender, vars.DataBytesOut,
        vars.SndLimTransRwin, vars.SndLimTransCwnd, vars.SndLimTransSender,
        vars.MaxSsthresh, vars.CurrentRTO, vars.CurrentRwinRcvd);
    strlcat(logstr1, logstr2, sizeof(logstr1));
    snprintf(
        logstr2, sizeof(logstr2),
        "link=%d,mismatch=%d,bad_cable=%d,half_duplex=%d,congestion=%d,"
        "c2s_linkspeed_data=%d,c2sack=%d,s2cdata=%d,s2cack=%d,"
        "CongestionSignals=%"VARtype",PktsOut=%"VARtype",MinRTT=%"
        VARtype",RcvWinScale=%"VARtype"\n",
        link, mismatch, bad_cable, half_duplex, congestion, c2s_linkspeed_data,
        c2s_linkspeed_ack, s2c_linkspeed_data, s2c_linkspeed_ack,
        vars.CongestionSignals, vars.PktsOut, vars.MinRTT, vars.RcvWinScale);
    strlcat(logstr1, logstr2, sizeof(logstr1));
    syslog(LOG_FACILITY | LOG_INFO, "%s", logstr1);
    closelog();
    log_println(4, "%s", logstr1);
  }

  // close resources

  if (testopt->s2copt) {
    close(testopt->s2csockfd);
  }

  /* If the admin view is turned on then the client process is going to update
   * these variables.  The need to be shipped back to the parent so the admin
   * page can be updated.  Otherwise the changes are lost when the client
   * terminates.
   */
  if (admin_view == 1) {
    totalcnt = calculate(date, vars.SumRTT, vars.CountRTT,
                         vars.CongestionSignals, vars.PktsOut, vars.DupAcksIn,
                         vars.AckPktsIn, vars.CurrentMSS, vars.SndLimTimeRwin,
                         vars.SndLimTimeCwnd, vars.SndLimTimeSender,
                         vars.MaxRwinRcvd, vars.CurrentCwnd, vars.Sndbuf,
                         vars.DataBytesOut, mismatch, bad_cable, (int) c2sspd,
                         (int) s2cspd, c2s_linkspeed_data, s2c_linkspeed_ack,
                         1);
    gen_html((int) c2sspd, (int) s2cspd, vars.MinRTT, vars.PktsRetrans,
             vars.Timeouts, vars.Sndbuf, vars.MaxRwinRcvd, vars.CurrentCwnd,
             mismatch, bad_cable, totalcnt, refresh);
  }
  if (ctl->ssl != NULL) {
    SSL_shutdown(ctl->ssl);
  } else {
    shutdown(ctl->socket, SHUT_WR);
  }
  /* shutdown(ctlsockfd, SHUT_RDWR); */
  return (0);
}

/* web100srv.c contains both a main() that runs things, but is also a source of
 * library code run by other parts of the program.  In order to test those
 * other parts, we must be able to compile this file without the main()
 * function.  To use this file as a library, pass in
 * -DUSE_WEB100SRV_ONLY_AS_LIBRARY as a compile-time option.
 */
#ifndef USE_WEB100SRV_ONLY_AS_LIBRARY
/**
 * Initializes data structures,
 *  web100 structures and logging systems.  Read/load configuration, get process
 *  execution options. Accept test requests and manage their execution order and
 *  initiate tests. Keep track of running processes.
 * @param argc Number of arguments
 * @param argv string command line arguments
 * */
int main(int argc, char **argv) {
  int c, i, loopcnt;
  struct sigaction new;
  char *lbuf = NULL, *ctime();
  FILE *fp;
  size_t lbuf_max = 0;
  char *private_key_file = NULL;
  char *certificate_file = NULL;
  SSL_CTX *ssl_context = NULL;
  char ssl_error[255];
  unsigned long ssl_err;
  time_t tt;
  I2Addr listenaddr = NULL;
  int listenfd;
  char *srcname = NULL;
  int debug = 0;

  // variables used for protocol validation logs
  // char startsrvmsg[256];  // used to log start of server process
  // char *srvstatusdesc;
  // enum PROCESS_STATUS_INT srvstatusenum = UNKNOWN;
  // // temp storage for process name
  // char statustemparr[PROCESS_STATUS_DESC_SIZE];

  options.limit = 0;
  options.snapDelay = 5;
  options.avoidSndBlockUp = 0;
  options.snaplog = 0;
  options.cwndDecrease = 0;
  options.tls = 0;
  memset(options.s2c_logname, 0, 256);
  memset(options.c2s_logname, 0, 256);
  peaks.min = -1;
  peaks.max = -1;
  peaks.amount = -1;
  DataDirName = NULL;

  memset(&testopt, 0, sizeof(testopt));

#ifdef AF_INET6
#define GETOPT_LONG_INET6(x) "46"x
#else
#define GETOPT_LONG_INET6(x) x
#endif

#ifdef EXPERIMENTAL_ENABLED
#define GETOPT_LONG_EXP(x) "y:"x
#else
#define GETOPT_LONG_EXP(x) x
#endif

  opterr = 0;
  while ((c =
          getopt_long(
              argc,
              argv,
              GETOPT_LONG_INET6(
                  GETOPT_LONG_EXP("adhmoqrstvzc:x:b:f:i:l:u:e:p:T:A:S:")),
              long_options, 0)) != -1) {
                switch (c) {
                  case 'c':
                    ConfigFileName = optarg;
                    break;
                  case 'd':
                    debug++;
                    break;
                }
              }

  // Initialize logging system, and then read configuration
  log_init(argv[0], debug);

  if (ConfigFileName == NULL)
    ConfigFileName = CONFIGFILE;

  // Load Config file.
  // lbuf/lbuf_max keep track of a dynamically grown "line" buffer.
  // (It is grown using realloc.)
  // This will be used throughout all the config file reading and
  // should be free'd once all config files have been read.

  opterr = optind = 1;
  LoadConfig(argv[0], &lbuf, &lbuf_max);
  debug = 0;

  // set options for test direction
  enum Tx_DIRECTION currentDirn = S_C;
  setCurrentDirn(currentDirn);
  // end protocol logging

#if USE_WEB10G
  log_println(0, "WARNING: The Web10G NDT server is still in testing"
              " and may contain bugs.");
#endif
  // Get server execution options
  while ((c =
          getopt_long(
              argc,
              argv,
              GETOPT_LONG_INET6(
                  GETOPT_LONG_EXP("adhmoqrstvzc:x:b:f:i:l:u:e:p:T:A:S:")),
              long_options, 0)) != -1) {
                switch (c) {
                  case '4':
                    conn_options |= OPT_IPV4_ONLY;
                    break;
                  case '6':
                    conn_options |= OPT_IPV6_ONLY;
                    break;
                  case 'r':
                    record_reverse = 1;
                    break;
                  case 'h':
                    srv_long_usage("ANL/Internet2 NDT version " VERSION
                                   " (server)");
                    break;
                  case 'v':
                    printf("ANL/Internet2 NDT version %s (server)\n", VERSION);
                    exit(0);
                    break;
                  case 'p':
                    port = optarg;

                    if (check_int(port, &testopt.mainport)) {
                      char tmpText[200];
                      snprintf(tmpText, sizeof(tmpText),
                               "Invalid primary port number: " "%s", optarg);
                      short_usage(argv[0], tmpText);
                    }
                    break;
                  case 302:
                    if (check_int(optarg, &testopt.midsockport)) {
                      char tmpText[200];
                      snprintf(tmpText, sizeof(tmpText),
                               "Invalid Middlebox test port number: %s",
                               optarg);
                      short_usage(argv[0], tmpText);
                    }
                    break;
                  case 303:
                    if (check_int(optarg, &testopt.c2ssockport)) {
                      char tmpText[200];
                      snprintf(tmpText, sizeof(tmpText),
                               "Invalid C2S throughput test port number: %s",
                               optarg);
                      short_usage(argv[0], tmpText);
                    }
                    break;
                  case 304:
                    if (check_int(optarg, &testopt.s2csockport)) {
                      char tmpText[200];
                      snprintf(tmpText, sizeof(tmpText),
                               "Invalid S2C throughput test port number: %s",
                               optarg);
                      short_usage(argv[0], tmpText);
                    }
                    break;
                  case 'a':
                    admin_view = 1;
                    break;
                  case 'f':
#if USE_WEB100
                    VarFileName = optarg;
#elif USE_WEB10G
                    log_println(2, "Web10G doesn't require varfile. Ignored.");
#endif
                    break;
                  case 'i':
                    device = optarg;
                    break;
                  case 'l':
                    set_logfile(optarg);
                    break;
                  case 'u':
                    set_protologdir(optarg);
                    break;
                  case 'e':
                    log_println(7, "Enabling protocol logging");
                    enableprotocollogging();
                    break;
                  case 'o':
                    old_mismatch = 1;
                    break;
                  case 301:
                    if (mrange_parse(optarg)) {
                      char tmpText[300];
                      snprintf(tmpText, sizeof(tmpText), "Invalid range: %s",
                               optarg);
                      short_usage(argv[0], tmpText);
                    }
                  case 'z':
                    compress = 0;
                    break;
                  case 'm':
                    multiple = 1;
                    break;
                  case 'x':
                    max_clients = atoi(optarg);
                    break;
                  case 'q':
                    queue = 0;
                    break;
                  case 's':
                    usesyslog = 1;
                    break;
                  case 't':
                    dumptrace = 1;
                    break;
                  case 'b':
                    if (check_int(optarg, &window)) {
                      char tmpText[200];
                      snprintf(tmpText, sizeof(tmpText),
                               "Invalid window size: %s", optarg);
                      short_usage(argv[0], tmpText);
                    }
                    set_buff = 1;
                    break;
                  case 'd':
                    debug++;
                    break;
                  case 305:
                    options.snapDelay = atoi(optarg);
                    break;
                  case 'y':
                    options.limit = atoi(optarg);
                    break;
                  case 306:
                    options.avoidSndBlockUp = 1;
                    break;
                  case 308:
                    options.cwndDecrease = 1;
                  case 307:
                    options.snaplog = 1;
                    break;
                  case 309:
                    cputime = 1;
                    break;
                  case 310:
                    useDB = 1;
                    break;
                  case 311:
                    dbDSN = optarg;
                    break;
                  case 312:
                    dbUID = optarg;
                    break;
                  case 313:
                    dbPWD = optarg;
                    break;
                  case 'T':
                    refresh = atoi(optarg);
                    break;
                  case 'A':
                    AdminFileName = optarg;
                    break;
                  case 'L':
                    DataDirName = optarg;
                    break;
                  case 'S':
                    SysLogFacility = optarg;
                    break;
                  case 314:
                    options.tls = 1;
                    break;
                  case 315:
                    private_key_file = optarg;
                    break;
                  case 316:
                    certificate_file = optarg;
                    break;
                  case '?':
                    short_usage(argv[0], "");
                    break;
                }
              }

  if (options.tls) {
    if (private_key_file == NULL || certificate_file == NULL) {
      short_usage(argv[0], "TLS requires a private key and a certificate");
    }

    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    ssl_context = SSL_CTX_new(SSLv23_server_method());
    if (ssl_context == NULL) {
      ssl_err = ERR_get_error();
      ERR_error_string_n(ssl_err, ssl_error, sizeof(ssl_error));
      printf("SSL_CTX_new: %s\n", ssl_error);
      short_usage(argv[0], "SSL/TLS context could not be created.");
    }
    SSL_CTX_set_mode(ssl_context, SSL_MODE_AUTO_RETRY);
    // SSL private key file initialization goes here
    if (SSL_CTX_use_certificate_file(ssl_context, certificate_file,
                                     SSL_FILETYPE_PEM) != 1) {
      ssl_err = ERR_get_error();
      ERR_error_string_n(ssl_err, ssl_error, sizeof(ssl_error));
      printf("SSL_CTX_use_certificate_file: %s\n", ssl_error);
      short_usage(argv[0], "SSL/TLS certificate file could not be loaded.");
    }
    if (SSL_CTX_use_PrivateKey_file(ssl_context, private_key_file,
                                    SSL_FILETYPE_PEM) != 1) {
      ssl_err = ERR_get_error();
      ERR_error_string_n(ssl_err, ssl_error, sizeof(ssl_error));
      printf("SSL_CTX_use_PrivateKey_file: %s\n", ssl_error);
      short_usage(argv[0], "SSL/TLS private key file could not be loaded.");
    }
    if (!SSL_CTX_check_private_key(ssl_context)) {
      short_usage(argv[0], "Private key does go with certificate.");
    }
  }

  if (optind < argc) {
    short_usage(argv[0], "Unrecognized non-option elements");
  }

  if (debug > get_debuglvl()) {
    set_debuglvl(debug);
  }

  testopt.multiple = multiple;

  // First check to see if program is running as root.  If not, then warn
  // the user that link type detection is suspect.  Then downgrade effective
  // userid to non-root user until needed.

  if (getuid() != 0) {
    log_print(
        0,
        "Warning: This program must be run as root to enable the Link Type");
    log_println(
        0,
        " detection algorithm.\nContinuing execution without this algorithm");
  }

  if (VarFileName == NULL) {
    snprintf(wvfn, sizeof(wvfn), "%s/%s", BASEDIR, WEB100_FILE);
    VarFileName = wvfn;
  }

  if (AdminFileName == NULL) {
    snprintf(apfn, sizeof(apfn), "%s/%s", BASEDIR, ADMINFILE);
    AdminFileName = apfn;
  }

  if (DataDirName == NULL) {
    snprintf(logd, sizeof(logd), "%s/%s/", BASEDIR, LOGDIR);
    DataDirName = logd;
  } else if (DataDirName[0] != '/') {
    snprintf(logd, sizeof(logd), "%s/%s/", BASEDIR, DataDirName);
    DataDirName = logd;
  }

  create_protolog_dir();

  if (SysLogFacility != NULL) {
    i = 0;
    while (facilitynames[i].c_name) {
      if (strcmp(facilitynames[i].c_name, SysLogFacility) == 0) {
        syslogfacility = facilitynames[i].c_val;
        break;
      }
      ++i;
    }
    if (facilitynames[i].c_name == NULL) {
      log_println(
          0,
          "Warning: Unknown syslog facility [%s] --> using default (%d)",
          SysLogFacility, syslogfacility);
      SysLogFacility = NULL;
    }
  }

  log_println(1, "ANL/Internet2 NDT ver %s", VERSION);
  log_println(1, "\tVariables file = %s\n\tlog file = %s", VarFileName,
              get_logfile());
  if (admin_view) {
    log_println(1, "\tAdmin file = %s", AdminFileName);
  }
  if (usesyslog) {
    log_println(1, "\tSyslog facility = %s (%d)",
                SysLogFacility ? SysLogFacility : "default", syslogfacility);
  }
  log_println(1, "\tDebug level set to %d", debug);

  initialize_db(useDB, dbDSN, dbUID, dbPWD);

  memset(&new, 0, sizeof(new));
  new.sa_handler = cleanup;

  // Grab all signals and run them through the cleanup routine.
  for (i = 1; i < 32; i++) {
    if ((i == SIGKILL) || (i == SIGSTOP))
      continue;
    sigaction(i, &new, NULL);
  }

  // Bind our local address so that the client can send to us.

  if (srcname && !(listenaddr = I2AddrByNode(get_errhandle(), srcname))) {
    err_sys("server: Invalid source address specified");
  }
  if ((listenaddr =
          CreateListenSocket(listenaddr, port, conn_options,
                             ((set_buff > 0) ? window : 0))) == NULL) {
    err_sys("server: CreateListenSocket failed");
  }
  listenfd = I2AddrFD(listenaddr);

  if (listenfd == -1) {
    log_println(0, "ERROR: Socket already in use.");
    return 0;
  }

  log_println(1, "server ready on port %s (family %d)", port, meta.family);

  // Initialize tcp_stat structures
  count_vars = tcp_stat_init(VarFileName);
  if (count_vars == -1) {
    log_println(0, "No Web100 variables file found, terminating program");
    exit(-5);
  }

  //   The administrator view automatically generates a usage page for the
  //    NDT server.  This page is then accessible to the general public.
  //    At this point read the existing log file and generate the necessary
  //    data.  This data is then updated at the end of each test.
  //    RAC 11/28/03

  if (admin_view == 1)
    view_init(refresh);

  // Get the server's metadata info (OS name and kernel version
  // RAC 7/7/09

  if ((fp = fopen("/proc/sys/kernel/hostname", "r")) == NULL) {
    log_println(0, "Unable to determine server Hostname.");
  } else {
    fscanf(fp, "%s", meta.server_name);
    fclose(fp);
  }
  if ((fp = fopen("/proc/sys/kernel/osrelease", "r")) == NULL) {
    log_println(0, "Unable to determine server OS Version.");
  } else {
    fscanf(fp, "%s", meta.server_os);
    fclose(fp);
  }

  // create a log file entry every time the web100srv process starts up

  ndtpid = getpid();
  tt = time(0);
  log_println(6, "NDT server (v%s) process [%d] started at %15.15s", VERSION,
              ndtpid, ctime(&tt) + 4);
  fp = fopen(get_logfile(), "a");
  if (fp == NULL) {
    log_println(0,
                "Unable to open log file '%s', continuing on without logging",
                get_logfile());
  } else {
    fprintf(fp, "Web100srv (ver %s) process (%d) started %15.15s\n",
            VERSION, ndtpid, ctime(&tt) + 4);
    fclose(fp);
  }
  if (usesyslog == 1)
    syslog(LOG_FACILITY | LOG_INFO, "Web100srv (ver %s) process started",
           VERSION);

  options.compress = compress;
  // These flags keep track of running processes.  The 'testing' flag
  // indicates a test is currently being performed.  The 'waiting' counter
  // shows how many tests are pending.
  testing = 0;
  mclients = 0;
  waiting = 0;
  loopcnt = 0;
  head_ptr = NULL;
  sig17 = 0;
  sem_init(&ndtq, 0, 1);
  NDT_server_main_loop(ssl_context, listenfd);
  return 0;
}
#endif  // USE_WEB100SRV_ONLY_AS_LIBRARY

/**
 * Method to get remote host's address.
 * @return remote host's address
 * */
char *get_remotehostaddress() { return rmt_addr; }
