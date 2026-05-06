/**************************************************************************
*  File: storage.c                                         Part of tbaMUD *
*  Usage: Runtime storage backend selection and SQL config/import.        *
**************************************************************************/

#include "conf.h"
#include "sysdep.h"

#if defined(CIRCLE_UNIX)
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#endif

#include "structs.h"
#include "utils.h"
#include "storage.h"

#define SQL_MAX_TABLE_NAME 128
#define SQL_MAX_QUERY 4096

typedef struct st_mysql MYSQL;
typedef char **MYSQL_ROW;
typedef struct st_mysql_res MYSQL_RES;

struct mysql_api {
  void *handle;
  MYSQL *(*init)(MYSQL *);
  MYSQL *(*real_connect)(MYSQL *, const char *, const char *, const char *,
    const char *, unsigned int, const char *, unsigned long);
  int (*query)(MYSQL *, const char *);
  unsigned long (*real_escape_string)(MYSQL *, char *, const char *, unsigned long);
  MYSQL_RES *(*store_result)(MYSQL *);
  MYSQL_ROW (*fetch_row)(MYSQL_RES *);
  unsigned long *(*fetch_lengths)(MYSQL_RES *);
  unsigned long long (*num_rows)(MYSQL_RES *);
  void (*free_result)(MYSQL_RES *);
  void (*close)(MYSQL *);
  const char *(*error)(MYSQL *);
};

struct storage_table_spec {
  const char *base_name;
  const char *source_dir;
  const char *extension;
};

static enum storage_backend selected_backend = STORAGE_BACKEND_FLATFILES;
static struct sql_storage_config sql_config;
static struct mysql_api mysql;
static MYSQL *sql_conn = NULL;

static char *sql_escape(const char *value, unsigned long value_len);
static void sql_die(const char *message);

static const struct storage_table_spec storage_tables[] = {
  { "plrfiles", "plrfiles", NULL },
  { "plrobjs", "plrobjs", NULL },
  { "plrvars", "plrvars", NULL },
  { "house", "house", NULL },
  { "mob", "world/mob", ".mob" },
  { "obj", "world/obj", ".obj" },
  { "qst", "world/qst", ".qst" },
  { "shp", "world/shp", ".shp" },
  { "trg", "world/trg", ".trg" },
  { "wld", "world/wld", ".wld" },
  { "zon", "world/zon", ".zon" }
};

static char *trim(char *str)
{
  char *end;

  while (*str && isspace(*str))
    str++;

  if (!*str)
    return str;

  end = str + strlen(str) - 1;
  while (end > str && isspace(*end))
    *end-- = '\0';

  return str;
}

static void copy_config_value(char *dest, size_t dest_size, const char *value)
{
  if (dest_size == 0)
    return;

  snprintf(dest, dest_size, "%s", value);
}

static void make_table_name(char *buf, size_t buf_size, const char *base_name)
{
  size_t prefix_len = strlen(sql_config.table_prefix);

  if (prefix_len == 0)
    snprintf(buf, buf_size, "%s", base_name);
  else if (sql_config.table_prefix[prefix_len - 1] == '_')
    snprintf(buf, buf_size, "%s%s", sql_config.table_prefix, base_name);
  else
    snprintf(buf, buf_size, "%s_%s", sql_config.table_prefix, base_name);
}

static const char *path_basename(const char *path)
{
  const char *slash = strrchr(path, '/');

  return slash ? slash + 1 : path;
}

static void normalize_storage_path(char *buf, size_t buf_size, const char *path)
{
  size_t used = 0;
  int previous_slash = FALSE;

  while (path[0] == '.' && path[1] == '/')
    path += 2;

  while (*path && used + 1 < buf_size) {
    char c = (*path == '\\') ? '/' : *path;

    if (c == '/') {
      if (previous_slash) {
        path++;
        continue;
      }
      previous_slash = TRUE;
    } else
      previous_slash = FALSE;

    buf[used++] = c;
    path++;
  }

  buf[used] = '\0';
}

