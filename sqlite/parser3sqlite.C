/** @file
	Parser SQLite driver.

	(c) Dmitry "Creator" Bobrik, 2004
*/

#include "config_includes.h"

#include "pa_sql_driver.h"

volatile const char * IDENT_PARSER3SQLITE_C="$Id: parser3sqlite.C,v 1.19 2021/02/01 19:27:32 moko Exp $" IDENT_PA_SQL_DRIVER_H;

#define NO_CLIENT_LONG_LONG
#include "sqlite3.h"
#include "ltdl.h"

#define MAX_STRING 0x400
#define MAX_NUMBER 20

#define SQLITE_DEFAULT_CHARSET "UTF-8"
#define PA_REGEXP

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

static void toupper_str(char *out, const char *in, size_t size) {
	while(size--)
		*out++=(char)toupper(*in++);
}

struct Connection {
	SQL_Driver_services* services;

	sqlite3* handle;
	const char* client_charset;
	bool multi_statements;
	bool autocommit;
	int busy_timeout;
};

// sqlite client library funcs

typedef int (*t_sqlite3_open)(const char *filename, sqlite3 **ppDb); t_sqlite3_open pa_sqlite3_open;

typedef int (*t_sqlite3_close)(sqlite3 *); t_sqlite3_close pa_sqlite3_close;

typedef int (*t_sqlite3_busy_timeout)(sqlite3*, int ms); t_sqlite3_busy_timeout pa_sqlite3_busy_timeout;

typedef int (*t_sqlite3_exec)(sqlite3*, const char *sql, sqlite3_callback, void *, char **errmsg); t_sqlite3_exec pa_sqlite3_exec;

typedef void (*t_sqlite3_free)(char *z); t_sqlite3_free pa_sqlite3_free;

typedef const char *(* t_sqlite3_errmsg)(sqlite3*); t_sqlite3_errmsg pa_sqlite3_errmsg;

typedef int (* t_sqlite3_prepare)(sqlite3 *db, const char *zSql, int nBytes, sqlite3_stmt **ppStmt, const char **pzTail); t_sqlite3_prepare pa_sqlite3_prepare;

typedef int (* t_sqlite3_column_count)(sqlite3_stmt *pStmt); t_sqlite3_column_count pa_sqlite3_column_count;

typedef int (* t_sqlite3_finalize)(sqlite3_stmt *pStmt); t_sqlite3_finalize pa_sqlite3_finalize;

typedef const char *(* t_sqlite3_column_name)(sqlite3_stmt*,int); t_sqlite3_column_name pa_sqlite3_column_name;

typedef int (* t_sqlite3_step)(sqlite3_stmt*); t_sqlite3_step pa_sqlite3_step;

typedef int (* t_sqlite3_column_type)(sqlite3_stmt*, int iCol); t_sqlite3_column_type pa_sqlite3_column_type;

typedef const unsigned char *(* t_sqlite3_column_text)(sqlite3_stmt*, int iCol); t_sqlite3_column_text pa_sqlite3_column_text;

typedef const unsigned char *(* t_sqlite3_column_blob)(sqlite3_stmt*, int iCol); t_sqlite3_column_blob pa_sqlite3_column_blob;

typedef int (* t_sqlite3_column_bytes)(sqlite3_stmt*, int iCol); t_sqlite3_column_bytes pa_sqlite3_column_bytes;

#ifdef PA_REGEXP
typedef int (* t_sqlite3_create_function)(sqlite3 *, const char *, int, int, void *, void (*)(sqlite3_context*,int,sqlite3_value**), void *, void *);  t_sqlite3_create_function pa_sqlite3_create_function;

typedef const unsigned char *(* t_sqlite3_value_text)(sqlite3_value*); static t_sqlite3_value_text pa_sqlite3_value_text;

typedef void *(* t_sqlite3_get_auxdata)(sqlite3_context*, int N); static t_sqlite3_get_auxdata pa_sqlite3_get_auxdata;

typedef void (* t_sqlite3_set_auxdata)(sqlite3_context*, int N, void*, void (*)(void*)); static t_sqlite3_set_auxdata pa_sqlite3_set_auxdata;

typedef void (* t_sqlite3_result_error)(sqlite3_context*, const char*, int); static t_sqlite3_result_error pa_sqlite3_result_error;

typedef void (* t_sqlite3_result_error_nomem)(sqlite3_context*); static t_sqlite3_result_error_nomem pa_sqlite3_result_error_nomem;

typedef void (* t_sqlite3_result_int)(sqlite3_context*, int); static t_sqlite3_result_int pa_sqlite3_result_int;


// "regexp.h" hardcoded
struct ReCompiled;
const char *pa_re_compile(ReCompiled **ppRe, const char *zIn, int noCase);
void pa_re_free(ReCompiled *pRe);
int pa_re_match(ReCompiled *pRe, const unsigned char *zIn, int nIn);

