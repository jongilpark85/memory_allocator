# Makefile for Malloc
CC=gcc
CFLAGS=-g -fPIC -Wall 

default: build

rebuild: clean build

build: libmalloc.so test1

test: build
	LD_PRELOAD=./libmalloc.so ./test1

clean:
	rm -rf libmalloc.so malloc.o core.o test1.o test1

libmalloc.so: malloc.o core.o
	$(CC) $(CFLAGS) -shared -Wl,--unresolved-symbols=ignore-all -o libmalloc.so malloc.o core.o -lpthread

malloc.o: malloc.c malloc.h core.c core.h
	$(CC) $(CFLAGS) -c malloc.c

core.o: core.c core.h
	$(CC) $(CFLAGS) -c core.c

test1: test1.o
	$(CC) $(CFLAGS) -o test1 test1.o -lpthread

test1.o: test1.c
	$(CC) $(CFLAGS) -c test1.c
