#ifndef HTTP_H
#define HTTP_H

typedef struct {
    char method[8];
    char path[256];
    char query[512];
    char body[8192];
    char cookie[512];
    char authorization[512];
    int  content_length;
} HttpRequest;

int  http_parse_request(const char *raw, int len, HttpRequest *req);
void http_send_response(int fd, int status, const char *status_text, const char *content_type, const char *body, int body_len);
void http_send_redirect(int fd, const char *location);
void http_send_file(int fd, const char *filepath);
void http_send_json(int fd, const char *json);
void http_send_error(int fd, int status, const char *message);
void http_url_decode(const char *src, char *dst, int dst_size);
void http_parse_form_field(const char *body, const char *field, char *value, int value_size);
void http_parse_query_param(const char *query, const char *param, char *value, int value_size);

#endif
