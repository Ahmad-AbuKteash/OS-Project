#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "raylib.h"

#define INF INT_MAX
#define MAX_TRAVELERS 15

/*
 * IPC Choice: Pipes (pipe())
 * Reason: Pipes are simple, built-in, and well-suited for parent-child
 * communication. Each child gets one pipe to send node-arrival messages to the
 * parent and one acknowledgement pipe to receive permission to continue. The
 * parent uses non-blocking reads (O_NONBLOCK) on all child-to-parent pipe
 * read-ends to poll messages without blocking the raylib GUI loop.
 *
 * Synchronization: Each child waits for SIGUSR1 from the parent before
 * starting its journey, so all travelers begin simultaneously when PLAY
 * is pressed.
 */

// ─── Graph structures ────────────────────────────────────────────────────────

typedef struct Node {
    int dest;
    int weight;
    struct Node* next;
} Node;

typedef struct Graph {
    int numVertices;
    Node** adjLists;
} Graph;

typedef struct {
    Vector2 position;
} NodePos;

typedef enum {
    STATE_IDLE,
    STATE_MOVING,
    STATE_WAITING_NODE,
    STATE_FINISHED
} SimState;

// ─── IPC message sent from child → parent via pipe ───────────────────────────

typedef struct {
    pid_t pid;
    int   currentNode;  // node just arrived at
    int   nextNode;     // next node in path (-1 = DESTINATION)
} NodeMessage;

// ─── Traveler ─────────────────────────────────────────────────────────────────

typedef struct {
    pid_t pid;
    int startNode;
    int endNode;
    Color color;
    SimState state;
    int currentPathIndex;
    int edgeCurrentJump;
    int edgeTotalJumps;
    double lastUpdateTime;
    Vector2 position;
    bool finished;
    bool childTerminated;
    bool childStarted;      // has SIGUSR1 been sent to this child?

    // Child-to-parent pipe: pipefd[0] = read end (parent), pipefd[1] = write end (child)
    int pipefd[2];

    // Parent-to-child ACK pipe: ackPipefd[0] = read end (child), ackPipefd[1] = write end (parent)
    int ackPipefd[2];

    // Path computed by parent for GUI animation
    int* path;
    int  pathLen;
} Traveler;

static const Color TRAVELER_COLORS[] = {
    GOLD, SKYBLUE, LIME, PURPLE, ORANGE, PINK,
    MAROON, DARKGREEN, VIOLET, BEIGE, BROWN, YELLOW
};

// ─── Signal handling in child ─────────────────────────────────────────────────

static volatile sig_atomic_t g_startSignalReceived = 0;

static void sigusr1Handler(int sig) {
    (void)sig;
    g_startSignalReceived = 1;
}

// ─── Graph helpers ────────────────────────────────────────────────────────────

Node* createNode(int d, int w) {
    Node* newNode = (Node*)malloc(sizeof(Node));
    if (!newNode) return NULL;
    newNode->dest = d;
    newNode->weight = w;
    newNode->next = NULL;
    return newNode;
}

Graph* createGraph(int vertices) {
    Graph* graph = (Graph*)malloc(sizeof(Graph));
    if (!graph) return NULL;
    graph->numVertices = vertices;
    graph->adjLists = (Node**)malloc(vertices * sizeof(Node*));
    if (!graph->adjLists) { free(graph); return NULL; }
    for (int i = 0; i < vertices; i++) graph->adjLists[i] = NULL;
    return graph;
}

void addEdge(Graph* graph, int src, int dest, int weight) {
    Node* newNode = createNode(dest, weight);
    newNode->next = graph->adjLists[src];
    graph->adjLists[src] = newNode;
}

int getEdgeWeight(Graph* graph, int src, int dest) {
    Node* temp = graph->adjLists[src];
    while (temp) {
        if (temp->dest == dest) return temp->weight;
        temp = temp->next;
    }
    return 0;
}

void freeGraph(Graph* graph) {
    if (!graph) return;
    for (int i = 0; i < graph->numVertices; i++) {
        Node* temp = graph->adjLists[i];
        while (temp) {
            Node* toFree = temp;
            temp = temp->next;
            free(toFree);
        }
    }
    free(graph->adjLists);
    free(graph);
}

