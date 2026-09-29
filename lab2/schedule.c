#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/time.h>
#include "scheduler.h"
#include "pqueue.h"

//all timer handlers, and helpers
static volatile sig_atomic_t quant_expired = 0; //flag

static void alarm_handler(int sig){
    (void)sig;
    quant_expired = 1;
}

static void start_time(long quant){
    struct itimerval timer = {0};

    timer.it_value.tv_sec = quant / 1000; //put into seconds
    timer.it_value.tv_usec = (quant % 1000) * 1000; //put into microseconds
    //use ITMER_REAL so calculates real time, won't stop on wait
    if(setitimer(ITIMER_REAL, &timer, NULL) == -1){
        printf("Timer start failed.\n");
        exit(1);
    }
}

static void stop_time(){
    struct itimerval timer = {0};

    if(setitimer(ITIMER_REAL, &timer, NULL) == -1){
        printf("Timer start failed.\n");
        exit(1);
    }
}

/*
Your scheduler program should expect two parameters:
A time quantum in milliseconds
A tab-separated file where each line in the file represents a process defined as follows:
    a process identifier: any unique natural number.
    a process priority: natural number from 0 to 127, with lower values indicating higher priority.
    a process binary file
    and optional parameters for the binary file.
*/

int main(int argc, char* argv[]){
    if(argc != 3){
        printf("Invalid number of arguments. Arguments follow format: time-quantum file-name\n");
        return 1;
    }

    //from man pages, strtol to get time quantum into long int
    char *endptr, *str;
    long quantum;
    str = argv[1];

    errno = 0;    /* To distinguish success/failure after call */
    quantum = strtol(str, &endptr, 10);

    /* Check for various possible errors. */
    if (errno == ERANGE) {
        perror("strtol");
        exit(EXIT_FAILURE);
    }

    if (endptr == str) {
        fprintf(stderr, "No digits were found\n");
        exit(EXIT_FAILURE);
    }

    //open file for reading processes
    FILE *file;
    file = fopen(argv[2], "r");

    if (file == NULL){
        printf("Could not open file %s\n", argv[2]);
        return 1;
    }

    //let's try a max of 10 parameters
    char** params = malloc(10 * sizeof(char *));

    char buff[256]; //hopefully won't exceed this
    while(fgets(buff, sizeof(buff), file) != NULL){ //read line at a time
        int param_idx = 0;
        const char* delim = " \t\n"; //get rid of whitespaces
        char* token = strtok(buff, delim); //have to get first token and then loop

        while(token != NULL){
            params[param_idx] = token;
            token = strtok(NULL, delim); //all calls after use null
            param_idx++;
        }

        //assign params/values to a process struct
        Process *proc = malloc(sizeof(Process));
        char** proc_params = malloc(5 * sizeof(char *)); //don't think we'll need 5

        proc->id = strtol(params[0], NULL, 10);
        proc->priority = strtol(params[1], NULL, 10);
        proc->fname = strdup(params[2]);
        proc->param_cnt = param_idx - 3;

        for (int i = 0; i < proc->param_cnt; i++){
            //extra func params for process
            proc_params[i] = strdup(params[i+3]); //since first 3 not func params
        }

        proc->params = proc_params;

        //fork child for process
        pid_t pid = fork();

        if(pid == 0){
            raise(SIGSTOP); //stops here, continues after

            char path[128];
            snprintf(path, 128, "./%s", proc->fname);
            char **args = malloc((proc->param_cnt + 2) * sizeof(char*)); //for null term, and file path
            args[0] = path;
            for(int i = 0; i < proc->param_cnt; i++){
                args[i+1] = proc->params[i];
            }

            args[proc->param_cnt + 1] = NULL; //null terminate for execvp

            execvp(path, args);
            printf("Execvp failed\n");
            exit(1); //if execvp failed
        } else {
            int status;
            if(waitpid(pid, &status, WUNTRACED) == -1){
                printf("Waitpid failed.\n");
                exit(1);
            }

            proc->pid = pid;
        }

        //start creating nodes for linked list, and push into pqueue
        Node *cur_node = malloc(sizeof(Node));
        cur_node->process = proc;
        cur_node->next = NULL;
        push(cur_node);
    }

    //set up signal handler for timer going off
    struct sigaction sa = {0};
    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);

    if(sigaction(SIGALRM, &sa, NULL) == -1){
        printf("Sigaction failed.\n");
        exit(1);
    }

    //running the pqueue
    //run first process to completion
    Process *first_proc = pop();
    kill(first_proc->pid, SIGCONT); //continue the process
    int status;
    waitpid(first_proc->pid, &status, WUNTRACED); //wait on completion or sigalrm

    Process *proc;
    while((proc = pop()) != NULL){ //while still have stuff in pqueue
        Process *next = peek(); //for comparison of priority
        quant_expired = 0; //reset flags

        if(next != NULL && next->priority == proc->priority){ //if equal priority, start time quantum
            start_time(quantum);
        }

        kill(proc->pid, SIGCONT); //continue the process

        int status;
        pid_t wait_proc = waitpid(proc->pid, &status, WUNTRACED); //wait on completion or sigalrm
        stop_time();

        if(wait_proc == -1 && quant_expired){ //if interrupted by sigalrm, and flags set
            kill(proc->pid, SIGSTOP); //pause process

            //create node, process, add back to pqueue
            Node *node = malloc(sizeof(Node));
            node->process = proc;
            push(node);
        }
    }
}