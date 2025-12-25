# Compiler and build settings
CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Werror
LDFLAGS =

# Default target
all: aesdsocket

# Build aesdsocket application
aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o aesdsocket aesdsocket.c

# Clean target - remove aesdsocket binary and all object files
clean:
	rm -f aesdsocket *.o

.PHONY: all clean
