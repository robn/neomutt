#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#define MUTT_LAUNCH_C
void mutt_launch_usage(void)
{
  puts("mutt_launch: a utility for launching attachment viewer programs in the "
       "background.");
  puts("\tusage: mutt_launch <command>\n\
  \t<command>\tthe viewer command line to launch\n");
}

int main(int argc, char **argv)
{
  int execreturn;
  int devnullfd;
  pid_t thepid;
  char **execline;
  struct rlimit fdlimit;
  int close_result;

  if (argc <= 1)
  {
    mutt_launch_usage();
    exit(1);
  }

  /* fdlimit.rlim_cur a value one greater than the maximum file descriptor number that can be opened by this process */
  getrlimit(RLIMIT_NOFILE, &fdlimit);
  for (int fd = 3; fd < fdlimit.rlim_cur; fd++)
  {
    if (!close(fd) && errno != EBADF)
    {
      fprintf(stderr,
              "mutt_launch: Unable to close filedescriptor %d. Error: %s\n", fd,
              strerror(errno));
      exit(1);
    }
  }

  execline = calloc(argc + 2, sizeof(char *));
  execline[0] = malloc(strlen("/bin/sh"));
  strcpy(execline[0], "/bin/sh");
  execline[1] = malloc(strlen("-c"));
  strcpy(execline[1], "-c");

  for (int i = 1; i < argc; i++)
  {
    execline[i + 1] = argv[i];
  }

  if ((thepid = fork()) == 0)
  {
    if ((devnullfd = open("/dev/null", O_WRONLY)) == -1)
    {
      fprintf(stderr, "mutt_launch: Unable to open /dev/null. Error: %s\n",
              errno, strerror(errno));
      exit(1);
    }

    if (dup2(devnullfd, 0) == -1 || dup2(devnullfd, 1) == -1 || dup2(devnullfd, 2) == -1)
    {
      printf("error closing stdio or duplicating devnull\n");
      exit(1);
    }


    if ((execreturn = execvp(execline[0], execline)) == -1)
    {
      fprintf(stderr, "Unable to execute command Error: %d - %s", errno, strerror(errno));
      exit(1);
    }
  }
  else
  {
    printf("Viewer process spawned `");
    for (int i = 0; i < argc + 1; i++)
    {
      printf("%s ", execline[i]);
    }
    printf("\b` with pid %d\n", thepid);
  }
  return 0;
}