static int path_starts_with(const char *path, const char *prefix)
{
  return !strncmp(path, prefix, strlen(prefix));
}

static const struct storage_table_spec *table_spec_for_path(const char *path)
{
  size_t i;

  if (!strcmp(path, "etc/hcontrol"))
    return &storage_tables[3];

  for (i = 0; i < sizeof(storage_tables) / sizeof(storage_tables[0]); i++)
    if (path_starts_with(path, storage_tables[i].source_dir))
      return &storage_tables[i];

  return NULL;
}

static void source_subdir_for_path(char *buf, size_t buf_size,
  const struct storage_table_spec *spec, const char *path)
{
  const char *relative = path + strlen(spec->source_dir);
  const char *slash;
  size_t len;

  if (!path_starts_with(path, spec->source_dir)) {
    *buf = '\0';
    return;
  }

  if (*relative == '/')
    relative++;

  slash = strrchr(relative, '/');
  if (!slash) {
    *buf = '\0';
    return;
  }

  len = slash - relative;
  if (len >= buf_size)
    len = buf_size - 1;

  memcpy(buf, relative, len);
  buf[len] = '\0';
}

static int has_extension(const char *filename, const char *extension)
{
  size_t filename_len, extension_len;

  if (!extension)
    return TRUE;

  filename_len = strlen(filename);
  extension_len = strlen(extension);

  if (filename_len < extension_len)
    return FALSE;

  return !str_cmp(filename + filename_len - extension_len, extension);
}

static char *sql_fetch_file_data(const char *path, unsigned long *data_len)
{
  const struct storage_table_spec *spec;
  MYSQL_RES *result;
  MYSQL_ROW row;
  char table_name[SQL_MAX_TABLE_NAME], query[SQL_MAX_QUERY], normalized_path[MAX_STRING_LENGTH];
  char subdir[MAX_INPUT_LENGTH], *escaped_path, *escaped_subdir, *escaped_name, *data;
  unsigned long *lengths;
  const char *source_name;

  *data_len = 0;

  normalize_storage_path(normalized_path, sizeof(normalized_path), path);
  if (!(spec = table_spec_for_path(normalized_path)))
    return NULL;

  make_table_name(table_name, sizeof(table_name), spec->base_name);
  source_name = path_basename(normalized_path);
  source_subdir_for_path(subdir, sizeof(subdir), spec, normalized_path);
  escaped_path = sql_escape(normalized_path, strlen(normalized_path));
  escaped_subdir = sql_escape(subdir, strlen(subdir));
  escaped_name = sql_escape(source_name, strlen(source_name));
  snprintf(query, sizeof(query),
    "SELECT data FROM `%s` "
    "WHERE source_path='%s' OR (source_subdir='%s' AND source_name='%s') "
    "ORDER BY imported_at DESC LIMIT 1",
    table_name, escaped_path, escaped_subdir, escaped_name);
  free(escaped_name);
  free(escaped_subdir);
  free(escaped_path);

  if (mysql.query(sql_conn, query) != 0)
    sql_die("SQL file read query failed");

  result = mysql.store_result(sql_conn);
  if (!result)
    sql_die("SQL file read result failed");

  row = mysql.fetch_row(result);
  if (!row) {
    mysql.free_result(result);
    return NULL;
  }

  lengths = mysql.fetch_lengths(result);
  *data_len = lengths ? lengths[0] : (unsigned long)strlen(row[0]);
  CREATE(data, char, *data_len + 1);
  memcpy(data, row[0], *data_len);
  data[*data_len] = '\0';
  mysql.free_result(result);

  return data;
}

static int parse_vnum_from_filename(const char *filename, const char *extension)
{
  char *end;
  long value;

  if (!extension)
    return -1;

  value = strtol(filename, &end, 10);
  if (end == filename || str_cmp(end, extension))
    return -1;

  return (int)value;
}

