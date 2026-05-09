CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

milestone1: dijkstra.c
	$(CC) $(CFLAGS) dijkstra.c -o dijkstra

clean:
	rm -f dijkstra sim