// ─── Dijkstra ─────────────────────────────────────────────────────────────────

int* dijkstra(Graph* graph, int startNode, int endNode, int* pathLen) {
    int n = graph->numVertices;
    int* dist     = (int*)malloc(n * sizeof(int));
    int* prev     = (int*)malloc(n * sizeof(int));
    bool* visited = (bool*)malloc(n * sizeof(bool));

    for (int i = 0; i < n; i++) {
        dist[i]    = INF;
        prev[i]    = -1;
        visited[i] = false;
    }
    dist[startNode] = 0;

    for (int count = 0; count < n; count++) {
        int u = -1, minDist = INF;
        for (int i = 0; i < n; i++) {
            if (!visited[i] && dist[i] < minDist) { minDist = dist[i]; u = i; }
        }
        if (u == -1 || u == endNode) break;
        visited[u] = true;

        Node* temp = graph->adjLists[u];
        while (temp) {
            int v = temp->dest, w = temp->weight;
            if (!visited[v] && dist[u] != INF && dist[u] + w < dist[v]) {
                dist[v] = dist[u] + w;
                prev[v] = u;
            }
            temp = temp->next;
        }
    }

    int* path = NULL;
    *pathLen = 0;

    if (dist[endNode] != INF) {
        int tempPath[200];
        int cnt = 0;
        for (int at = endNode; at != -1; at = prev[at]) tempPath[cnt++] = at;
        *pathLen = cnt;
        path = (int*)malloc(cnt * sizeof(int));
        for (int i = 0; i < cnt; i++) path[i] = tempPath[cnt - 1 - i];
    }

    free(dist); free(prev); free(visited);
    return path;
}

// ─── Drawing helpers ──────────────────────────────────────────────────────────

void DrawArrow(Vector2 start, Vector2 end, float radius, float thickness, Color color) {
    float angle = atan2f(end.y - start.y, end.x - start.x);
    Vector2 adjustedEnd = { end.x - radius * cosf(angle), end.y - radius * sinf(angle) };
    DrawLineEx(start, adjustedEnd, thickness, color);
    float arrowHeadLen   = 15;
    float arrowHeadAngle = PI / 6;
    Vector2 p1 = { adjustedEnd.x - arrowHeadLen * cosf(angle - arrowHeadAngle),
                   adjustedEnd.y - arrowHeadLen * sinf(angle - arrowHeadAngle) };
    Vector2 p2 = { adjustedEnd.x - arrowHeadLen * cosf(angle + arrowHeadAngle),
                   adjustedEnd.y - arrowHeadLen * sinf(angle + arrowHeadAngle) };
    DrawTriangle(adjustedEnd, p2, p1, color);
}

bool isPathEdge(const int* path, int pathLen, int src, int dst) {
    if (pathLen < 2) return false;
    for (int j = 0; j < pathLen - 1; j++)
        if (path[j] == src && path[j + 1] == dst) return true;
    return false;
}

// ─── Traveler helpers (parent side) ──────────────────────────────────────────

bool canAnimate(const Traveler* t)            { return t->pathLen > 1; }
bool hasReachedDestination(const Traveler* t) {
    if (!t->finished) return false;
    if (t->startNode == t->endNode) return true;
    return t->pathLen > 1;
}
bool allAnimatableFinished(const Traveler* travelers, int num) {
    for (int i = 0; i < num; i++)
        if (canAnimate(&travelers[i]) && !travelers[i].finished) return false;
    return true;
}

void terminateChild(Traveler* t) {
    if (t->pid > 0 && !t->childTerminated) {
        kill(t->pid, SIGTERM);
        waitpid(t->pid, NULL, 0);
        t->childTerminated = true;
    }
}

bool waitForParentAck(int ackReadFd) {
    char ack;
    ssize_t n;

    do {
        n = read(ackReadFd, &ack, sizeof(ack));
    } while (n < 0 && errno == EINTR);

    return n == (ssize_t)sizeof(ack);
}

