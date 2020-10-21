#pragma once

#include "sys/types.h"  // pid_t

#define	SIGKILL	9	// kill

typedef int sig_atomic_t;

int kill(pid_t pid, int sig);

typedef void (*sighandler_t)(int);

sighandler_t signal(int signum, sighandler_t sighandler);

#define SIG_DFL  ((sighandler_t)0)

#define SIGINT  (2)
