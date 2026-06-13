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
#include <semaphore.h>
#include <errno.h>
#include <time.h>
#include "raylib.h"

typedef struct _XDisplay Display;
extern Display* XOpenDisplay(const char* display_name);
extern int XCloseDisplay(Display* display);

#define INF INT_MAX
#define MAX_TRAVELERS 15
#define SEM_NAME_LEN 64
#define GUI_NODE_EMPTY_DELAY 0.7

/*
 * IPC Choice: Pipes (pipe())
 * Reason: Pipes are simple, built-in, and well-suited for one-directional
 * parent-child communication. Each child gets its own pipe to send node status
 * messages to the parent. The parent uses non-blocking reads (O_NONBLOCK) on
 * all pipe read-ends to poll messages without blocking the raylib GUI loop.
 *
 * Synchronization: Each graph node has a named POSIX semaphore initialized to
 * 1. A child must lock the node semaphore before it enters and waits inside
 * that node for one second. Other children block outside the node until the
 * semaphore is released. The GUI mirrors the same rule with parent-side node
 * occupancy state so it never draws two travelers inside the same node.
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
    STATE_WAITING_OUTSIDE,
    STATE_WAITING_NODE,
    STATE_FINISHED
} SimState;

// ─── IPC message sent from child → parent via pipe ───────────────────────────

typedef enum {
    MSG_WAITING_OUTSIDE,
    MSG_ENTERED_NODE,
    MSG_LEFT_NODE
} MessageType;

typedef struct {
    pid_t pid;
    int currentNode;
    int nextNode;
    MessageType type;
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
    int waitingForNode;
    double lastUpdateTime;
    Vector2 position;
    bool finished;
    bool childTerminated;
    bool childStarted;      // has SIGUSR1 been sent to this child?

    // Pipe: pipefd[0] = read end (parent), pipefd[1] = write end (child)
    int pipefd[2];

    // Path computed by parent for GUI animation
    int* path;
    int  pathLen;
} Traveler;

typedef struct {
    int occupantTraveler;
    double availableTime;
} GuiNodeLock;

void runChild(int startNode, int endNode, int writeFd, Graph* graph, sem_t** nodeSemaphores);

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

void waitForStartSignal(void) {
    g_startSignalReceived = 0;

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = sigusr1Handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR1, &action, NULL);

    sigset_t waitMask;
    sigemptyset(&waitMask);
    while (!g_startSignalReceived) {
        sigsuspend(&waitMask);
    }

    sigset_t unblockMask;
    sigemptyset(&unblockMask);
    sigaddset(&unblockMask, SIGUSR1);
    sigprocmask(SIG_UNBLOCK, &unblockMask, NULL);
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

Vector2 getOutsideNodePosition(NodePos* nodePositions, int node, int travelerIndex) {
    float angle = (float)(travelerIndex % MAX_TRAVELERS) * 2.0f * PI / (float)MAX_TRAVELERS;
    float radius = 48.0f + (float)(travelerIndex / MAX_TRAVELERS) * 18.0f;
    Vector2 center = nodePositions[node].position;
    Vector2 pos = {
        center.x + radius * cosf(angle),
        center.y + radius * sinf(angle)
    };
    return pos;
}

void sendNodeMessage(int writeFd, pid_t pid, int currentNode, int nextNode, MessageType type) {
    NodeMessage msg = { pid, currentNode, nextNode, type };
    write(writeFd, &msg, sizeof(msg));
}

void buildSemaphoreName(char* out, size_t outSize, pid_t ownerPid, int node) {
    snprintf(out, outSize, "/os_graph_node_%d_%d", (int)ownerPid, node);
}

// ─── Traveler helpers (parent side) ──────────────────────────────────────────

bool canAnimate(const Traveler* t)            { return t->pathLen > 0; }
bool hasReachedDestination(const Traveler* t) {
    if (!t->finished) return false;
    if (t->startNode == t->endNode) return true;
    return t->pathLen > 0;
}
bool allAnimatableFinished(const Traveler* travelers, int num) {
    for (int i = 0; i < num; i++)
        if (canAnimate(&travelers[i]) && !travelers[i].finished) return false;
    return true;
}

void terminateChild(Traveler* t) {
    if (t->pid > 0 && !t->childTerminated) {
        kill(t->pid, SIGCONT);
        kill(t->pid, SIGTERM);
        waitpid(t->pid, NULL, WNOHANG);
        t->childTerminated = true;
    }
}

void pauseChildren(Traveler* travelers, int num) {
    for (int i = 0; i < num; i++) {
        if (travelers[i].pid > 0 && travelers[i].childStarted &&
            !travelers[i].childTerminated) {
            kill(travelers[i].pid, SIGSTOP);
        }
    }
}

void resumeChildren(Traveler* travelers, int num) {
    for (int i = 0; i < num; i++) {
        if (travelers[i].pid > 0 && travelers[i].childStarted &&
            !travelers[i].childTerminated) {
            kill(travelers[i].pid, SIGCONT);
        }
    }
}

void closeTravelerRuntime(Traveler* travelers, int numTravelers) {
    for (int i = 0; i < numTravelers; i++) {
        if (travelers[i].pipefd[0] >= 0) {
            close(travelers[i].pipefd[0]);
            travelers[i].pipefd[0] = -1;
        }
        if (travelers[i].pipefd[1] >= 0) {
            close(travelers[i].pipefd[1]);
            travelers[i].pipefd[1] = -1;
        }
        terminateChild(&travelers[i]);
        travelers[i].pid = -1;
        travelers[i].childStarted = false;
    }
}

void reapTravelerRuntime(Traveler* travelers, int numTravelers) {
    for (int i = 0; i < numTravelers; i++) {
        if (travelers[i].pid > 0 && !travelers[i].childTerminated) {
            int status;
            pid_t result = waitpid(travelers[i].pid, &status, WNOHANG);
            if (result == 0) {
                kill(travelers[i].pid, SIGCONT);
                kill(travelers[i].pid, SIGTERM);
                waitpid(travelers[i].pid, &status, WNOHANG);
            }
            travelers[i].childTerminated = true;
        }
        if (travelers[i].pipefd[0] >= 0) {
            close(travelers[i].pipefd[0]);
            travelers[i].pipefd[0] = -1;
        }
        if (travelers[i].pipefd[1] >= 0) {
            close(travelers[i].pipefd[1]);
            travelers[i].pipefd[1] = -1;
        }
        travelers[i].pid = -1;
        travelers[i].childStarted = false;
    }
}

void resetNodeSemaphores(sem_t** nodeSemaphores, int numNodes) {
    for (int i = 0; i < numNodes; i++) {
        while (sem_trywait(nodeSemaphores[i]) == 0) {
            ;
        }
        sem_post(nodeSemaphores[i]);
    }
}

void sleepForMicroseconds(long microseconds) {
    struct timespec remaining;
    remaining.tv_sec = microseconds / 1000000L;
    remaining.tv_nsec = (microseconds % 1000000L) * 1000L;

    while (nanosleep(&remaining, &remaining) == -1 && errno == EINTR) {
        ;
    }
}

bool spawnTravelerChildren(Traveler* travelers, int numTravelers,
                           Graph* graph, sem_t** nodeSemaphores) {
    sigset_t startMask;
    sigset_t oldMask;
    sigemptyset(&startMask);
    sigaddset(&startMask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &startMask, &oldMask);

    for (int i = 0; i < numTravelers; i++) {
        travelers[i].pid = -1;
        travelers[i].childTerminated = false;
        travelers[i].childStarted = false;
        travelers[i].pipefd[0] = -1;
        travelers[i].pipefd[1] = -1;
    }

    for (int i = 0; i < numTravelers; i++) {
        if (pipe(travelers[i].pipefd) < 0) {
            perror("pipe failed");
            for (int j = 0; j <= i; j++) {
                if (travelers[j].pipefd[0] >= 0) close(travelers[j].pipefd[0]);
                if (travelers[j].pipefd[1] >= 0) close(travelers[j].pipefd[1]);
                travelers[j].pipefd[0] = -1;
                travelers[j].pipefd[1] = -1;
            }
            sigprocmask(SIG_SETMASK, &oldMask, NULL);
            return false;
        }
    }

    for (int i = 0; i < numTravelers; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            for (int j = 0; j < i; j++) terminateChild(&travelers[j]);
            for (int j = 0; j < numTravelers; j++) {
                if (travelers[j].pipefd[0] >= 0) close(travelers[j].pipefd[0]);
                if (travelers[j].pipefd[1] >= 0) close(travelers[j].pipefd[1]);
                travelers[j].pipefd[0] = -1;
                travelers[j].pipefd[1] = -1;
            }
            sigprocmask(SIG_SETMASK, &oldMask, NULL);
            return false;
        }

        if (pid == 0) {
            for (int j = 0; j < numTravelers; j++)
                close(travelers[j].pipefd[0]);
            for (int j = 0; j < numTravelers; j++) {
                if (j != i) close(travelers[j].pipefd[1]);
            }
            runChild(travelers[i].startNode, travelers[i].endNode,
                     travelers[i].pipefd[1], graph, nodeSemaphores);
        }

        travelers[i].pid = pid;
    }

    for (int i = 0; i < numTravelers; i++) {
        close(travelers[i].pipefd[1]);
        travelers[i].pipefd[1] = -1;

        int flags = fcntl(travelers[i].pipefd[0], F_GETFL, 0);
        fcntl(travelers[i].pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    sigprocmask(SIG_SETMASK, &oldMask, NULL);
    return true;
}

// ─── Child process logic ──────────────────────────────────────────────────────
/*
 * Each child:
 *   1. Installs a SIGUSR1 handler and waits (pause) for the parent's start signal.
 *   2. Calculates its own Dijkstra path.
 *   3. Travels the path: before each node it reports that it is waiting,
 *      locks the node semaphore, reports that it entered, waits one second,
 *      releases the semaphore, and then travels to the next edge.
 *   4. Exits normally when done.
 *
 * Sleep times match the GUI animation:
 *   - 300 ms × edge_weight  (edge travel)
 *   - 1000 ms critical section inside each node
 */

