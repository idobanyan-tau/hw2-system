#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>

int prepare(void){
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = child_handler;
    if(sigaction(SIGCHLD, &sa, NULL) == -1){
        fprintf(stderr, "Error in signal handler initialization, Error: %s\n", strerror(errno));
        exit(1);
    }
    signal(SIGCHLD, SIG_IGN);
    struct sigaction s_action;
    signal(SIGINT, SIG_DFL);
    s_action.sa_handler = SIG_IGN;
    s_action.sa_flags   = SA_RESTART;
    if (sigaction(SIGINT, &s_action, NULL) == -1)
    {
        fprintf(stderr, "Error in signal handler initialization, Error: %s\n", strerror(errno));
        exit(1);
    }
    return 0;
}

int defult_signal_handler(){
    struct sigaction example_handler;
    signal(SIGINT, SIG_DFL);
    s_action.sa_handler = SIG_DFL;
    s_action.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &example_handler, NULL) == -1)
    {
        fprintf(stderr, "Error in signal handler initialization, Error: %s\n", strerror(errno));
        exit(1);
    }
}

int exe_command(int count, char **arglist){
    int pid = fork();
    int status;
    if (pid == 0){
        defult_signal_handler();
        execvp(arglist[0], arglist);
        fprintf(stderr, "Error in execvp function, Error: %s\n", strerror(errno));
        exit(1);
    }
    else if (pid != -1){
        int waiting = waitpid(pid, &status, 0);
        if(wait_value == -1 && errno != ECHILD && errno != EINTR) {
            fprintf(stderr, "Error in wait function, Error: %s\n", strerror(errno));
            return 0;
        }
        return 1;
    }
    else{
        fprintf(stderr, "Error in fork function, Error: %s\n", strerror(errno));
    }
    return 0;
}

int exe_back_command(int count, char **arglist){
    int pid = fork();
    if (pid == 0){
        arglist[count-1] = NULL // from the assumptions the last char ism '&'
        execvp(arglist[0], arglist);
        fprintf(stderr, "Error in execvp function, Error: %s\n", strerror(errno));
        exit(1);
    }
    else if (pid == -1){
        fprintf(stderr, "Error in fork function, Error: %s\n", strerror(errno));
        return 0;
    }
    return 1;
}

int exe_single_pipe_command(int count, char **arglist, int loc){
    int fd[2];
    int status1;
    int status2;
    if (pipe(fd) == -1){
        fprintf(stderr, "Error in pipe function, Error: %s\n", strerror(errno));
        return 0;
    }
    int pid1 = fork();
    if (pid1 < 0){
        fprintf(stderr, "Error in fork function, Error: %s\n", strerror(errno));
        return 0;
    }
    if (pid1 == 0){
        // Child process for the first command arg
        defult_signal_handler();
        if (dup2(fd[1], 1) == -1){
            fprintf(stderr, "Error in dup2 function, Error: %s\n", strerror(errno));
            return 0;
        }

        close(fd[0]);
        close(fd[1]);

        execvp(arglist[0], arglist);
        fprintf(stderr, "Error in execvp function, Error: %s\n", strerror(errno));
        exit(1);
    }

    int pid2 = fork();
    if (pid2 < 0){
        fprintf(stderr, "Error in fork function, Error: %s\n", strerror(errno));
        return 0;
    }
    if (pid2 == 0){
        // Child process for the second command arg
        defult_signal_handler();
        if (dup2(fd[0], 0) == -1){
            fprintf(stderr, "Error in dup2 function, Error: %s\n", strerror(errno));
            return 0;
        }

        close(fd[0]);
        close(fd[1]);

        execvp(arglist[loc], arglist + loc);
        fprintf(stderr, "Error in execvp function, Error: %s\n", strerror(errno));
        exit(1);
    }

    int waiting = waitpid(pid1, &status1, 0);
    if(wait_value == -1 && errno != ECHILD && errno != EINTR) {
        fprintf(stderr, "Error in wait function, Error: %s\n", strerror(errno));
        return 0;
    }
    int waiting = waitpid(pid2, &status2, 0);
    if(wait_value == -1 && errno != ECHILD && errno != EINTR) {
        fprintf(stderr, "Error in wait function, Error: %s\n", strerror(errno));
        return 0;
    }
    return 1;
}

int exe_redirect_command(int count, char **arglist){
    int pid = fork();
    if(pid == 0){
        defult_signal_handler();
        int file = open(arglist[count-1], O_CREAT|O_TRUNC|O_RDWR, S_IRUSR|S_IWUSR);
        if(dup2(file_desc, 1) == -1){
            fprintf(stderr, "Error in dup2 function, Error: %s\n", strerror(errno));
            return 0;
        }
        close(file_desc);
        arglist[count-2] = NULL;
        execvp(arglist[0], arglist);
        fprintf(stderr, "Error in execvp function, Error: %s\n", strerror(errno));
        exit(1);
    }
    else if(fork_value > 0) {
        return 1;
    }
    return 0;

}
int process_arglist(int count, char **arglist){
    char symbol = '';
    int loc = -1;
    for(int i=0; i<count; i++){
        if(strcmp(arglist[i], "&") == 0) {
            symbol = '&';
        }
        else if(strcmp(arglist[i], "|") == 0) {
            symbol = '|';
            loc = i + 1;
        }
        else if(strcmp(arglist[i], ">") == 0) {
            symbol = '>'
        }
    }

    if (symbol == ''){
        exe_command(count, arglist);
    }
    else if (symbol == '&'){
        exe_back_command(count, arglist);
    }
    else if (symbol == '|'){
        exe_single_pipe_command(count, arglist, loc);
    }
    else if (symbol == '>'){
        exe_redirect_command(count, arglist)
    }

}

int finalize(void){
    return 0;
}