void sendParentAck(int ackWriteFd, pid_t childPid) {
    char ack = 'A';
    ssize_t n;

    do {
        n = write(ackWriteFd, &ack, sizeof(ack));
    } while (n < 0 && errno == EINTR);

    if (n == (ssize_t)sizeof(ack)) {
        printf("[PID=%d] ACK sent: %c\n", childPid, ack);
        fflush(stdout);
    }
}

// ─── Child process logic ──────────────────────────────────────────────────────
/*
 * Each child:
 *   1. Installs a SIGUSR1 handler and waits (pause) for the parent's start signal.
 *   2. Calculates its own Dijkstra path.
 *   3. Travels the path: at each node arrival it sends a NodeMessage to the
 *      parent through the pipe, waits for an ACK from the parent, then sleeps
 *      to simulate travel time.
 *   4. Exits normally when done.
 *
 * Sleep times match the GUI animation:
 *   - 300 ms × edge_weight  (edge travel)
 *   - 1000 ms wait at intermediate nodes
 */

void runChild(int startNode, int endNode, int writeFd, int ackReadFd, Graph* graph) {
    pid_t myPid = getpid();

    signal(SIGUSR1, sigusr1Handler);

    // Block until SIGUSR1 arrives
    while (!g_startSignalReceived) {
        pause();
    }

    // Calculate own path
    int pathLen = 0;
    int* path   = NULL;

    if (startNode == endNode) {
        pathLen = 1;
        path    = (int*)malloc(sizeof(int));
        if (path) path[0] = startNode;
    } else {
        path = dijkstra(graph, startNode, endNode, &pathLen);
    }

    if (!path || pathLen == 0) {
        NodeMessage msg = { myPid, startNode, -1 };
        write(writeFd, &msg, sizeof(msg));
        waitForParentAck(ackReadFd);
        close(writeFd);
        close(ackReadFd);
        free(path);
        exit(0);
    }

    // Travel the path
    for (int i = 0; i < pathLen; i++) {
        int currentNode = path[i];
        int nextNode    = (i < pathLen - 1) ? path[i + 1] : -1;

        // Report arrival at this node
        NodeMessage msg = { myPid, currentNode, nextNode };
        write(writeFd, &msg, sizeof(msg));
        if (!waitForParentAck(ackReadFd)) break;

        if (nextNode == -1) break; // reached destination

        // Simulate edge travel: 300ms × edge weight
        int edgeWeight = getEdgeWeight(graph, currentNode, nextNode);
        if (edgeWeight <= 0) edgeWeight = 1;
        usleep(edgeWeight * 300 * 1000);

        // Wait 1 second at intermediate nodes (not source, not destination)
        if (i > 0 && i < pathLen - 2) {
            usleep(1000 * 1000);
        }
    }

    close(writeFd);
    close(ackReadFd);
    free(path);
    exit(0);
}

// ─── Parent: non-blocking read — one message per call ────────────────────────
/*
 * Reads exactly ONE NodeMessage from the pipe (non-blocking).
 * Returns true if a message was available.
 * Reading one message per frame ensures the terminal log matches the moment
 * the child actually arrives at each node.
 */
bool pollChildPipe(int readFd, NodeMessage* outMsg) {
    ssize_t n = read(readFd, outMsg, sizeof(NodeMessage));
    return (n == (ssize_t)sizeof(NodeMessage));
}

// ─── GUI: smooth animation driven by parent ───────────────────────────────────

