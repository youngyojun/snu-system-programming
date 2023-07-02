/* 
 * tsh - A tiny shell program with job control
 * 
 * 2020-14378
 * stu70@sp01.snucse.org
 *
 * Gyojun Youn
 * youngyojun [at] snu.ac.kr
 * 
 * College of Engineering
 * Dept. of Computer Science and Engineering
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE   1024      /* max line size */
#define MAXARGS   128       /* max args on a command line */
#define MAXJOBS   16        /* max jobs at any point in time */
#define MAXJID    (1<<16)   /* max job ID */

/* Job states */
#define UNDEF 0   /* undefined */
#define FG    1   /* running in foreground */
#define BG    2   /* running in background */
#define ST    3   /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {            /* The job struct */
  pid_t pid;              /* job PID */
  int jid;                /* job ID [1, 2, ...] */
  int state;              /* UNDEF, BG, FG, or ST */
  char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
struct job_t *addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

size_t safe_strlen(const char *str);
void safe_reverse(char *str);
void safe_ltoa(long v, char *str);
ssize_t safe_write(const char *str);
ssize_t safe_printlong(const long n);
void safe_error(const char *str);
void safe_unix_error(char *msg);
void safe_print_sigend(pid_t pid, int jid, int sig, int termed);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
void app_error_printf(const char *format, ...);

void print_bgfg_noarg(const char *cmd);
void print_bgfg_notint(const char *cmd);
void print_bgfg_nojob(const char *arg);
void print_bgfg_noproc(pid_t pid);
void printjob(const struct job_t *job);

typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
  char c;
  char cmdline[MAXLINE];
  int emit_prompt = 1; /* emit prompt (default) */

  /* Redirect stderr to stdout (so that driver will get all output
   * on the pipe connected to stdout) */
  dup2(1, 2);

  /* Parse the command line */
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
      case 'h':							/* print help message */
        usage();
        break;
      case 'v':             /* emit additional diagnostic info */
        verbose = 1;
        break;
      case 'p':             /* don't print a prompt */
        emit_prompt = 0;		/* handy for automatic testing */
        break;
      default:
        usage();
    }
  }

  /* Install the signal handlers */

  /* These are the ones you will need to implement */
  Signal(SIGINT , sigint_handler );		/* ctrl-c */
  Signal(SIGTSTP, sigtstp_handler);		/* ctrl-z */
  Signal(SIGCHLD, sigchld_handler);		/* Terminated or stopped child */

  /* This one provides a clean way to kill the shell */
  Signal(SIGQUIT, sigquit_handler); 

  /* Initialize the job list */
  initjobs(jobs);

  /* Execute the shell's read/eval loop */
  while (1) {
    /* Read command line */
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }
    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
      app_error("fgets error");
    if (feof(stdin)) { /* End of file (ctrl-d) */
      fflush(stdout);
      exit(0);
    }

    /* Evaluate the command line */
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  exit(0); /* control never reaches here */
}

/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
 */
