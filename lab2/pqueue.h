#ifndef PQUEUE_H
#define PQUEUE_H
#include <stdio.h>
#include <stdlib.h>
#include "scheduler.h"

typedef struct{
    Process* process;
    struct Node* next;
} Node;

#endif