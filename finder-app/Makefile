# Compiler and build settings
CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Werror
LDFLAGS =

# Default target
all: writer

# Build writer application
writer: writer.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o writer writer.c

# Clean target - remove writer binary and all object files
clean:
	rm -f writer *.o

.PHONY: all clean
