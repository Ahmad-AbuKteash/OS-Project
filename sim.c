#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "raylib.h"

// Graph structures from Milestone 1
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

void DrawArrow(Vector2 start, Vector2 end, float thickness, Color color) {
    DrawLineEx(start, end, thickness, color);

    float angle = atan2f(end.y - start.y, end.x - start.x);
    float arrowHeadLen = 15;
    float arrowHeadAngle = PI / 6;

    // Head of the arrow is at 'end'
    Vector2 p1 = { end.x - arrowHeadLen * cosf(angle - arrowHeadAngle), end.y - arrowHeadLen * sinf(angle - arrowHeadAngle) };
    Vector2 p2 = { end.x - arrowHeadLen * cosf(angle + arrowHeadAngle), end.y - arrowHeadLen * sinf(angle + arrowHeadAngle) };

    DrawTriangle(end, p2, p1, color);
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

    Graph* graph = createGraph(N);
    for (int i = 0; i < M; i++) {
        int src, dst, weight;
        fscanf(file, "%d %d %d", &src, &dst, &weight);
        addEdge(graph, src, dst, weight);
    }
    // We don't necessarily need the query (start/end) for visualization at this stage,
    // but we read it to clear the file if needed.
    int dummyStart, dummyEnd;
    fscanf(file, "%d %d", &dummyStart, &dummyEnd);
    fclose(file);

    // GUI Initialization
    const int screenWidth = 800;
    const int screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "Traffic Simulation - Milestone 2");
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

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);

        // Draw Edges
        for (int i = 0; i < N; i++) {
            Node* temp = graph->adjLists[i];
            while (temp) {
                Vector2 start = nodePositions[i].position;
                Vector2 end = nodePositions[temp->dest].position;

                // Adjust start and end points to be at the edge of the circles
                float nodeRadius = 25.0f;
                float angle = atan2f(end.y - start.y, end.x - start.x);
                Vector2 adjustedStart = { start.x + nodeRadius * cosf(angle), start.y + nodeRadius * sinf(angle) };
                Vector2 adjustedEnd = { end.x - nodeRadius * cosf(angle), end.y - nodeRadius * sinf(angle) };

                DrawArrow(adjustedStart, adjustedEnd, 2.0f, DARKGRAY);

                // Draw Weight
                char weightText[10];
                sprintf(weightText, "%d", temp->weight);
                Vector2 midPoint = { (start.x + end.x) / 2, (start.y + end.y) / 2 };
                
                // Calculate normal vector to the edge for offset
                float dx = end.x - start.x;
                float dy = end.y - start.y;
                float length = sqrtf(dx*dx + dy*dy);
                Vector2 normal = { -dy/length, dx/length };
                
                // Offset weight text along the normal vector to avoid overlap with the edge
                float textOffset = 15.0f;
                Vector2 weightPos = { midPoint.x + normal.x * textOffset, midPoint.y + normal.y * textOffset };
                
                DrawText(weightText, weightPos.x - 5, weightPos.y - 10, 20, MAROON);

                temp = temp->next;
            }
        }

        // Draw Nodes
        for (int i = 0; i < N; i++) {
            float nodeRadius = 25.0f;
            DrawCircleV(nodePositions[i].position, nodeRadius, LIGHTGRAY);
            DrawCircleLines(nodePositions[i].position.x, nodePositions[i].position.y, nodeRadius, BLACK);

            char idText[12];
            sprintf(idText, "%d", i);
            int textWidth = MeasureText(idText, 20);
            DrawText(idText, nodePositions[i].position.x - (float)textWidth / 2, nodePositions[i].position.y - 10, 20, BLACK);
        }

        DrawText("Milestone 2: Graph Visualization", 10, 10, 20, DARKBLUE);
        EndDrawing();
    }

    CloseWindow();
    free(nodePositions);
    freeGraph(graph);

    return 0;
}