void eval(char *cmdline) 
{
  char *argv[MAXARGS];
  pid_t pid;    /* process id */
  int bg;       /* background job? */

  /* Parse the command line. */
  bg = parseline(cmdline, argv);

  /* Empty line is given, nothing to do. */
  if (NULL == argv[0]) {
    return;
  }

  /* For built-in commands, we are done. */
  if (builtin_cmd(argv)) {
    return;
  }

  /* get full sigset */
  sigset_t sig_full;
  if (sigfillset(&sig_full) < 0) {
    unix_error("sigfillset error");
  }

  /* get sigset with only SIGCHLD */
  sigset_t sig_sigchld;
  if (sigemptyset(&sig_sigchld) < 0) {
    unix_error("sigemptyset error");
  }
  if (sigaddset(&sig_sigchld, SIGCHLD) < 0) {
    unix_error("sigaddset error");
  }

  /* Block SIGCHLD to fork. */
  sigset_t sig_prev;
  if (sigprocmask(SIG_BLOCK, &sig_sigchld, &sig_prev) < 0) {
    unix_error("sigprocmask error");
  }

  /* fork */
  if ((pid = fork()) < 0) {
    unix_error("fork error");
  }

  if (!pid) { /* child process */
    /* Restore sigset. */
    if (sigprocmask(SIG_SETMASK, &sig_prev, NULL) < 0) {
      unix_error("sigprocmask error");
    }

    /* Make child process run its own process group. */
    if (setpgid(0, 0) < 0) {
      unix_error("setpgid error");
    }

    /* run the user's job given */
    if (execve(argv[0], argv, environ) < 0) { /* never return if succeed */
      /* if it does, then the command given is unknown */
      app_error_printf("%s: Command not found\n", argv[0]);
    }

    exit(0); /* control never reaches here */
  }

  /* parent process */

  /* Block all signals before adding jobs. */
  if (sigprocmask(SIG_BLOCK, &sig_full, NULL) < 0) {
    unix_error("sigprocmask error");
  }

  struct job_t *job = addjob(jobs, pid, bg ? BG : FG, cmdline);

  /* Restore sigset. */
  if (sigprocmask(SIG_SETMASK, &sig_prev, NULL) < 0) {
    unix_error("sigprocmask error");
  }

  if (bg) { /* background job */
    /* print appropriate message */
    printjob(job);
  } else { /* foreground job */
    /* wait to be terminated */
    waitfg(pid);
  }

  return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
  static char array[MAXLINE]; /* holds local copy of command line */
  char *buf = array;          /* ptr that traverses command line */
  char *delim;                /* points to first space delimiter */
  int argc;                   /* number of args */
  int bg;                     /* background job? */

  strcpy(buf, cmdline);
  buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
  while (*buf && (*buf == ' ')) /* ignore leading spaces */
    buf++;

  /* Build the argv list */
  argc = 0;
  if (*buf == '\'') {
    buf++;
    delim = strchr(buf, '\'');
  }
  else {
    delim = strchr(buf, ' ');
  }

  while (delim) {
    argv[argc++] = buf;
    *delim = '\0';
    buf = delim + 1;
    while (*buf && (*buf == ' ')) /* ignore spaces */
      buf++;

    if (*buf == '\'') {
      buf++;
      delim = strchr(buf, '\'');
    }
    else {
      delim = strchr(buf, ' ');
    }
  }
  argv[argc] = NULL;

  if (argc == 0)  /* ignore blank line */
    return 1;

  /* should the job run in the background? */
  if ((bg = (*argv[argc-1] == '&')) != 0) {
    argv[--argc] = NULL;
  }
  return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
  /* quit */
  if (!strcmp(argv[0], "quit")) {
    exit(0);
  }

  /* fg and bg */
  if (!strcmp(argv[0], "fg") || !strcmp(argv[0], "bg")) {
    do_bgfg(argv);
    return 1;
  }

  /* jobs */
  if (!strcmp(argv[0], "jobs")) {
    listjobs(jobs);
    return 1;
  }

  return 0; /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
  /* fg and bg need job argument */
  if (NULL == argv[1]) {
    print_bgfg_noarg(argv[0]);
    return;
  }

  struct job_t *job;  /* job entry */
  pid_t pid;          /* process id */

  if ('%' == argv[1][0]) {  /* job id is given */
    int jid = atoi(argv[1] + 1);  /* job id */

    /* find the job */
    if (NULL == (job = getjobjid(jobs, jid))) { /* no such jobs */
      print_bgfg_nojob(argv[1]);
      return;
    }

    pid = job->pid;
  } else {  /* process id is given */
    /* pid must be numeric */
    if (!isdigit(argv[1][0])) {
      print_bgfg_notint(argv[0]);
      return;
    }

    pid = atoi(argv[1]);

    /* find the job */
    if (NULL == (job = getjobpid(jobs, pid))) { /* no such jobs */
      print_bgfg_noproc(pid);
      return;
    }
  }

  /* Note that argv[0] is either "bg" or "fg". */
  int bg = 'b' == argv[0][0]; /* bg command? */

  /* get full sigset */
  sigset_t sig_full;
  if (sigfillset(&sig_full) < 0) {
    unix_error("sigfillset error");
  }

  /* Block all signals. */
  sigset_t sig_prev;
  if (sigprocmask(SIG_BLOCK, &sig_full, &sig_prev) < 0) {
    unix_error("sigprocmask error");
  }

  /* Send SIGCONT signal to every process of the group. */
  if (kill(-pid, SIGCONT) < 0) {
    unix_error("kill error");
  }

  /* update the state of the job */
  if (bg) { /* bg command */
    job->state = BG;
    printjob(job);
  } else {  /* fg command */
    job->state = FG;
  }

  /* Restore sigset. */
  if (sigprocmask(SIG_SETMASK, &sig_prev, NULL) < 0) {
    unix_error("sigprocmask error");
  }

  /* for fg command, wait the process */
  if (!bg) {
    waitfg(pid);
  }
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
  struct job_t *job;

  while (NULL != (job = getjobpid(jobs, pid)) && FG == job->state) {
    if (sleep(1) < 0) {
      unix_error("sleep error");
    }
  }
}

