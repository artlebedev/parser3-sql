/** @file
	Parser MySQL driver.

	Copyright (c) 2001-2012 Art. Lebedev Studio (http://www.artlebedev.com)

	Author: Alexandr Petrosian <paf@design.ru> (http://paf.design.ru)

	2001-07-30 using MySQL 3.23.22b

	2001-11-06 numrows on "HP-UX istok1 B.11.00 A 9000/869 448594332 two-user license"
		3.23.42 & 4.0.0.alfa never worked, both subst & .sl version returned 0
*/

#include "config_includes.h"

#include "pa_sql_driver.h"

volatile const char * IDENT_PARSER3MYSQL_C="$Id: parser3mysql.C,v 1.42 2012/06/06 14:47:07 moko Exp $" IDENT_PA_SQL_DRIVER_H;

#define NO_CLIENT_LONG_LONG
#include "mysql.h"
#include "ltdl.h"

#define MAX_STRING 0x400
#define MAX_NUMBER 20

#if _MSC_VER
#	define snprintf _snprintf
#	define strcasecmp _stricmp
#endif

// for mysql < 4.1 
#if !defined(CLIENT_MULTI_RESULTS) || !defined(CLIENT_MULTI_STATEMENTS)
#	define OLD_MYSQL_CLIENT 1
#endif

#ifndef CLIENT_MULTI_RESULTS
#	define	CLIENT_MULTI_RESULTS (1UL << 17)
#endif

#ifndef CLIENT_MULTI_STATEMENTS
#	define	CLIENT_MULTI_STATEMENTS (1UL << 16)
#endif

static char *lsplit(char *string, char delim){
	if(string) {
		if(char *v=strchr(string, delim)){
			*v=0;
			return v+1;
		}
	}
	return 0;
}

static char *lsplit(char **string_ref, char delim){
	char *result=*string_ref;
	char *next=lsplit(*string_ref, delim);
	*string_ref=next;
	return result;
}

static char* rsplit(char* string, char delim){
	if(string){
		if(char* v=strrchr(string, delim)){
			*v=0;
			return v+1;
		}
	}
	return NULL;	
}

static void toupper_str(char *out, const char *in, size_t size){
	while(size--)
		*out++=(char)toupper(*in++);
}

inline static bool is_column_transcode_required(enum_field_types type) {
	switch(type) {
		case MYSQL_TYPE_NULL:

		case MYSQL_TYPE_DECIMAL:
		case MYSQL_TYPE_NEWDECIMAL:
		case MYSQL_TYPE_FLOAT:
		case MYSQL_TYPE_DOUBLE:

		case MYSQL_TYPE_TINY:
		case MYSQL_TYPE_SHORT:
		case MYSQL_TYPE_LONG:
		case MYSQL_TYPE_LONGLONG:
		case MYSQL_TYPE_INT24:
		case MYSQL_TYPE_BIT:

		case MYSQL_TYPE_DATE:
		case MYSQL_TYPE_NEWDATE:
		case MYSQL_TYPE_TIME:
		case MYSQL_TYPE_DATETIME:
		case MYSQL_TYPE_YEAR:
		case MYSQL_TYPE_TIMESTAMP:

		case MYSQL_TYPE_BLOB:
		case MYSQL_TYPE_TINY_BLOB:
		case MYSQL_TYPE_MEDIUM_BLOB:
		case MYSQL_TYPE_LONG_BLOB:
			return false;
			break;
	}
	return true;
}

inline static const char* strdup(SQL_Driver_services& services, char* str, size_t length) {
	char *strm=(char*)services.malloc_atomic(length+1);
	memcpy(strm, str, length);
	strm[length]=0;
	return (const char*)strm;
}

struct Connection {
	SQL_Driver_services* services;

	MYSQL* handle;
	const char* client_charset;
	bool autocommit;
};

 
/**
	MySQL server driver
*/
class MySQL_Driver : public SQL_Driver {
public:

	MySQL_Driver() : SQL_Driver() {}

#ifndef FREEBSD4
	~MySQL_Driver() {
		if(mysql_server_end)
			mysql_server_end();
	}
#endif

