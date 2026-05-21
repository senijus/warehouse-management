#ifndef AUTH_H
#define AUTH_H

#include "http.h"

int  auth_check(HttpRequest *req);
int  auth_require_admin(HttpRequest *req);
void auth_get_username(HttpRequest *req, char *username, int size);
void auth_generate_token(char *token, int size);
int  auth_validate_token(const char *token, char *username, int username_size);
int  auth_get_token_user(const char *token, char *username, int username_size);
void auth_login_user(HttpRequest *req, int fd, const char *username);
void auth_logout_user(HttpRequest *req, int fd);

#endif
