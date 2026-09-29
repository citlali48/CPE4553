all: schedule demo test

schedule: schedule.c pqueue.c
	gcc -Wall -o schedule schedule.c pqueue.c

test: test.c
	gcc -Wall -o test test.c

demo: demo.c
	gcc -Wall -o demo demo.c

clean:
	rm -f schedule demo test