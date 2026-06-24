CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

milestone1: dijkstra.c
	$(CC) $(CFLAGS) dijkstra.c -o dijkstra

milestone2: sim.c
	$(CC) $(CFLAGS) sim.c -o sim $(LDFLAGS)


milestone3: sim.c
	$(CC) $(CFLAGS) sim.c -o sim $(LDFLAGS)


milestone4: sim.c
	$(CC) $(CFLAGS) sim.c -o sim $(LDFLAGS)


milestone5: sim.c
	$(CC) $(CFLAGS) sim.c -o sim $(LDFLAGS)


milestone6: sim.c
	$(CC) $(CFLAGS) sim.c -o sim $(LDFLAGS)

milestone7: sim.c
	$(CC) $(CFLAGS) sim.c -o sim-schd $(LDFLAGS)

clean:
	rm -f dijkstra sim