// regexp(pattern, string) sqlite3 function implementation
static void regexp_sql_func(sqlite3_context *context, int argc, sqlite3_value **argv){
	ReCompiled *pRe = (ReCompiled *)pa_sqlite3_get_auxdata(context, 0);
	int setAux = 0; /* True to invoke sqlite3_set_auxdata() */

	if( pRe==0 ){
		const char *zPattern = (const char*)pa_sqlite3_value_text(argv[0]);
		if( zPattern==0 ) return;
		const char *zErr = pa_re_compile(&pRe, zPattern, 0);
		if( zErr ){
			pa_re_free(pRe);
			pa_sqlite3_result_error(context, zErr, -1);
			return;
		}
		if( pRe==0 ){
			pa_sqlite3_result_error_nomem(context);
			return;
		}
		setAux = 1;
	}
	const unsigned char *zStr = (const unsigned char*)pa_sqlite3_value_text(argv[1]);
	if( zStr!=0 ){
		pa_sqlite3_result_int(context, pa_re_match(pRe, zStr, -1));
	}
	if( setAux ){
		pa_sqlite3_set_auxdata(context, 0, pRe, (void(*)(void*))pa_re_free);
	}
}
#endif


/**
	SQLite server driver
*/
class SQLite_Driver : public SQL_Driver {
public:

	SQLite_Driver() : SQL_Driver() {
	}

	/// get api version
	int api_version() { return SQL_DRIVER_API_VERSION; }

	/// initialize driver by loading sql dynamic link library
	const char *initialize(char *dlopen_file_spec) {
		return dlopen_file_spec ? dlink(dlopen_file_spec) : "client library column is empty";
	}

	/**	connect
		@param url
			format: @b db-file|:memory:|temporary:?
			autocommit=1&			// =0 disable autocommit. in this case 1 connect == 1 transaction.
									//	or you can use begin/commit|rollback explicitly
			multi_statements=0&		// =1 allow many statements in 1 query
			ClientCharset=UTF-8		// will transcode to/from specified charset instead of UTF-8 (default for sqlite)
	*/
	void connect(
				char *url, 
				SQL_Driver_services& services, 
				void **connection_ref ///< output: Connection*
		){

		Connection& connection=*(Connection *)services.malloc(sizeof(Connection));
		*connection_ref=&connection;
		connection.services=&services;

		connection.client_charset=SQLITE_DEFAULT_CHARSET;
		connection.multi_statements=false;
		connection.autocommit=true;
		connection.busy_timeout=4000;

		char* db_path=0;
		char* db=url;
		char* options=lsplit(db, '?');

		if(strcmp(db, ":memory:")==0){ // in-memory temporary DB
			db_path=db;
		} else if(strcmp(db, ":temporary:")==0){ // on-disk temporary DB
			// do nothing: empty path mean temporary table on disk
		} else {
			char* document_root=(char*)services.request_document_root();
			if(!document_root) // path to DB-file which was specified by user is path from document_root as anywhere in parser
				services._throw("document_root is empty");

			db_path=(char*)services.malloc_atomic(strlen(document_root)+1+strlen(db)+1);
			strcpy(db_path, document_root);
			strcat(db_path, "/");
			strcat(db_path, db);
		}

		//services._throw(db_path);

		while(options){
			if(char* key=lsplit(&options, '&')){
				if(*key) {
					if(char* value=lsplit(key, '=')){
						if(strcasecmp(key, "multi_statements")==0){
							if(atoi(value)!=0)
								connection.multi_statements=true;
						} else if(strcasecmp(key, "busy_timeout")==0){
							connection.busy_timeout=atoi(value);
						} else if(strcasecmp(key, "autocommit")==0){
							if(atoi(value)==0)
								connection.autocommit=false;
						} else if(strcmp(key, "ClientCharset")==0){
							toupper_str(value, value, strlen(value));
							connection.client_charset=value;
						} else
							services._throw("unknown connect option" /*key*/);
					} else 
						services._throw("connect option without =value" /*key*/);
				}
			}
		}

		// transcode database_name from $request:charset to UTF-8
		if(db_path && _transcode_required(connection, SQLITE_DEFAULT_CHARSET)){
			size_t length=strlen(db_path);
			services.transcode((const char*)db_path, length,
				(const char*&)db_path, length,
				services.request_charset(),
				SQLITE_DEFAULT_CHARSET);

		}
		

		int rc=pa_sqlite3_open(db_path, &connection.handle);
#ifdef PA_REGEXP
		if(rc==SQLITE_OK)
			rc=pa_sqlite3_create_function(connection.handle, "regexp", 2, SQLITE_UTF8, 0, regexp_sql_func, 0, 0);
#endif
		if(rc!=SQLITE_OK){
			const char* error_msg=pa_sqlite3_errmsg(connection.handle);
			pa_sqlite3_close(connection.handle);
			_throw(connection, error_msg);
		}
		
		pa_sqlite3_busy_timeout(connection.handle, connection.busy_timeout);
		
		_begin_transaction(connection);
	}

