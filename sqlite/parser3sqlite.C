/** @file
	Parser SQLite driver.

	(c) Dmitry "Creator" Bobrik, 2004
*/
//static const char *RCSId="$Id: parser3sqlite.C,v 1.1 2007/02/19 10:54:36 misha Exp $"; 

#include "config_includes.h"

#include "pa_sql_driver.h"
//#include "windows.h"  // for messagebox

#define NO_CLIENT_LONG_LONG
#include "sqlite3.h"
#include "ltdl.h"

#define MAX_STRING 0x400
#define MAX_NUMBER 20

#if _MSC_VER
#	define snprintf _snprintf
#	define strcasecmp _stricmp
#endif

static void MBox(const char *message, const char *title){
//	MessageBox(0, (LPCSTR)message, (LPCSTR)title, MB_OK);
}

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
			format: @b database
			SQLite options - database file name
	*/
	void connect(
		char *url, 
		SQL_Driver_services& services, 
		void **connection_ref ///< output: Connection*
		) {

		int rc;

//		char *cstrBackwardCompAskServerToTranscode=0;

		Connection& connection=*(Connection  *)services.malloc(sizeof(Connection));
		connection.services=&services;

		rc = sqlite3_open(url, &connection.handle);

		if( SQLITE_OK != rc ){
			services._throw(sqlite3_errmsg(connection.handle));
			sqlite3_close(connection.handle);
		}

		connection.cstrClientCharset=0;	
		connection.autocommit=true;   // ������ ��� ��� INSERT� � UPDATE� ���������� ������������� ������ ����������� ��������� ��������
		*connection_ref=&connection;

	}

	void exec(Connection& connection, const char* statement) {

		char *zErr;
		int rc;

		rc = sqlite3_exec(connection.handle, statement, 0, 0, &zErr);

		MBox(statement, "exec_stat");

		if( SQLITE_OK!=rc ){
			MBox(zErr, "exec_error");
			connection.services->_throw(zErr);
			sqlite3_free(zErr);
		}

	}

	void disconnect(void *aconnection) {
		Connection& connection=*static_cast<Connection*>(aconnection);

		sqlite3_close(connection.handle);
		connection.handle=0;

		MBox("disconnect", "disconnect");

	}
	void commit(void *aconnection) {
		//_asm int 3;
		Connection& connection=*static_cast<Connection*>(aconnection);
		MBox("commit", "commit");

		if(!connection.autocommit)
			exec(connection, "commit");
	}
	void rollback(void *aconnection) {
		Connection& connection=*static_cast<Connection*>(aconnection);

		MBox("rollback", "rollback");

		if(!connection.autocommit)
			exec(connection, "rollback");
	}

	bool ping(void *aconnection) {
		Connection& connection=*static_cast<Connection*>(aconnection);

		return true;  // ���� �� ���������, �.�. ������ �� �������� :)
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
		MBox(from, "quote");
		char *to=result;
		while(length--) {
			if(*from=='\'') { // ' -> ''
				*to++='\'';
			}
			*to++=*from++;
		}
		*to=0;
//		mysql_escape_string(result, from, length);
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
			services._throw("bind variables not supported (yet)");

		const char *statement;
		// ��� ���� ���� ��������� � ������ LIMIT N, M ���� ������. SQLite ������������ ��� ���������
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


		char *zErr;
		const char *pzTail;
		sqlite3_stmt *SQL;
		int rc;
		int i;
		SQL_Error sql_error;
		bool failed = false;

		do{ // cycling through SQL commands

//			MBox(statement, "statement");
			rc = sqlite3_prepare(connection.handle, statement, -1, &SQL, &pzTail);
			MBox(pzTail, "tail");

			if( SQLITE_OK!=rc ){
				MBox(sqlite3_errmsg(connection.handle), "query_error");
				services._throw(sqlite3_errmsg(connection.handle));
				sqlite3_free(zErr);
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

					CHECK(handlers.add_column(sql_error, (const char*)strm, length));
				}
				CHECK(handlers.before_rows(sql_error));

				int column_type;
				const unsigned char *str;
				size_t length = 0;

				do{
					rc = sqlite3_step(SQL);
					if( rc == SQLITE_ROW ){   // ����� ������!!

						CHECK(handlers.add_row(sql_error));

						for(i=0; i<column_count; i++){

							column_type = sqlite3_column_type(SQL, i);
		
							// SQLite ��������� ���� ������ ���� �������� � ���� ������ ����� sqlite3_column_text
							// ������ ������������ ���� ���������
							// � ������ ������ ��������� �������� ��������
							// �� switch � ��-���� ������ - ���, �� �������
							switch(column_type) {
								case SQLITE_TEXT:
									str = sqlite3_column_text(SQL, i);
									length = strlen((const char*)str);
									break;
								case SQLITE_INTEGER:
									str = sqlite3_column_text(SQL, i);
									length = strlen((const char*)str);
									break;
								default:
									str = sqlite3_column_text(SQL, i);
									length = strlen((const char*)str);
									break;
							}

							//MBox((const char*)str, "query_in_step");

							char* strm=(char*)services.malloc_atomic(length+1);
							memcpy(strm, str, length+1);

							CHECK(handlers.add_row_cell(sql_error, (const char*)strm, length));

						}
					}
				} while( rc == SQLITE_BUSY || rc == SQLITE_ROW );

			}  // if column

			if( rc == SQLITE_ERROR || rc == SQLITE_MISUSE ){
				services._throw(sqlite3_errmsg(connection.handle));
			}

	cleanup:
			sqlite3_finalize(SQL);
			statement = pzTail;
		} while (strlen(pzTail) > 0);

		if(failed)
			services._throw(sql_error);
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