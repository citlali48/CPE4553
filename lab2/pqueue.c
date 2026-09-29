#include "pqueue.h"
#include <unistd.h>

Node* head = NULL;

void push(Node* node){
    if(head == NULL){//check if no head
        head = node;
        return;
    } else if(node->process->priority < head->process->priority){//if highest priority
        node->next = head;
        head = node;
        return;
    }

    Node *cur_node = head;
    while(cur_node->next != NULL && cur_node->next->process->priority <= node->process->priority) {
        cur_node = cur_node->next;
    }

    //place between current next node and current node
    node->next = cur_node->next;
    cur_node->next = node;
}

Process* pop(){
    if(head == NULL){
        return NULL;
    }

    Node *temp = head;
    Process *process = temp->process;
    head = head->next;
    free(temp);

    return process;
}

Process* peek(){
    if(head == NULL){
        return NULL;
    }

    return head->process;
}