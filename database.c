#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "database.h"

static sqlite3 *db = NULL;

static void json_escape(char *dst, const char *src, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 2; i++) {
        switch (src[i]) {
            case '"':  if (j < dst_size - 3) { dst[j++] = '\\'; dst[j++] = '"'; } break;
            case '\\': if (j < dst_size - 3) { dst[j++] = '\\'; dst[j++] = '\\'; } break;
            case '\n': if (j < dst_size - 3) { dst[j++] = '\\'; dst[j++] = 'n'; } break;
            case '\r': if (j < dst_size - 3) { dst[j++] = '\\'; dst[j++] = 'r'; } break;
            case '\t': if (j < dst_size - 3) { dst[j++] = '\\'; dst[j++] = 't'; } break;
            default:   dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
}

int db_init(const char *db_path) {
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
    sqlite3_exec(db, "PRAGMA foreign_keys=ON;", 0, 0, 0);

    const char *sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username TEXT UNIQUE NOT NULL,"
        "  password TEXT NOT NULL,"
        "  role TEXT NOT NULL DEFAULT 'operator',"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS devices ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  category TEXT DEFAULT '',"
        "  quantity INTEGER NOT NULL DEFAULT 0,"
        "  location TEXT DEFAULT '',"
        "  description TEXT DEFAULT '',"
        "  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS logs ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username TEXT NOT NULL,"
        "  action TEXT NOT NULL,"
        "  device_name TEXT DEFAULT '',"
        "  quantity INTEGER DEFAULT 0,"
        "  detail TEXT DEFAULT '',"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    char *errmsg = NULL;
    rc = sqlite3_exec(db, sql, 0, 0, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }

    sqlite3_exec(db, "INSERT OR IGNORE INTO users (username, password, role) VALUES ('admin', 'admin123', 'admin');", 0, 0, 0);

    return 0;
}

void db_close(void) {
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}

int db_user_auth(const char *username, const char *password, char *role, int role_size) {
    const char *sql = "SELECT role FROM users WHERE username=? AND password=?";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *r = (const char *)sqlite3_column_text(stmt, 0);
        strncpy(role, r, role_size - 1);
        role[role_size - 1] = '\0';
        sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_finalize(stmt);
    return -1;
}

int db_user_exists(const char *username) {
    const char *sql = "SELECT COUNT(*) FROM users WHERE username=?";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    int exists = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(stmt, 0) > 0;
    }
    sqlite3_finalize(stmt);
    return exists;
}

int db_user_get_role(const char *username, char *role, int role_size) {
    const char *sql = "SELECT role FROM users WHERE username=?";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *r = (const char *)sqlite3_column_text(stmt, 0);
        strncpy(role, r, role_size - 1);
        role[role_size - 1] = '\0';
        sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_finalize(stmt);
    return -1;
}