/*****************
 * Signal handlers
 *****************/

/*
 * reap_zombie - Reap a zombie child process pid.
 *    Must used only for sigchld_handler.
 */
void reap_zombie(pid_t pid, int wstatus)
{
  /* process ended normally */
  if (WIFEXITED(wstatus)) {
    deletejob(jobs, pid);
    return;
  }

  /* process ended by an uncaught signal */
  if (WIFSIGNALED(wstatus)) {
    safe_print_sigend(pid, pid2jid(pid), WTERMSIG(wstatus), 1);
    deletejob(jobs, pid);
    return;
  }

  /* process is stopped */
  if (WIFSTOPPED(wstatus)) {
    /* check job is in the job list */
    struct job_t *job = getjobpid(jobs, pid);
    if (NULL != job) {
      safe_print_sigend(job->pid, job->jid, WSTOPSIG(wstatus), 0);

      /* set the state stopped */
      job->state = ST;
    }
    return;
  }
}

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
  int prev_errno = errno; /* store errno */

  /* get full sigset */
  sigset_t sig_full;
  if (sigfillset(&sig_full) < 0) {
    safe_unix_error("sigfillset error");
  }

  sigset_t sig_prev;  /* previous sigset for backup */
  pid_t pid;          /* process id to reap */
  int wstatus;        /* wait status */

  /* init errno to catch waitpid error */
  errno = 0;

  /* get stopped child process but do not wait for */
  while (0 < (pid = waitpid(-1, &wstatus, WUNTRACED | WNOHANG))) {
    /* Block all signals. */
    if (sigprocmask(SIG_BLOCK, &sig_full, &sig_prev) < 0) {
      safe_unix_error("sigprocmask error");
    }

    /* reap the process with appropriate message */
    reap_zombie(pid, wstatus);

    /* Restore sigset. */
    if (sigprocmask(SIG_SETMASK, &sig_prev, NULL) < 0) {
      safe_unix_error("sigprocmask error");
    }
  }

  /* detect waitpid error */
  if (pid < 0 && errno && ECHILD != errno) {
    safe_unix_error("waitpid error");
  }

  /* restore errno */
  errno = prev_errno;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
  int prev_errno = errno; /* store errno */

  pid_t pid = fgpid(jobs);  /* get foreground pid */

  if (0 < pid) {  /* must check pid is valid */
    /* send SIGINT signal */
    if (kill(-pid, SIGINT) < 0) {
      safe_unix_error("kill error");
    }
  }

  /* restore errno */
  errno = prev_errno;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
  int prev_errno = errno; /* store errno */

  pid_t pid = fgpid(jobs);  /* get foreground pid */

  if (0 < pid) {  /* must check pid is valid */
    /* send SIGTSTP signal */
    if (kill(-pid, SIGTSTP) < 0) {
      safe_unix_error("kill error");
    }
  }

  /* restore errno */
  errno = prev_errno;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
  job->pid = 0;
  job->jid = 0;
  job->state = UNDEF;
  job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
  int i;

  for (i = 0; i < MAXJOBS; i++)
    clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
  int i, max=0;

  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid > max)
      max = jobs[i].jid;
  return max;
}

/* addjob - Add a job to the job list */
struct job_t *addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
  int i;

  if (pid < 1)
    return NULL;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == 0) {
      jobs[i].pid = pid;
      jobs[i].state = state;
      jobs[i].jid = nextjid++;
      if (nextjid > MAXJOBS)
        nextjid = 1;
      strcpy(jobs[i].cmdline, cmdline);
      if(verbose){
        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
      }
      return &jobs[i];
    }
  }

  printf("Tried to create too many jobs\n");
  return NULL;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
  int i;

  if (pid < 1)
    return 0;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == pid) {
      clearjob(&jobs[i]);
      nextjid = maxjid(jobs)+1;
      return 1;
    }
  }
  return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
  int i;

  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].state == FG)
      return jobs[i].pid;
  return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
  int i;

  if (pid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].pid == pid)
      return &jobs[i];
  return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
  int i;

  if (jid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid == jid)
      return &jobs[i];
  return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
  int i;

  if (pid < 1)
    return 0;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].pid == pid) {
      return jobs[i].jid;
    }
  return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
  int i;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid != 0) {
      printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
      switch (jobs[i].state) {
        case BG: 
          printf("Running ");
          break;
        case FG: 
          printf("Foreground ");
          break;
        case ST: 
          printf("Stopped ");
          break;
        default:
          printf("listjobs: Internal error: job[%d].state=%d ", 
              i, jobs[i].state);
      }
      printf("%s", jobs[i].cmdline);
    }
  }
}
/******************************
 * end job list helper routines
 ******************************/


