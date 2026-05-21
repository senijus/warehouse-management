#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>

int  db_init(const char *db_path);
void db_close(void);

int  db_user_auth(const char *username, const char *password, char *role, int role_size);
int  db_user_exists(const char *username);
int  db_user_get_role(const char *username, char *role, int role_size);
int  db_user_add(const char *username, const char *password, const char *role);
int  db_user_delete(const char *username);
int  db_user_list(char *json_out, int out_size);

int  db_device_add(const char *name, const char *category, int quantity, const char *location, const char *desc);
int  db_device_delete(int device_id);
int  db_device_list(char *json_out, int out_size);
int  db_device_search(const char *keyword, char *json_out, int out_size);
int  db_device_get_quantity(int device_id);

int  db_stock_in(int device_id, int quantity, const char *username);
int  db_stock_out(int device_id, int quantity, const char *username);

int  db_log_add(const char *username, const char *action, const char *device_name, int quantity, const char *detail);
int  db_log_list(int limit, char *json_out, int out_size);

#endif
