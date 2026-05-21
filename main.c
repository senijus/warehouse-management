#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "server.h"
#include "database.h"

static void signal_handler(int sig) {
    (void)sig;
    printf("\nShutting down...\n");
    server_stop();
    db_close();
    exit(0);
}

int main(int argc, char *argv[]) {
    int port = 8080;
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return 1;
        }
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize database */
    if (db_init("data/inventory.db") != 0) {
        fprintf(stderr, "Failed to initialize database\n");
        return 1;
    }
    printf("Database initialized\n");

    /* Initialize and run server */
    if (server_init(port) != 0) {
        fprintf(stderr, "Failed to start server\n");
        db_close();
        return 1;
    }

    printf("=== Warehouse Inventory Management System ===\n");
    printf("Access: http://localhost:%d\n", port);
    printf("Default admin: admin / admin123\n\n");

    server_run();

    db_close();
    return 0;
}