	/// get api version
	int api_version() { return SQL_DRIVER_API_VERSION; }

	/// initialize driver by loading sql dynamic link library
	const char *initialize(char *dlopen_file_spec) {
		return dlopen_file_spec?
			dlink(dlopen_file_spec):"client library column is empty";
	}

	/**	connect
		@param url
			format: @b user:pass@host[:port]|[/unix/socket]/database?
				charset=value&	// transcode by server with command 'SET CHARACTER SET value'
				ClientCharset=charset&	// transcode by parser
				timeout=3&
				compress=0&
				named_pipe=1&
				autocommit=1&
				multi_statements=0	// allows more then one statement in one query
				old_client=1		// simulates 3.xx client. not compatible with multi_statements option
			3.x, 4.0 
				only option for charset is cp1251_koi8.
			4.1+ accept not 'cp1251_koi8' but 'cp1251', 'utf8' and much more
				it is usable for transcoding using sql server
	*/
	void connect(
				char *url, 
				SQL_Driver_services& services, 
				void **connection_ref ///< output: Connection*
	){
		char *user=url;
		char *s=rsplit(user, '@');
		char *host=0;
		char *unix_socket=0;
		if(s && s[0]=='[') { // unix socket
			unix_socket=1+s;
			s=lsplit(unix_socket, ']');
		} else { // IP
			host=s;
		}
		char *db=lsplit(s, '/');
		char *pwd=lsplit(user, ':');
		char *error_pos=0;
		char *port_cstr=lsplit(host, ':');
		int port=port_cstr?strtol(port_cstr, &error_pos, 0):0;
		char *options=lsplit(db, '?');

		char *charset=0;

#ifdef OLD_MYSQL_CLIENT
		int client_flag=0;
#else
		int client_flag=CLIENT_MULTI_RESULTS;
#endif

		Connection& connection=*(Connection *)services.malloc(sizeof(Connection));
		*connection_ref=&connection;
		connection.services=&services;
		connection.handle=mysql_init(NULL);
		connection.client_charset=0;	
		connection.autocommit=true;

		while(options){
			if(char *key=lsplit(&options, '&')){
				if(*key){
					if(char *value=lsplit(key, '=')){
						if(strcmp(key, "ClientCharset")==0){ // transcoding with parser
							toupper_str(value, value, strlen(value));
							connection.client_charset=value;
						} else if(strcasecmp(key, "charset")==0){ // transcoding with server
							charset=value;
						} else if(strcasecmp(key, "timeout")==0){
							unsigned int timeout=(unsigned int)atoi(value);
							if(mysql_options(connection.handle, MYSQL_OPT_CONNECT_TIMEOUT, (const char *)&timeout)!=0)
								services._throw(mysql_error(connection.handle));
						} else if(strcasecmp(key, "compress")==0){
							if(atoi(value))
								if(mysql_options(connection.handle, MYSQL_OPT_COMPRESS, 0)!=0)
									services._throw(mysql_error(connection.handle));
						} else if(strcasecmp(key, "named_pipe")==0){
							if(atoi(value))
								if(mysql_options(connection.handle, MYSQL_OPT_NAMED_PIPE, 0)!=0)
									services._throw(mysql_error(connection.handle));
						} else if(strcasecmp(key, "local_infile")==0){
							if(atoi(value))
								if(mysql_options(connection.handle, MYSQL_OPT_LOCAL_INFILE, 0)!=0)
									services._throw(mysql_error(connection.handle));
						} else if(strcasecmp(key, "autocommit")==0){
							if(atoi(value)==0)
								connection.autocommit=false;
						} else if(strcasecmp(key, "multi_statements")==0){
#ifdef OLD_MYSQL_CLIENT
							services._throw("driver was built with old mysql includes so multi_statements option can't be used");
#else
							if(!(client_flag==CLIENT_MULTI_RESULTS || client_flag==CLIENT_MULTI_STATEMENTS))
								services._throw("you can't specify together options old_client and multi_statements");
							if(atoi(value)!=0)
								client_flag=CLIENT_MULTI_STATEMENTS;
#endif
						} else if(strcasecmp(key, "old_client")==0){
#ifdef OLD_MYSQL_CLIENT
							services._throw("driver was built with old mysql includes so old_client option can't be used");
#else
							if(!(client_flag==CLIENT_MULTI_RESULTS || client_flag==0))
								services._throw("you can't specify together options old_client and multi_statements");
							if(atoi(value)!=0)
								client_flag=0;
#endif
						} else
							services._throw("unknown connect option" /*key*/);
					} else 
						services._throw("connect option without =value" /*key*/);
				}
			}
		}

		if(!mysql_real_connect(
					connection.handle, 
					host, user, pwd, db,
					port?port:MYSQL_PORT,
					unix_socket,
					client_flag
				)
		){
			services._throw(mysql_error(connection.handle));
		}

		if(charset){
			char statement[MAX_STRING+1]="SET CHARACTER SET ";
			strncat(statement, charset, MAX_STRING);
			_exec(connection, statement);
		}

		if(!connection.autocommit)
			_exec(connection, "SET AUTOCOMMIT=0");
	}

