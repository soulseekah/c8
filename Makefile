P=c8
CFLAGS=`pkg-config --cflags sdl2` -std=gnu11 -O0 -Wall -Werror -g
CC=gcc
ROM=roms/BLITZ
LDLIBS=`pkg-config --libs sdl2`

$(P): $(OBJECT)

run: $(P)
	./$(P) $(ROM)

test: $(P)
	TEST=1 ./$(P)

clean:
	rm $(P)