void runChild(int startNode, int endNode, int writeFd, Graph* graph, sem_t** nodeSemaphores) {
    pid_t myPid = getpid();

    waitForStartSignal();

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
        sendNodeMessage(writeFd, myPid, startNode, -1, MSG_ENTERED_NODE);
        close(writeFd);
        free(path);
        exit(0);
    }

    // Travel the path
    for (int i = 0; i < pathLen; i++) {
        int currentNode = path[i];
        int nextNode    = (i < pathLen - 1) ? path[i + 1] : -1;

        bool reportedWaiting = false;
        while (sem_trywait(nodeSemaphores[currentNode]) == -1) {
            if (errno == EINTR) continue;

            if (!reportedWaiting) {
                sendNodeMessage(writeFd, myPid, currentNode, nextNode, MSG_WAITING_OUTSIDE);
                reportedWaiting = true;
            }

            while (sem_wait(nodeSemaphores[currentNode]) == -1 && errno == EINTR) {
                ;
            }
            break;
        }
        sendNodeMessage(writeFd, myPid, currentNode, nextNode, MSG_ENTERED_NODE);
        sleepForMicroseconds(1000L * 1000L);
        sem_post(nodeSemaphores[currentNode]);
        sendNodeMessage(writeFd, myPid, currentNode, nextNode, MSG_LEFT_NODE);

        if (nextNode == -1) break; // reached destination

        // Simulate edge travel: 300ms × edge weight
        int edgeWeight = getEdgeWeight(graph, currentNode, nextNode);
        if (edgeWeight <= 0) edgeWeight = 1;
        sleepForMicroseconds((long)edgeWeight * 300L * 1000L);
    }

    close(writeFd);
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

