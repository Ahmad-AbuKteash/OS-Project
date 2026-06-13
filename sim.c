#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "raylib.h"

#define INF INT_MAX
#define MAX_TRAVELERS 15

// Graph structures
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

typedef struct {
    pid_t pid;
    int startNode;
    int endNode;
    int* path;
    int pathLen;
    Color color;
    SimState state;
    int currentPathIndex;
    int edgeCurrentJump;
    int edgeTotalJumps;
    double lastUpdateTime;
    Vector2 position;
    bool finished;
    bool childTerminated;
} Traveler;

static const Color TRAVELER_COLORS[] = {
    GOLD, SKYBLUE, LIME, PURPLE, ORANGE, PINK,
    MAROON, DARKGREEN, VIOLET, BEIGE, BROWN, YELLOW
};

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
    if (!graph->adjLists) {
        free(graph);
        return NULL;
    }
    for (int i = 0; i < vertices; i++)
        graph->adjLists[i] = NULL;
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

int* dijkstra(Graph* graph, int startNode, int endNode, int* pathLen) {
    int n = graph->numVertices;
    int* dist = (int*)malloc(n * sizeof(int));
    int* prev = (int*)malloc(n * sizeof(int));
    bool* visited = (bool*)malloc(n * sizeof(bool));

    for (int i = 0; i < n; i++) {
        dist[i] = INF;
        prev[i] = -1;
        visited[i] = false;
    }

    dist[startNode] = 0;

    for (int count = 0; count < n; count++) {
        int u = -1;
        int minDist = INF;

        for (int i = 0; i < n; i++) {
            if (!visited[i] && dist[i] < minDist) {
                minDist = dist[i];
                u = i;
            }
        }

        if (u == -1 || u == endNode) break;

        visited[u] = true;

        Node* temp = graph->adjLists[u];
        while (temp) {
            int v = temp->dest;
            int weight = temp->weight;
            if (!visited[v] && dist[u] != INF && dist[u] + weight < dist[v]) {
                dist[v] = dist[u] + weight;
                prev[v] = u;
            }
            temp = temp->next;
        }
    }

    int* path = NULL;
    *pathLen = 0;

    if (dist[endNode] != INF) {
        int tempPath[100];
        int count = 0;
        for (int at = endNode; at != -1; at = prev[at]) {
            tempPath[count++] = at;
        }
        *pathLen = count;
        path = (int*)malloc(count * sizeof(int));
        for (int i = 0; i < count; i++) {
            path[i] = tempPath[count - 1 - i];
        }
    }

    free(dist);
    free(prev);
    free(visited);
    return path;
}

void DrawArrow(Vector2 start, Vector2 end, float radius, float thickness, Color color) {
    float angle = atan2f(end.y - start.y, end.x - start.x);
    Vector2 adjustedEnd = { end.x - radius * cosf(angle), end.y - radius * sinf(angle) };

    DrawLineEx(start, adjustedEnd, thickness, color);

    float arrowHeadLen = 15;
    float arrowHeadAngle = PI / 6;
    Vector2 p1 = { adjustedEnd.x - arrowHeadLen * cosf(angle - arrowHeadAngle), adjustedEnd.y - arrowHeadLen * sinf(angle - arrowHeadAngle) };
    Vector2 p2 = { adjustedEnd.x - arrowHeadLen * cosf(angle + arrowHeadAngle), adjustedEnd.y - arrowHeadLen * sinf(angle + arrowHeadAngle) };

    DrawTriangle(adjustedEnd, p2, p1, color);
}

bool isPathEdge(const int* path, int pathLen, int src, int dst) {
    if (pathLen < 2) return false;
    for (int j = 0; j < pathLen - 1; j++) {
        if (path[j] == src && path[j + 1] == dst) return true;
    }
    return false;
}

bool canAnimate(const Traveler* traveler) {
    return traveler->pathLen > 1;
}

bool hasReachedDestination(const Traveler* traveler) {
    if (!traveler->finished) return false;
    if (traveler->startNode == traveler->endNode) return true;
    return traveler->pathLen > 1;
}

bool allAnimatableFinished(const Traveler* travelers, int numTravelers) {
    for (int i = 0; i < numTravelers; i++) {
        if (canAnimate(&travelers[i]) && !travelers[i].finished) {
            return false;
        }
    }
    return true;
}

void terminateChild(Traveler* traveler) {
    if (traveler->pid > 0 && !traveler->childTerminated) {
        kill(traveler->pid, SIGTERM);
        waitpid(traveler->pid, NULL, 0);
        traveler->childTerminated = true;
    }
}

pid_t spawnChild(void) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return -1;
    }
    if (pid == 0) {
        printf("[%d] started\n", getpid());
        fflush(stdout);
        while (1) {
            pause();
        }
        return 0;
    }
    return pid;
}

