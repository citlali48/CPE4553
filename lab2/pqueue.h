#ifndef PQUEUE_H
#define PQUEUE_H
#include <stdio.h>
#include <stdlib.h>
#include "scheduler.h"

typedef struct Node{
    Process* process;
    struct Node* next;
} Node;

void push(Node* node);
Process* peak();

#endif