void beginEdgeTravel(Traveler* traveler, Graph* graph, double currentTime) {
    traveler->state = STATE_MOVING;
    traveler->edgeCurrentJump = 0;
    int src = traveler->path[traveler->currentPathIndex];
    int dst = traveler->path[traveler->currentPathIndex + 1];
    traveler->edgeTotalJumps = getEdgeWeight(graph, src, dst);
    if (traveler->edgeTotalJumps <= 0) traveler->edgeTotalJumps = 1;
    traveler->lastUpdateTime = currentTime;
}

bool tryEnterGuiNode(Traveler* traveler, int travelerIndex, GuiNodeLock* nodeLocks,
                     NodePos* nodePositions, int node, double currentTime) {
    if (nodeLocks[node].occupantTraveler == -1 &&
        currentTime >= nodeLocks[node].availableTime) {
        nodeLocks[node].occupantTraveler = travelerIndex;
        traveler->state = STATE_WAITING_NODE;
        traveler->waitingForNode = node;
        traveler->position = nodePositions[node].position;
        traveler->lastUpdateTime = currentTime;
        return true;
    }

    traveler->state = STATE_WAITING_OUTSIDE;
    traveler->waitingForNode = node;
    traveler->position = getOutsideNodePosition(nodePositions, node, travelerIndex);
    return false;
}

