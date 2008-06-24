/** @file
	Parser SQLite driver.

	(c) Dmitry "Creator" Bobrik, 2004
*/
//static const char *RCSId="$Id: parser3sqlite.C,v 1.4 2008/06/24 17:46:57 misha Exp $"; 

#include "config_includes.h"

#include "pa_sql_driver.h"
//#include "windows.h"  // for messagebox

#define NO_CLIENT_LONG_LONG
#include "sqlite3.h"
#include "ltdl.h"

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
	const char* cstrClientCharset;
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
			format: @b [localhost/]dbfile?
			ClientCharset=UTF-8&
			autocommit=1
	*/
	void connect(
			char *url, 
			SQL_Driver_services& services, 
			void **connection_ref ///< output: Connection*
		){

		int rc;

		Connection& connection=*(Connection *)services.malloc(sizeof(Connection));
		connection.services=&services;
		connection.cstrClientCharset=SQLITE_DEFAULT_CHARSET;	
		connection.autocommit=true;

		char *db = url;
		char *options = lsplit(db, '?');

		char *db_path=(char*)services.malloc(strlen((char*)services.request_document_root()) + strlen(db) + 2); 
		db_path=strncat(db_path, (char*)services.request_document_root(), MAX_STRING);
		db_path+="/";
		db_path=strncat(db_path, db, MAX_STRING);

		while(options) {
			if(char *key=lsplit(&options, '&')) {
				if(*key) {
					if(char *value=lsplit(key, '=')) {
						if(strcmp(key, "ClientCharset" )==0) { // transcoding with parser
							toupper_str(value, value, strlen(value));
							connection.cstrClientCharset=value;
							continue;
						} else if(strcasecmp(key, "autocommit")==0) {
							if(atoi(value)==0)
								connection.autocommit=false;
							continue;
						} else
							services._throw("unknown connect option" /*key*/);
					} else 
						services._throw("connect option without =value" /*key*/);
				}
			}
		}

		// transcode database_name from $request:charset to UTF-8
		size_t transcoded_db_path_size;
		const char* sdb = db_path;
		services.transcode(sdb, strlen(db_path),
			sdb, transcoded_db_path_size,
			services.request_charset(),
			SQLITE_DEFAULT_CHARSET);
		
                rc = sqlite3_open(db_path, &connection.handle);

		if( SQLITE_OK != rc ){
			const char* errmsg = sqlite3_errmsg(connection.handle);
			_throw(connection, errmsg);
			sqlite3_close(connection.handle);
		}

		*connection_ref=&connection;
		
		if(!connection.autocommit)
			exec(connection, "SET AUTOCOMMIT=0");
			
	}

	void exec(Connection& connection, const char* statement) {
		char *zErr;
		int rc;
		rc=sqlite3_exec(connection.handle, statement, 0, 0, &zErr);
		if(rc!=SQLITE_OK){
			_throw(connection, zErr);
			sqlite3_free(zErr); // error? can't free memory after throw
		}

	}

	void disconnect(void *aconnection) {
		Connection& connection=*static_cast<Connection*>(aconnection);
		sqlite3_close(connection.handle);
		connection.handle=0;
	}

	void commit(void *aconnection) {
		Connection& connection=*static_cast<Connection*>(aconnection);
		if(!connection.autocommit)
			exec(connection, "COMMIT");
	}

	void rollback(void *aconnection) {
		Connection& connection=*static_cast<Connection*>(aconnection);
		if(!connection.autocommit)
			exec(connection, "ROLLBACK");
	}

	bool ping(void *aconnection) {
		return true;  // not needed
	}

	const char* quote(void *aconnection, const char *from, unsigned int length) {
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
		SQL_Driver_query_event_handlers& handlers) {

		Connection& connection=*static_cast<Connection*>(aconnection);
		SQL_Driver_services& services=*connection.services;
		const char* cstrClientCharset=connection.cstrClientCharset;

		if(placeholders_count>0)
			_throw(connection, "bind variables not supported yet");
   
		// transcode from $request:charset to ClientCharset
		if(cstrClientCharset) {
			size_t transcoded_statement_size;
			services.transcode(astatement, strlen(astatement),
				astatement, transcoded_statement_size,
				services.request_charset(),
				cstrClientCharset);
		}
		
		const char *statement;
		if(offset || limit) {
			size_t statement_size=strlen(astatement);
			char *statement_limited=(char *)services.malloc_atomic(
				statement_size+MAX_NUMBER*2+8/* limit #,#*/+1);
			char *cur=statement_limited;
			memcpy(cur, astatement, statement_size); cur+=statement_size;
			cur+=sprintf(cur, " limit ");
			if(offset)
				cur+=snprintf(cur, MAX_NUMBER+1, "%u,", offset);
			if(limit)
				cur+=snprintf(cur, MAX_NUMBER, "%u", limit);
			statement=statement_limited;
		} else
			statement=astatement;


		const char *pzTail;
		sqlite3_stmt *SQL;
		int rc;
		int i;
		SQL_Error sql_error;
		bool failed = false;

		do{ // cycling through SQL commands

			rc = sqlite3_prepare(connection.handle, statement, -1, &SQL, &pzTail);

			if(rc!=SQLITE_OK){
				_throw(connection, sqlite3_errmsg(connection.handle));
				sqlite3_free(pzTail); // error? can't free memory after throw
			}
			

			#define CHECK(afailed) if(afailed) { failed=true; goto cleanup; }

			int column_count = sqlite3_column_count(SQL);

			if(!column_count){  // empty result: insert|delete|update|...
				rc = sqlite3_step(SQL);
			} else {

				for(i=0; i<column_count; i++){
					const char *column_name = sqlite3_column_name(SQL, i);
					size_t length = strlen(column_name);

					char* strm=(char*)services.malloc_atomic(length+1);
					memcpy(strm, column_name, length+1);
					const char* str = strm;
					// transcode to $request:charset from connect-string?ClientCharset
					if(cstrClientCharset) {
						services.transcode(str, length,
							str, length,
							cstrClientCharset,
							services.request_charset());
					}
							
					CHECK(handlers.add_column(sql_error, (const char*)strm, length));
				}
				CHECK(handlers.before_rows(sql_error));

				int column_type;
				const unsigned char *str;
				size_t length = 0;

				do{
					rc = sqlite3_step(SQL);
					if( rc == SQLITE_ROW ){   // новая строка!!

						CHECK(handlers.add_row(sql_error));

						for(i=0; i<column_count; i++){

							column_type = sqlite3_column_type(SQL, i);
		
							// SQLite позволяет поле любого типа получить в виде строки через sqlite3_column_text
							// просто перекодирует если требуется
							// а парсер только строковые значения получает
							// но switch я всё-таки сделал - так, на будущее
							switch(column_type) {
								case SQLITE_TEXT:
									str=(const unsigned char*)sqlite3_column_text(SQL, i);
									length=strlen(str);
									break;
								case SQLITE_INTEGER:
									str=(const unsigned char*)sqlite3_column_text(SQL, i);
									length=strlen(str);
									break;
								case SQLITE_NULL:
									str=NULL;
									length=0;
									break;
								default:
									str=(const unsigned char*)sqlite3_column_text(SQL, i);
									length=strlen(str);
									break;
							}

							if(length){
								char* strm=(char*)services.malloc_atomic(length+1);
								memcpy(strm, str, length+1);
								str = strm;

								// transcode to $request:charset from connect-string?ClientCharset
								if(cstrClientCharset) {
									services.transcode(str, length,
										str, length,
										cstrClientCharset,
										services.request_charset());
								}
							} else
								str = 0;
							
							CHECK(handlers.add_row_cell(sql_error, str, length));

						}
					}
				} while( rc == SQLITE_BUSY || rc == SQLITE_ROW );

			}  // if column

			if( rc == SQLITE_ERROR || rc == SQLITE_MISUSE ){
				_throw(connection, sqlite3_errmsg(connection.handle));
			}

	cleanup:
			sqlite3_finalize(SQL);
			statement = pzTail;
		} while (strlen(pzTail) > 0);

		if(failed)
			_throw(connection, sql_error);
	}

private:
        void _throw(Connection& connection, const char* aerr_msg) {
			size_t err_length=strlen(aerr_msg);
			if(err_length && connection.cstrClientCharset) {
				connection.services->transcode(aerr_msg, err_length,
					aerr_msg, err_length,
					connection.cstrClientCharset,
					connection.services->request_charset());
			}
			connection.services->_throw(aerr_msg);
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
		return 0;
	}

};

extern "C" SQL_Driver *SQL_DRIVER_CREATE() {
	return new SQLite_Driver();
}
