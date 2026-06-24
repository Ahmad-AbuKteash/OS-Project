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
## Milestone 6: Synchronizing Access to Nodes
### Description
In this milestone, every graph node became a critical section.
- No more than one traveler may be inside the same node at the same time.
- A traveler that reaches a busy node waits outside it.
- The GUI draws waiting travelers outside the node with a different outline.
- When the node becomes available, one waiting traveler enters and waits inside for one full second.

### Synchronization Mechanism
The implementation uses named POSIX semaphores, one semaphore per graph node. Each semaphore is initialized to `1`, so a child process must call `sem_wait()` before entering a node and `sem_post()` after finishing the one-second stay. This makes the node stay a critical section across processes and prevents simultaneous entry.

The GUI also keeps parent-side node occupancy state that mirrors the same rule visually. This prevents the display from ever showing more than one traveler inside the same node.

### Compilation
To compile Milestone 6, run:
```bash
make milestone6
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


## Milestone 7: Scheduling Algorithms
### Description
In this final milestone, we replaced the random order of node entry with formal scheduling algorithms. When multiple travelers wait at a node's entrance, the parent process determines their entry order based on the chosen scheduler.

### Implemented Algorithms
1. **First-Come, First-Served (FCFS)**: Travelers enter the node in the exact order they arrived at the waiting area.
2. **Shortest Job First (SJF) / Priority**: Travelers enter based on a priority value assigned in the input file. In this implementation, a lower priority value (e.g., 1 vs 10) allows earlier entry. If priorities are equal, it falls back to FCFS.

### Technical Implementation
- The parent process manages a 'waiting queue' for each node.
- When a traveler reaches a node, it is added to the node's queue with an arrival timestamp and its priority.
- The scheduler (FCFS or SJF) selects the next traveler from the queue whenever the node becomes available.
- The choice of algorithm is passed as a command-line argument.

### Comparison of Algorithms
- **FCFS**: Ensures fairness based on arrival time. Travelers who arrive first are never bypassed. However, "shorter" tasks might wait behind "longer" ones.
- **SJF**: Can reduce average waiting time by allowing high-priority or shorter tasks to pass first. In our simulation, using SJF allows travelers with lower priority values to bypass others, which can be useful for urgent travelers.

### Compilation
To compile Milestone 7, run:
```bash
make milestone7
```

### Execution
To run the simulation program with a specific scheduler:
```bash
./sim-schd fcfs <file_name>
./sim-schd sjf <file_name>
```
Example:
```bash
./sim-schd sjf test_milestone7.txt
```

### Input File Format for Milestone 7
The traveler data now supports an optional priority value:
```
<num_travelers>
<src> <dest> <priority>
...
```
If priority is not provided, it defaults to 0.