	void disconnect(void *aconnection) {
		Connection& connection=*static_cast<Connection*>(aconnection);
		mysql_close(connection.handle);
		connection.handle=0;
	}

	void commit(void *aconnection) {
		Connection& connection=*static_cast<Connection*>(aconnection);
		if(!connection.autocommit)
			_exec(connection, "COMMIT");
	}

	void rollback(void *aconnection) {
		Connection& connection=*static_cast<Connection*>(aconnection);
		if(!connection.autocommit)
			_exec(connection, "ROLLBACK");
	}

	bool ping(void *aconnection) {
		Connection& connection=*static_cast<Connection*>(aconnection);
		return mysql_ping(connection.handle)==0;
	}

	// charset here is services.request_charset(), not connection.client_charset
	// thus we can't use the sql server quoting support
	const char* quote(void *aconnection, const char *str, unsigned int length) {
		const char* from;
		const char* from_end=str+length;

		size_t quoted=0;

		for(from=str; from<from_end; from++){
			switch (*from) {
			case 0:
			case '\n':
			case '\r':
			case '\032':
			case '\\':
			case '\'':
			case '"':
				quoted++;
			}
		}

		if(!quoted)
			return str;

		Connection& connection=*static_cast<Connection*>(aconnection);
		char *result=(char*)connection.services->malloc_atomic(length + quoted + 1);
		char *to = result;

		for(from=str; from<from_end; from++){
			char escape;
			switch (*from) {
			case 0: 
				escape= '0'; 
				break;
			case '\n': 
				escape= 'n'; 
				break;
			case '\r': 
				escape= 'r'; 
				break;
			case '\032': 
				escape= 'Z'; 
				break;
			case '\\': 
			case '\'': 
			case '"': 
				escape= *from; 
				break;
			default:
				*to++=*from;
				continue;
			}
			*to++= '\\';
			*to++= escape;
		}
		
		*to=0;
		return result;
	}