void updateTraveler(Traveler* traveler, Graph* graph, NodePos* nodePositions, bool isPlaying, double currentTime) {
    if (!isPlaying || traveler->finished || traveler->pathLen <= 1) return;

    if (traveler->state == STATE_IDLE) {
        traveler->state = STATE_MOVING;
        traveler->edgeCurrentJump = 0;
        int src = traveler->path[traveler->currentPathIndex];
        int dst = traveler->path[traveler->currentPathIndex + 1];
        traveler->edgeTotalJumps = getEdgeWeight(graph, src, dst);
        if (traveler->edgeTotalJumps <= 0) {
            traveler->edgeTotalJumps = 1;
        }
        traveler->lastUpdateTime = currentTime;
    } else if (traveler->state == STATE_MOVING) {
        if (currentTime - traveler->lastUpdateTime >= 0.3) {
            traveler->edgeCurrentJump++;
            traveler->lastUpdateTime = currentTime;

            Vector2 pStart = nodePositions[traveler->path[traveler->currentPathIndex]].position;
            Vector2 pEnd = nodePositions[traveler->path[traveler->currentPathIndex + 1]].position;
            float t = (float)traveler->edgeCurrentJump / traveler->edgeTotalJumps;

            traveler->position.x = pStart.x + (pEnd.x - pStart.x) * t;
            traveler->position.y = pStart.y + (pEnd.y - pStart.y) * t;

            if (traveler->edgeCurrentJump >= traveler->edgeTotalJumps) {
                traveler->currentPathIndex++;
                if (traveler->currentPathIndex == traveler->pathLen - 1) {
                    traveler->state = STATE_FINISHED;
                    traveler->finished = true;
                    terminateChild(traveler);
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

    traveler->state = STATE_IDLE;
    traveler->currentPathIndex = 0;
    traveler->edgeCurrentJump = 0;
    traveler->edgeTotalJumps = 0;
    traveler->finished = false;
    traveler->position = nodePositions[traveler->path[0]].position;

    terminateChild(traveler);
    pid_t pid = spawnChild();
    if (pid > 0) {
        traveler->pid = pid;
        traveler->childTerminated = false;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file_name>\n", argv[0]);
        return 1;
    }

    FILE* file = fopen(argv[1], "r");
    if (!file) {
        perror("Error opening file");
        return 1;
    }

    int N, M;
    if (fscanf(file, "%d %d", &N, &M) != 2) {
        fprintf(stderr, "Invalid file format\n");
        fclose(file);
        return 1;
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
    if (fscanf(file, "%d", &numTravelers) != 1 || numTravelers <= 0 || numTravelers > MAX_TRAVELERS) {
        fprintf(stderr, "Invalid number of travelers\n");
        fclose(file);
        freeGraph(graph);
        return 1;
    }

    Traveler* travelers = (Traveler*)calloc(numTravelers, sizeof(Traveler));
    if (!travelers) {
        fclose(file);
        freeGraph(graph);
        return 1;
    }

    for (int i = 0; i < numTravelers; i++) {
        int startNode, endNode;
        if (fscanf(file, "%d %d", &startNode, &endNode) != 2) {
            fprintf(stderr, "Invalid traveler data\n");
            fclose(file);
            freeGraph(graph);
            free(travelers);
            return 1;
        }

        travelers[i].startNode = startNode;
        travelers[i].endNode = endNode;
        travelers[i].color = TRAVELER_COLORS[i % (sizeof(TRAVELER_COLORS) / sizeof(TRAVELER_COLORS[0]))];
        travelers[i].pid = -1;
        travelers[i].childTerminated = false;
        travelers[i].finished = false;
        travelers[i].state = STATE_IDLE;
        travelers[i].pathLen = 0;
        travelers[i].path = NULL;

        if (hasNegativeWeight) {
            travelers[i].finished = true;
        } else if (startNode != endNode) {
            travelers[i].path = dijkstra(graph, startNode, endNode, &travelers[i].pathLen);
            if (travelers[i].pathLen == 0) {
                travelers[i].finished = true;
            }
        } else {
            travelers[i].pathLen = 1;
            travelers[i].path = (int*)malloc(sizeof(int));
            travelers[i].path[0] = startNode;
            travelers[i].finished = true;
        }
    }
    fclose(file);

    for (int i = 0; i < numTravelers; i++) {
        pid_t pid = spawnChild();
        if (pid < 0) {
            for (int j = 0; j < i; j++) {
                terminateChild(&travelers[j]);
            }
            for (int j = 0; j < numTravelers; j++) {
                if (travelers[j].path) free(travelers[j].path);
            }
            free(travelers);
            freeGraph(graph);
            return 1;
        }
        travelers[i].pid = pid;
    }

    usleep(100000);

    for (int i = 0; i < numTravelers; i++) {
        if (travelers[i].finished) {
            terminateChild(&travelers[i]);
        }
    }

    const int screenWidth = 800;
    const int screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "Traffic Simulation - Milestone 4");
    SetTargetFPS(60);

    NodePos* nodePositions = (NodePos*)malloc(N * sizeof(NodePos));
    Vector2 center = { (float)screenWidth / 2, (float)screenHeight / 2 };
    float layoutRadius = 200.0f;
    for (int i = 0; i < N; i++) {
        float angle = (float)i * 2.0f * PI / (float)N;
        nodePositions[i].position.x = center.x + layoutRadius * cosf(angle);
        nodePositions[i].position.y = center.y + layoutRadius * sinf(angle);
    }

    for (int i = 0; i < numTravelers; i++) {
        if (travelers[i].pathLen > 0 && travelers[i].path) {
            travelers[i].position = nodePositions[travelers[i].path[0]].position;
        }
    }

    bool isPlaying = false;
    Rectangle playBtn = { 10, 50, 100, 40 };

    while (!WindowShouldClose()) {
        if (CheckCollisionPointRec(GetMousePosition(), playBtn) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            isPlaying = !isPlaying;
            if (isPlaying && allAnimatableFinished(travelers, numTravelers)) {
                for (int i = 0; i < numTravelers; i++) {
                    resetTraveler(&travelers[i], nodePositions);
                }
            }
        }

        double currentTime = GetTime();
        if (isPlaying) {
            for (int i = 0; i < numTravelers; i++) {
                updateTraveler(&travelers[i], graph, nodePositions, isPlaying, currentTime);
            }

            if (allAnimatableFinished(travelers, numTravelers)) {
                isPlaying = false;
            }
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        for (int i = 0; i < N; i++) {
            Node* temp = graph->adjLists[i];
            while (temp) {
                Vector2 start = nodePositions[i].position;
                Vector2 end = nodePositions[temp->dest].position;
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

                float dx = end.x - start.x;
                float dy = end.y - start.y;
                float length = sqrtf(dx * dx + dy * dy);
                if (length == 0) length = 1.0f;
                Vector2 normal = { -dy / length, dx / length };
                float textOffsetValue = 15.0f;
                Vector2 weightPos = { midPoint.x + normal.x * textOffsetValue, midPoint.y + normal.y * textOffsetValue };

                DrawText(weightText, weightPos.x - 5, weightPos.y - 10, 20, MAROON);
                temp = temp->next;
            }
        }

        for (int i = 0; i < N; i++) {
            float nodeRadius = 25.0f;
            Color nodeColor = LIGHTGRAY;

            for (int t = 0; t < numTravelers; t++) {
                if (travelers[t].pathLen > 0) {
                    if (i == travelers[t].startNode) nodeColor = travelers[t].color;
                    else if (i == travelers[t].endNode) nodeColor = travelers[t].color;
                }
            }

            DrawCircleV(nodePositions[i].position, nodeRadius, nodeColor);
            DrawCircleLines(nodePositions[i].position.x, nodePositions[i].position.y, nodeRadius, BLACK);

            char idText[12];
            sprintf(idText, "%d", i);
            int textWidth = MeasureText(idText, 20);
            DrawText(idText, nodePositions[i].position.x - (float)textWidth / 2, nodePositions[i].position.y - 10, 20, BLACK);
        }

        for (int i = 0; i < numTravelers; i++) {
            if (travelers[i].pathLen > 0) {
                DrawCircleV(travelers[i].position, 15, travelers[i].color);
                DrawCircleLines(travelers[i].position.x, travelers[i].position.y, 15, BLACK);
            }
        }

        char infoText[32];
        sprintf(infoText, "Travelers: %d", numTravelers);
        DrawText("Milestone 4: Multiple Processes", 10, 10, 20, DARKBLUE);
        DrawText(infoText, 10, 32, 18, DARKGRAY);

        DrawRectangleRec(playBtn, isPlaying ? RED : LIME);
        DrawText(isPlaying ? "STOP" : "PLAY", playBtn.x + 25, playBtn.y + 10, 20, BLACK);

        if (hasNegativeWeight) {
            DrawText("Error: Negative weights detected in graph!", screenWidth / 2 - 200, 80, 20, RED);
        } else {
            int noPathCount = 0;
            for (int i = 0; i < numTravelers; i++) {
                if (!canAnimate(&travelers[i]) && travelers[i].startNode != travelers[i].endNode) {
                    noPathCount++;
                }
            }
            if (noPathCount > 0) {
                char noPathMsg[64];
                sprintf(noPathMsg, "%d traveler(s) have no valid path", noPathCount);
                DrawText(noPathMsg, screenWidth / 2 - 130, 80, 18, RED);
            }
        }

        if (!hasNegativeWeight) {
            int finishedCount = 0;
            for (int i = 0; i < numTravelers; i++) {
                if (hasReachedDestination(&travelers[i])) finishedCount++;
            }
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
        terminateChild(&travelers[i]);
    }

    for (int i = 0; i < numTravelers; i++) {
        if (travelers[i].path) free(travelers[i].path);
    }
    free(travelers);
    free(nodePositions);
    freeGraph(graph);

    return 0;
}
