#include <sys/types.h> 
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

sigset_t signalset;

//gcc -O3 -D_POSIX_C_SOURCE=200809 -Wall -std=c11 shell.c myshell.c

//reseting SIGINT handling to default, used by foreground child processes
void reset_sigint_handler() {
	struct sigaction term;
	term.sa_handler = SIG_DFL;
	sigemptyset(&term.sa_mask);
	term.sa_flags = SA_RESTART;
	if (sigaction(SIGINT, &term, 0) != 0) {
		fprintf(stderr, "Signal handle registration " "failed. %s\n", strerror(errno));
		exit(1);
	}
}

//reaping zombies code from http://www.microhowto.info/howto/reap_zombie_processes_using_a_sigchld_handler.html
void handle_sigchld(int sig) {
	int saved_errno = errno;
	int wait_ret;
	while ((wait_ret=waitpid((pid_t)(-1), 0, WNOHANG)) > 0) {}
	if(wait_ret==-1 && errno!=EINTR && errno!=ECHILD){
		fprintf(stderr, "wait failed in signal handler, (zombie reaping) Error: %s\n", strerror(errno));
		exit(1); 
	}
	errno = saved_errno;
}

// prepare and finalize calls for initialization and destruction of anything required
int prepare(void) {
	sigemptyset(&signalset);
	sigaddset(&signalset, SIGINT);
	//zombie slaying code from http://www.microhowto.info/howto/reap_zombie_processes_using_a_sigchld_handler.html
	struct sigaction reaper;
	reaper.sa_handler = &handle_sigchld;
	sigemptyset(&reaper.sa_mask);
	reaper.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	if (sigaction(SIGCHLD, &reaper, 0) != 0) {
		fprintf(stderr, "Signal handle registration " "failed. %s\n", strerror(errno));
		return -1;
	}
	//making shell ignore SIGINT, note this means every foreground child should update it's handler for SIGINT
	struct sigaction ignore;
	ignore.sa_handler = SIG_IGN;
	sigemptyset(&ignore.sa_mask);
	ignore.sa_flags = SA_RESTART;
	if (sigaction(SIGINT, &ignore, 0) != 0) {
		fprintf(stderr, "Signal handle registration " "failed. %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

// checks for pipe flag ,returns its index or 0 if there is no flag (can't be first in valid input)
int contains_pipe(char** arglist) {
	int index = 0;
	while (arglist[index] != NULL) {
		if (strcmp(arglist[index], "|") == 0) {
			return index;
		}
		index++;
	}
	return 0;
}

// implementation of piping process return 0 if any error occures in the parent process and 1 if none do.
// errors in child processes will only print to stderr and do not effect return value.
int piping(int count, char** arglist, int index) {
	int pid1, pid2, status,wait_ret;
	int pfds[2];
	if (pipe(pfds) == -1) {
		fprintf(stderr, "pipe failed, Error: %s\n", strerror(errno));
		return 0;
	}
	arglist[index] = NULL;

	sigprocmask(SIG_BLOCK, &signalset, NULL);
	pid1 = fork();
	if (pid1 == 0) { // executing first command as a child process inserting output to the pipe
		reset_sigint_handler();
		sigprocmask(SIG_UNBLOCK, &signalset, NULL);
		close(pfds[0]);
		if (dup2(pfds[1], 1) == -1) {
			fprintf(stderr, "dup2 failed, Error: %s\n", strerror(errno));
			exit(1);
		}
		close(pfds[1]);
		execvp(arglist[0], arglist);
		fprintf(stderr, "execvp returned, Error : %s\n", strerror(errno));
		exit(1);
	}
	sigprocmask(SIG_UNBLOCK, &signalset, NULL);
	if (pid1 < 0) {
		fprintf(stderr, "fork failed, Error: %s\n", strerror(errno));
		return 0;
	}

	sigprocmask(SIG_BLOCK, &signalset, NULL);
	pid2 = fork();
	if (pid2 == 0) { // executing second command as a child process taking input from the pipe
		reset_sigint_handler();
		sigprocmask(SIG_UNBLOCK, &signalset, NULL);
		close(pfds[1]);
		if (dup2(pfds[0], 0) == -1) {
			fprintf(stderr, "dup2 failed, Error: %s\n", strerror(errno));
			exit(1);
		}
		close(pfds[0]);
		execvp(arglist[index + 1], arglist + (index + 1));
		fprintf(stderr, "execvp returned, Error : %s\n", strerror(errno));
		exit(1);
	}
	sigprocmask(SIG_UNBLOCK, &signalset, NULL);
	if (pid2 < 0) {
		fprintf(stderr, "fork failed, Error: %s\n", strerror(errno));
		return 0;
	}

	close(pfds[0]);
	close(pfds[1]);
	wait_ret=waitpid(pid1, &status, 0);
	if(wait_ret==-1 && errno!=EINTR && errno!=ECHILD){
		fprintf(stderr, "wait failed, Error: %s\n", strerror(errno));
		return 0;
	}
	wait_ret=waitpid(pid2, &status, 0);
	if(wait_ret==-1 && errno!=EINTR && errno!=ECHILD){
		fprintf(stderr, "wait failed, Error: %s\n", strerror(errno));
		return 0;
	}
	return 1;
}

// arglist - a list of char* arguments (words) provided by the user
// it contains count+1 items, where the last item (arglist[count]) and *only* the last is NULL
// RETURNS - 1 if should continue, 0 otherwise
int process_arglist(int count, char** arglist) {
	int index, pid, status, wait_ret;
	// backgroung process: 
	if (strcmp(arglist[count - 1], "&") == 0) {
		pid = fork();
		if (pid == 0) { // executing as a child process
			arglist[count - 1] = NULL;
			execvp(arglist[0], arglist);
			fprintf(stderr, "execvp returned, Error : %s\n", strerror(errno));
			exit(1);
		}
		if (pid < 0) {
			fprintf(stderr, "fork failed, Error: %s\n", strerror(errno));
			return 0;
		}
		return 1;
	}
	// piping process: 
	index = contains_pipe(arglist);
	if (index != 0) {
		return piping(count, arglist, index);
	}
	// normal process: 
	sigprocmask(SIG_BLOCK, &signalset, NULL);
	pid = fork();
	if (pid == 0) { // executing as a child process
		reset_sigint_handler();
		sigprocmask(SIG_UNBLOCK, &signalset, NULL);
		execvp(arglist[0], arglist);
		fprintf(stderr, "execvp returned, Error: %s\n", strerror(errno));
		exit(1);
	}
	sigprocmask(SIG_UNBLOCK, &signalset, NULL);
	if (pid < 0) {
		fprintf(stderr, "fork failed, Error: %s\n", strerror(errno));
		return 0;
	}
	wait_ret=waitpid(pid, &status, 0);
	if(wait_ret==-1 && errno!=EINTR && errno!=ECHILD){
		fprintf(stderr, "wait failed, Error: %s\n", strerror(errno));
		return 0;
	}
	return 1;
}

int finalize(void) {
	return 0;
}