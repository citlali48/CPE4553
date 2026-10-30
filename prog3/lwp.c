#include <unistd.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/mman.h>

#include "rr.h"
#include "lwp.h"

/* What we Need:
- current thread pointer context
- current scheduler pointer
- thread id count (what the newest one should be)
- list of all valid threads for tid2thread (lib_one and lib_two reserved for lib use)
    - Note: Using lib_one for this
- fifo queue of terminated threads
    - so lwp_wait() can return oldest terminated thread
    - Note: Using libtwo for this
- fifo queue of waiting threads
    - if no terminated threads lwp_wait() has to block, so deschedule thread
      that called lwp_wait() (with sched->remove()) and place in wait queue
    - Note: Using libtwo for this
*/

static scheduler sched = &rr_publish;
static tid_t tid_count = 1;
static thread cur_thread = NULL;

//list of valid threads for tid2thread
static thread valid_t = NULL;

//fifo queue pointers of terminated and waiting threads
static thread wait_head = NULL;
static thread wait_tail = NULL;
static thread exit_head = NULL;
static thread exit_tail = NULL;

//wrapper from instructions
static void lwp_wrap(lwpfun fun, void* arg) {
    /* 
     * Call the given function with the given argument
     * Call lwp_exit() with its return value
    */
    int rval;
    rval = fun(arg);
    lwp_exit(rval);
}

