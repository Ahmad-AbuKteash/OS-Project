# Traffic Simulation in a Directed Graph

## Group Members
- Name: Ahmad Abu Kteash

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


## Milestone 3: Movement Animation on the Graph
### Description
In this milestone, we added animation to represent entities moving along the shortest path.
- **Pathfinding**: Integrated Dijkstra's algorithm to calculate the shortest path from source to destination.
- **Animation Control**: Added a "PLAY/STOP" button to control the simulation.
- **Movement Logic**:
    - **Edge Travel**: Entities jump across edges based on their weight (300ms per unit of weight).
    - **Node Waiting**: Entities wait for 1 second at each intermediate node.
- **Visual Feedback**:
    - Highlighted source (Green) and destination (Red) nodes.
    - Highlighted the shortest path in Blue.
    - Displayed a success message upon reaching the destination.
- **Improved Robustness**:
    - Added detection and visual reporting for negative weights.
    - Added detection and visual reporting for cases where source equals destination.
    - Simplified edge rendering and motion logic by focusing on straight-line paths.

### Compilation
To compile Milestone 3, run:
```bash
make milestone3
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

## Milestone 4: Multiple Processes and Parent Process
### Description
In this milestone, we extended the simulation to support multiple travelers moving simultaneously using process management.
- **Extended Input**: The input file now includes the number of travelers and a source/destination pair for each one.
- **Parent Process**:
    - Reads the file and calculates the Dijkstra path for each traveler.
    - Creates child processes using `fork()`.
    - Manages the `raylib` GUI loop and displays all travelers on the screen.
    - Each traveler is represented by a different color.
    - Sends a termination signal to each child when its trip finishes.
    - Waits for all children before exiting.
- **Child Processes**:
    - Print `[PID] started` to the terminal immediately after creation.
    - Sleep until the parent terminates them at the end of their trip.
- **Parallel Animation**: All travelers move simultaneously on the graph when PLAY is pressed.

### Compilation
To compile Milestone 4, run:
```bash
make milestone4
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


## Milestone 5: Inter-Process Communication (IPC)
### Description
In this milestone, child processes became autonomous travelers.
- Each child calculates its own shortest path using Dijkstra's algorithm.
- Each child sends route status messages to the parent through its own pipe.
- The parent uses non-blocking reads so the `raylib` GUI loop keeps running while it receives child updates.
- Only the parent prints progress logs to the terminal.

### IPC Mechanism
The implementation uses UNIX pipes created with `pipe()`. Each traveler has one private child-to-parent pipe. This fits the project because messages are small, ordered per child, and one-directional.

### Compilation
To compile Milestone 5, run:
```bash
make milestone5
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