static void sql_die(const char *message)
{
  log("SYSERR: %s%s%s", message,
    (sql_conn && mysql.error) ? ": " : "",
    (sql_conn && mysql.error) ? mysql.error(sql_conn) : "");
  exit(1);
}

static void *load_symbol(const char *name)
{
#if defined(CIRCLE_UNIX)
  void *symbol = dlsym(mysql.handle, name);

  if (!symbol) {
    log("SYSERR: Could not load SQL client symbol %s.", name);
    exit(1);
  }

  return symbol;
#else
  log("SYSERR: SQL storage currently requires a Unix-compatible runtime.");
  exit(1);
#endif
}

static void load_mysql_client_or_die(void)
{
#if defined(CIRCLE_UNIX)
  const char *libs[] = {
    "libmariadb.so.3",
    "libmariadb.so",
    "libmysqlclient.so.21",
    "libmysqlclient.so"
  };
  size_t i;

  if (mysql.handle)
    return;

  for (i = 0; i < sizeof(libs) / sizeof(libs[0]); i++) {
    mysql.handle = dlopen(libs[i], RTLD_NOW);
    if (mysql.handle)
      break;
  }

  if (!mysql.handle) {
    log("SYSERR: SQL storage requested, but no MariaDB/MySQL client library could be loaded.");
    log("SYSERR: Install libmariadb3 or libmysqlclient, or start without --usesql.");
    exit(1);
  }

  mysql.init = load_symbol("mysql_init");
  mysql.real_connect = load_symbol("mysql_real_connect");
  mysql.query = load_symbol("mysql_query");
  mysql.real_escape_string = load_symbol("mysql_real_escape_string");
  mysql.store_result = load_symbol("mysql_store_result");
  mysql.fetch_row = load_symbol("mysql_fetch_row");
  mysql.fetch_lengths = load_symbol("mysql_fetch_lengths");
  mysql.num_rows = load_symbol("mysql_num_rows");
  mysql.free_result = load_symbol("mysql_free_result");
  mysql.close = load_symbol("mysql_close");
  mysql.error = load_symbol("mysql_error");
#else
  log("SYSERR: SQL storage currently requires a Unix-compatible runtime.");
  exit(1);
#endif
}

static void sql_query_or_die(const char *query)
{
  if (mysql.query(sql_conn, query) != 0)
    sql_die("SQL query failed");
}

static int sql_table_has_rows(const char *table_name)
{
  char query[SQL_MAX_QUERY];
  MYSQL_RES *result;
  int has_rows;

  snprintf(query, sizeof(query), "SELECT 1 FROM `%s` LIMIT 1", table_name);
  if (mysql.query(sql_conn, query) != 0)
    sql_die("SQL row-count query failed");

  result = mysql.store_result(sql_conn);
  if (!result)
    sql_die("SQL row-count result failed");

  has_rows = mysql.num_rows(result) > 0;
  mysql.free_result(result);

  return has_rows;
}

static char *read_file_data(const char *path, unsigned long *size)
{
  FILE *fl;
  char *data;
  long file_size;

  *size = 0;

  if (!(fl = fopen(path, "rb")))
    return NULL;

  if (fseek(fl, 0, SEEK_END) != 0 || (file_size = ftell(fl)) < 0) {
    fclose(fl);
    return NULL;
  }
  rewind(fl);

  CREATE(data, char, file_size + 1);
  if (file_size > 0 && fread(data, 1, file_size, fl) != (size_t)file_size) {
    fclose(fl);
    free(data);
    return NULL;
  }

  fclose(fl);
  data[file_size] = '\0';
  *size = (unsigned long)file_size;

  return data;
}

static char *read_stream_data(FILE *fl, unsigned long *size)
{
  char *data;
  long file_size;

  *size = 0;

  if (fflush(fl) != 0 || fseek(fl, 0, SEEK_END) != 0 || (file_size = ftell(fl)) < 0)
    return NULL;
  rewind(fl);

  CREATE(data, char, file_size + 1);
  if (file_size > 0 && fread(data, 1, file_size, fl) != (size_t)file_size) {
    free(data);
    return NULL;
  }

  data[file_size] = '\0';
  *size = (unsigned long)file_size;

  return data;
}