int db_user_add(const char *username, const char *password, const char *role) {
    const char *sql = "INSERT INTO users (username, password, role) VALUES (?, ?, ?)";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, role, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_user_delete(const char *username) {
    if (strcmp(username, "admin") == 0) return -1;
    const char *sql = "DELETE FROM users WHERE username=? AND username != 'admin'";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_user_list(char *json_out, int out_size) {
    const char *sql = "SELECT id, username, role, created_at FROM users ORDER BY id";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    int pos = 0;
    pos += snprintf(json_out + pos, out_size - pos, "[");
    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW && pos < out_size - 200) {
        if (!first) json_out[pos++] = ',';
        first = 0;

        char escaped[256];
        json_escape(escaped, (const char *)sqlite3_column_text(stmt, 1), sizeof(escaped));
        pos += snprintf(json_out + pos, out_size - pos,
            "{\"id\":%d,\"username\":\"%s\",\"role\":\"%s\",\"created_at\":\"%s\"}",
            sqlite3_column_int(stmt, 0),
            escaped,
            sqlite3_column_text(stmt, 2) ? (const char *)sqlite3_column_text(stmt, 2) : "",
            sqlite3_column_text(stmt, 3) ? (const char *)sqlite3_column_text(stmt, 3) : "");
    }
    json_out[pos++] = ']';
    json_out[pos] = '\0';
    sqlite3_finalize(stmt);
    return 0;
}

int db_device_add(const char *name, const char *category, int quantity, const char *location, const char *desc) {
    const char *sql = "INSERT INTO devices (name, category, quantity, location, description) VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, category, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, quantity);
    sqlite3_bind_text(stmt, 4, location, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, desc, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_device_delete(int device_id) {
    const char *sql = "DELETE FROM devices WHERE id=?";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, device_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_device_list(char *json_out, int out_size) {
    const char *sql = "SELECT id, name, category, quantity, location, description, updated_at FROM devices ORDER BY id";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    int pos = 0;
    pos += snprintf(json_out + pos, out_size - pos, "[");
    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW && pos < out_size - 500) {
        if (!first) json_out[pos++] = ',';
        first = 0;

        char esc_name[256], esc_cat[128], esc_loc[128], esc_desc[256];
        json_escape(esc_name, (const char *)sqlite3_column_text(stmt, 1), sizeof(esc_name));
        json_escape(esc_cat, sqlite3_column_text(stmt, 2) ? (const char *)sqlite3_column_text(stmt, 2) : "", sizeof(esc_cat));
        json_escape(esc_loc, sqlite3_column_text(stmt, 4) ? (const char *)sqlite3_column_text(stmt, 4) : "", sizeof(esc_loc));
        json_escape(esc_desc, sqlite3_column_text(stmt, 5) ? (const char *)sqlite3_column_text(stmt, 5) : "", sizeof(esc_desc));

        pos += snprintf(json_out + pos, out_size - pos,
            "{\"id\":%d,\"name\":\"%s\",\"category\":\"%s\",\"quantity\":%d,\"location\":\"%s\",\"description\":\"%s\",\"updated_at\":\"%s\"}",
            sqlite3_column_int(stmt, 0), esc_name, esc_cat,
            sqlite3_column_int(stmt, 3), esc_loc, esc_desc,
            sqlite3_column_text(stmt, 6) ? (const char *)sqlite3_column_text(stmt, 6) : "");
    }
    json_out[pos++] = ']';
    json_out[pos] = '\0';
    sqlite3_finalize(stmt);
    return 0;
}

int db_device_search(const char *keyword, char *json_out, int out_size) {
    const char *sql = "SELECT id, name, category, quantity, location, description, updated_at FROM devices "
                      "WHERE name LIKE ? OR category LIKE ? OR location LIKE ? ORDER BY id";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    char pattern[256];
    snprintf(pattern, sizeof(pattern), "%%%s%%", keyword);
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);

    int pos = 0;
    pos += snprintf(json_out + pos, out_size - pos, "[");
    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW && pos < out_size - 500) {
        if (!first) json_out[pos++] = ',';
        first = 0;

        char esc_name[256], esc_cat[128], esc_loc[128], esc_desc[256];
        json_escape(esc_name, (const char *)sqlite3_column_text(stmt, 1), sizeof(esc_name));
        json_escape(esc_cat, sqlite3_column_text(stmt, 2) ? (const char *)sqlite3_column_text(stmt, 2) : "", sizeof(esc_cat));
        json_escape(esc_loc, sqlite3_column_text(stmt, 4) ? (const char *)sqlite3_column_text(stmt, 4) : "", sizeof(esc_loc));
        json_escape(esc_desc, sqlite3_column_text(stmt, 5) ? (const char *)sqlite3_column_text(stmt, 5) : "", sizeof(esc_desc));

        pos += snprintf(json_out + pos, out_size - pos,
            "{\"id\":%d,\"name\":\"%s\",\"category\":\"%s\",\"quantity\":%d,\"location\":\"%s\",\"description\":\"%s\",\"updated_at\":\"%s\"}",
            sqlite3_column_int(stmt, 0), esc_name, esc_cat,
            sqlite3_column_int(stmt, 3), esc_loc, esc_desc,
            sqlite3_column_text(stmt, 6) ? (const char *)sqlite3_column_text(stmt, 6) : "");
    }
    json_out[pos++] = ']';
    json_out[pos] = '\0';
    sqlite3_finalize(stmt);
    return 0;
}

int db_device_get_quantity(int device_id) {
    const char *sql = "SELECT quantity FROM devices WHERE id=?";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, device_id);

    int qty = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        qty = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return qty;
}

