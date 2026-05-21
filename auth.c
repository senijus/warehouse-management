#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include "auth.h"
#include "database.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* In-memory token store */
#define MAX_TOKENS 256

typedef struct {
    char token[65];
    char username[256];
    int  active;
} SessionToken;

static SessionToken tokens[MAX_TOKENS];

void auth_generate_token(char *token, int size) {
    static int seeded = 0;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = 1; }

    const char *hex = "0123456789abcdef";
    int len = size - 1;
    if (len > 64) len = 64;
    for (int i = 0; i < len; i++) {
        token[i] = hex[rand() % 16];
    }
    token[len] = '\0';
}

int auth_validate_token(const char *token, char *username, int username_size) {
    if (!token || token[0] == '\0') return 0;

    for (int i = 0; i < MAX_TOKENS; i++) {
        if (tokens[i].active && strcmp(tokens[i].token, token) == 0) {
            strncpy(username, tokens[i].username, username_size - 1);
            username[username_size - 1] = '\0';
            return 1;
        }
    }
    return 0;
}

int auth_get_token_user(const char *token, char *username, int username_size) {
    return auth_validate_token(token, username, username_size);
}

/* Store a new token for a user */
static void auth_store_token(const char *token, const char *username) {
    /* Find empty slot or reuse oldest */
    int slot = -1;
    for (int i = 0; i < MAX_TOKENS; i++) {
        if (!tokens[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0; /* Overwrite oldest */

    strncpy(tokens[slot].token, token, 64);
    tokens[slot].token[64] = '\0';
    strncpy(tokens[slot].username, username, 255);
    tokens[slot].username[255] = '\0';
    tokens[slot].active = 1;
}

/* Remove a token */
static void auth_remove_token(const char *token) {
    for (int i = 0; i < MAX_TOKENS; i++) {
        if (tokens[i].active && strcmp(tokens[i].token, token) == 0) {
            tokens[i].active = 0;
            break;
        }
    }
}

/* Extract token from Authorization header: "Bearer <token>" */
static void auth_extract_token(HttpRequest *req, char *token, int size) {
    token[0] = '\0';

    /* From Authorization header */
    if (strncasecmp(req->authorization, "Bearer ", 7) == 0) {
        strncpy(token, req->authorization + 7, size - 1);
        token[size - 1] = '\0';
        /* Trim trailing whitespace */
        int len = strlen(token);
        while (len > 0 && (token[len-1] == ' ' || token[len-1] == '\r')) token[--len] = '\0';
        return;
    }
}

/* Extract token from query string */
static void auth_extract_query_token(HttpRequest *req, char *token, int size) {
    http_parse_query_param(req->query, "token", token, size);
}

/* Core auth check: extract token, validate, return whether valid */
static int auth_validate_request(HttpRequest *req, char *username, int username_size) {
    char token[128];
    auth_extract_token(req, token, sizeof(token));

    if (token[0] == '\0') {
        /* Try query parameter for page requests */
        auth_extract_query_token(req, token, sizeof(token));
    }

    if (token[0] == '\0') return 0;
    return auth_validate_token(token, username, username_size);
}

int auth_check(HttpRequest *req) {
    char username[256];
    return auth_validate_request(req, username, sizeof(username));
}

int auth_require_admin(HttpRequest *req) {
    char username[256];
    if (!auth_validate_request(req, username, sizeof(username))) return 0;

    char role[32];
    if (db_user_get_role(username, role, sizeof(role)) == 0) {
        return strcmp(role, "admin") == 0;
    }
    return 0;
}

void auth_get_username(HttpRequest *req, char *username, int size) {
    username[0] = '\0';
    auth_validate_request(req, username, size);
}

/* Called after successful login: generates token, returns it */
void auth_login_user(HttpRequest *req, int fd, const char *username) {
    (void)req;
    char token[65];
    auth_generate_token(token, sizeof(token));
    auth_store_token(token, username);

    char json[512];
    snprintf(json, sizeof(json), "{\"success\":true,\"token\":\"%s\",\"username\":\"%s\"}", token, username);
    http_send_json(fd, json);
}

/* Logout: invalidate token */
void auth_logout_user(HttpRequest *req, int fd) {
    char token[128];
    auth_extract_token(req, token, sizeof(token));
    if (token[0] == '\0') auth_extract_query_token(req, token, sizeof(token));
    if (token[0]) auth_remove_token(token);

    http_send_json(fd, "{\"success\":true}");
}
