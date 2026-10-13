# Makefile for Programming Assignment 02 (ls clone)
#
#   make          build  bin/ls
#   make clean    delete obj/*.o and bin/ls
#
# It compiles EVERY .c file found in src/, so it works unchanged for all
# versions (src/ls-v1.0.0.c, src/ls-v1.1.0.c, ... only one of them is kept).

CC      = gcc
CFLAGS  = -Wall -Wextra -std=gnu11
SRCS    = $(wildcard src/*.c)
OBJS    = $(patsubst src/%.c, obj/%.o, $(SRCS))
TARGET  = bin/ls

all: $(TARGET)

# link: all object files -> final program
$(TARGET): $(OBJS) | bin
	$(CC) $(OBJS) -o $@

# compile: one .c file -> one .o file
obj/%.o: src/%.c | obj
	$(CC) $(CFLAGS) -c $< -o $@

# the "| bin" / "| obj" are order-only prerequisites: create the folder if it is missing
bin obj:
	mkdir -p $@

clean:
	rm -f obj/*.o $(TARGET)

.PHONY: all clean