static int db_stock_update(int device_id, int delta, const char *username, const char *action) {
    int current = db_device_get_quantity(device_id);
    if (current < 0) return -1;
    if (delta < 0 && current + delta < 0) return -2;

    const char *sql = "UPDATE devices SET quantity = quantity + ?, updated_at = CURRENT_TIMESTAMP WHERE id=?";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, delta);
    sqlite3_bind_int(stmt, 2, device_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    const char *name_sql = "SELECT name FROM devices WHERE id=?";
    sqlite3_prepare_v2(db, name_sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, device_id);
    char dev_name[256] = "";
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        strncpy(dev_name, (const char *)sqlite3_column_text(stmt, 0), sizeof(dev_name) - 1);
    }
    sqlite3_finalize(stmt);

    char detail[512];
    snprintf(detail, sizeof(detail), "%s %d 个，当前库存: %d", action, abs(delta), current + delta);
    db_log_add(username, action, dev_name, abs(delta), detail);

    return 0;
}

int db_stock_in(int device_id, int quantity, const char *username) {
    return db_stock_update(device_id, quantity, username, "入库");
}

int db_stock_out(int device_id, int quantity, const char *username) {
    return db_stock_update(device_id, -quantity, username, "出库");
}

int db_log_add(const char *username, const char *action, const char *device_name, int quantity, const char *detail) {
    const char *sql = "INSERT INTO logs (username, action, device_name, quantity, detail) VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, action, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, device_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, quantity);
    sqlite3_bind_text(stmt, 5, detail, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_log_list(int limit, char *json_out, int out_size) {
    const char *sql = "SELECT id, username, action, device_name, quantity, detail, created_at FROM logs ORDER BY id DESC LIMIT ?";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, limit);

    int pos = 0;
    pos += snprintf(json_out + pos, out_size - pos, "[");
    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW && pos < out_size - 500) {
        if (!first) json_out[pos++] = ',';
        first = 0;

        char esc_user[128], esc_action[64], esc_dev[256], esc_detail[512];
        json_escape(esc_user, (const char *)sqlite3_column_text(stmt, 1), sizeof(esc_user));
        json_escape(esc_action, (const char *)sqlite3_column_text(stmt, 2), sizeof(esc_action));
        json_escape(esc_dev, sqlite3_column_text(stmt, 3) ? (const char *)sqlite3_column_text(stmt, 3) : "", sizeof(esc_dev));
        json_escape(esc_detail, sqlite3_column_text(stmt, 5) ? (const char *)sqlite3_column_text(stmt, 5) : "", sizeof(esc_detail));

        pos += snprintf(json_out + pos, out_size - pos,
            "{\"id\":%d,\"username\":\"%s\",\"action\":\"%s\",\"device_name\":\"%s\",\"quantity\":%d,\"detail\":\"%s\",\"created_at\":\"%s\"}",
            sqlite3_column_int(stmt, 0), esc_user, esc_action, esc_dev,
            sqlite3_column_int(stmt, 4), esc_detail,
            sqlite3_column_text(stmt, 6) ? (const char *)sqlite3_column_text(stmt, 6) : "");
    }
    json_out[pos++] = ']';
    json_out[pos] = '\0';
    sqlite3_finalize(stmt);
    return 0;
}