	void disconnect(void *aconnection){
		Connection& connection=*static_cast<Connection*>(aconnection);
		pa_sqlite3_close(connection.handle);
		connection.handle=0;
	}

	void commit(void *aconnection){
		Connection& connection=*static_cast<Connection*>(aconnection);
		if(!connection.autocommit)
			_execute_cmd(connection, "COMMIT");

		_begin_transaction(connection);
	}

	void rollback(void *aconnection){
		Connection& connection=*static_cast<Connection*>(aconnection);
		if(!connection.autocommit)
			_execute_cmd(connection, "ROLLBACK");

		_begin_transaction(connection);
	}

	bool ping(void *aconnection){
		return true; // not needed
	}

	// charset here is services.request_charset(), not connection.client_charset
	// thus we can't use the sql server quoting support
	const char* quote(void *aconnection, const char *str, unsigned int length) 
	{
		const char* from;
		const char* from_end=str+length;

		size_t quoted=0;

		for(from=str; from<from_end; from++){
			if(*from=='\'')
				quoted++;
		}

		if(!quoted)
			return str;

		Connection& connection=*static_cast<Connection*>(aconnection);
		char *result=(char*)connection.services->malloc_atomic(length + quoted + 1);
		char *to = result;

		for(from=str; from<from_end; from++){
			if(*from=='\'')
				*to++= '\''; // ' -> ''
			*to++=*from;
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

		if(placeholders_count>0)
			services._throw("bind variables not supported yet");
		
		const char* request_charset=services.request_charset();
		const char* client_charset=connection.client_charset;
		bool transcode_needed=_transcode_required(connection);

		// transcode query from $request:charset to ?ClientCharset
		if(transcode_needed){
			size_t length=strlen(astatement);
			services.transcode(astatement, length,
				astatement, length,
				request_charset,
				client_charset);
		}
		
		const char *statement;
		if(offset || limit!=SQL_NO_LIMIT){
			size_t statement_size=strlen(astatement);
			char *statement_limited=(char *)services.malloc_atomic(
				statement_size+MAX_NUMBER*2+8/* LIMIT #,#*/+1);
			char *cur=statement_limited;
			memcpy(cur, astatement, statement_size);
			cur+=statement_size;
			cur+=sprintf(cur, " LIMIT ");
			if(offset)
				cur+=snprintf(cur, MAX_NUMBER+1, "%lu,", offset);
			if(limit!=SQL_NO_LIMIT)
				cur+=snprintf(cur, MAX_NUMBER, "%lu", limit);
			statement=statement_limited;
		} else
			statement=astatement;


		const char *pzTail;
		int next_statement_length=0;
		sqlite3_stmt *SQL;
		int rc;
		SQL_Error sql_error;
		bool failed=false;

		do{ // cycling through SQL commands
			rc=pa_sqlite3_prepare(connection.handle, statement, -1, &SQL, &pzTail);
			next_statement_length=strlen(pzTail);
			if(rc!=SQLITE_OK){
				//pa_sqlite3_free((char*)pzTail);
				_throw(connection, pa_sqlite3_errmsg(connection.handle));
			}
			if(!connection.multi_statements && next_statement_length>0){ // multi statements was not allowed but pzTail point to not empty one
				//pa_sqlite3_free((char*)pzTail);
				_throw(connection, "multi statements are not allowed until option ?multi_statements=1 in connect string is specified.");
			}
			
			#define CHECK(afailed) if(afailed){ failed=true; goto cleanup; }

			int column_count=pa_sqlite3_column_count(SQL);

			if(!column_count){ // empty result: insert|delete|update|...
				rc=pa_sqlite3_step(SQL);
			} else {
				for(int i=0; i<column_count; i++){
					const char *column_name=pa_sqlite3_column_name(SQL, i);
					size_t length=strlen(column_name);

					char* strm=(char*)services.malloc_atomic(length+1);
					memcpy(strm, column_name, length+1);
					const char* str=strm;
					// transcode column name from ?ClientCharset to $request:charset 
					if(transcode_needed){
						services.transcode(str, length,
							str, length,
							client_charset,
							request_charset);
					}
							
					CHECK(handlers.add_column(sql_error, (const char*)str, length));
				}
				CHECK(handlers.before_rows(sql_error));

				const char *str;
				size_t length=0;

				do{
					rc=pa_sqlite3_step(SQL);
					if(rc==SQLITE_ROW){ // new line!!

						CHECK(handlers.add_row(sql_error));

						for(int i=0; i<column_count; i++){
							// SQLite allow to get value of any type using pa_sqlite3_column_text function
							bool transcode_value=false;
							int column_type=pa_sqlite3_column_type(SQL, i);
							switch(column_type){
								case SQLITE_NULL:
									length=0;
									str=NULL;
									break;
								case SQLITE_BLOB:
									str=(const char*)pa_sqlite3_column_blob(SQL, i);
									length=(size_t)pa_sqlite3_column_bytes(SQL, i);
									break;
								case SQLITE_TEXT: // for text transcoding can be required
								default: // anything else?
									transcode_value=transcode_needed;
								case SQLITE_INTEGER:
								case SQLITE_FLOAT:
									str=(const char*)pa_sqlite3_column_text(SQL, i);
									length=(size_t)pa_sqlite3_column_bytes(SQL, i);
									break;
							}

							if(length){
								char* strm=(char*)services.malloc_atomic(length+1);
								memcpy(strm, str, length);
								strm[length]=0;
								str=strm;

								if(transcode_value){
									// transcode cell value from ?ClientCharset to $request:charset
									services.transcode(str, length,
										str, length,
										client_charset,
										request_charset);
								}
							} else
								str=0;
							
							CHECK(handlers.add_row_cell(sql_error, str, length));

						}
					}
				} while(rc==SQLITE_BUSY || rc==SQLITE_ROW);

			}

			if(rc==SQLITE_ERROR || rc==SQLITE_MISUSE){
				_throw(connection, pa_sqlite3_errmsg(connection.handle));
			}

	cleanup:
			pa_sqlite3_finalize(SQL);
			statement=pzTail;
		} while (next_statement_length>0);

		if(failed)
			services._throw(sql_error);
	}

private:
	void _begin_transaction(Connection& connection) {
		if(!connection.autocommit){
			_execute_cmd(connection, "BEGIN");
		}
	}

	void _execute_cmd(Connection& connection, const char* statement){
		char* zErr;
		int rc=pa_sqlite3_exec(connection.handle, statement, 0, 0, &zErr);
		if(rc!=SQLITE_OK){
			size_t length=strlen(zErr);
			char* err_msg=(char *)connection.services->malloc_atomic(length+1);
			memcpy(err_msg, zErr, length);

			pa_sqlite3_free(zErr);
			_throw(connection, err_msg);
		}

	}

	void _throw(Connection& connection, const char* aerr_msg){
		size_t length=strlen(aerr_msg);
		if(length && _transcode_required(connection)){
			// transcode server error message from ?ClientCharset to $request:charset 
			connection.services->transcode(aerr_msg, length,
				aerr_msg, length,
				connection.client_charset,
				connection.services->request_charset());
		}
		connection.services->_throw(aerr_msg);
	}

	bool _transcode_required(Connection& connection, const char* charset=0){
		return (strcmp(charset?charset:connection.client_charset, connection.services->request_charset())!=0);
	}


private: // sqlite client library funcs linking

	const char *dlink(const char *dlopen_file_spec) {
		if(lt_dlinit()){
			if(const char* result=lt_dlerror())
				return result;
			return "can not prepare to dynamic loading";
		}

		lt_dlhandle handle=lt_dlopen(dlopen_file_spec);

		if(!handle){
			if(const char* result=lt_dlerror())
				return result;
			return "can not open the dynamic link module";
		}

		#define DSLINK(name, action) \
			pa_##name=(t_##name)lt_dlsym(handle, #name); \
				if(!pa_##name) \
					action;

		#define DLINK(name) DSLINK(name, return "function " #name " was not found")
		#define SLINK(name) DSLINK(name, name=subst_##name)
		
		DLINK(sqlite3_open);
		DLINK(sqlite3_close);
		DLINK(sqlite3_busy_timeout);
		DLINK(sqlite3_exec);
		DLINK(sqlite3_free);
		DLINK(sqlite3_errmsg);
		DLINK(sqlite3_prepare);
		DLINK(sqlite3_column_count);
		DLINK(sqlite3_finalize);
		DLINK(sqlite3_column_name);
		DLINK(sqlite3_step);
		DLINK(sqlite3_column_type);
		DLINK(sqlite3_column_text);
		DLINK(sqlite3_column_blob);
		DLINK(sqlite3_column_bytes);
#ifdef PA_REGEXP
		DLINK(sqlite3_create_function);
		DLINK(sqlite3_value_text);
		DLINK(sqlite3_get_auxdata);
		DLINK(sqlite3_set_auxdata);
		DLINK(sqlite3_result_error);
		DLINK(sqlite3_result_error_nomem);
		DLINK(sqlite3_result_int);
#endif
		return 0;
	}

};

extern "C" SQL_Driver *SQL_DRIVER_CREATE() {
	return new SQLite_Driver();
}
