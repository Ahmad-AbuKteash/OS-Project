#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdbool.h>

#define INF INT_MAX

typedef struct Node {
    int dest;
    int weight;
    struct Node* next;
} Node;

typedef struct Graph {
    int numVertices;
    Node** adjLists;
} Graph;

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

void dijkstra(Graph* graph, int startNode, int endNode) {
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

    if (dist[endNode] == INF) {
        printf("No path found\n");
    } else {
        // Reconstruct path
        int* path = (int*)malloc(n * sizeof(int));
        int pathLen = 0;
        for (int at = endNode; at != -1; at = prev[at]) {
            path[pathLen++] = at;
        }

        for (int i = pathLen - 1; i >= 0; i--) {
            printf("%d%s", path[i], (i == 0 ? "" : " -> "));
        }
        printf("\n%d\n", dist[endNode]);
        free(path);
    }

    free(dist);
    free(prev);
    free(visited);
}

int main(int argc, char* argv[]) {
    // argc = 2;
    // argv[1] = "input.txt";
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
    if (!graph) {
        fclose(file);
        return 1;
    }

    for (int i = 0; i < M; i++) {
        int src, dst, weight;
        if (fscanf(file, "%d %d %d", &src, &dst, &weight) != 3) {
            fprintf(stderr, "Invalid edge format\n");
            freeGraph(graph);
            fclose(file);
            return 1;
        }
        if (weight < 0) {
            fprintf(stderr, "Negative weight is invalid\n");
            freeGraph(graph);
            fclose(file);
            return 1;
        }
        if (src < 0 || src >= N || dst < 0 || dst >= N) {
            fprintf(stderr, "Invalid vertex index\n");
            freeGraph(graph);
            fclose(file);
            return 1;
        }
        addEdge(graph, src, dst, weight); 
    }

    int start, end;
    if (fscanf(file, "%d %d", &start, &end) != 2) {
        fprintf(stderr, "Invalid query format\n");
        freeGraph(graph);
        fclose(file);
        return 1;
    }

    if (start < 0 || start >= N || end < 0 || end >= N) {
        fprintf(stderr, "Invalid query vertex index\n");
        freeGraph(graph);
        fclose(file);
        return 1;
    }

    dijkstra(graph, start, end);

    freeGraph(graph);
    fclose(file);
    return 0;
}
