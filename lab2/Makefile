all: scheduler demo test

scheduler: scheduler.c pqueue.c
	gcc -Wall -o scheduler scheduler.c pqueue.c

test: test.c
	gcc -Wall -o test test.c

demo: demo.c
	gcc -Wall -o demo demo.c

clean:
	rm -f scheduler demo test