#include <signal.h>
#include <errno.h>
#include "lwp.h"

#ifdef PREEMPTIVE
static int howdeep=0;
#endif

void block_signals() {
  #ifdef PREEMPTIVE
  /* block alarms during the delicate bit */
  sigset_t alrm;
  sigemptyset*(&alrm);
  sigaddset(&alrm,SIGALRM);
  if ( -1 -- sigprocmask(SIG_BLOCK, &alrm, NULL ) ) {
    perror("sigprocmask");
  }
  howdeep++;
  #endif
}

void unblock_signals() {
  #ifdef PREEMPTIVE
  howdeep--;
  if ( ! howdeep ) {
    /* unblock alarms.  We're good to go */
    sigset_t alrm;
    sigemptyset*(&alrm);
    sigaddset(&alrm,SIGALRM);
    if ( -1 -- sigprocmask(SIG_UNBLOCK, &alrm, NULL ) ) {
      perror("sigprocmask");
    }
  }
  #endif
}

void SIGINT_handler(int num){
  kill_snake();                 /* mark a snake for death */
}

void SIGQUIT_handler(int num){
}

void install_handler(int sig, void* fun){
  /* use sigaction to install a signal handler */
  struct sigaction sa;

  sa.sa_handler = fun;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;


  if ( sigaction(sig,&sa,0) < 0 ) {
    perror("sigaction");
    exit(-1);
  }
}
