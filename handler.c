#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "handler.h"
#include "database.h"
#include "auth.h"
#include "http.h"

static void handle_login(HttpRequest *req, int fd) {
    char username[256], password[256];
    http_parse_form_field(req->body, "username", username, sizeof(username));
    http_parse_form_field(req->body, "password", password, sizeof(password));

    if (username[0] == '\0' || password[0] == '\0') {
        http_send_json(fd, "{\"success\":false,\"message\":\"用户名和密码不能为空\"}");
        return;
    }

    char role[32];
    if (db_user_auth(username, password, role, sizeof(role)) == 0) {
        db_log_add(username, "登录", "-", 0, "用户登录成功");
        auth_login_user(req, fd, username);
    } else {
        http_send_json(fd, "{\"success\":false,\"message\":\"用户名或密码错误\"}");
    }
}

static void handle_logout(HttpRequest *req, int fd) {
    char username[256];
    auth_get_username(req, username, sizeof(username));
    if (username[0]) db_log_add(username, "登出", "-", 0, "用户登出");
    auth_logout_user(req, fd);
}

static void handle_api_devices(HttpRequest *req, int fd) {
    if (!auth_check(req)) { http_send_error(fd, 401, "Unauthorized"); return; }

    char keyword[256];
    http_parse_query_param(req->query, "search", keyword, sizeof(keyword));

    char json[65536];
    if (keyword[0]) {
        db_device_search(keyword, json, sizeof(json));
    } else {
        db_device_list(json, sizeof(json));
    }
    http_send_json(fd, json);
}

static void handle_api_device_add(HttpRequest *req, int fd) {
    if (!auth_require_admin(req)) { http_send_error(fd, 403, "Forbidden"); return; }

    char name[256], category[128], location[128], desc[256], qty_str[32];
    http_parse_form_field(req->body, "name", name, sizeof(name));
    http_parse_form_field(req->body, "category", category, sizeof(category));
    http_parse_form_field(req->body, "quantity", qty_str, sizeof(qty_str));
    http_parse_form_field(req->body, "location", location, sizeof(location));
    http_parse_form_field(req->body, "description", desc, sizeof(desc));

    if (name[0] == '\0') {
        http_send_json(fd, "{\"success\":false,\"message\":\"设备名称不能为空\"}");
        return;
    }

    int quantity = qty_str[0] ? atoi(qty_str) : 0;

    if (db_device_add(name, category, quantity, location, desc) == 0) {
        char username[256];
        auth_get_username(req, username, sizeof(username));
        char detail[512];
        snprintf(detail, sizeof(detail), "添加设备: %s, 数量: %d", name, quantity);
        db_log_add(username, "添加设备", name, quantity, detail);
        http_send_json(fd, "{\"success\":true,\"message\":\"设备添加成功\"}");
    } else {
        http_send_json(fd, "{\"success\":false,\"message\":\"设备添加失败\"}");
    }
}

static void handle_api_device_delete(HttpRequest *req, int fd) {
    if (!auth_require_admin(req)) { http_send_error(fd, 403, "Forbidden"); return; }

    char id_str[32];
    http_parse_form_field(req->body, "id", id_str, sizeof(id_str));
    int device_id = atoi(id_str);

    if (device_id <= 0) {
        http_send_json(fd, "{\"success\":false,\"message\":\"无效的设备ID\"}");
        return;
    }

    if (db_device_delete(device_id) == 0) {
        char username[256];
        auth_get_username(req, username, sizeof(username));
        char detail[256];
        snprintf(detail, sizeof(detail), "删除设备 ID: %d", device_id);
        db_log_add(username, "删除设备", "-", 0, detail);
        http_send_json(fd, "{\"success\":true,\"message\":\"设备删除成功\"}");
    } else {
        http_send_json(fd, "{\"success\":false,\"message\":\"设备删除失败\"}");
    }
}

