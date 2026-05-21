#ifndef HANDLER_H
#define HANDLER_H

#include "http.h"

void handle_request(HttpRequest *req, int fd);

#endif
