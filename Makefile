P=c8
CFLAGS=-std=gnu11 -O0 -Wall -Werror -g
CC=gcc
ROM=roms/PONG

$(P): $(OBJECT)

run: $(P)
	./$(P) $(ROM)

test: $(P)
	TEST=1 ./$(P)
