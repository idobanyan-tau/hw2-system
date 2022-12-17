#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>


#define PIPE_COMMAND 0
#define BACKGROUND_COMMAND 1
#define REDIRECT_COMMAND 2


void sigint_IGN(){
    struct sigaction s_action;
    signal(SIGINT, SIG_DFL);
    s_action.sa_handler = SIG_IGN;
    s_action.sa_flags   = SA_RESTART;
    if (sigaction(SIGINT, &s_action, NULL) == -1)
    {
        perror("Failed to ignore signal handler for SIGINT");
        exit(1);
    }
}

void sigint_DFL(){
    struct sigaction s_action;
    signal(SIGINT, SIG_DFL);
    s_action.sa_handler = SIG_DFL;
    s_action.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &s_action, NULL) == -1)
    {
        perror("Failed to reset signal handler for SIGINT");
        exit(1);
    }
}

void print_dup2_error_message(){
    perror("Failed to dup2");
    exit(1);
}


int redirect_command(int count, char** arglist){
    int fork_value = fork();
    if(fork_value == 0){
        sigint_DFL();
        int file_desc = open(arglist[count-1], O_CREAT|O_TRUNC|O_RDWR, S_IRUSR|S_IWUSR);
        if(dup2(file_desc, 1) == -1)
            print_dup2_error_message();
        close(file_desc);
        arglist[count-2] = NULL;
        execvp(arglist[0], arglist);
        perror("Failed to redirect command");
        exit(1);
    }
    else if(fork_value > 0) {
        return 1;
    }
    return 0;
}

static void child_handler(int sig){
    //Eran's trick
    pid_t pid;
    int status;
    while((pid = waitpid(-1, &status, WNOHANG)) > 0){}
}

void kill_zombies(){
    //Eran's trick
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = child_handler;
    if(sigaction(SIGCHLD, &sa, NULL) == -1){
        perror("Failed to kill zombies");
        exit(1);
    }
    signal(SIGCHLD, SIG_IGN);
}



int prepare(void){
    //Eran's trick
    kill_zombies();
    sigint_IGN();
    return 0;
}

int no_operator_command(int count, char** arglist){
    int fork_value = fork();
    int status;
    if(fork_value == 0) {
        sigint_DFL();
        execvp(arglist[0], arglist);
        perror("Failed to no operator command");
        exit(1);
    }
    else if(fork_value > 0) {
        int wait_value = waitpid(fork_value, &status, 0);
        if(wait_value == -1 && errno != ECHILD && errno != EINTR) {
            perror("Failed to wait");
            return 0;
        }
        return 1;
    }
    return 0;
}




int get_operator(int count, char** arglist){
    for(int i=0;i<count;++i){
        if(strcmp(arglist[i], "|") == 0) return PIPE_COMMAND;
        if(strcmp(arglist[i], "&") == 0) return BACKGROUND_COMMAND;
        if(strcmp(arglist[i], ">") == 0) return REDIRECT_COMMAND;
    }
    return 3;
}


int background_command(int count, char** arglist) {
    int fork_value = fork();
    if(fork_value == 0) {
        arglist[count-1] = NULL;
        execvp(arglist[0], arglist);
        perror("Failed to background command");
        exit(1);
    }
    else if(fork_value > 0) {
        return 1;
    }
    return 0;
}

int split_arglist(int count, char** arglist){
    for(int i=0;i<count;i++)
        if(strcmp(arglist[i], "|") == 0) {
            arglist[i] = NULL;
            return i;
        }
    return 0;
}

void print_error_message(){
    perror("Failed to pipe command");
    exit(1);
}


int pipe_command(int count, char** arglist){
    int idx_of_pipe;
    int status_first;
    int status_second;
    int fd[2];
    int pipe_value = pipe(fd);
    if(pipe_value == -1)
        print_error_message();
    idx_of_pipe = split_arglist(count, arglist);
    int fork_value_first = fork();
    int fork_value_second;
    if(fork_value_first == 0) {
        sigint_DFL();
        if(dup2(fd[1], 1) == -1)
            print_dup2_error_message();
        close(fd[0]);
        close(fd[1]);
        execvp(arglist[0], arglist);
        print_error_message();
    }
    else if(fork_value_first > 0){
        close(fd[1]);
        fork_value_second = fork();
        if(fork_value_second == 0) {
            sigint_DFL();
            if(dup2(fd[0], 0) == -1)
                print_dup2_error_message();
            close(fd[0]);
            execvp(arglist[idx_of_pipe + 1], arglist + idx_of_pipe + 1);
            print_error_message();
        }
        else if(fork_value_second > 0) {
            close(fd[0]);
            arglist[idx_of_pipe] = "|";
            int wait_value = waitpid(fork_value_first, &status_first, 0);
            if(wait_value == -1 && errno != ECHILD && errno != EINTR) {
                perror("Failed to wait");
                return 0;
            }
            wait_value = waitpid(fork_value_second, &status_second, 0);
            if(wait_value == -1 && errno != ECHILD && errno != EINTR){
                perror("Failed to wait");
                return 0;
            }
            return 1;
        }
        else
            print_error_message();
    }
    else if(fork_value_first < 0)
        print_error_message();
    return 0;
}



int process_arglist(int count, char** arglist){
    int operator = get_operator(count, arglist);
    if(operator == PIPE_COMMAND)
        return pipe_command(count, arglist);
    else if(operator == BACKGROUND_COMMAND)
        return background_command(count, arglist);
    else if(operator == REDIRECT_COMMAND)
        return redirect_command(count, arglist);
    else
        no_operator_command(count, arglist);
    return 1;
}


int finalize(void){
    return 0;
}
