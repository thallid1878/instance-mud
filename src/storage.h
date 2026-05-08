/**
* @file storage.h
* Runtime storage backend selection.
*/
#ifndef _STORAGE_H_
#define _STORAGE_H_

#define SQL_CONFIG_FILE "sql.conf"

enum storage_backend {
  STORAGE_BACKEND_FLATFILES = 0,
  STORAGE_BACKEND_SQL
};

struct sql_storage_config {
  char host[128];
  unsigned int port;
  char user[128];
  char password[128];
  char database[128];
  char table_prefix[64];
};

void storage_use_sql(void);
bool storage_is_sql(void);
const char *storage_backend_name(void);
void storage_load_config_or_die(const char *filename);
const struct sql_storage_config *storage_sql_config(void);
void storage_sql_connect_or_die(void);
void storage_sql_prepare_or_die(void);
void storage_sql_close(void);
FILE *storage_fopen_read(const char *path);
FILE *storage_fopen_write(const char *path);
void storage_fclose_write(FILE *fl, const char *path);
void storage_sql_import_path(const char *path);
int storage_sql_export_zone(int zone_num);
void storage_sql_delete_path(const char *path);

#endif /* _STORAGE_H_ */
