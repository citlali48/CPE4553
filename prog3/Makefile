all: schedule test magic64

schedule: schedule.c pqueue.c
	gcc -Wall -o schedule schedule.c pqueue.c

test: test.c
	gcc -Wall -o test test.c

magic64: magic64.S
	gcc -o magic64.o -c magic64.S

clean:
	rm -f schedule test magic64