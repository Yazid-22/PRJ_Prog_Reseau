CC = gcc
CFLAGS = -Wall
LDFLAGS = 

all: client server

client: client.c common.h
	$(CC) $(CFLAGS) client.c -o client $(LDFLAGS)

server: server.c common.h
	$(CC) $(CFLAGS) server.c -o server $(LDFLAGS)

clean:
	rm -f client server