static void handle_api_stock_in(HttpRequest *req, int fd) {
    if (!auth_check(req)) { http_send_error(fd, 401, "Unauthorized"); return; }

    char id_str[32], qty_str[32];
    http_parse_form_field(req->body, "device_id", id_str, sizeof(id_str));
    http_parse_form_field(req->body, "quantity", qty_str, sizeof(qty_str));

    int device_id = atoi(id_str);
    int quantity  = atoi(qty_str);

    if (device_id <= 0 || quantity <= 0) {
        http_send_json(fd, "{\"success\":false,\"message\":\"无效的参数\"}");
        return;
    }

    char username[256];
    auth_get_username(req, username, sizeof(username));

    int result = db_stock_in(device_id, quantity, username);
    if (result == 0) {
        http_send_json(fd, "{\"success\":true,\"message\":\"入库成功\"}");
    } else {
        http_send_json(fd, "{\"success\":false,\"message\":\"入库失败\"}");
    }
}

static void handle_api_stock_out(HttpRequest *req, int fd) {
    if (!auth_check(req)) { http_send_error(fd, 401, "Unauthorized"); return; }

    char id_str[32], qty_str[32];
    http_parse_form_field(req->body, "device_id", id_str, sizeof(id_str));
    http_parse_form_field(req->body, "quantity", qty_str, sizeof(qty_str));

    int device_id = atoi(id_str);
    int quantity  = atoi(qty_str);

    if (device_id <= 0 || quantity <= 0) {
        http_send_json(fd, "{\"success\":false,\"message\":\"无效的参数\"}");
        return;
    }

    char username[256];
    auth_get_username(req, username, sizeof(username));

    int result = db_stock_out(device_id, quantity, username);
    if (result == 0) {
        http_send_json(fd, "{\"success\":true,\"message\":\"出库成功\"}");
    } else if (result == -2) {
        http_send_json(fd, "{\"success\":false,\"message\":\"库存不足\"}");
    } else {
        http_send_json(fd, "{\"success\":false,\"message\":\"出库失败\"}");
    }
}

static void handle_api_logs(HttpRequest *req, int fd) {
    if (!auth_require_admin(req)) { http_send_error(fd, 403, "Forbidden"); return; }

    char limit_str[32];
    http_parse_query_param(req->query, "limit", limit_str, sizeof(limit_str));
    int limit = limit_str[0] ? atoi(limit_str) : 100;
    if (limit <= 0 || limit > 1000) limit = 100;

    char json[65536];
    db_log_list(limit, json, sizeof(json));
    http_send_json(fd, json);
}

static void handle_api_users(HttpRequest *req, int fd) {
    if (!auth_require_admin(req)) { http_send_error(fd, 403, "Forbidden"); return; }

    char json[65536];
    db_user_list(json, sizeof(json));
    http_send_json(fd, json);
}

static void handle_api_user_add(HttpRequest *req, int fd) {
    if (!auth_require_admin(req)) { http_send_error(fd, 403, "Forbidden"); return; }

    char username[256], password[256], role[32];
    http_parse_form_field(req->body, "username", username, sizeof(username));
    http_parse_form_field(req->body, "password", password, sizeof(password));
    http_parse_form_field(req->body, "role", role, sizeof(role));

    if (username[0] == '\0' || password[0] == '\0') {
        http_send_json(fd, "{\"success\":false,\"message\":\"用户名和密码不能为空\"}");
        return;
    }
    if (role[0] == '\0') strcpy(role, "operator");

    if (db_user_exists(username)) {
        http_send_json(fd, "{\"success\":false,\"message\":\"用户名已存在\"}");
        return;
    }

    if (db_user_add(username, password, role) == 0) {
        char admin_user[256];
        auth_get_username(req, admin_user, sizeof(admin_user));
        char detail[512];
        snprintf(detail, sizeof(detail), "添加用户: %s, 角色: %s", username, role);
        db_log_add(admin_user, "添加用户", "-", 0, detail);
        http_send_json(fd, "{\"success\":true,\"message\":\"用户添加成功\"}");
    } else {
        http_send_json(fd, "{\"success\":false,\"message\":\"用户添加失败\"}");
    }
}