/*****************************
 * Signal-safe helper routines
 *****************************/

/*
 * safe_strlen - return the length of string
 */
size_t safe_strlen(const char *str)
{
  size_t i = 0;
  while (str[i]) i++;
  return i;
}

/*
 * safe_reverse - reverse the string
 */
void safe_reverse(char *str)
{
  int i = 0, j = (int)(safe_strlen(str)) - 1;
  char t;

  while (i < j) {
    t = str[i];
    str[i] = str[j];
    str[j] = t;
    i++; j--;
  }
}

/*
 * safe_ltoa - convert long to string (base 10)
 */
void safe_ltoa(long v, char *str)
{
  size_t i = 0;
  int neg = v < 0;

  if (neg) {
    v = -v;
  }

  do {
    str[i++] = '0' + v%10;
    v /= 10;
  } while(v);

  if (neg) {
    str[i++] = '-';
  }

  str[i] = 0;

  safe_reverse(str);
}

/*
 * safe_write - write the string
 */
ssize_t safe_write(const char *str)
{
  return write(STDOUT_FILENO, str, safe_strlen(str));
}

/*
 * safe_printlong - print long int
 */
ssize_t safe_printlong(const long n)
{
  char str[32];
  safe_ltoa(n, str);
  return safe_write(str);
}

/*
 * safe_error - signal-safe error routine
 */
void safe_error(const char *str)
{
  safe_write(str);
  _exit(1);
}

/*
 * safe_unix_error - signal-safe unix-style error routine
 */
void safe_unix_error(char *msg)
{
  char str_err[1024];
  strerror_r(errno, str_err, 1024);

  safe_write(msg);
  safe_write(": ");
  safe_write(str_err);
  safe_write("\n");

  _exit(1);
}

/*
 * safe_print_sigend - Print appropriate message
 *    when a child process is ended.
 *    'termed' is flagged when it is terminated,
 *    otherwise, it is stopped.
 */
void safe_print_sigend(pid_t pid, int jid, int sig, int termed) {
  safe_write("Job [");
  safe_printlong((long)jid);
  safe_write("] (");
  safe_printlong((long)pid);
  safe_write(") ");
  safe_write(termed ? "terminated" : "stopped");
  safe_write(" by signal ");
  safe_printlong((long)sig);
  safe_write("\n");
}
/*********************************
 * end signal-safe helper routines
 *********************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
  printf("Usage: shell [-hvp]\n");
  printf("   -h   print this message\n");
  printf("   -v   print additional diagnostic information\n");
  printf("   -p   do not emit a command prompt\n");
  exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
  fprintf(stdout, "%s: %s\n", msg, strerror(errno));
  exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
  fprintf(stdout, "%s\n", msg);
  exit(1);
}

/*
 * app_error_printf - application-style error routine with formatted string
 */
void app_error_printf(const char *format, ...)
{
  va_list args;
  va_start(args, format);

  vfprintf(stdout, format, args);

  va_end(args);

  exit(1);
}

/*
 * print_bgfg_noarg - Print appropriate message for do_bgfg
 *    when not enough arguments are given.
 */
void print_bgfg_noarg(const char *cmd)
{
  printf("%s command requires PID or %%jobid argument\n", cmd);
}

/*
 * print_bgfg_notint - Print appropriate message for do_bgfg
 *    when the given argument is a not integer.
 */
void print_bgfg_notint(const char *cmd)
{
  printf("%s: argument must be a PID or %%jobid\n", cmd);
}

/*
 * print_bgfg_nojob - Print appropriate message for do_bgfg
 *    when no such job is found.
 */
void print_bgfg_nojob(const char *arg)
{
  printf("%s: No such job\n", arg);
}

/*
 * print_bgfg_noproc - Print appropriate message for do_bgfg
 *    when no such process is found.
 */
void print_bgfg_noproc(pid_t pid)
{
  printf("(%d): No such process\n", pid);
}

/*
 * printjob - print the job info
 */
void printjob(const struct job_t *job)
{
  printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
  struct sigaction action, old_action;

  action.sa_handler = handler;  
  sigemptyset(&action.sa_mask); /* block sigs of type being handled */
  action.sa_flags = SA_RESTART; /* restart syscalls if possible */

  if (sigaction(signum, &action, &old_action) < 0)
    unix_error("Signal error");
  return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
  printf("Terminating after receipt of SIGQUIT signal\n");
  exit(1);
}
