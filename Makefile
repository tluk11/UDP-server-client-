CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -Iincludes
LDLIBS = -lcrypto
VPATH = src

all: client server

client: src/client.c
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

server: src/server.c 
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -f client server src/*.o *.o

.PHONY: all clean
