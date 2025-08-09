# Makefile - For building on a Linux system
CC = gcc
TARGET = morse_decoder
SRCS = main.c
CFLAGS = -Wall -O2 `sdl2-config --cflags` `pkg-config --cflags fftw3`
LDFLAGS = `sdl2-config --libs` -lSDL2_ttf `pkg-config --libs fftw3` -lm

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)
