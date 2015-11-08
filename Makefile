CC=gcc
CFLAGS=-DDEBUG -g

.PHONY=clean

all: event_loop

clean:
	rm event_loop