	void query(void *aconnection, 
				const char *astatement, 
				size_t placeholders_count, Placeholder* placeholders, 
				unsigned long offset, unsigned long limit,
				SQL_Driver_query_event_handlers& handlers
	){
		Connection& connection=*static_cast<Connection*>(aconnection);
		SQL_Driver_services& services=*connection.services;
		MYSQL_RES *res=NULL;

		if(placeholders_count>0)
			services._throw("bind variables not supported (yet)");

		bool transcode_needed=_transcode_required(connection);

		size_t statement_size=0;

		if(transcode_needed) {
			statement_size=strlen(astatement);
			// transcode query from $request:charset to ?ClientCharset
			services.transcode(astatement, statement_size,
				astatement, statement_size,
				services.request_charset(),
				connection.client_charset);
		}

		const char *statement;
		if(offset || limit!=SQL_NO_LIMIT) {
			if(!statement_size)
				statement_size=strlen(astatement);
			char *statement_limited=(char *)services.malloc_atomic(
				statement_size+MAX_NUMBER*2+8/* LIMIT #,#*/+1);
			char *cur=statement_limited;
			memcpy(cur, astatement, statement_size); cur+=statement_size;
			cur+=sprintf(cur, " LIMIT ");
			if(offset)
				cur+=snprintf(cur, MAX_NUMBER, "%lu,", offset);
			if(limit!=SQL_NO_LIMIT)
				cur+=snprintf(cur, MAX_NUMBER, "%lu", limit);
			statement=statement_limited;
		} else
			statement=astatement;

		if(mysql_query(connection.handle, statement)) 
			_throw(connection, mysql_error(connection.handle));
		if(!(res=mysql_store_result(connection.handle)) && mysql_field_count(connection.handle))
			_throw(connection, mysql_error(connection.handle));
		if(!res) // empty result: insert|delete|update|...
			return;

		size_t column_count=mysql_num_fields(res);
		if(!column_count) // old client
			column_count=mysql_field_count(connection.handle);

		if(!column_count){
			mysql_free_result(res);
			services._throw("result contains no columns");
		}

		bool failed=false;
		SQL_Error sql_error;

#define CHECK(afailed)    \
		if(afailed) {     \
			failed=true;  \
			goto cleanup; \
		}

#define DO_FETCH_FIELDS(transcode_column_name) {                                \
			MYSQL_FIELD *fields = mysql_fetch_fields(res);                      \
			for(size_t i=0; i<column_count; i++) {                              \
				size_t length=fields[i].name_length;                            \
				const char* str=strdup(services, fields[i].name, length);       \
				transcode_column_name                                           \
				CHECK(handlers.add_column(sql_error, str, length));             \
			}                                                                   \
		}

#define DO_FETCH_ROWS(transcode_cell_value) {                                   \
			while(MYSQL_ROW mysql_row=mysql_fetch_row(res)) {                   \
				CHECK(handlers.add_row(sql_error));                             \
				unsigned long *lengths=mysql_fetch_lengths(res);                \
				for(size_t i=0; i<column_count; i++) {                          \
					const char* str=0;                                          \
					size_t length=lengths[i];                                   \
					if(length) {                                                \
						str=strdup(services, mysql_row[i], length);             \
						transcode_cell_value                                    \
					}                                                           \
					CHECK(handlers.add_row_cell(sql_error, str, length));       \
				}                                                               \
			}                                                                   \
		}

		bool* transcode_column=0;
		if(transcode_needed) {
			transcode_column = new bool[column_count];
			DO_FETCH_FIELDS(
				transcode_column[i] = is_column_transcode_required(fields[i].type);
				// transcode column's name from ?ClientCharset to $request:charset
				services.transcode(str, length,
					str, length,
					connection.client_charset,
					services.request_charset());
			)
			CHECK(handlers.before_rows(sql_error));
			DO_FETCH_ROWS(
				if(transcode_column[i])
					// transcode cell's value from ?ClientCharset to $request:charset
					services.transcode(str, length,
						str, length,
						connection.client_charset,
						services.request_charset());
			)
		} else {
			// without transcoding
			DO_FETCH_FIELDS()
			CHECK(handlers.before_rows(sql_error));
			DO_FETCH_ROWS()
		}
cleanup:
		if(transcode_column)
			delete transcode_column;
		mysql_free_result(res);
		if(failed)
			services._throw(sql_error);
	}

private:
	void _exec(Connection& connection, const char* statement) {
		if(mysql_query(connection.handle, statement)) 
			_throw(connection, mysql_error(connection.handle));
		(*mysql_store_result)(connection.handle); // throw out the result [don't need but must call]
	}

	void _throw(Connection& connection, const char* aerr_msg) {
		size_t length=strlen(aerr_msg);
		if(length && _transcode_required(connection)) {
			connection.services->transcode(aerr_msg, length,
				aerr_msg, length,
				connection.client_charset,
				connection.services->request_charset());
		}
		connection.services->_throw(aerr_msg);
	}

	bool _transcode_required(Connection& connection){
		return (connection.client_charset && strcmp(connection.client_charset, connection.services->request_charset())!=0);
	}