void releaseGuiNode(Traveler* traveler, int travelerIndex, GuiNodeLock* nodeLocks,
                    double currentTime) {
    int node = traveler->waitingForNode;
    if (node >= 0 && nodeLocks[node].occupantTraveler == travelerIndex) {
        nodeLocks[node].occupantTraveler = -1;
        nodeLocks[node].availableTime = currentTime + GUI_NODE_EMPTY_DELAY;
    }
    traveler->waitingForNode = -1;
}

void updateTraveler(Traveler* traveler, int travelerIndex, Graph* graph,
                    NodePos* nodePositions, GuiNodeLock* nodeLocks,
                    bool isPlaying, double currentTime) {
    if (!isPlaying || traveler->finished || traveler->pathLen <= 0) return;

    if (traveler->state == STATE_IDLE) {
        int currentNode = traveler->path[traveler->currentPathIndex];
        tryEnterGuiNode(traveler, travelerIndex, nodeLocks, nodePositions,
                        currentNode, currentTime);

    } else if (traveler->state == STATE_WAITING_OUTSIDE) {
        int node = traveler->waitingForNode;
        tryEnterGuiNode(traveler, travelerIndex, nodeLocks, nodePositions,
                        node, currentTime);

    } else if (traveler->state == STATE_WAITING_NODE) {
        if (currentTime - traveler->lastUpdateTime >= 1.0) {
            releaseGuiNode(traveler, travelerIndex, nodeLocks, currentTime);

            if (traveler->currentPathIndex >= traveler->pathLen - 1) {
                traveler->state = STATE_FINISHED;
                traveler->finished = true;
                traveler->position = getOutsideNodePosition(nodePositions,
                                                            traveler->path[traveler->currentPathIndex],
                                                            travelerIndex);
            } else {
                beginEdgeTravel(traveler, graph, currentTime);
            }
        }

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
                int arrivedNode = traveler->path[traveler->currentPathIndex];
                tryEnterGuiNode(traveler, travelerIndex, nodeLocks, nodePositions,
                                arrivedNode, currentTime);
            }
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
    traveler->waitingForNode   = -1;
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
        travelers[i].waitingForNode  = -1;
        travelers[i].pathLen         = 0;
        travelers[i].path            = NULL;
        travelers[i].pipefd[0]       = -1;
        travelers[i].pipefd[1]       = -1;

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
        }
    }
    fclose(file);

    sem_t** nodeSemaphores = (sem_t**)calloc(N, sizeof(sem_t*));
    char (*semaphoreNames)[SEM_NAME_LEN] = calloc(N, sizeof(*semaphoreNames));
    if (!nodeSemaphores || !semaphoreNames) {
        perror("semaphore allocation failed");
        for (int j = 0; j < numTravelers; j++)
            if (travelers[j].path) free(travelers[j].path);
        free(nodeSemaphores);
        free(semaphoreNames);
        free(travelers);
        freeGraph(graph);
        return 1;
    }

    pid_t semaphoreOwnerPid = getpid();
    for (int i = 0; i < N; i++) {
        buildSemaphoreName(semaphoreNames[i], SEM_NAME_LEN, semaphoreOwnerPid, i);
        sem_unlink(semaphoreNames[i]);
        nodeSemaphores[i] = sem_open(semaphoreNames[i], O_CREAT | O_EXCL, 0600, 1);
        if (nodeSemaphores[i] == SEM_FAILED) {
            perror("sem_open failed");
            for (int j = 0; j < i; j++) {
                sem_close(nodeSemaphores[j]);
                sem_unlink(semaphoreNames[j]);
            }
            for (int j = 0; j < numTravelers; j++)
                if (travelers[j].path) free(travelers[j].path);
            free(nodeSemaphores);
            free(semaphoreNames);
            free(travelers);
            freeGraph(graph);
            return 1;
        }
    }

    if (!spawnTravelerChildren(travelers, numTravelers, graph, nodeSemaphores)) {
        for (int j = 0; j < numTravelers; j++)
            if (travelers[j].path) free(travelers[j].path);
        for (int j = 0; j < N; j++) {
            sem_close(nodeSemaphores[j]);
            sem_unlink(semaphoreNames[j]);
        }
        free(nodeSemaphores);
        free(semaphoreNames);
        free(travelers);
        freeGraph(graph);
        return 1;
    }

    // ── GUI setup ────────────────────────────────────────────────────────────
    const int screenWidth  = 900;
    const int screenHeight = 650;
    Display* testDisplay = XOpenDisplay(NULL);
    if (!testDisplay) {
        fprintf(stderr, "Failed to initialize GUI window\n");
        closeTravelerRuntime(travelers, numTravelers);
        for (int i = 0; i < N; i++) {
            sem_close(nodeSemaphores[i]);
            sem_unlink(semaphoreNames[i]);
        }
        for (int i = 0; i < numTravelers; i++)
            if (travelers[i].path) free(travelers[i].path);
        free(nodeSemaphores);
        free(semaphoreNames);
        free(travelers);
        freeGraph(graph);
        return 1;
    }
    XCloseDisplay(testDisplay);

    InitWindow(screenWidth, screenHeight, "Traffic Simulation - Milestone 6");
    if (!IsWindowReady() || GetWindowHandle() == NULL || GetMonitorCount() <= 0) {
        fprintf(stderr, "Failed to initialize GUI window\n");
        closeTravelerRuntime(travelers, numTravelers);
        for (int i = 0; i < N; i++) {
            sem_close(nodeSemaphores[i]);
            sem_unlink(semaphoreNames[i]);
        }
        for (int i = 0; i < numTravelers; i++)
            if (travelers[i].path) free(travelers[i].path);
        free(nodeSemaphores);
        free(semaphoreNames);
        free(travelers);
        freeGraph(graph);
        return 1;
    }
    SetTargetFPS(60);

    NodePos* nodePositions = (NodePos*)malloc(N * sizeof(NodePos));
    GuiNodeLock* nodeLocks = (GuiNodeLock*)malloc(N * sizeof(GuiNodeLock));
    if (!nodePositions || !nodeLocks) {
        CloseWindow();
        for (int i = 0; i < numTravelers; i++) {
            if (travelers[i].pipefd[0] >= 0) close(travelers[i].pipefd[0]);
            terminateChild(&travelers[i]);
        }
        for (int i = 0; i < N; i++) {
            sem_close(nodeSemaphores[i]);
            sem_unlink(semaphoreNames[i]);
        }
        for (int i = 0; i < numTravelers; i++)
            if (travelers[i].path) free(travelers[i].path);
        free(nodePositions);
        free(nodeLocks);
        free(nodeSemaphores);
        free(semaphoreNames);
        free(travelers);
        freeGraph(graph);
        return 1;
    }
    for (int i = 0; i < N; i++) {
        nodeLocks[i].occupantTraveler = -1;
        nodeLocks[i].availableTime = 0.0;
    }

    Vector2 center = { (float)screenWidth / 2, (float)screenHeight / 2 };
    float layoutRadius = 220.0f;
    for (int i = 0; i < N; i++) {
        float angle = (float)i * 2.0f * PI / (float)N;
        nodePositions[i].position.x = center.x + layoutRadius * cosf(angle);
        nodePositions[i].position.y = center.y + layoutRadius * sinf(angle);
    }

    int* initialNodeCounts = (int*)calloc(N, sizeof(int));
    for (int i = 0; i < numTravelers; i++) {
        int node = (travelers[i].pathLen > 0 && travelers[i].path) ?
                   travelers[i].path[0] : travelers[i].startNode;
        if (initialNodeCounts && initialNodeCounts[node] > 0)
            travelers[i].position = getOutsideNodePosition(nodePositions, node, i);
        else
            travelers[i].position = nodePositions[node].position;
        if (initialNodeCounts) initialNodeCounts[node]++;
    }
    free(initialNodeCounts);

    bool isPlaying  = false;
    bool startSent  = false;   // have we sent SIGUSR1 to all children?
    Rectangle playBtn = { 10, 50, 100, 40 };

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (!WindowShouldClose()) {

        // ── Play/stop button ──────────────────────────────────────────────────
        if (CheckCollisionPointRec(GetMousePosition(), playBtn) &&
            IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            if (isPlaying) {
                isPlaying = false;
                pauseChildren(travelers, numTravelers);
            } else {
                if (startSent && allAnimatableFinished(travelers, numTravelers)) {
                    reapTravelerRuntime(travelers, numTravelers);
                    resetNodeSemaphores(nodeSemaphores, N);
                    for (int i = 0; i < N; i++) {
                        nodeLocks[i].occupantTraveler = -1;
                        nodeLocks[i].availableTime = 0.0;
                    }
                    for (int i = 0; i < numTravelers; i++)
                        resetTraveler(&travelers[i], nodePositions);

                    if (!spawnTravelerChildren(travelers, numTravelers,
                                               graph, nodeSemaphores)) {
                        isPlaying = false;
                        startSent = false;
                        continue;
                    }
                    startSent = false;
                }

                isPlaying = true;
                if (!startSent) {
                    for (int i = 0; i < numTravelers; i++) {
                        if (travelers[i].pid > 0 && !travelers[i].childTerminated) {
                            kill(travelers[i].pid, SIGUSR1);
                            travelers[i].childStarted = true;
                        }
                    }
                    startSent = true;
                } else {
                    resumeChildren(travelers, numTravelers);
                }
            }
        }

        // ── Poll ONE message per child per frame ──────────────────────────────
        if (isPlaying || allAnimatableFinished(travelers, numTravelers)) {
            for (int i = 0; i < numTravelers; i++) {
                if (travelers[i].pipefd[0] < 0) continue;

                NodeMessage msg;
                if (pollChildPipe(travelers[i].pipefd[0], &msg)) {
                    if (msg.type == MSG_WAITING_OUTSIDE) {
                        printf("[PID=%d] waiting outside node %d\n",
                               msg.pid, msg.currentNode);
                        fflush(stdout);
                    } else if (msg.type == MSG_ENTERED_NODE) {
                        if (msg.nextNode == -1) {
                            printf("[PID=%d] arrived at node %d | DESTINATION\n",
                                   msg.pid, msg.currentNode);
                        } else {
                            printf("[PID=%d] arrived at node %d | next node: %d\n",
                                   msg.pid, msg.currentNode, msg.nextNode);
                        }
                        fflush(stdout);
                    } else if (msg.type == MSG_LEFT_NODE && msg.nextNode == -1) {
                        close(travelers[i].pipefd[0]);
                        travelers[i].pipefd[0] = -1;

                        if (travelers[i].pid > 0 && !travelers[i].childTerminated) {
                            pid_t result = waitpid(travelers[i].pid, NULL, WNOHANG);
                            if (result == travelers[i].pid || (result == -1 && errno == ECHILD)) {
                                travelers[i].childTerminated = true;
                            }
                        }
                        printf("[PID=%d] finished\n", msg.pid);
                        fflush(stdout);
                    }
                }
            }
        }

        // ── Animate travelers ─────────────────────────────────────────────────
        double currentTime = GetTime();
        if (isPlaying) {
            for (int i = 0; i < numTravelers; i++)
                updateTraveler(&travelers[i], i, graph, nodePositions, nodeLocks,
                               isPlaying, currentTime);

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

        // Draw traveler circles. Waiting travelers are shown outside the node
        // with an orange outline so the blocked state is visible.
        for (int i = 0; i < numTravelers; i++) {
            if (travelers[i].pathLen > 0) {
                if (travelers[i].state == STATE_WAITING_OUTSIDE) {
                    DrawCircleV(travelers[i].position, 13, travelers[i].color);
                    DrawCircleLines(travelers[i].position.x, travelers[i].position.y, 17, ORANGE);
                    DrawCircleLines(travelers[i].position.x, travelers[i].position.y, 13, BLACK);
                } else if (!travelers[i].finished) {
                    DrawCircleV(travelers[i].position, 15, travelers[i].color);
                    DrawCircleLines(travelers[i].position.x, travelers[i].position.y, 15, BLACK);
                } else {
                    DrawCircleV(travelers[i].position, 11, travelers[i].color);
                    DrawCircleLines(travelers[i].position.x, travelers[i].position.y, 11, DARKGREEN);
                }
            }
        }

        // ── HUD ───────────────────────────────────────────────────────────────
        char infoText[32];
        sprintf(infoText, "Travelers: %d", numTravelers);
        DrawText("Milestone 6: Node Semaphores", 10, 10, 20, DARKBLUE);
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
        terminateChild(&travelers[i]);
    }
    for (int i = 0; i < numTravelers; i++)
        if (travelers[i].path) free(travelers[i].path);
    for (int i = 0; i < N; i++) {
        sem_close(nodeSemaphores[i]);
        sem_unlink(semaphoreNames[i]);
    }
    free(travelers);
    free(nodePositions);
    free(nodeLocks);
    free(nodeSemaphores);
    free(semaphoreNames);
    freeGraph(graph);

    return 0;
}
