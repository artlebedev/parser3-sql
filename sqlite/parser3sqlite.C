/** @file
	Parser SQLite driver.

	(c) Dmitry "Creator" Bobrik, 2004
*/
//static const char *RCSId="$Id: parser3sqlite.C,v 1.9 2008/07/03 07:17:25 misha Exp $"; 

#include "config_includes.h"

#include "pa_sql_driver.h"
//#include "windows.h"  // for messagebox

#define NO_CLIENT_LONG_LONG
#include "sqlite3.h"
#include "ltdl.h"

#define MAX_COLS   500
#define MAX_STRING 0x400
#define MAX_NUMBER 20

#define SQLITE_DEFAULT_CHARSET "UTF-8"

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
};



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
		return dlopen_file_spec?
			dlink(dlopen_file_spec):"client library column is empty";
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
						} else if(strcasecmp(key, "autocommit")==0){
							if(atoi(value)==0)
								connection.autocommit=false;
							continue;
						} else if(strcmp(key, "ClientCharset")==0){
							toupper_str(value, value, strlen(value));
							connection.client_charset=value;
							continue;
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
		

		int rc=sqlite3_open(db_path, &connection.handle);
		if(rc!=SQLITE_OK){
			const char* error_msg=sqlite3_errmsg(connection.handle);
			sqlite3_close(connection.handle);
			_throw(connection, error_msg);
		}
		
		_begin_transaction(connection);
	}

	void disconnect(void *aconnection){
		Connection& connection=*static_cast<Connection*>(aconnection);
		sqlite3_close(connection.handle);
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

	const char* quote(void *aconnection, const char *from, unsigned int length){
		Connection& connection=*static_cast<Connection*>(aconnection);
		/*
			You must allocate the to buffer to be at least length*2+1 bytes long. 
			In the worse case, each character may need to be encoded as using two bytes, 
			and you need room for the terminating null byte.
		*/
		char *result=(char*)connection.services->malloc_atomic(length*2+1);
		char *to=result;
		while(length--) {
			if(*from=='\'') { // ' -> ''
				*to++='\'';
			} else if(*from=='\"') { // " -> ""
				*to++='\"';
			}
			*to++=*from++;
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
				cur+=snprintf(cur, MAX_NUMBER+1, "%u,", offset);
			if(limit!=SQL_NO_LIMIT)
				cur+=snprintf(cur, MAX_NUMBER, "%u", limit);
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
			rc=sqlite3_prepare(connection.handle, statement, -1, &SQL, &pzTail);
			next_statement_length=strlen(pzTail);
			if(rc!=SQLITE_OK){
				//sqlite3_free((char*)pzTail);
				_throw(connection, sqlite3_errmsg(connection.handle));
			}
			if(!connection.multi_statements && next_statement_length>0){ // multi statements was not allowed but pzTail point to not empty one
				//sqlite3_free((char*)pzTail);
				_throw(connection, "multi statements are not allowed until opption ?multi_statements=1 in connect string is specified.");
			}
			
			#define CHECK(afailed) if(afailed){ failed=true; goto cleanup; }

			int column_count=sqlite3_column_count(SQL);

			if(!column_count){ // empty result: insert|delete|update|...
				rc=sqlite3_step(SQL);
			} else {
				if(column_count>MAX_COLS)
					column_count=MAX_COLS;

				int column_types[MAX_COLS];
				bool transcode_column[MAX_COLS];

				for(int i=0; i<column_count; i++){
					const char *column_name=sqlite3_column_name(SQL, i);
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
				bool first_row=true;

				do{
					rc=sqlite3_step(SQL);
					if(rc==SQLITE_ROW){ // new line!!

						CHECK(handlers.add_row(sql_error));

						for(int i=0; i<column_count; i++){
							if(first_row){
								column_types[i]=sqlite3_column_type(SQL, i);
								switch(column_types[i]){
									case SQLITE_INTEGER:
									case SQLITE_FLOAT:
									case SQLITE_NULL:
										transcode_column[i]=false;
										break;
									default:
										transcode_column[i]=transcode_needed;
										break;
								}
							}

							// SQLite allow to get value of any type using sqlite3_column_text function
							switch(column_types[i]){
								case SQLITE_NULL:
									length=0;
									str=NULL;
									break;
								case SQLITE_BLOB:
									str=(const char*)sqlite3_column_blob(SQL, i);
									length=(size_t)sqlite3_column_bytes(SQL, i);
									break;
								default: // anything else?
									str=(const char*)sqlite3_column_text(SQL, i);
									length=(size_t)sqlite3_column_bytes(SQL, i);
									break;
							}

							if(length){
								char* strm=(char*)services.malloc_atomic(length+1);
								memcpy(strm, str, length+1);
								str=strm;

								if(transcode_column[i]){
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
						first_row=false;
					}
				} while(rc==SQLITE_BUSY || rc==SQLITE_ROW);

			}

			if(rc==SQLITE_ERROR || rc==SQLITE_MISUSE){
				_throw(connection, sqlite3_errmsg(connection.handle));
			}

	cleanup:
			sqlite3_finalize(SQL);
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
		int rc=sqlite3_exec(connection.handle, statement, 0, 0, &zErr);
		if(rc!=SQLITE_OK){
			size_t length=strlen(zErr);
			char* err_msg=(char *)connection.services->malloc_atomic(length+1);
			memcpy(err_msg, zErr, length);

			sqlite3_free(zErr);
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


private: // sqlite client library funcs

	typedef int (*t_sqlite3_open)(const char *filename, sqlite3 **ppDb); t_sqlite3_open sqlite3_open;

	typedef int (*t_sqlite3_close)(sqlite3 *); t_sqlite3_close sqlite3_close;

	typedef int (*t_sqlite3_exec)(sqlite3*, const char *sql, sqlite3_callback, void *, char **errmsg); t_sqlite3_exec sqlite3_exec;

	typedef void (*t_sqlite3_free)(char *z); t_sqlite3_free sqlite3_free;

	typedef const char *(* t_sqlite3_errmsg)(sqlite3*); t_sqlite3_errmsg sqlite3_errmsg;

	typedef int (* t_sqlite3_prepare)(sqlite3 *db, const char *zSql, int nBytes, sqlite3_stmt **ppStmt, const char **pzTail); t_sqlite3_prepare sqlite3_prepare;

	typedef int (* t_sqlite3_column_count)(sqlite3_stmt *pStmt); t_sqlite3_column_count sqlite3_column_count;

	typedef int (* t_sqlite3_finalize)(sqlite3_stmt *pStmt); t_sqlite3_finalize sqlite3_finalize;

	typedef const char *(* t_sqlite3_column_name)(sqlite3_stmt*,int); t_sqlite3_column_name sqlite3_column_name;

	typedef int (* t_sqlite3_step)(sqlite3_stmt*); t_sqlite3_step sqlite3_step;

	typedef int (* t_sqlite3_column_type)(sqlite3_stmt*, int iCol); t_sqlite3_column_type sqlite3_column_type;

	typedef const unsigned char *(* t_sqlite3_column_text)(sqlite3_stmt*, int iCol); t_sqlite3_column_text sqlite3_column_text;

	typedef const unsigned char *(* t_sqlite3_column_blob)(sqlite3_stmt*, int iCol); t_sqlite3_column_blob sqlite3_column_blob;

	typedef int (* t_sqlite3_column_bytes)(sqlite3_stmt*, int iCol); t_sqlite3_column_bytes sqlite3_column_bytes;


private: // sqlite client library funcs linking

	const char *dlink(const char *dlopen_file_spec) {
		if(lt_dlinit())
			return lt_dlerror();
        lt_dlhandle handle=lt_dlopen(dlopen_file_spec);
        if (!handle) {
            if(const char* result=lt_dlerror())
            	return result;

			return "can not open the dynamic link module";
		}

		#define DSLINK(name, action) \
			name=(t_##name)lt_dlsym(handle, #name); \
				if(!name) \
					action;

		#define DLINK(name) DSLINK(name, return "function " #name " was not found")
		#define SLINK(name) DSLINK(name, name=subst_##name)
		
		DLINK(sqlite3_open);
		DLINK(sqlite3_close);
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
		return 0;
	}

};

extern "C" SQL_Driver *SQL_DRIVER_CREATE() {
	return new SQLite_Driver();
}
