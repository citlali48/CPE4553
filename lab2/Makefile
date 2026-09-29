all: schedule demo test test2

schedule: schedule.c pqueue.c
	gcc -Wall -o schedule schedule.c pqueue.c

test: test.c
	gcc -Wall -o test test.c

test2: test2.c
	gcc -Wall -o test2 test2.c

demo: demo.c
	gcc -Wall -o demo demo.c

clean:
	rm -f schedule demo test