void updateTraveler(Traveler* traveler, Graph* graph, NodePos* nodePositions,
                    bool isPlaying, double currentTime) {
    if (!isPlaying || traveler->finished || traveler->pathLen <= 1) return;

    if (traveler->state == STATE_IDLE) {
        traveler->state = STATE_MOVING;
        traveler->edgeCurrentJump = 0;
        int src = traveler->path[traveler->currentPathIndex];
        int dst = traveler->path[traveler->currentPathIndex + 1];
        traveler->edgeTotalJumps = getEdgeWeight(graph, src, dst);
        if (traveler->edgeTotalJumps <= 0) traveler->edgeTotalJumps = 1;
        traveler->lastUpdateTime = currentTime;

    } else if (traveler->state == STATE_MOVING) {
        if (currentTime - traveler->lastUpdateTime >= 0.3) {
            traveler->edgeCurrentJump++;
            traveler->lastUpdateTime = currentTime;

            Vector2 pStart = nodePositions[traveler->path[traveler->currentPathIndex]].position;
            Vector2 pEnd   = nodePositions[traveler->path[traveler->currentPathIndex + 1]].position;
            float t = (float)traveler->edgeCurrentJump / traveler->edgeTotalJumps;
            traveler->position.x = pStart.x + (pEnd.x - pStart.x) * t;
            traveler->position.y = pStart.y + (pEnd.y - pStart.y) * t;

            if (traveler->edgeCurrentJump >= traveler->edgeTotalJumps) {
                traveler->currentPathIndex++;
                if (traveler->currentPathIndex == traveler->pathLen - 1) {
                    traveler->state    = STATE_FINISHED;
                    traveler->finished = true;
                } else {
                    traveler->state = STATE_WAITING_NODE;
                }
            }
        }
    } else if (traveler->state == STATE_WAITING_NODE) {
        if (currentTime - traveler->lastUpdateTime >= 1.0) {
            traveler->state = STATE_IDLE;
        }
    }
}

