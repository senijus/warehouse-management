#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include "http.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

void http_url_decode(const char *src, char *dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 1; i++) {
        if (src[i] == '%' && src[i + 1] && src[i + 2]) {
            dst[j++] = (char)((hex_to_int(src[i + 1]) << 4) | hex_to_int(src[i + 2]));
            i += 2;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

int http_parse_request(const char *raw, int len, HttpRequest *req) {
    memset(req, 0, sizeof(HttpRequest));

    if (len < 10) return -1;

    int i = 0;

    /* Parse method */
    int j = 0;
    while (i < len && raw[i] != ' ' && j < (int)sizeof(req->method) - 1)
        req->method[j++] = raw[i++];
    req->method[j] = '\0';
    while (i < len && raw[i] == ' ') i++;

    /* Parse path */
    j = 0;
    while (i < len && raw[i] != ' ' && raw[i] != '?' && j < (int)sizeof(req->path) - 1)
        req->path[j++] = raw[i++];
    req->path[j] = '\0';

    /* Parse query string if present */
    if (i < len && raw[i] == '?') {
        i++;
        j = 0;
        while (i < len && raw[i] != ' ' && j < (int)sizeof(req->query) - 1)
            req->query[j++] = raw[i++];
        req->query[j] = '\0';
    }

    /* Skip to end of request line */
    while (i < len && raw[i] != '\n') i++;
    if (i < len) i++;

    /* Parse headers */
    while (i < len) {
        if (raw[i] == '\r' || raw[i] == '\n') break;

        /* Check for Content-Length */
        if (strncasecmp(raw + i, "Content-Length:", 15) == 0) {
            i += 15;
            while (i < len && raw[i] == ' ') i++;
            char cl[16] = {0};
            j = 0;
            while (i < len && raw[i] >= '0' && raw[i] <= '9' && j < 15)
                cl[j++] = raw[i++];
            req->content_length = atoi(cl);
        }
        /* Check for Cookie */
        else if (strncasecmp(raw + i, "Cookie:", 7) == 0) {
            i += 7;
            while (i < len && raw[i] == ' ') i++;
            j = 0;
            while (i < len && raw[i] != '\r' && raw[i] != '\n' && j < (int)sizeof(req->cookie) - 1)
                req->cookie[j++] = raw[i++];
            req->cookie[j] = '\0';
        }
        /* Check for Authorization */
        else if (strncasecmp(raw + i, "Authorization:", 14) == 0) {
            i += 14;
            while (i < len && raw[i] == ' ') i++;
            j = 0;
            while (i < len && raw[i] != '\r' && raw[i] != '\n' && j < (int)sizeof(req->authorization) - 1)
                req->authorization[j++] = raw[i++];
            req->authorization[j] = '\0';
        }

        /* Skip to next line */
        while (i < len && raw[i] != '\n') i++;
        if (i < len) i++;
    }

    /* Skip blank line between headers and body */
    while (i < len && (raw[i] == '\r' || raw[i] == '\n')) i++;

    /* Parse body */
    if (req->content_length > 0 && i < len) {
        int body_len = len - i;
        if (body_len > req->content_length) body_len = req->content_length;
        if (body_len > (int)sizeof(req->body) - 1) body_len = (int)sizeof(req->body) - 1;
        memcpy(req->body, raw + i, body_len);
        req->body[body_len] = '\0';
    }

    return 0;
}

void http_send_response(int fd, int status, const char *status_text, const char *content_type, const char *body, int body_len) {
    char header[1024];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, content_type, body_len);

    send(fd, header, hlen, MSG_NOSIGNAL);
    if (body_len > 0 && body) {
        send(fd, body, body_len, MSG_NOSIGNAL);
    }
}

void http_send_redirect(int fd, const char *location) {
    char resp[512];
    int len = snprintf(resp, sizeof(resp),
        "HTTP/1.1 302 Found\r\n"
        "Location: %s\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n", location);
    send(fd, resp, len, MSG_NOSIGNAL);
}

void http_send_file(int fd, const char *filepath) {
    char fullpath[512];
    snprintf(fullpath, sizeof(fullpath), "web%s", filepath);

    int file_fd = open(fullpath, O_RDONLY);
    if (file_fd < 0) {
        http_send_error(fd, 404, "File Not Found");
        return;
    }

    struct stat st;
    fstat(file_fd, &st);
    int file_size = st.st_size;

    const char *content_type = "text/plain";
    if (strstr(filepath, ".html")) content_type = "text/html; charset=utf-8";
    else if (strstr(filepath, ".css")) content_type = "text/css";
    else if (strstr(filepath, ".js")) content_type = "application/javascript";
    else if (strstr(filepath, ".json")) content_type = "application/json";
    else if (strstr(filepath, ".png")) content_type = "image/png";
    else if (strstr(filepath, ".jpg") || strstr(filepath, ".jpeg")) content_type = "image/jpeg";
    else if (strstr(filepath, ".ico")) content_type = "image/x-icon";

    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n", content_type, file_size);
    send(fd, header, hlen, MSG_NOSIGNAL);

    char buf[4096];
    int n;
    while ((n = read(file_fd, buf, sizeof(buf))) > 0) {
        send(fd, buf, n, MSG_NOSIGNAL);
    }
    close(file_fd);
}

void http_send_json(int fd, const char *json) {
    http_send_response(fd, 200, "OK", "application/json; charset=utf-8", json, strlen(json));
}

void http_send_error(int fd, int status, const char *message) {
    char body[512];
    int blen = snprintf(body, sizeof(body),
        "<!DOCTYPE html><html><head><title>%d</title></head>"
        "<body><h1>%d %s</h1></body></html>",
        status, status, message);
    char status_text[32];
    snprintf(status_text, sizeof(status_text), "%d", status);
    http_send_response(fd, status, message, "text/html; charset=utf-8", body, blen);
}

void http_parse_form_field(const char *body, const char *field, char *value, int value_size) {
    value[0] = '\0';
    char search[256];
    int slen = snprintf(search, sizeof(search), "%s=", field);

    const char *p = body;
    while (p) {
        if (strncmp(p, search, slen) == 0) {
            p += slen;
            const char *end = strchr(p, '&');
            int len = end ? (int)(end - p) : (int)strlen(p);
            if (len > value_size - 1) len = value_size - 1;
            char encoded[1024];
            if (len > (int)sizeof(encoded) - 1) len = (int)sizeof(encoded) - 1;
            memcpy(encoded, p, len);
            encoded[len] = '\0';
            http_url_decode(encoded, value, value_size);
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

void http_parse_query_param(const char *query, const char *param, char *value, int value_size) {
    http_parse_form_field(query, param, value, value_size);
}