static char *sql_escape(const char *value, unsigned long value_len)
{
  char *escaped;
  unsigned long escaped_len;

  CREATE(escaped, char, (value_len * 2) + 1);
  escaped_len = mysql.real_escape_string(sql_conn, escaped, value, value_len);
  escaped[escaped_len] = '\0';

  return escaped;
}

static void sql_import_file(const char *table_name, const char *source_path,
  const char *source_subdir, const char *source_name, int vnum)
{
  char normalized_path[MAX_STRING_LENGTH];
  char *data, *escaped_path, *escaped_subdir, *escaped_name, *escaped_data;
  char *query;
  char vnum_literal[32];
  unsigned long data_len;
  size_t query_len;

  if (!(data = read_file_data(source_path, &data_len))) {
    log("SYSERR: Could not read %s for SQL import.", source_path);
    return;
  }

  normalize_storage_path(normalized_path, sizeof(normalized_path), source_path);
  escaped_path = sql_escape(normalized_path, strlen(normalized_path));
  escaped_subdir = sql_escape(source_subdir, strlen(source_subdir));
  escaped_name = sql_escape(source_name, strlen(source_name));
  escaped_data = sql_escape(data, data_len);

  query_len = strlen(escaped_path) + strlen(escaped_subdir) + strlen(escaped_name) +
    strlen(escaped_data) + strlen(table_name) + 512;
  CREATE(query, char, query_len);
  if (vnum < 0)
    snprintf(vnum_literal, sizeof(vnum_literal), "NULL");
  else
    snprintf(vnum_literal, sizeof(vnum_literal), "%d", vnum);

  snprintf(query, query_len,
    "INSERT INTO `%s` "
    "(source_path, source_subdir, source_name, vnum, data) "
    "VALUES ('%s', '%s', '%s', %s, '%s') "
    "ON DUPLICATE KEY UPDATE "
    "source_subdir=VALUES(source_subdir), source_name=VALUES(source_name), "
    "vnum=VALUES(vnum), data=VALUES(data), imported_at=CURRENT_TIMESTAMP",
    table_name, escaped_path, escaped_subdir, escaped_name,
    vnum_literal, escaped_data);

  sql_query_or_die(query);

  free(query);
  free(escaped_data);
  free(escaped_name);
  free(escaped_subdir);
  free(escaped_path);
  free(data);
}

static void sql_store_data(const char *path, const char *data, unsigned long data_len)
{
  const struct storage_table_spec *spec;
  char normalized_path[MAX_STRING_LENGTH];
  char table_name[SQL_MAX_TABLE_NAME], subdir[MAX_INPUT_LENGTH];
  char *escaped_path, *escaped_subdir, *escaped_name, *escaped_data, *query;
  char vnum_literal[32];
  const char *source_name;
  size_t query_len;
  int vnum;

  normalize_storage_path(normalized_path, sizeof(normalized_path), path);
  if (!(spec = table_spec_for_path(normalized_path))) {
    log("SYSERR: SQL storage does not know where to store %s.", path);
    return;
  }

  make_table_name(table_name, sizeof(table_name), spec->base_name);
  source_name = path_basename(normalized_path);
  source_subdir_for_path(subdir, sizeof(subdir), spec, normalized_path);
  vnum = parse_vnum_from_filename(source_name, spec->extension);

  escaped_path = sql_escape(normalized_path, strlen(normalized_path));
  escaped_subdir = sql_escape(subdir, strlen(subdir));
  escaped_name = sql_escape(source_name, strlen(source_name));
  escaped_data = sql_escape(data, data_len);
  if (vnum < 0)
    snprintf(vnum_literal, sizeof(vnum_literal), "NULL");
  else
    snprintf(vnum_literal, sizeof(vnum_literal), "%d", vnum);

  query_len = strlen(escaped_path) + strlen(escaped_subdir) + strlen(escaped_name) +
    strlen(escaped_data) + strlen(table_name) + 512;
  CREATE(query, char, query_len);
  snprintf(query, query_len,
    "INSERT INTO `%s` "
    "(source_path, source_subdir, source_name, vnum, data) "
    "VALUES ('%s', '%s', '%s', %s, '%s') "
    "ON DUPLICATE KEY UPDATE "
    "source_subdir=VALUES(source_subdir), source_name=VALUES(source_name), "
    "vnum=VALUES(vnum), data=VALUES(data), imported_at=CURRENT_TIMESTAMP",
    table_name, escaped_path, escaped_subdir, escaped_name, vnum_literal, escaped_data);

  sql_query_or_die(query);

  free(query);
  free(escaped_data);
  free(escaped_name);
  free(escaped_subdir);
  free(escaped_path);
}