static void handle_api_user_delete(HttpRequest *req, int fd) {
    if (!auth_require_admin(req)) { http_send_error(fd, 403, "Forbidden"); return; }

    char username[256];
    http_parse_form_field(req->body, "username", username, sizeof(username));

    if (username[0] == '\0') {
        http_send_json(fd, "{\"success\":false,\"message\":\"用户名不能为空\"}");
        return;
    }
    if (strcmp(username, "admin") == 0) {
        http_send_json(fd, "{\"success\":false,\"message\":\"不能删除默认管理员\"}");
        return;
    }

    if (db_user_delete(username) == 0) {
        char admin_user[256];
        auth_get_username(req, admin_user, sizeof(admin_user));
        char detail[256];
        snprintf(detail, sizeof(detail), "删除用户: %s", username);
        db_log_add(admin_user, "删除用户", "-", 0, detail);
        http_send_json(fd, "{\"success\":true,\"message\":\"用户删除成功\"}");
    } else {
        http_send_json(fd, "{\"success\":false,\"message\":\"用户删除失败\"}");
    }
}

/* Page routes: check token from query param, redirect to / if invalid */
static int check_page_auth(HttpRequest *req, int fd) {
    if (!auth_check(req)) {
        http_send_redirect(fd, "/");
        return 0;
    }
    return 1;
}

static int check_page_admin(HttpRequest *req, int fd) {
    if (!auth_require_admin(req)) {
        http_send_redirect(fd, "/dashboard");
        return 0;
    }
    return 1;
}

void handle_request(HttpRequest *req, int fd) {
    /* Login page: always serve (no auto-redirect) */
    if (strcmp(req->path, "/") == 0 || strcmp(req->path, "/index.html") == 0) {
        http_send_file(fd, "/index.html");
        return;
    }
    if (strcmp(req->path, "/style.css") == 0) {
        http_send_file(fd, "/style.css");
        return;
    }

    /* Protected pages: check token, redirect to / if invalid */
    if (strcmp(req->path, "/dashboard") == 0) {
        if (!check_page_auth(req, fd)) return;
        http_send_file(fd, "/dashboard.html");
        return;
    }
    if (strcmp(req->path, "/admin") == 0) {
        if (!check_page_admin(req, fd)) return;
        http_send_file(fd, "/admin.html");
        return;
    }
    if (strcmp(req->path, "/logs") == 0) {
        if (!check_page_admin(req, fd)) return;
        http_send_file(fd, "/logs.html");
        return;
    }

    /* Auth endpoints */
    if (strcmp(req->path, "/login") == 0 && strcmp(req->method, "POST") == 0) {
        handle_login(req, fd);
        return;
    }
    if (strcmp(req->path, "/logout") == 0) {
        handle_logout(req, fd);
        return;
    }

    /* API endpoints */
    if (strcmp(req->path, "/api/devices") == 0 && strcmp(req->method, "GET") == 0) {
        handle_api_devices(req, fd);
        return;
    }
    if (strcmp(req->path, "/api/device/add") == 0 && strcmp(req->method, "POST") == 0) {
        handle_api_device_add(req, fd);
        return;
    }
    if (strcmp(req->path, "/api/device/delete") == 0 && strcmp(req->method, "POST") == 0) {
        handle_api_device_delete(req, fd);
        return;
    }
    if (strcmp(req->path, "/api/stock/in") == 0 && strcmp(req->method, "POST") == 0) {
        handle_api_stock_in(req, fd);
        return;
    }
    if (strcmp(req->path, "/api/stock/out") == 0 && strcmp(req->method, "POST") == 0) {
        handle_api_stock_out(req, fd);
        return;
    }
    if (strcmp(req->path, "/api/logs") == 0 && strcmp(req->method, "GET") == 0) {
        handle_api_logs(req, fd);
        return;
    }
    if (strcmp(req->path, "/api/users") == 0 && strcmp(req->method, "GET") == 0) {
        handle_api_users(req, fd);
        return;
    }
    if (strcmp(req->path, "/api/user/add") == 0 && strcmp(req->method, "POST") == 0) {
        handle_api_user_add(req, fd);
        return;
    }
    if (strcmp(req->path, "/api/user/delete") == 0 && strcmp(req->method, "POST") == 0) {
        handle_api_user_delete(req, fd);
        return;
    }

    http_send_error(fd, 404, "Not Found");
}