void resetTraveler(Traveler* traveler, NodePos* nodePositions) {
    if (!canAnimate(traveler)) {
        traveler->finished = true;
        return;
    }
    traveler->state            = STATE_IDLE;
    traveler->currentPathIndex = 0;
    traveler->edgeCurrentJump  = 0;
    traveler->edgeTotalJumps   = 0;
    traveler->finished         = false;
    traveler->position         = nodePositions[traveler->path[0]].position;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file_name>\n", argv[0]);
        return 1;
    }

    // ── Read graph file ──────────────────────────────────────────────────────
    FILE* file = fopen(argv[1], "r");
    if (!file) { perror("Error opening file"); return 1; }

    int N, M;
    if (fscanf(file, "%d %d", &N, &M) != 2) {
        fprintf(stderr, "Invalid file format\n"); fclose(file); return 1;
    }

    bool hasNegativeWeight = false;
    Graph* graph = createGraph(N);
    for (int i = 0; i < M; i++) {
        int src, dst, weight;
        if (fscanf(file, "%d %d %d", &src, &dst, &weight) != 3) break;
        if (weight < 0) hasNegativeWeight = true;
        addEdge(graph, src, dst, weight);
    }

    int numTravelers = 0;
    if (fscanf(file, "%d", &numTravelers) != 1 ||
        numTravelers <= 0 || numTravelers > MAX_TRAVELERS) {
        fprintf(stderr, "Invalid number of travelers\n");
        fclose(file); freeGraph(graph); return 1;
    }

    Traveler* travelers = (Traveler*)calloc(numTravelers, sizeof(Traveler));
    if (!travelers) { fclose(file); freeGraph(graph); return 1; }

    for (int i = 0; i < numTravelers; i++) {
        int startNode, endNode;
        if (fscanf(file, "%d %d", &startNode, &endNode) != 2) {
            fprintf(stderr, "Invalid traveler data\n");
            fclose(file); freeGraph(graph); free(travelers); return 1;
        }
        travelers[i].startNode       = startNode;
        travelers[i].endNode         = endNode;
        travelers[i].color           = TRAVELER_COLORS[i % (sizeof(TRAVELER_COLORS) / sizeof(TRAVELER_COLORS[0]))];
        travelers[i].pid             = -1;
        travelers[i].childTerminated = false;
        travelers[i].childStarted    = false;
        travelers[i].finished        = false;
        travelers[i].state           = STATE_IDLE;
        travelers[i].pathLen         = 0;
        travelers[i].path            = NULL;
        travelers[i].pipefd[0]       = -1;
        travelers[i].pipefd[1]       = -1;
        travelers[i].ackPipefd[0]    = -1;
        travelers[i].ackPipefd[1]    = -1;

        // Parent computes path for GUI animation
        if (hasNegativeWeight) {
            travelers[i].finished = true;
        } else if (startNode != endNode) {
            travelers[i].path = dijkstra(graph, startNode, endNode, &travelers[i].pathLen);
            if (travelers[i].pathLen == 0) travelers[i].finished = true;
        } else {
            travelers[i].pathLen = 1;
            travelers[i].path    = (int*)malloc(sizeof(int));
            if (travelers[i].path) travelers[i].path[0] = startNode;
            travelers[i].finished = true;
        }
    }
    fclose(file);

    // ── Create all pipes first, then fork all children ───────────────────────
    // Each traveler has one child-to-parent pipe for status messages and one
    // parent-to-child pipe for ACK messages.

    for (int i = 0; i < numTravelers; i++) {
        if (pipe(travelers[i].pipefd) < 0 || pipe(travelers[i].ackPipefd) < 0) {
            perror("pipe failed");
            if (travelers[i].pipefd[0] >= 0) close(travelers[i].pipefd[0]);
            if (travelers[i].pipefd[1] >= 0) close(travelers[i].pipefd[1]);
            for (int j = 0; j < i; j++) {
                close(travelers[j].pipefd[0]);
                close(travelers[j].pipefd[1]);
                close(travelers[j].ackPipefd[0]);
                close(travelers[j].ackPipefd[1]);
            }
            for (int j = 0; j < numTravelers; j++)
                if (travelers[j].path) free(travelers[j].path);
            free(travelers); freeGraph(graph);
            return 1;
        }
    }

    for (int i = 0; i < numTravelers; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            // Terminate already-forked children
            for (int j = 0; j < i; j++) terminateChild(&travelers[j]);
            for (int j = 0; j < numTravelers; j++) {
                if (travelers[j].pipefd[0] >= 0) close(travelers[j].pipefd[0]);
                if (travelers[j].pipefd[1] >= 0) close(travelers[j].pipefd[1]);
                if (travelers[j].ackPipefd[0] >= 0) close(travelers[j].ackPipefd[0]);
                if (travelers[j].ackPipefd[1] >= 0) close(travelers[j].ackPipefd[1]);
                if (travelers[j].path) free(travelers[j].path);
            }
            free(travelers); freeGraph(graph);
            return 1;
        }

        if (pid == 0) {
            // ── Child process ────────────────────────────────────────────────
            // Close child-to-parent read-ends and parent-to-child write-ends.
            for (int j = 0; j < numTravelers; j++) {
                close(travelers[j].pipefd[0]);
                close(travelers[j].ackPipefd[1]);
            }
            // Close pipe ends that belong to other travelers.
            for (int j = 0; j < numTravelers; j++) {
                if (j != i) close(travelers[j].pipefd[1]);
                if (j != i) close(travelers[j].ackPipefd[0]);
            }
            int writeFd = travelers[i].pipefd[1];
            int ackReadFd = travelers[i].ackPipefd[0];
            runChild(travelers[i].startNode, travelers[i].endNode, writeFd, ackReadFd, graph);
            // runChild calls exit(), never returns
        }

        // ── Parent: record PID, close write-end ──────────────────────────────
        travelers[i].pid = pid;
    }

    // Parent closes child-owned pipe ends after all forks are done.
    for (int i = 0; i < numTravelers; i++) {
        close(travelers[i].pipefd[1]);
        travelers[i].pipefd[1] = -1;
        close(travelers[i].ackPipefd[0]);
        travelers[i].ackPipefd[0] = -1;

        // Set read-end non-blocking
        int flags = fcntl(travelers[i].pipefd[0], F_GETFL, 0);
        fcntl(travelers[i].pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    // ── GUI setup ────────────────────────────────────────────────────────────
    const int screenWidth  = 900;
    const int screenHeight = 650;
    InitWindow(screenWidth, screenHeight, "Traffic Simulation - Milestone 5");
    SetTargetFPS(60);

    NodePos* nodePositions = (NodePos*)malloc(N * sizeof(NodePos));
    Vector2 center = { (float)screenWidth / 2, (float)screenHeight / 2 };
    float layoutRadius = 220.0f;
    for (int i = 0; i < N; i++) {
        float angle = (float)i * 2.0f * PI / (float)N;
        nodePositions[i].position.x = center.x + layoutRadius * cosf(angle);
        nodePositions[i].position.y = center.y + layoutRadius * sinf(angle);
    }

    for (int i = 0; i < numTravelers; i++) {
        if (travelers[i].pathLen > 0 && travelers[i].path)
            travelers[i].position = nodePositions[travelers[i].path[0]].position;
        else
            travelers[i].position = nodePositions[travelers[i].startNode].position;
    }

    bool isPlaying  = false;
    bool startSent  = false;   // have we sent SIGUSR1 to all children?
    Rectangle playBtn = { 10, 50, 100, 40 };

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (!WindowShouldClose()) {

        // ── Play/stop button ──────────────────────────────────────────────────
        if (CheckCollisionPointRec(GetMousePosition(), playBtn) &&
            IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            isPlaying = !isPlaying;

            // Send SIGUSR1 to ALL children simultaneously on first PLAY press
            if (isPlaying && !startSent) {
                for (int i = 0; i < numTravelers; i++) {
                    if (travelers[i].pid > 0 && !travelers[i].childTerminated) {
                        kill(travelers[i].pid, SIGUSR1);
                        travelers[i].childStarted = true;
                    }
                }
                startSent = true;
            }

            if (isPlaying && allAnimatableFinished(travelers, numTravelers)) {
                for (int i = 0; i < numTravelers; i++)
                    resetTraveler(&travelers[i], nodePositions);
            }
        }

        // ── Poll ONE message per child per frame ──────────────────────────────
        for (int i = 0; i < numTravelers; i++) {
            if (travelers[i].pipefd[0] < 0) continue;

            NodeMessage msg;
            if (pollChildPipe(travelers[i].pipefd[0], &msg)) {
                if (msg.nextNode == -1) {
                    printf("[PID=%d] arrived at node %d | DESTINATION\n",
                           msg.pid, msg.currentNode);
                    fflush(stdout);
                    sendParentAck(travelers[i].ackPipefd[1], msg.pid);

                    close(travelers[i].pipefd[0]);
                    travelers[i].pipefd[0] = -1;
                    close(travelers[i].ackPipefd[1]);
                    travelers[i].ackPipefd[1] = -1;

                    if (travelers[i].pid > 0 && !travelers[i].childTerminated) {
                        waitpid(travelers[i].pid, NULL, 0);
                        travelers[i].childTerminated = true;
                    }
                    printf("[PID=%d] finished\n", msg.pid);
                    fflush(stdout);
                } else {
                    printf("[PID=%d] arrived at node %d | next node: %d\n",
                           msg.pid, msg.currentNode, msg.nextNode);
                    fflush(stdout);
                    sendParentAck(travelers[i].ackPipefd[1], msg.pid);
                }
            }
        }

        // ── Animate travelers ─────────────────────────────────────────────────
        double currentTime = GetTime();
        if (isPlaying) {
            for (int i = 0; i < numTravelers; i++)
                updateTraveler(&travelers[i], graph, nodePositions, isPlaying, currentTime);

            if (allAnimatableFinished(travelers, numTravelers))
                isPlaying = false;
        }

        // ── Draw ──────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground(RAYWHITE);

        // Draw edges
        for (int i = 0; i < N; i++) {
            Node* temp = graph->adjLists[i];
            while (temp) {
                Vector2 start = nodePositions[i].position;
                Vector2 end   = nodePositions[temp->dest].position;
                float nodeRadius = 25.0f;

                Color edgeColor = DARKGRAY;
                for (int t = 0; t < numTravelers; t++) {
                    if (isPathEdge(travelers[t].path, travelers[t].pathLen, i, temp->dest)) {
                        edgeColor = travelers[t].color;
                        break;
                    }
                }

                DrawArrow(start, end, nodeRadius, 2.0f, edgeColor);

                char weightText[10];
                sprintf(weightText, "%d", temp->weight);
                Vector2 midPoint = { (start.x + end.x) / 2, (start.y + end.y) / 2 };
                float dx = end.x - start.x, dy = end.y - start.y;
                float length = sqrtf(dx * dx + dy * dy);
                if (length == 0) length = 1.0f;
                Vector2 normal    = { -dy / length, dx / length };
                Vector2 weightPos = { midPoint.x + normal.x * 15.0f,
                                      midPoint.y + normal.y * 15.0f };
                DrawText(weightText, (int)weightPos.x - 5, (int)weightPos.y - 10, 20, MAROON);

                temp = temp->next;
            }
        }

        // Draw nodes
        for (int i = 0; i < N; i++) {
            float nodeRadius = 25.0f;
            Color nodeColor  = LIGHTGRAY;

            for (int t = 0; t < numTravelers; t++) {
                if (travelers[t].pathLen > 0) {
                    if (i == travelers[t].startNode) nodeColor = travelers[t].color;
                    else if (i == travelers[t].endNode) nodeColor = travelers[t].color;
                }
            }

            DrawCircleV(nodePositions[i].position, nodeRadius, nodeColor);
            DrawCircleLines(nodePositions[i].position.x, nodePositions[i].position.y,
                            nodeRadius, BLACK);

            char idText[12];
            sprintf(idText, "%d", i);
            int textWidth = MeasureText(idText, 20);
            DrawText(idText,
                     (int)(nodePositions[i].position.x - (float)textWidth / 2),
                     (int)(nodePositions[i].position.y - 10), 20, BLACK);
        }

        // Draw traveler circles (color only, no label)
        for (int i = 0; i < numTravelers; i++) {
            if (travelers[i].pathLen > 0) {
                DrawCircleV(travelers[i].position, 15, travelers[i].color);
                DrawCircleLines(travelers[i].position.x, travelers[i].position.y, 15, BLACK);
            }
        }

        // ── HUD ───────────────────────────────────────────────────────────────
        char infoText[32];
        sprintf(infoText, "Travelers: %d", numTravelers);
        DrawText("Milestone 5: IPC via Pipes", 10, 10, 20, DARKBLUE);
        DrawText(infoText, 10, 32, 18, DARKGRAY);

        DrawRectangleRec(playBtn, isPlaying ? RED : LIME);
        DrawText(isPlaying ? "STOP" : "PLAY", playBtn.x + 25, playBtn.y + 10, 20, BLACK);

        // Error / status messages (same as milestone 4)
        if (hasNegativeWeight) {
            DrawText("Error: Negative weights detected in graph!",
                     screenWidth / 2 - 200, 80, 20, RED);
        } else {
            int noPathCount = 0;
            for (int i = 0; i < numTravelers; i++) {
                if (!canAnimate(&travelers[i]) &&
                    travelers[i].startNode != travelers[i].endNode)
                    noPathCount++;
            }
            if (noPathCount > 0) {
                char noPathMsg[64];
                sprintf(noPathMsg, "%d traveler(s) have no valid path", noPathCount);
                DrawText(noPathMsg, screenWidth / 2 - 130, 80, 18, RED);
            }
        }

        if (!hasNegativeWeight) {
            int finishedCount = 0;
            for (int i = 0; i < numTravelers; i++)
                if (hasReachedDestination(&travelers[i])) finishedCount++;
            if (finishedCount > 0) {
                char msg[64];
                sprintf(msg, "%d traveler(s) reached destination!", finishedCount);
                DrawText(msg, screenWidth / 2 - 140, 50, 20, DARKGREEN);
            }
        }

        EndDrawing();
    }

    CloseWindow();

    for (int i = 0; i < numTravelers; i++) {
        if (travelers[i].pipefd[0] >= 0) close(travelers[i].pipefd[0]);
        if (travelers[i].ackPipefd[1] >= 0) close(travelers[i].ackPipefd[1]);
        terminateChild(&travelers[i]);
    }
    for (int i = 0; i < numTravelers; i++)
        if (travelers[i].path) free(travelers[i].path);
    free(travelers);
    free(nodePositions);
    freeGraph(graph);

    return 0;
}
