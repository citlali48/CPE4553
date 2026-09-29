#ifndef SCHEDULER_H
#define SCHEDULER_H
#include <stdio.h>
#include <stdlib.h>

/*
a process identifier: any unique natural number.
a process priority: natural number from 0 to 127, with lower values indicating higher priority.
a process binary file
and optional parameters for the binary file.
*/
typedef struct{
    int id;
    int priority;
    char* fname;
    char** params;
    int param_cnt;
} Process;

#endif