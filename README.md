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
