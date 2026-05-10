CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

milestone1: dijkstra.c
	$(CC) $(CFLAGS) dijkstra.c -o dijkstra

milestone2: sim.c
	$(CC) $(CFLAGS) sim.c -o sim $(LDFLAGS)

clean:
	rm -f dijkstra sim
