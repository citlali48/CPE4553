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
- fifo queue of terminated threads
    - so lwp_wait() can return oldest terminated thread
- fifo queue of waiting threads
    - if no terminated threads lwp_wait() has to block, so deschedule thread
      that called lwp_wait() (with sched->remove()) and place in wait queue
- 
*/

static scheduler sched = &rr_publish;
static tid_t tid_count = 1;
static thread cur_thread = NULL;

//list of valid threads for tid2thread
static thread valid_t = NULL;

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
    //returns termination status: low 8 bits of passed in status
    //once thread is waited for in lwp_wait(), resources deallocated
    //  and uses lwp_yield()
}

tid_t lwp_gettid(void){  
    if(cur_thread == NULL){
        return NO_THREAD;
    }
    return cur_thread->tid;
}

void  lwp_yield(void){

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

}

void  lwp_set_scheduler(scheduler fun){

}

scheduler lwp_get_scheduler(void){
    return sched;
}

thread tid2thread(tid_t tid){
    //need to look at valid list of threads
}