static void append_text(char **buf, size_t *used, size_t *size, const char *text)
{
  size_t len = strlen(text);

  if (*used + len + 1 >= *size) {
    while (*used + len + 1 >= *size)
      *size *= 2;
    RECREATE(*buf, char, *size);
  }

  memcpy(*buf + *used, text, len);
  *used += len;
  (*buf)[*used] = '\0';
}

static void sql_rebuild_index_for_spec(const struct storage_table_spec *spec)
{
  MYSQL_RES *result;
  MYSQL_ROW row;
  char table_name[SQL_MAX_TABLE_NAME], query[SQL_MAX_QUERY], index_path[MAX_INPUT_LENGTH];
  char *data;
  size_t used = 0, size = 1024;

  if (!spec->extension)
    return;

  make_table_name(table_name, sizeof(table_name), spec->base_name);
  snprintf(query, sizeof(query),
    "SELECT source_name FROM `%s` WHERE source_name LIKE '%%%s' "
    "ORDER BY COALESCE(vnum, 2147483647), source_name",
    table_name, spec->extension);

  if (mysql.query(sql_conn, query) != 0)
    sql_die("SQL index rebuild query failed");

  result = mysql.store_result(sql_conn);
  if (!result)
    sql_die("SQL index rebuild result failed");

  CREATE(data, char, size);
  *data = '\0';
  while ((row = mysql.fetch_row(result)) != NULL) {
    append_text(&data, &used, &size, row[0]);
    append_text(&data, &used, &size, "\n");
  }
  mysql.free_result(result);

  append_text(&data, &used, &size, "$\n");
  snprintf(index_path, sizeof(index_path), "%s/index", spec->source_dir);
  sql_store_data(index_path, data, (unsigned long)used);
  free(data);
}

static void sql_import_directory(const char *table_name,
  const struct storage_table_spec *spec, const char *path, const char *subdir)
{
#if defined(CIRCLE_UNIX)
  DIR *dir;
  struct dirent *entry;
  struct stat st;
  char child_path[MAX_STRING_LENGTH], child_subdir[MAX_INPUT_LENGTH];

  if (!(dir = opendir(path))) {
    log("SYSERR: Could not open %s for SQL import.", path);
    return;
  }

  while ((entry = readdir(dir)) != NULL) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;

    snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
    if (stat(child_path, &st) != 0)
      continue;

    if (S_ISDIR(st.st_mode)) {
      if (*subdir)
        snprintf(child_subdir, sizeof(child_subdir), "%s/%s", subdir, entry->d_name);
      else
        snprintf(child_subdir, sizeof(child_subdir), "%s", entry->d_name);
      sql_import_directory(table_name, spec, child_path, child_subdir);
    } else if (S_ISREG(st.st_mode) && has_extension(entry->d_name, spec->extension)) {
      sql_import_file(table_name, child_path, subdir, entry->d_name,
        parse_vnum_from_filename(entry->d_name, spec->extension));
    }
  }

  closedir(dir);