private: // mysql client library funcs

	typedef MYSQL*	(STDCALL *t_mysql_init)(MYSQL *); 	t_mysql_init mysql_init;
	
	typedef void	(STDCALL *t_mysql_server_end)(); 	t_mysql_server_end mysql_server_end;

	typedef int		(STDCALL *t_mysql_options)(MYSQL *mysql, enum mysql_option option, const char *arg); t_mysql_options mysql_options;
	
	typedef MYSQL_RES* (STDCALL *t_mysql_store_result)(MYSQL *); t_mysql_store_result mysql_store_result;
	
	typedef int		(STDCALL *t_mysql_query)(MYSQL *, const char *q); t_mysql_query mysql_query;
	
	typedef char*	(STDCALL *t_mysql_error)(MYSQL *); t_mysql_error mysql_error;
	static char* STDCALL subst_mysql_error(MYSQL *mysql) { return (mysql)->net.last_error; }

	typedef MYSQL*	(STDCALL *t_mysql_real_connect)(MYSQL *, const char *host,
						const char *user,
						const char *passwd,
						const char *db,
						unsigned int port,
						const char *unix_socket,
						unsigned int clientflag); t_mysql_real_connect mysql_real_connect;

	typedef void	(STDCALL *t_mysql_close)(MYSQL *); t_mysql_close mysql_close;
	
	typedef int		(STDCALL *t_mysql_ping)(MYSQL *); t_mysql_ping mysql_ping;
	
	typedef unsigned long	(STDCALL *t_mysql_escape_string)(char *to,const char *from,
						unsigned long from_length); t_mysql_escape_string mysql_escape_string;
	
	typedef void	(STDCALL *t_mysql_free_result)(MYSQL_RES *result); t_mysql_free_result mysql_free_result;
	
	typedef unsigned long* (STDCALL *t_mysql_fetch_lengths)(MYSQL_RES *result); t_mysql_fetch_lengths mysql_fetch_lengths;
	
	typedef MYSQL_ROW	(STDCALL *t_mysql_fetch_row)(MYSQL_RES *result); t_mysql_fetch_row mysql_fetch_row;
	
	typedef MYSQL_FIELD*	(STDCALL *t_mysql_fetch_fields)(MYSQL_RES *result); t_mysql_fetch_fields mysql_fetch_fields;
	
	typedef unsigned int	(STDCALL *t_mysql_num_fields)(MYSQL_RES *); t_mysql_num_fields mysql_num_fields;
	typedef unsigned int	(STDCALL *t_mysql_field_count)(MYSQL *); t_mysql_field_count mysql_field_count;

	static unsigned int	STDCALL subst_mysql_num_fields(MYSQL_RES *res) { return res->field_count; }
	static unsigned int	STDCALL subst_mysql_field_count(MYSQL *mysql) { return mysql->field_count; }

private: // mysql client library funcs linking

	const char *dlink(const char *dlopen_file_spec) {
		if(lt_dlinit())
			return lt_dlerror();

		lt_dlhandle handle=lt_dlopen(dlopen_file_spec);

		if(!handle){
			if(const char* result=lt_dlerror())
				return result;
			return "can not open the dynamic link module";
		}

		#define GLINK(name) \
			name=(t_##name)lt_dlsym(handle, #name);

		#define DSLINK(name, action) \
			GLINK(name) \
				if(!name) \
					action;

		#define DLINK(name) DSLINK(name, return "function " #name " was not found")
		#define SLINK(name) DSLINK(name, name=subst_##name)
		
		DLINK(mysql_init);
		GLINK(mysql_server_end);
		DLINK(mysql_options);
		DLINK(mysql_store_result);
		DLINK(mysql_query);
		SLINK(mysql_error);
		DLINK(mysql_real_connect);
		DLINK(mysql_close);
		DLINK(mysql_ping);
		DLINK(mysql_escape_string);
		DLINK(mysql_free_result);
		DLINK(mysql_fetch_lengths);
		DLINK(mysql_fetch_row);
		DLINK(mysql_fetch_fields);
		SLINK(mysql_num_fields);
		SLINK(mysql_field_count);
		return 0;
	}

};

extern "C" SQL_Driver *SQL_DRIVER_CREATE() {
	static MySQL_Driver Driver;
	return &Driver;
}
