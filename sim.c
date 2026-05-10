#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include <stdbool.h>
#include "raylib.h"

#define INF INT_MAX

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

// Dijkstra Algorithm
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
        int tempPath[100]; // Max 100 nodes for path reconstruction
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
    int startNode, endNode;
    fscanf(file, "%d %d", &startNode, &endNode);
    fclose(file);

    int pathLen = 0;
    int* path = NULL;
    if (!hasNegativeWeight) {
        path = dijkstra(graph, startNode, endNode, &pathLen);
    }

    // GUI Initialization
    const int screenWidth = 800;
    const int screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "Traffic Simulation - Milestone 3");
    SetTargetFPS(60);

    // Calculate node positions (Circular layout)
    NodePos* nodePositions = (NodePos*)malloc(N * sizeof(NodePos));
    Vector2 center = { (float)screenWidth / 2, (float)screenHeight / 2 };
    float radius = 200.0f;
    for (int i = 0; i < N; i++) {
        float angle = (float)i * 2.0f * PI / (float)N;
        nodePositions[i].position.x = center.x + radius * cosf(angle);
        nodePositions[i].position.y = center.y + radius * sinf(angle);
    }

    // Animation variables
    bool isPlaying = false;
    SimState currentState = STATE_IDLE;
    int currentPathIndex = 0; // Current node index in path array
    int edgeCurrentJump = 0;
    int edgeTotalJumps = 0;
    double lastUpdateTime = 0;
    Vector2 entityPos = { 0, 0 };
    if (pathLen > 0) entityPos = nodePositions[path[0]].position;

    Rectangle playBtn = { 10, 50, 100, 40 };

    while (!WindowShouldClose()) {
        // Update Logic
        if (CheckCollisionPointRec(GetMousePosition(), playBtn) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            isPlaying = !isPlaying;
            if (isPlaying && currentState == STATE_FINISHED) {
                // Reset animation
                currentState = STATE_IDLE;
                currentPathIndex = 0;
                if (pathLen > 0) entityPos = nodePositions[path[0]].position;
            }
        }

        if (isPlaying && pathLen > 1) {
            double currentTime = GetTime();

            if (currentState == STATE_IDLE) {
                currentState = STATE_MOVING;
                edgeCurrentJump = 0;
                int src = path[currentPathIndex];
                int dst = path[currentPathIndex + 1];
                edgeTotalJumps = getEdgeWeight(graph, src, dst);
                lastUpdateTime = currentTime;
            } else if (currentState == STATE_MOVING) {
                if (currentTime - lastUpdateTime >= 0.3) { // 300ms per jump
                    edgeCurrentJump++;
                    lastUpdateTime = currentTime;

                    Vector2 pStart = nodePositions[path[currentPathIndex]].position;
                    Vector2 pEnd = nodePositions[path[currentPathIndex + 1]].position;
                    float t = (float)edgeCurrentJump / edgeTotalJumps;

                    // Straight path movement - reach center
                    entityPos.x = pStart.x + (pEnd.x - pStart.x) * t;
                    entityPos.y = pStart.y + (pEnd.y - pStart.y) * t;

                    if (edgeCurrentJump >= edgeTotalJumps) {
                        currentPathIndex++;
                        if (currentPathIndex == pathLen - 1) {
                            currentState = STATE_FINISHED;
                            isPlaying = false;
                        } else {
                            currentState = STATE_WAITING_NODE;
                        }
                    }
                }
            } else if (currentState == STATE_WAITING_NODE) {
                if (currentTime - lastUpdateTime >= 1.0) { // 1s wait
                    currentState = STATE_IDLE;
                    // No need to reset lastUpdateTime here, it will be reset in STATE_IDLE's transition
                }
            }
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        // Draw Edges
        for (int i = 0; i < N; i++) {
            Node* temp = graph->adjLists[i];
            while (temp) {
                Vector2 start = nodePositions[i].position;
                Vector2 end = nodePositions[temp->dest].position;

                float nodeRadius = 25.0f;

                Color edgeColor = DARKGRAY;
                // Highlight path edges
                if (pathLen > 1) {
                    for (int j = 0; j < pathLen - 1; j++) {
                        if (path[j] == i && path[j+1] == temp->dest) {
                            edgeColor = BLUE;
                            break;
                        }
                    }
                }

                DrawArrow(start, end, nodeRadius, 2.0f, edgeColor);
                char weightText[10];
                sprintf(weightText, "%d", temp->weight);
                Vector2 midPoint = { (start.x + end.x) / 2, (start.y + end.y) / 2 };
                
                // Use normal vector for weight offset to avoid overlapping with edges (as done in sim2.c)
                float dx = end.x - start.x;
                float dy = end.y - start.y;
                float length = sqrtf(dx*dx + dy*dy);
                if (length == 0) length = 1.0f;
                Vector2 normal = { -dy/length, dx/length };
                float textOffsetValue = 15.0f;
                Vector2 weightPos = { midPoint.x + normal.x * textOffsetValue, midPoint.y + normal.y * textOffsetValue };
                
                DrawText(weightText, weightPos.x - 5, weightPos.y - 10, 20, MAROON);
                temp = temp->next;
            }
        }

        // Draw Nodes
        for (int i = 0; i < N; i++) {
            float nodeRadius = 25.0f;
            Color nodeColor = LIGHTGRAY;
            if (pathLen > 0) {
                if (i == startNode) nodeColor = GREEN;
                else if (i == endNode) nodeColor = RED;
            }
            
            DrawCircleV(nodePositions[i].position, nodeRadius, nodeColor);
            DrawCircleLines(nodePositions[i].position.x, nodePositions[i].position.y, nodeRadius, BLACK);
            
            char idText[12];
            sprintf(idText, "%d", i);
            int textWidth = MeasureText(idText, 20);
            DrawText(idText, nodePositions[i].position.x - (float)textWidth / 2, nodePositions[i].position.y - 10, 20, BLACK);
        }

        // Draw Entity
        if (pathLen > 0) {
            DrawCircleV(entityPos, 15, GOLD);
            DrawCircleLines(entityPos.x, entityPos.y, 15, ORANGE);
        }

        // Draw UI
        DrawRectangleRec(playBtn, isPlaying ? RED : LIME);
        DrawText(isPlaying ? "STOP" : "PLAY", playBtn.x + 25, playBtn.y + 10, 20, BLACK);

        if (currentState == STATE_FINISHED) {
            DrawText("Destination Reached!", screenWidth / 2 - 100, 50, 25, DARKGREEN);
        }

        if (hasNegativeWeight) {
            DrawText("Error: Negative weights detected in graph!", screenWidth / 2 - 200, 80, 20, RED);
        } else if (startNode == endNode) {
            DrawText("Source is same as Destination!", screenWidth / 2 - 150, 80, 20, ORANGE);
        } else if (pathLen == 0) {
            DrawText("No path found from src to dst!", screenWidth / 2 - 150, 80, 20, RED);
        }

        DrawText("Milestone 3: Animation", 10, 10, 20, DARKBLUE);
        EndDrawing();
    }

    CloseWindow();
    free(nodePositions);
    if (path) free(path);
    freeGraph(graph);

    return 0;
}
