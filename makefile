CFLAGS= -std=c++11 -Wall

all:
	gcc main.cpp cpu.cpp -o chip8 $(CFLAGS) `pkg-config sdl3 --cflags --libs`
