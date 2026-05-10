# Traffic Simulation in a Directed Graph

## Group Members
- Student Name: Ahmad Abu Kteash

## Description
This project is a traffic simulation system in a directed graph. It involves multiple processes (passengers) moving across the graph using OS concepts like IPC, synchronization, and scheduling.

## Milestone 1: Graph Representation and Dijkstra's Algorithm
### Description
In this milestone, we implemented the foundation for graph handling and pathfinding.
- Graph is represented using an Adjacency List for efficiency.
- Dijkstra's algorithm is used to find the shortest path between two nodes.
- The program reads graph data and queries from a text file.

### Compilation
To compile Milestone 1, run:
```bash
make milestone1
```

### Execution
To run the Dijkstra program:
```bash
./dijkstra <file_name>
```
Example:
```bash
./dijkstra input_sample.txt
```

## Milestone 2: Graphical Interface – Displaying the Graph
### Description
In this milestone, we implemented the graphical visualization of the graph using the `raylib` library.
- Nodes are displayed as circles with their IDs.
- Edges are displayed as arrows pointing from source to destination.
- Weights are displayed along the edges.
- A circular layout is used to distribute nodes evenly and avoid overlapping.

### Compilation
To compile Milestone 2, run:
```bash
make milestone2
```

### Execution
To run the simulation program:
```bash
./sim <file_name>
```
Example:
```bash
./sim input_sample.txt
```