tid_t lwp_create(lwpfun func, void * arg){
    thread new_thread;
    struct rlimit limit; //resource limit
    size_t stack_size; //in bytes
    unsigned long *end_stack; //end of stack frame
    unsigned long *base_stack; //base of stack, beginning of stack frame

    new_thread = calloc(1, sizeof(context)); //init to 0s
    if(new_thread == NULL) { //if can't be created return NO_THREAD
        return NO_THREAD;
    }

    //get page size and allocate stack and context using mmap
    long page_size = sysconf(_SC_PAGE_SIZE);

    stack_size = 8 * 1024 * 1024; //8 * 1024KB * 1024 B = 8MB for deafult
    //success and not infinite limit
    if(getrlimit(RLIMIT_STACK, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY){
        stack_size = (size_t)limit.rlim_cur;
    }

    //round up to page size multiple
    long next_mult = (stack_size + page_size - 1) / page_size; //get to next multiple of page count
    stack_size = next_mult * page_size; 
    new_thread->stacksize = stack_size;

    //allocate thread stack (from instr)
    new_thread->stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if(new_thread->stack == MAP_FAILED){
        free(new_thread);
        return NO_THREAD;
    }

    new_thread->tid = tid_count++;
    new_thread->status = LWP_LIVE;

    //init context and stack so swap_rfiles() properly returns to lwp's func
    //  with stack and registers arranged
    end_stack = new_thread->stack + (stack_size / sizeof(unsigned long)); //stack is word addressable, convert stack_size to word
    base_stack = end_stack - 3; 
    base_stack[0] = 0;
    base_stack[1] = (unsigned long)lwp_wrap;
    base_stack[2] = 0;

    new_thread->state.fxsave = FPU_INIT;
    new_thread->state.rbp = (unsigned long)base_stack;
    new_thread->state.rsp = (unsigned long)base_stack;
    //for lwp_wrap, func and args
    new_thread->state.rdi = (unsigned long)func;
    new_thread->state.rsi = (unsigned long)arg;
    
    //add to lib list of valid threads and admit to current scheduler
    new_thread->lib_one = valid_t;
    valid_t = new_thread;
    sched->admit(new_thread);

    return new_thread->tid;
}

void  lwp_exit(int status){
    //terminates current lwp
    //yields to thread in scheduler, doesn't return
    //once thread is waited for in lwp_wait(), resources deallocated
    //  and uses lwp_yield()

    //need to save to queue of terminated threads
    thread term_thread = cur_thread;
    //MKTERMSTAT combines low 8 bits of status and lwp_term flag
    cur_thread->status = MKTERMSTAT(LWP_TERM, status);
    sched->remove(term_thread);

    if(wait_head != NULL){
        thread wait_pop = wait_head;
        wait_head = wait_head->lib_two; //set wait_head to next waiting thread

        if(wait_head == NULL){ //if nothing left in queue, set tail to null too
            wait_tail = NULL;
        }

        wait_pop->lib_two = NULL; //no longer wait queue, going to be scheduled
        wait_pop->exited = term_thread; //associate term thread with oldest waiting
        sched->admit(wait_pop);
    } else {
        term_thread->lib_two = NULL; //so doesn't point to any waiting threads, in term queue now

        if(exit_tail != NULL){
            exit_tail->lib_two = term_thread; //add to end of terminated fifo
            exit_tail = term_thread;
        } else {
            exit_head = term_thread;
            exit_tail = term_thread;
        }
    }

    lwp_yield();
}

tid_t lwp_gettid(void){  
    if(cur_thread == NULL){
        return NO_THREAD;
    }
    return cur_thread->tid;
}

void  lwp_yield(void){
    //save current lwp context
    //pick next lwp from scheduler rrnext()
    //if no next thread terminate program
    //first see if have next thread, if so swap_rfiles to save context and do next
    thread old_thread = cur_thread;
    thread next_thread = sched->next();
    if(next_thread == NULL){
        exit(0);
    }

    cur_thread = next_thread;
    //old thread context in rdi, new in rsi
    swap_rfiles(&(old_thread->state), &(next_thread->state));
}

void  lwp_start(void){
    //converts calling/original thread to lwp by creating context
    //and admitting to scheduler
    //don't allocate stack for it, use stack already hasw
    //call lwp_yield
    thread caller = calloc(1, sizeof(context));
    caller->tid = tid_count++;
    caller->status = LWP_LIVE;
    cur_thread = caller;
    caller->lib_one = valid_t;
    valid_t = caller;
    sched->admit(caller);
    lwp_yield();
}

tid_t lwp_wait(int *status){
    //deallocated terminated thread resources
    //reports termination status if not NULL
    //returns tid of terminated thread
    //if multiple terminated threads
        //return oldest thread (first in FIFO) w/o blocking
    //if no terminated threads
        //deschedule sched->remove()
        //place on queue of waiting threads
        //and block
    //if multiple waiting threads?

    thread term_thread;

    if(exit_head == NULL){ //no threads in terminated fifo queue
        //if no threads left to wait on
        if(sched->qlen() <= 1){
            return NO_THREAD;
        }
        //deschedue cur_thread sched->remove()
        sched->remove(cur_thread);
        //place in queue for waiting, wait_tail
        if(wait_tail == NULL){ //empty waiting fifo queue
            cur_thread->lib_two = NULL;
            wait_tail = cur_thread;
            wait_head = cur_thread;
        } else {
            cur_thread->lib_two = NULL;
            wait_tail->lib_two = cur_thread;
            wait_tail = cur_thread;
        }
        //block until a thread terminates
        lwp_yield();

        //once recieves terminated thread, in thread->exited
        term_thread = cur_thread->exited;
        cur_thread->exited = NULL;
    } else {
        //return oldest waiting terminated, exit_head
        term_thread = exit_head;
        exit_head = exit_head->lib_two; //next term thread in fifo queue
        if(exit_head == NULL){ //if after popping from queue its empty
            exit_tail = NULL;
        }
    }
    
    if(status != NULL){ //have to populat *status
        *status = term_thread->status;
    }

    //free stuff, remove from valid, munmap (returns 0 on success)
    tid_t exit_tid = term_thread->tid;
    thread start = valid_t;
    thread prev = NULL;
    while(start != NULL){ //lib_one holds valid threads
        if(start->tid == term_thread->tid){
            //remove from list
            break;
        }
        prev = start;
        start = start->lib_one;
    }

    //take out of valid list
    if(start != NULL){
        if(prev == NULL){
            valid_t = start->lib_one; //nothing before, so just remove and set to next
        } else {
            prev->lib_one = start->lib_one;
        }
    }
    
    if(term_thread->stack != NULL){ //if null, then original/calling thread
        munmap(term_thread->stack, term_thread->stacksize);
    }
    free(term_thread);

    return exit_tid;
}

void  lwp_set_scheduler(scheduler fun){
    if(fun == NULL){
        sched = &rr_publish;
    } else {
        //actually have to set schedulers
    }
}

scheduler lwp_get_scheduler(void){
    return sched;
}

thread tid2thread(tid_t tid){
    //need to look at valid list of threads
    //returns thread corresponding to tid
    if(tid == NO_THREAD){ //invalid
        return NULL;
    }

    thread start = valid_t;
    while(start != NULL){ //lib_one holds valid threads
        if(start->tid == tid){
            return start;
        }
        start = start->lib_one;
    }

    return NULL;
}