#else
  log("SYSERR: SQL flat-file import currently requires a Unix-compatible runtime.");
#endif
}

static void sql_create_storage_table(const char *table_name)
{
  char query[SQL_MAX_QUERY];

  snprintf(query, sizeof(query),
    "CREATE TABLE IF NOT EXISTS `%s` ("
    "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
    "source_path VARCHAR(255) NOT NULL,"
    "source_subdir VARCHAR(64) NOT NULL DEFAULT '',"
    "source_name VARCHAR(128) NOT NULL,"
    "vnum INT NULL,"
    "data MEDIUMTEXT NOT NULL,"
    "imported_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
    "PRIMARY KEY (id),"
    "UNIQUE KEY source_path (source_path),"
    "KEY vnum (vnum)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
    table_name);

  sql_query_or_die(query);
}

void storage_use_sql(void)
{
  selected_backend = STORAGE_BACKEND_SQL;
}

bool storage_is_sql(void)
{
  return selected_backend == STORAGE_BACKEND_SQL;
}

const char *storage_backend_name(void)
{
  return storage_is_sql() ? "sql" : "flatfiles";
}

const struct sql_storage_config *storage_sql_config(void)
{
  return &sql_config;
}

void storage_load_config_or_die(const char *filename)
{
  FILE *fl;
  char line[READ_SIZE], *key, *value, *equals, *comment;
  int lineno = 0;

  memset(&sql_config, 0, sizeof(sql_config));
  sql_config.port = 3306;

  if (!(fl = fopen(filename, "r"))) {
    log("SYSERR: SQL storage requested, but could not open %s.", filename);
    exit(1);
  }

  while (fgets(line, sizeof(line), fl)) {
    lineno++;

    if ((comment = strchr(line, '#')) != NULL)
      *comment = '\0';

    key = trim(line);
    if (!*key)
      continue;

    if ((equals = strchr(key, '=')) == NULL) {
      log("SYSERR: Invalid SQL config line %d in %s. Expected key=value.", lineno, filename);
      fclose(fl);
      exit(1);
    }

    *equals = '\0';
    value = trim(equals + 1);
    key = trim(key);

    if (!str_cmp(key, "host"))
      copy_config_value(sql_config.host, sizeof(sql_config.host), value);
    else if (!str_cmp(key, "port"))
      sql_config.port = (unsigned int)atoi(value);
    else if (!str_cmp(key, "user"))
      copy_config_value(sql_config.user, sizeof(sql_config.user), value);
    else if (!str_cmp(key, "password"))
      copy_config_value(sql_config.password, sizeof(sql_config.password), value);
    else if (!str_cmp(key, "database"))
      copy_config_value(sql_config.database, sizeof(sql_config.database), value);
    else if (!str_cmp(key, "table_prefix"))
      copy_config_value(sql_config.table_prefix, sizeof(sql_config.table_prefix), value);
    else
      log("SYSERR: Ignoring unknown SQL config key '%s' in %s.", key, filename);
  }

  fclose(fl);

  if (!*sql_config.host || !*sql_config.user || !*sql_config.database || sql_config.port == 0) {
    log("SYSERR: SQL config requires host, port, user, and database.");
    exit(1);
  }
}

void storage_sql_connect_or_die(void)
{
  load_mysql_client_or_die();

  if (!(sql_conn = mysql.init(NULL)))
    sql_die("Could not initialize SQL client");

  if (!mysql.real_connect(sql_conn, sql_config.host, sql_config.user,
      sql_config.password, sql_config.database, sql_config.port, NULL, 0))
    sql_die("Could not connect to SQL database");
}

