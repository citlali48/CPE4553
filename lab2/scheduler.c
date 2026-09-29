#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "scheduler.h"
#include "pqueue.h"

/*
Your scheduler program should expect two parameters:
A time quantum in milliseconds
A tab-separated file where each line in the file represents a process defined as follows:
    a process identifier: any unique natural number.
    a process priority: natural number from 0 to 127, with lower values indicating higher priority.
    a process binary file
    and optional parameters for the binary file.
Your file can have any number of processes with any arbitrary number of parameters. An example for the input file:
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

    /*
    Want to read:
        a process identifier: any unique natural number.
        a process priority: natural number from 0 to 127, with lower values indicating higher priority.
        a process binary file
        and optional parameters for the binary file.
    Essentially, need to read at least 3 params but a variable number of parameters afterwards, can't we just read until a newline?
    And save all params into param char* that is malloc()d, and when reading extra parameters we can count them (after we've gotten first 3)
    to see how many parameters we read into the array so we can only take out those first couple indexes of parameters (since mallocd)
    */

    //let's try a max of 10 parameters
    char** params = malloc(10 * sizeof(char *));

    char buff[256]; //hopefully won't exceed this
    Node* head = NULL;
    while(fgets(buff, sizeof(buff), file) != NULL){ //read line at a time
        int param_idx = 0;
        const char* delim = " \t\n"; //get rid of whitespaces
        char* token = strtok(buff, delim); //have to get first token and then loop
        //params[param_idx] = token;
        //param_idx++;

        while(token != NULL){
            if(param_idx > 9){
                //TO DO: need to realloc
                break;
            }
            params[param_idx] = token;
            printf("%s\n", token);
            token = strtok(NULL, delim); //all calls after use null
            param_idx++;
        }

        //assign params/values to a process struct
        Process *proc = malloc(sizeof(Process));
        char** proc_params = malloc(5 * sizeof(char *)); //don't think we'll need 5

        proc->id = strtol(params[0], NULL, 10);
        proc->priority = strtol(params[1], NULL, 10);
        proc->fname = strdup(params[2]);
        printf("Process file name: %s\n", proc->fname);
        proc->param_cnt = param_idx - 3;

        for (int i = 0; i < proc->param_cnt; i++){
            //extra func params for process
            proc_params[i] = strdup(params[i]);
        }

        proc->params = proc_params;
        printf("1 line read, 1 process created\n");

        //start creating nodes for linked list
        Node *cur_node = malloc(sizeof(Node));
        cur_node->process = proc;
        //**CHANGE THIS LOGIC LATER IN PQUEUE IMPLEMENTATION**
        if(head == NULL){ //check if head
            head = cur_node;
        } else {
            Node *start = head;
            while(start->next != NULL) {
                start = start->next;
            }

            start->next = cur_node;
        }

    //read line by line and parse into arguments
    //use fread
    //create a process variable, and pass into a node variable to create the priority queue
    //cur node, and head node variables declared
    //while reading
        //parse arguments
        //create process with arguments
        //create node for process, null next
        //check if head is null still
            //set node as head
        //else
            //push to priority queue (using pqueue funcs)
    }
    //once done reading, start running processes in pqueue
    //runs first process to completion
    //for rest set timer for time quantum
        //fork for process
        //pop off priority queue
        //use execvp or other exec to run the process
    //if interrupted? have to add back to priority queue, check sigint stuff

    //process execution, running the pqueue
    //first see if execvp and running any of these programs actually works
    
}