#include "client_priv.h"
#include "my_default.h"
/*#include <m_ctype.h>
#include <stdarg.h>
#include <my_dir.h>*/
#include <mysqld_error.h>
#include "welcome_copyright_notice.h"
#include <csignal>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <string>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <list>
#include <dirent.h>
#include <algorithm>

#include "mysql.h"
using namespace std;

//operation of 
enum port_op
{
  OP_INVALID = 0,
  OP_EXPORT,
  OP_IMPORT,
};
typedef enum port_op port_op_t;

static MYSQL mysql;            /* The connection */
static MEM_ROOT hash_mem_root; /* memory object */

static my_bool connected = 0;
static const CHARSET_INFO *charset_info = &my_charset_latin1;
static my_bool tty_password = 0;
//args
static char *opt_host = NULL;
static uint opt_mysql_port = 0;
static char *opt_mysql_unix_port = 0;
static char *opt_user = NULL;
static char *opt_db = NULL;
static char *opt_password = NULL;
static char *opt_op = NULL;
static char *opt_data_dir = NULL;
static char *opt_file_dir = NULL;
//static char *opt_tables = NULL;
static port_op_t op = OP_INVALID;
static char *opt_owner = NULL;
static char default_user[] = "mysql";

static struct my_option my_long_options[] =
    {
        {"help", '?', "Display this help and exit.", 0, 0, 0, GET_NO_ARG, NO_ARG, 0,
         0, 0, 0, 0, 0},
        {"help", 'I', "Synonym for -?", 0, 0, 0, GET_NO_ARG, NO_ARG, 0,
         0, 0, 0, 0, 0},
        {"version", 'V', "Output version information and exit.", 0, 0, 0,
         GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
        {"host", 'h', "Connect to host.", &opt_host,
         &opt_host, 0, GET_STR_ALLOC, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
        {"port", 'P', "Port number to use for connection or 0 for default to, in "
                      "order of preference, my.cnf, $MYSQL_TCP_PORT, "
                      #if MYSQL_PORT_DEFAULT == 0
                      "/etc/services, "
                      #endif
                      "built-in default (" STRINGIFY_ARG(MYSQL_PORT) ").",
            &opt_mysql_port,
            &opt_mysql_port, 0, GET_UINT, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
        {"socket", 'S', "The socket file to use for connection.",
         &opt_mysql_unix_port, &opt_mysql_unix_port, 0, GET_STR_ALLOC,
         REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
        {"user", 'u', "User for login if not current user.", &opt_user,
         &opt_user, 0, GET_STR_ALLOC, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
        {"db", 'D', "porting database.", &opt_db,
         &opt_db, 0, GET_STR_ALLOC, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
        {"password", 'p',
         "Password to use when connecting to server. If password is not given it's asked from the tty.",
         0, 0, 0, GET_PASSWORD, OPT_ARG, 0, 0, 0, 0, 0, 0},
        {"operation", 'o', "Valid value is export or import.", &opt_op,
         &opt_op, 0, GET_STR_ALLOC, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
        {"datadir", 'd', "MySQL table data directory.", &opt_data_dir,
         &opt_data_dir, 0, GET_STR_ALLOC, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
        {"filedir", 'f', "Directory to store ported type file.", &opt_file_dir,
         &opt_file_dir, 0, GET_STR_ALLOC, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
        /*  {"tables", 't', "Port tables.", &opt_tables,
            &opt_tables, 0, GET_STR_ALLOC, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},*/
        {"owner", 'O', "change files' ownership to owner",
         &opt_owner, &opt_owner, 0, GET_STR_ALLOC, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
    };

typedef char table_name_t[256];
typedef char db_name_t[256];

struct table_struct
{
  table_name_t name;
};
typedef table_struct table_t;

#define MAX_TABLE_BUFFER_SIZE (1024)
struct table_buffer_struct
{
  int number;
  table_t tables[MAX_TABLE_BUFFER_SIZE];
  table_buffer_struct() { number = 0; }
};
typedef table_buffer_struct table_buffer_t;

struct db_struct
{
  db_name_t name;
};
typedef db_struct db_t;

#define MAX_DATABASE_BUFFER_SIZE (1024)
struct db_buffer_struct
{
  int number;
  db_t databases[MAX_DATABASE_BUFFER_SIZE];
  db_buffer_struct() { number = 0; }
};
typedef db_buffer_struct db_buffer_t;

db_buffer_t database_buffer;

table_buffer_t table_buffer;

#define HTP_WELCOME_COPYRIGHT_NOTICE(first_year) \
  (strcmp(first_year, COPYRIGHT_NOTICE_CURRENT_YEAR) ? \
   "Copyright (c) " first_year ", " COPYRIGHT_NOTICE_CURRENT_YEAR ", " \
   "Htp and/or its affiliates. All rights reserved.\n\nHtp is a " \
   "registered trademark of Htp Corporation and/or its\naffiliates. " \
   "Other names may be trademarks of their respective\nowners.\n" : \
   "Copyright (c) " first_year ", Htp and/or its affiliates. " \
   "All rights reserved.\n\nHtp is a registered trademark of " \
   "Htp Corporation and/or its\naffiliates. Other names may be " \
   "trademarks of their respective\nowners.\n")

#define VER "0.01"

static void usage(int version)
{
  cout << my_progname << " Ver " << VER << " Distrib " << MYSQL_SERVER_VERSION << ", for " << SYSTEM_TYPE << "("
       << MACHINE_TYPE << ")" << endl;

  if (version)
    return;
  puts(HTP_WELCOME_COPYRIGHT_NOTICE("2013"));
  cout << "Usage: " << my_progname << " [OPTIONS] [database]" << endl;
  /*
    Turn default for zombies off so that the help on how to 
    turn them off text won't show up.
    This is safe to do since it's followed by a call to exit().
  */
  for (struct my_option *optp = my_long_options; optp->name; optp++)
  {
    if (optp->id == OPT_SECURE_AUTH)
    {
      optp->def_value = 0;
      break;
    }
  }
  my_print_help(my_long_options);
  //  print_defaults("my", load_default_groups);
  my_print_variables(my_long_options);
}

list<char *> option_tables;

/*void
get_option_tables(const char *tables)
{
  if (opt_tables != NULL || strlen(tables) > 0)
  {
    int len = 0;
    int table_count = 0;
    const char *table = tables;
    const char *tail = tables;
    while (*tail != 0)
    {
      if (*tail == ',')
      {
        strncpy(table_buffer.tables[table_count].name, table, len);
        table_buffer.tables[table_count].name[len] = 0;
        table_buffer.number++;
        option_tables.push_back(table_buffer.tables[table_count].name);
        table_count++;
        len = 0;
        table = tail + 1;
      }
      else
      {
        len++;
      }
      tail++;
    }
    if (len != 0)
    {
      strncpy(table_buffer.tables[table_count].name, table, len);
      table_buffer.tables[table_count].name[len] = 0;
      table_buffer.number++;
      option_tables.push_back(table_buffer.tables[table_count].name);
      len = 0;
    }
  }
  }*/

my_bool
get_one_option(int optid, const struct my_option *opt MY_ATTRIBUTE((unused)),
               char *argument)
{
  switch (optid)
  {
    /*  case 't':
    get_option_tables(opt_tables);
    break;*/
    case 'p':
      tty_password = 1;
      break;
    case 'o':
      if (strcasecmp(opt_op, "export") == 0)
      {
        op = OP_EXPORT;
      }
      else if (strcasecmp(opt_op, "import") == 0)
      {
        op = OP_IMPORT;
      }
      else
      {
      }
      break;
    case 'I':
    case '?':
      usage(0);
      exit(0);
  }
  return 0;
}

static int get_options(int argc, char **argv)
{
  int ho_error;

  if ((ho_error = handle_options(&argc, &argv, my_long_options, get_one_option)))
    exit(ho_error);

  if (argc != 0)
  {
    usage(0);
    exit(1);
  }
  if (tty_password)
    opt_password = get_tty_password(NullS);

  return (0);
}

void
stop_for_dbg()
{
  printf("stop_for_dbg\n");
}

static void
init_connection_options(MYSQL *mysql)
{
  /*  my_bool handle_expired= (opt_connect_expired_password || !status.batch) ?
    TRUE : FALSE;

  if (opt_init_command)
    mysql_options(mysql, MYSQL_INIT_COMMAND, opt_init_command);

  if (opt_connect_timeout)
  {
    uint timeout= opt_connect_timeout;
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, (char*) &timeout);
  }

  if (opt_bind_addr)
    mysql_options(mysql, MYSQL_OPT_BIND, opt_bind_addr);

  if (opt_compress)
    mysql_options(mysql, MYSQL_OPT_COMPRESS, NullS);

  if (using_opt_local_infile)
    mysql_options(mysql, MYSQL_OPT_LOCAL_INFILE, (char*) &opt_local_infile);

  SSL_SET_OPTIONS(mysql);

  if (opt_protocol)
    mysql_options(mysql, MYSQL_OPT_PROTOCOL, (char*) &opt_protocol);

#if defined (_WIN32) && !defined (EMBEDDED_LIBRARY)
  if (shared_memory_base_name)
    mysql_options(mysql, MYSQL_SHARED_MEMORY_BASE_NAME, shared_memory_base_name);
#endif

  if (safe_updates)
  {
    char init_command[100];
    sprintf(init_command,
	    "SET SQL_SAFE_UPDATES=1,SQL_SELECT_LIMIT=%lu,MAX_JOIN_SIZE=%lu",
	    select_limit, max_join_size);
    mysql_options(mysql, MYSQL_INIT_COMMAND, init_command);
  }

  mysql_set_character_set(mysql, default_charset);

  if (opt_plugin_dir && *opt_plugin_dir)
    mysql_options(mysql, MYSQL_PLUGIN_DIR, opt_plugin_dir);

  if (opt_default_auth && *opt_default_auth)
    mysql_options(mysql, MYSQL_DEFAULT_AUTH, opt_default_auth);

#if !defined(HAVE_YASSL)
  if (opt_server_public_key && *opt_server_public_key)
    mysql_options(mysql, MYSQL_SERVER_PUBLIC_KEY, opt_server_public_key);
#endif

  if (using_opt_enable_cleartext_plugin)
    mysql_options(mysql, MYSQL_ENABLE_CLEARTEXT_PLUGIN,
                  (char*) &opt_enable_cleartext_plugin);

  mysql_options(mysql, MYSQL_OPT_CONNECT_ATTR_RESET, 0);
  mysql_options4(mysql, MYSQL_OPT_CONNECT_ATTR_ADD, "program_name", "mysql");

  mysql_options(mysql, MYSQL_OPT_CAN_HANDLE_EXPIRED_PASSWORDS, &handle_expired);*/
}

static int
sql_real_connect(const char *host, const char *user, const char *password)
{
  assert(connected == 0);

  mysql_init(&mysql);
  init_connection_options(&mysql);

#ifdef _WIN32
  uint cnv_errors;
  String converted_database, converted_user;
  if (!my_charset_same(&my_charset_utf8mb4_bin, mysql.charset))
  {
    /* Convert user and database from UTF8MB4 to connection character set */
    if (user)
    {
      converted_user.copy(user, strlen(user) + 1,
                          &my_charset_utf8mb4_bin, mysql.charset,
                          &cnv_errors);
      user= (char *) converted_user.ptr();
    }
    if (database)
    {
      converted_database.copy(database, strlen(database) + 1,
                              &my_charset_utf8mb4_bin, mysql.charset,
                              &cnv_errors);
      database= (char *) converted_database.ptr();
    }
  }
#endif

  if (!mysql_real_connect(&mysql, host, user, password,
                          NULL, opt_mysql_port, opt_mysql_unix_port,
                          CLIENT_INTERACTIVE | CLIENT_MULTI_STATEMENTS))
  {
    if (mysql_errno(&mysql) == ER_MUST_CHANGE_PASSWORD_LOGIN)
    {
      fprintf(stdout, "Please use --connect-expired-password option or " \
                           "invoke mysql in interactive mode.\n");
      return -1;
    }
    if ((mysql_errno(&mysql) != CR_CONN_HOST_ERROR &&
        mysql_errno(&mysql) != CR_CONNECTION_ERROR))
    {
      //printf("error connect");
      cout << "error connect" << endl;
    }
    return -1;                    // Retryable
  }

#ifdef _WIN32
  /* Convert --execute buffer from UTF8MB4 to connection character set */
  if (!execute_buffer_conversion_done++ &&
      status.line_buff &&
      !status.line_buff->file && /* Convert only -e buffer, not real file */
      status.line_buff->buffer < status.line_buff->end && /* Non-empty */
      !my_charset_same(&my_charset_utf8mb4_bin, mysql.charset))
  {
    String tmp;
    size_t len= status.line_buff->end - status.line_buff->buffer;
    uint dummy_errors;
    /*
      Don't convert trailing '\n' character - it was appended during
      last batch_readline_command() call. 
      Oherwise we'll get an extra line, which makes some tests fail.
    */
    if (status.line_buff->buffer[len - 1] == '\n')
      len--;
    if (tmp.copy(status.line_buff->buffer, len,
                 &my_charset_utf8mb4_bin, mysql.charset, &dummy_errors))
      return 1;

    /* Free the old line buffer */
    batch_readline_end(status.line_buff);

    /* Re-initialize line buffer from the converted string */
    if (!(status.line_buff= batch_readline_command(NULL, (char *) tmp.c_ptr_safe())))
      return 1;
  }
#endif /* _WIN32 */

  charset_info = mysql.charset;

  connected = 1;

  return 0;
}

const int MAX_RECONNECT_TIME = 5;
static int
sql_connect()
{
  int try_count = 1;

  while (true)
  {
    int error = sql_real_connect(opt_host, opt_user, opt_password);
    if (error != 0)
    {
      if (try_count > MAX_RECONNECT_TIME)
      {
        cout << "fail to connect, try " << MAX_RECONNECT_TIME << " times!" << mysql_error(&mysql);
        exit(-1);
      }
      try_count++;
    }
    else
    {
      break;
    }
  }
  return 0;
}

//tables to export or import。
//该list保存的内容分两种情况，当未指定导入/导出表时，该列表保存的是database中
//的表，全部进行导出/导入。如果指定指定倒入导出表时，则记录指定的表
static list<char *> tables;

//list for databases
static list<char *> databases;

char buffer[1024 * 32];

bool
get_tables_from_db()
{
  table_buffer.number = 1 ;
  tables.clear();
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  sprintf(buffer, "show tables;");
  mysql_query(&mysql, buffer);
  if (!(result = mysql_store_result(&mysql)))
  {
    return false;
  }
  else
  {
    int table_count = 0;
    while ((row = mysql_fetch_row(result)))
    {
      char *table_name = strdup_root(&hash_mem_root, (char *) row[0]);
      strcpy(table_buffer.tables[table_count].name, table_name);
      table_buffer.number++;
      tables.push_back(table_buffer.tables[table_count].name);
      table_count++;
    }
    mysql_free_result(result);
  }

  return true;
}

bool
get_databases_from_server()
{
  database_buffer.number = 1;
  databases.clear();
  string tmp_dbname;
  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  sprintf(buffer, "show databases;");
  mysql_query(&mysql, buffer);
  if (!(result = mysql_store_result(&mysql)))
  {
    return false;
  }
  else
  {
    int db_count = 0;
    while ((row = mysql_fetch_row(result)))
    {
      char *db_name = strdup_root(&hash_mem_root, (char *) row[0]);
      tmp_dbname = db_name;
      if ((tmp_dbname.find("mysql", 0) == string::npos) && (tmp_dbname.find("sys", 0) == string::npos)
          && (tmp_dbname.find("performance_schema", 0) == string::npos)
          && (tmp_dbname.find("information_schema", 0) == string::npos))
      {
        strcpy(database_buffer.databases[database_buffer.number - 1].name, db_name);
        databases.push_back(database_buffer.databases[database_buffer.number - 1].name);
        database_buffer.number++;
      }
      db_count++;
    }
    mysql_free_result(result);
  }

  return true;
}

bool
get_tables_from_option()
{
  return true;
}

bool
get_tables()
{
  if (option_tables.empty())
    return get_tables_from_db();
  else
    return get_tables_from_option();
}

bool get_databases_from_option()
{
  databases.push_back(opt_db);
  return true;
}
bool get_databases()
{
  bool succ;
  if (opt_db != NULL)
    succ = get_databases_from_option();
  else
    succ = get_databases_from_server();
  if (databases.empty())
    succ = false;
  return succ;
}
bool
export_check(string *err)
{
  return true;
}

static int export_show_master_status(string *err)
{
  MYSQL_ROW row;
  MYSQL_RES *result;
  mysql_query(&mysql, "SHOW MASTER STATUS");
  if (!(result = mysql_store_result(&mysql)))
  {
    err->append(mysql_error(&mysql));
    return false;
  }
  sprintf(buffer, "%s/master.info", opt_file_dir);
  FILE *master_info = fopen(buffer, "w+");
  row = mysql_fetch_row(result);
  if (row && row[0] && row[1])
  {
    /* SHOW MASTER STATUS reports file and position */
    fprintf(master_info,
            "\n--\n-- Position to start replication or point-in-time "
            "recovery from\n--\n\n");
    fprintf(master_info,
            "CHANGE MASTER TO MASTER_LOG_FILE='%s', MASTER_LOG_POS=%s;\n",
            row[0], row[1]);
  }
  mysql_free_result(result);
  fflush(master_info);
  fclose(master_info);
  return true;
}

static int do_check_slave()
{
  MYSQL_RES *result;
  int r = mysql_query(&mysql, "SHOW SLAVE STATUS;");
  if (r != 0)
  {
    return -1;
  }

  result = mysql_store_result(&mysql);
  if (mysql_num_rows(result))
  {
    mysql_free_result(result);
    return 1;
  }
  else
    return 0;
}
static bool export_flush_tables_with_read_lock(string *err, int is_slave_flag)
{
  /*
    flush tables with read lock
    锁定数据库，确保数据的一致性
  */

  int r = mysql_query(&mysql, "FLUSH TABLES");
  if (r != 0)
  {
    err->append(mysql_error(&mysql));
    return false;
  }
  r = mysql_query(&mysql, "FLUSH TABLES WITH READ LOCK");
  if (r != 0)
  {
    err->append(mysql_error(&mysql));
    return false;
  }
  r = mysql_query(&mysql, "set global read_only=1;");
  if (r != 0)
  {
    err->append(mysql_error(&mysql));
    return false;
  }

  r = mysql_query(&mysql, "set global super_read_only=1;");
  if (r != 0)
  {
    err->append(mysql_error(&mysql));
    return false;
  }
  if (is_slave_flag)
  {
    r = mysql_query(&mysql, "stop slave;");
    if (r != 0)
    {
      err->append(mysql_error(&mysql));
      return false;
    }
  }
  return true;
}

void signalHandler(int signum)
{
  cout << "Interrupt signal (" << signum << ") received" << endl;
  exit(signum);
}

bool check_frm_files(const string opt_dir, const string tbname, int check_file)
{
  char s[100];
  DIR *dir;
  string tmp_opt_dir = opt_dir;
  string res = tbname;
  res += ".frm";
  string tmp_filename;
  struct dirent *rent;
  //signal(SIGSEGV, signalHandler);
  if ((dir = opendir(tmp_opt_dir.c_str())) == NULL)
  {
    //cout<<"Open directory "<<opt_dir<<" error,please check manually!"<<endl;
    return false;
  }
  if (!check_file)
    return true;
  while ((rent = readdir(dir)))
  {
    strcpy(s, rent->d_name);
    tmp_filename = s;
    if (tmp_filename.find(res) != string::npos)
      return true;
  }
  return false;
}

bool
export_single_table(const string &db_name, const string &table_name)
{
  int r = 0;
  //锁定表，并生成.cfg文件
  string opt_data_dir_fi = opt_data_dir;
  string opt_file_dir_fi = opt_file_dir;
  opt_data_dir_fi += "/";
  opt_data_dir_fi += db_name;
  opt_file_dir_fi += "/";
  opt_file_dir_fi += db_name;

  sprintf(buffer, "FLUSH TABLES %s FOR EXPORT;", table_name.c_str());
  r = mysql_query(&mysql, buffer);
  if (r != 0)
    return false;

  MYSQL_RES *result = NULL;
  MYSQL_ROW row;
  sprintf(buffer, "show create table %s", table_name.c_str());
  if ((r = mysql_query(&mysql, buffer)) != 0)
    return false;
  if (!(result = mysql_store_result(&mysql)))
  {
    return false;
  }
  else
  {
    row = mysql_fetch_row(result);
    char *table_sql = strdup_root(&hash_mem_root, (char *) row[1]);
    char table_file[512];
    sprintf(table_file, "%s/%s.def", opt_file_dir_fi.c_str(), table_name.c_str());
    FILE *f = fopen(table_file, "w");
    fprintf(f, "%s", table_sql);
    fclose(f);
    mysql_free_result(result);
  }

  //文件拷贝
  //普通表的拷贝
  sprintf(buffer, "cp %s/%s.* %s/", opt_data_dir_fi.c_str(), table_name.c_str(), opt_file_dir_fi.c_str());

  r = system(buffer);
  if (r != 0)
    return false;

  //清除锁，否则在下一次的flush将会报告错误
  sprintf(buffer, "UNLOCK TABLES;");
  r = mysql_query(&mysql, buffer);
  if (r != 0)
    return false;

  return true;
}

bool
export_tables(string *err, const string db_name)
{
  FILE *fstream;
  list<char *>::iterator iter;
  string opt_file_dir_fi;
  string cmd = "";
  opt_file_dir_fi += opt_file_dir;
  opt_file_dir_fi += "/";
  opt_file_dir_fi += db_name;
  iter = tables.begin();

  if (!check_frm_files(opt_file_dir_fi, "", 0))
  {
    cmd += "mkdir  ";
    cmd += opt_file_dir_fi;
    fstream = NULL;
    fstream = popen(cmd.c_str(), "w");
    pclose(fstream);
  }
  string table_name;
  int exported_table_count = 0;
  while (iter != tables.end())
  {
    table_name = *iter;
    bool succ;
    succ = export_single_table(db_name, table_name);
    if (succ)
    {
      exported_table_count++;
      //cout<<"database "<<db_name<<" table "<<table_name<<" exported"<<endl;
    }
    else
    {
      cout << "\033[32m" << "database " << db_name << " table " << table_name << " exporting failed" << "\033[0m"
           << endl;
    }
    if (check_frm_files(opt_file_dir_fi, table_name, 1))
    {
      cmd = "";
      cmd += "rm -rf ";
      cmd += opt_file_dir_fi;
      cmd += "/" + table_name + ".frm";
      fstream = NULL;
      fstream = popen(cmd.c_str(), "w");
      pclose(fstream);
    }

    iter++;

  }
  cout << "database " << "\033[34m" << db_name << "\033[0m" << " totally " << "\033[34m" << exported_table_count
       << "\033[0m" << " tables exported" << endl;
  return true;
}

void
export_clean(const int is_slave_flag)
{
  int r = mysql_query(&mysql, "set global read_only=0;");
  if (r != 0)
  {
  }
  r = mysql_query(&mysql, "set global super_read_only=0;");
  if (r != 0)
  {
  }
  if (is_slave_flag)
  {
    r = mysql_query(&mysql, "start slave;");
    if (r !=0)
    {
      cout << "error!" <<endl;
    }
  }
}
bool
set_database(string *err, const string db_name)
{

  sprintf(buffer, "use %s", db_name.c_str());
  if (mysql_query(&mysql, buffer))
  {
    err->append("Error in setting database ");
    err->append(opt_db);
    err->append(".");
    return false;
  }
  return true;
}

bool
set_database(const string db_name)
{

  sprintf(buffer, "use %s", db_name.c_str());
  if (mysql_query(&mysql, buffer))
  {
    cout << "Error in setting database" << endl;
    return false;
  }
  return true;
}

bool
do_export(const bool slave_flag)
{
  string err;
  bool succ;
  list<char *>::iterator iter;
  succ = get_databases();
  if (!succ)
  {
    cout << "Cannot get databases from server or options, please check manully!" << endl;
    return false;
  }
  iter = databases.begin();
  string para_dbname;
  succ = export_flush_tables_with_read_lock(&err, slave_flag);
  if (!succ)
  {
    cout << " Flush tables with read lock error, please check manully!" << endl;
    return false;
  }
  succ = export_show_master_status(&err);
  if (!succ)
  {
    cout << " Show master status error, please check manully!" << endl;
    return false;
  }

  while (iter != databases.end())
  {
    para_dbname = *iter;
    succ = set_database(&err, para_dbname);
    if (!succ)
    {
      cout << err << endl;
      return false;
    }
    succ = get_tables();
    if (!succ)
    {
      cout << "failed when process exportting tables." << endl;
      return false;
    }
    succ = export_tables(&err, para_dbname);
    if (!succ)
    {
      cout << err << endl;
      return false;
    }
    iter++;
  }
  export_clean(slave_flag);

  return true;
}

bool
import_check(string *err)
{
  return true;
}

bool import_mk_db(const string &db_name)
{
  MYSQL_RES *result = NULL;
  string cmd = " show databases like '%";
  cmd += db_name;
  cmd += "%';";
  mysql_query(&mysql, cmd.c_str());
  result = mysql_store_result(&mysql);
  if ((result != NULL) && (result->row_count != 0))
  {
    cout << "\033[31m" << db_name << "  already exists!" << "\033[0m" << endl;
    mysql_free_result(result);
    return false;
  }
  while (!mysql_next_result(&mysql))
  {
    if (!(result = mysql_store_result(&mysql)))
      break;
    mysql_free_result(result);
  }
  cmd = "create database " + db_name + ";";
  int r = mysql_query(&mysql, cmd.c_str());
  if (r != 0)
    return false;
  return true;
}

table_buffer_t file_table_buffer;
//在给定的opt_file_dir中获取文件数，通过合tables变量的组合，确定倒入的表
list<char *> file_tables;

bool
import_get_file_tables(const string db_name, string *err)
{
  int table_count = 0;
  file_tables.clear();
  struct dirent *ptr;
  DIR *dir;
  string opt_file_dir_fi = opt_file_dir;
  opt_file_dir_fi += "/";
  opt_file_dir_fi += db_name;
  dir = opendir(opt_file_dir_fi.c_str());
  while ((ptr = readdir(dir)) != NULL)
  {
    //跳过'.'和'..'两个目录
    if (ptr->d_name[0] == '.')
      continue;
    //printf("%s\n",ptr->d_name);
    if (strstr(ptr->d_name, ".def") != NULL)
    {
      int len = strlen(ptr->d_name);
      strncpy(file_table_buffer.tables[table_count].name, ptr->d_name, len - 4);
      table_buffer.number++;
      file_tables.push_back(file_table_buffer.tables[table_count].name);
      table_count++;
    }
  }
  closedir(dir);
  return true;
}

bool import_get_file_databases(string *err)
{
  database_buffer.number = 1;
  databases.clear();
  DIR *dir;
  char s[100] = {0};
  string tmp_filename;
  struct dirent *rent;
  if ((dir = opendir(opt_file_dir)) == NULL)
    return false;
  while ((rent = readdir(dir)))
  {
    strncpy(s, rent->d_name, strlen(rent->d_name));
    s[strlen(rent->d_name)] = 0;
    tmp_filename = s;
    if (s[0] == '.' || tmp_filename.find("master.info") != string::npos)
      continue;
    transform(tmp_filename.begin(), tmp_filename.end(), tmp_filename.begin(), ::tolower);
    strcpy(database_buffer.databases[database_buffer.number - 1].name, s);
    databases.push_back(database_buffer.databases[database_buffer.number - 1].name);
    database_buffer.number++;
  }
  if (opt_db != NULL)
  {
    list<char *>::iterator iter;
    iter = databases.begin();
    string opt_database_name = opt_db;
    string tmp_databases_name;
    while (iter != databases.end())
    {
      tmp_databases_name = *iter;
      if (tmp_databases_name.find(opt_database_name,0) != string::npos)
      {
        database_buffer.number = 1;
        strcpy(database_buffer.databases[database_buffer.number -1].name, opt_db);
        databases.clear();
        databases.push_back(database_buffer.databases[database_buffer.number -1].name);
        return true;
      }
      iter++;
    }
    cout << "\033[31m" << "Database " <<  opt_db << "'s backup files not found!" << "\033[0m" << endl;
    return false;
  }
  return true;
}
bool
import_single_table(const string &db_name, const char *table_name)
{
  list<char *>::iterator iter;
  iter = tables.begin();
  char *db_table_name = NULL;
  bool table_exist = false;
  string opt_data_dir_fi = opt_data_dir;
  string opt_file_dir_fi = opt_file_dir;

  while (iter != tables.end())
  {
    db_table_name = *iter;
    if (strcasecmp(table_name, db_table_name) == 0)
    {
      table_exist = true;
      break;
    }
    iter++;
  }
  bool do_it = false;
  if (table_exist)
  {
    //是否覆盖原有表
    do_it = true;
  }
  else
  {
    //未指定倒入表，所以是整库导入，导入当前的文件表
    //在数据库中创建该表
    opt_data_dir_fi += "/";
    opt_data_dir_fi += db_name;
    opt_file_dir_fi += "/";
    opt_file_dir_fi += db_name;
    int r = 0;
    sprintf(buffer, "%s/%s%s", opt_file_dir_fi.c_str(), table_name, ".def");
    FILE *f = fopen(buffer, "r");
    fread(buffer, 1, sizeof(buffer), f);
    fclose(f);
    r = mysql_query(&mysql, buffer);
    if (r != 0)
      return false;
    do_it = true;
  }
  if (do_it)
  {
    int r = 0;
    //锁定表，并生成.cfg文件
    sprintf(buffer, "ALTER TABLE %s DISCARD TABLESPACE;", table_name);
    r = mysql_query(&mysql, buffer);
    if (r != 0)
      return false;
    //文件拷贝
    //普通表/以及.def文件的拷贝
    sprintf(buffer, "cp %s/%s.* %s", opt_file_dir_fi.c_str(), table_name, opt_data_dir_fi.c_str());

    r = system(buffer);
    if (r != 0)
      return false;

    //修改文件的所有者
    if (opt_owner != NULL)
    {
      sprintf(buffer, "chown %s %s/%s.*", opt_owner, opt_data_dir_fi.c_str(), table_name);
      r = system(buffer);
      if (r != 0)
        return false;
    }

    //导入数据文件
    sprintf(buffer, "ALTER TABLE %s IMPORT TABLESPACE;", table_name);
    r = mysql_query(&mysql, buffer);
    if (r != 0)
      return false;
    //清除工具所用的.def文件
    sprintf(buffer, "rm -rf %s/%s.def", opt_data_dir_fi.c_str(), table_name);
    r = system(buffer);
    if (r != 0)
      return false;
    //清除工具所用的.def文件
    sprintf(buffer, "rm -rf %s/%s*.cfg", opt_data_dir_fi.c_str(), table_name);
    r = system(buffer);
    if (r != 0)
      return false;
  }

  return true;
}

bool
import_start_binlog_repl()
{
  //int r = 0;
  char repl_buffer[1032 * 32]={0};
  sprintf(repl_buffer, "%s/master.info", opt_file_dir);
  FILE *f = fopen(repl_buffer, "r");
  if (f == NULL)
    return false;
  fread(repl_buffer, 1, sizeof(repl_buffer), f);
  fclose(f);
  cout << "Master info in Replication:" << repl_buffer <<endl;
  //r = mysql_query(&mysql, buffer);
  //if (r != 0)
  //  return false;
  return true;
}

bool
import_tables(const string &db_name, string *err)
{
  bool succ;
  list<char *>::iterator iter;

  succ = set_database(db_name);
  if (!succ)
    return false;
  iter = file_tables.begin();
  char *table_name = NULL;
  while (iter != file_tables.end())
  {
    table_name = *iter;
    succ = import_single_table(db_name, table_name);
    if (!succ)
    {
      sprintf(buffer, "table %s importing failed.\n", table_name);
      break;
    }
    else
    {
      sprintf(buffer, "table %s imported.\n", table_name);
    }
    iter++;
  }
  if (!succ)
  {
    sprintf(buffer, "table %s failed importing.\n", table_name);
    err->append(buffer);
    return false;
  }

//  succ = import_start_binlog_repl();
//  if (!succ)
//  {
//    err->append("failed start master binlog replication.\n");
//  }
  return true;
}

bool
do_import(const int slave_flag)
{
  if (slave_flag == 1)
  {
    cout << "Can not execute on slave" << endl;
    return false;
  }
  string err;
  bool succ = true;
  list<char *>::iterator iter;
  succ = import_get_file_databases(&err);
  if (!succ)
    return false;

  iter = databases.begin();
  string para_dbname;
  while (iter != databases.end())
  {
    para_dbname = *iter;
    cout << "Do import for database " << "\033[34m" << para_dbname << "\033[0m" << endl;
    succ = import_mk_db(para_dbname);
    if (!succ)
      return false;
    succ = import_get_file_tables(para_dbname, &err);
    if (!succ)
      return false;
    succ = import_tables(para_dbname, &err);
    if (!succ)
      return false;
    iter++;
  }
  succ = import_start_binlog_repl();
  if (!succ)
  {
    cout << "failed start master binlog replication." << endl;
    return false;
  }
  return succ;
}

bool
args_check(string *err)
{
  //database must be setted
  //if (opt_db == NULL || strlen(opt_db) == 0) {
  //  err->append("MySQL database must be setted");
  //  return false;
  //}
  //to do:check opt_data_dir/opt_file_dir

  if (opt_owner == NULL)
  {
    opt_owner = default_user;
  }
  if (opt_data_dir == NULL || strlen(opt_data_dir) == 0)
  {
    err->append("datadir must be setted");
    return false;
  }
  if (opt_file_dir == NULL || strlen(opt_file_dir) == 0)
  {
    err->append("filedir must be setted");
    return false;
  }

  //bool succ = true;
  //if (op == OP_EXPORT) {
  //  succ = args_export_check(err);
  //  if (!succ)
  //    return false;
  //}
  //else {
  //}

  return true;
}

int main(int argc, char *argv[])
{
  init_alloc_root(PSI_NOT_INSTRUMENTED, &hash_mem_root, 65535, 0);

  if (get_options(argc, (char **) argv))
  {
    exit(1);
  }
  if (op == OP_INVALID)
  {
    cout << "Invalid operation input,Valid value is export or import" << endl;
    exit(1);
  }

  string err;

  bool succ = args_check(&err);
  if (!succ)
  {
    cout << err << endl;
    exit(1);
  }
  sql_connect();

  int is_slave = 1;
  is_slave = do_check_slave();
  if (is_slave == -1)
  {
    cout << "Executing SQL error!" << endl;
    exit(1);
  }
  if (strcasecmp(opt_op, "export") == 0)
  {
    succ = do_export(is_slave);
    if (!succ)
      exit(1);
  }
  else
  {
    succ = do_import(is_slave);
    if (!succ)
      exit(1);
  }

  return 0;
}