void storage_sql_prepare_or_die(void)
{
  size_t i;
  char table_name[SQL_MAX_TABLE_NAME];
  int already_populated;

  for (i = 0; i < sizeof(storage_tables) / sizeof(storage_tables[0]); i++) {
    make_table_name(table_name, sizeof(table_name), storage_tables[i].base_name);
    sql_create_storage_table(table_name);

    already_populated = sql_table_has_rows(table_name);
    if (already_populated) {
      log("SQL storage table %s already has data; using SQL data.", table_name);
    } else {
      log("SQL storage table %s is empty; importing flat files.", table_name);
      sql_import_directory(table_name, &storage_tables[i], storage_tables[i].source_dir, "");
      if (!strcmp(storage_tables[i].base_name, "house"))
        storage_sql_import_path("etc/hcontrol");
    }

    sql_rebuild_index_for_spec(&storage_tables[i]);
  }
}

void storage_sql_close(void)
{
  if (sql_conn) {
    mysql.close(sql_conn);
    sql_conn = NULL;
  }

#if defined(CIRCLE_UNIX)
  if (mysql.handle) {
    dlclose(mysql.handle);
    memset(&mysql, 0, sizeof(mysql));
  }
#endif
}

FILE *storage_fopen_read(const char *path)
{
  FILE *fl;
  char *data;
  unsigned long data_len;

  if (!storage_is_sql())
    return fopen(path, "r");

  if (!table_spec_for_path(path))
    return fopen(path, "r");

  data = sql_fetch_file_data(path, &data_len);
  if (!data)
    return NULL;

  fl = tmpfile();
  if (!fl) {
    free(data);
    return NULL;
  }

  if (data_len > 0)
    fwrite(data, 1, data_len, fl);
  rewind(fl);
  free(data);

  return fl;
}

FILE *storage_fopen_write(const char *path)
{
  if (!storage_is_sql())
    return fopen(path, "w");

  if (!table_spec_for_path(path))
    return fopen(path, "w");

  return tmpfile();
}

void storage_fclose_write(FILE *fl, const char *path)
{
  const struct storage_table_spec *spec;
  char *data;
  unsigned long data_len;

  if (!fl)
    return;

  if (storage_is_sql() && (spec = table_spec_for_path(path)) != NULL) {
    if ((data = read_stream_data(fl, &data_len)) != NULL) {
      sql_store_data(path, data, data_len);
      sql_rebuild_index_for_spec(spec);
      free(data);
    } else
      log("SYSERR: Could not read temporary stream for SQL save of %s.", path);
  }

  fclose(fl);
}

void storage_sql_import_path(const char *path)
{
  const struct storage_table_spec *spec;
  char normalized_path[MAX_STRING_LENGTH];
  char table_name[SQL_MAX_TABLE_NAME], subdir[MAX_INPUT_LENGTH];
  const char *source_name;

  if (!storage_is_sql())
    return;

  normalize_storage_path(normalized_path, sizeof(normalized_path), path);
  if (!(spec = table_spec_for_path(normalized_path)))
    return;

  make_table_name(table_name, sizeof(table_name), spec->base_name);
  source_name = path_basename(normalized_path);
  source_subdir_for_path(subdir, sizeof(subdir), spec, normalized_path);
  sql_import_file(table_name, normalized_path, subdir, source_name,
    parse_vnum_from_filename(source_name, spec->extension));
  sql_rebuild_index_for_spec(spec);
}

void storage_sql_delete_path(const char *path)
{
  const struct storage_table_spec *spec;
  char normalized_path[MAX_STRING_LENGTH], table_name[SQL_MAX_TABLE_NAME], query[SQL_MAX_QUERY];
  char *escaped_path;

  if (!storage_is_sql())
    return;

  normalize_storage_path(normalized_path, sizeof(normalized_path), path);
  if (!(spec = table_spec_for_path(normalized_path)))
    return;

  make_table_name(table_name, sizeof(table_name), spec->base_name);
  escaped_path = sql_escape(normalized_path, strlen(normalized_path));
  snprintf(query, sizeof(query), "DELETE FROM `%s` WHERE source_path='%s'",
    table_name, escaped_path);
  free(escaped_path);

  sql_query_or_die(query);
  sql_rebuild_index_for_spec(spec);
}
