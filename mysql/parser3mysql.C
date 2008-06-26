/** @file
	Parser MySQL driver.

	Copyright(c) 2001, 2003 ArtLebedev Group (http://www.artlebedev.com)

	Author: Alexandr Petrosian <paf@design.ru> (http://paf.design.ru)

	2001.07.30 using MySQL 3.23.22b

	2001.11.06 numrows on "HP-UX istok1 B.11.00 A 9000/869 448594332 two-user license"
		3.23.42 & 4.0.0.alfa never worked, both subst & .sl version returned 0
*/
static const char *RCSId="$Id: parser3mysql.C,v 1.33 2008/06/26 14:30:48 misha Exp $"; 

#include "config_includes.h"

#include "pa_sql_driver.h"

#define NO_CLIENT_LONG_LONG
#include "mysql.h"
#include "ltdl.h"

#define MAX_STRING 0x400
#define MAX_NUMBER 20

#if _MSC_VER
#	define snprintf _snprintf
#	define strcasecmp _stricmp
#endif

static char *lsplit(char *string, char delim) {
	if(string) {
		char *v=strchr(string, delim);
		if(v) {
			*v=0;
			return v+1;
		}
	}
	return 0;
}

static char *lsplit(char **string_ref, char delim) {
	char *result=*string_ref;
	char *next=lsplit(*string_ref, delim);
	*string_ref=next;
	return result;
}

static char* rsplit(char* string, char delim) {
	if(string) {
		char* v=strrchr(string, delim); 
		if(v) {
			*v=0;
			return v+1;
		}
	}
	return NULL;	
}

static void toupper_str(char *out, const char *in, size_t size) {
	while(size--)
		*out++=(char)toupper(*in++);
}

struct Connection {
	SQL_Driver_services* services;

	MYSQL* handle;
	const char* client_charset;
	bool autocommit;
	bool multi_statements;
};

 
/**
	MySQL server driver
*/
class MySQL_Driver : public SQL_Driver {
public:

	MySQL_Driver() : SQL_Driver() {}

	~MySQL_Driver() {
		if(mysql_server_end)
			mysql_server_end();
	}

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
				multi_statements=0	// allow more then one statement in one query
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

		Connection& connection=*(Connection *)services.malloc(sizeof(Connection));
		*connection_ref=&connection;
		connection.services=&services;
		connection.handle=mysql_init(NULL);
		connection.client_charset=0;	
		connection.autocommit=true;
		connection.multi_statements=false;

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
						} else if(strcasecmp(key, "multi_statements")==0) {
							if(atoi(value)!=0)
								connection.multi_statements=true;
						} else if(strcasecmp(key, "autocommit")==0){
							if(atoi(value)==0)
								connection.autocommit=false;
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
					(connection.multi_statements) ? CLIENT_MULTI_STATEMENTS : CLIENT_MULTI_RESULTS
				)
		){
			services._throw(mysql_error(connection.handle));
		}

		if(charset){
			char statement[MAX_STRING]="SET CHARACTER SET ";
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

	const char* quote(void *aconnection, const char *from, unsigned int length) {
		Connection& connection=*static_cast<Connection*>(aconnection);
		/*
			3.23.22b
			You must allocate the to buffer to be at least length*2+1 bytes long. 
			(In the worse case, each character may need to be encoded as using two bytes, 
			and you need room for the terminating null byte.)
		*/
		char *result=(char*)connection.services->malloc_atomic(length*2+1);
		mysql_escape_string(result, from, length);
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

		// transcode query from $request:charset to ?ClientCharset
		if(transcode_needed){
			size_t length=strlen(astatement);
			services.transcode(astatement, length,
				astatement, length,
				services.request_charset(),
				connection.client_charset);
		}

		const char *statement;
		if(offset || limit!=SQL_NO_LIMIT){
			size_t statement_size=strlen(astatement);
			char *statement_limited=(char *)services.malloc_atomic(
				statement_size+MAX_NUMBER*2+8/* LIMIT #,#*/+1);
			char *cur=statement_limited;
			memcpy(cur, astatement, statement_size); cur+=statement_size;
			cur+=sprintf(cur, " LIMIT ");
			if(offset)
				cur+=snprintf(cur, MAX_NUMBER+1, "%u,", offset);
			if(limit!=SQL_NO_LIMIT)
				cur+=snprintf(cur, MAX_NUMBER, "%u", limit);
			statement=statement_limited;
		} else
			statement=astatement;

		if(mysql_query(connection.handle, statement)) 
			_throw(connection, mysql_error(connection.handle));
		if(!(res=mysql_store_result(connection.handle)) && mysql_field_count(connection.handle)) 
			_throw(connection, mysql_error(connection.handle));
		if(!res) // empty result: insert|delete|update|...
			return;
		
		int column_count=mysql_num_fields(res);
		if(!column_count) // old client
			column_count=mysql_field_count(connection.handle);

		if(!column_count){
			mysql_free_result(res);
			services._throw("result contains no columns");
		}
		
		bool failed=false;
		SQL_Error sql_error;
#define CHECK(afailed) \
		if(afailed) { \
			failed=true; \
			goto cleanup; \
		}

		for(int i=0; i<column_count; i++){
			if(MYSQL_FIELD *field=mysql_fetch_field(res)){
				size_t length=strlen(field->name);
				char* strm=(char*)services.malloc_atomic(length+1);
				memcpy(strm, field->name, length+1);
				const char* str=strm;

				// transcode column name from ?ClientCharset to $request:charset 
				if(transcode_needed){
					services.transcode(str, length,
						str, length,
						connection.client_charset,
						services.request_charset());
				}
				
				CHECK(handlers.add_column(sql_error, str, length));
			} else {
				// seen some broken client, 
				// which reported "44" for column count of response to "select 2+2"
				column_count=i;
				break;
			}
		}

		CHECK(handlers.before_rows(sql_error));
		
		while(MYSQL_ROW mysql_row=mysql_fetch_row(res)){
			CHECK(handlers.add_row(sql_error));
			unsigned long *lengths=mysql_fetch_lengths(res);
			for(int i=0; i<column_count; i++){
				size_t length=(size_t)lengths[i];
				const char* str;
				if(length){
					char *strm=(char*)services.malloc_atomic(length+1);
					memcpy(strm, mysql_row[i], length);
					strm[length]=0;
					str=strm;

					// transcode cell value from ?ClientCharset to $request:charset
					if(transcode_needed)
						services.transcode(str, length,
							str, length,
							connection.client_charset,
							services.request_charset());
				} else
					str=0;
				CHECK(handlers.add_row_cell(sql_error, str, length));
			}
		}
cleanup:
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
	
	typedef MYSQL_FIELD*	(STDCALL *t_mysql_fetch_field)(MYSQL_RES *result); t_mysql_fetch_field mysql_fetch_field;
	
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
		DLINK(mysql_fetch_field);
		SLINK(mysql_num_fields);
		SLINK(mysql_field_count);
		return 0;
	}

};

extern "C" SQL_Driver *SQL_DRIVER_CREATE() {
	static MySQL_Driver Driver;
	return &Driver;
}
