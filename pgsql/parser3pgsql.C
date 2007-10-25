/** @file
	Parser PgSQL driver.

	Copyright(c) 2001, 2003 ArtLebedev Group (http://www.artlebedev.com)

	Author: Alexandr Petrosian <paf@design.ru> (http://paf.design.ru)

	2007.10.25 using PgSQL 8.1.5
*/
static const char *RCSId="$Id: parser3pgsql.C,v 1.28 2007/10/25 17:08:41 misha Exp $"; 

#include "config_includes.h"

#include "pa_sql_driver.h"

#include <libpq-fe.h>
#include <libpq/libpq-fs.h>

// OIDOID from catalog/pg_type.h
#define OIDOID			26
// LO_BUFSIZE from interfaces\libpq\fe-lobj.c = 8192 (0x2000)
// actually writing chunks of that size failed, reduced it twice
#define LO_BUFSIZE		  0x1000
// from postgres_ext.h
//#define InvalidOid		((Oid) 0)


#include "ltdl.h"

#define MAX_STRING 0x400
#define MAX_NUMBER 20

#if _MSC_VER
#	define snprintf _snprintf
#	define strcasecmp _stricmp
#endif

#ifndef max
inline int max(int a,int b) { return a>b?a:b; }
inline int min(int a,int b){ return a<b?a:b; }
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

	PGconn *conn;
	const char* cstrClientCharset;
};

/**
	PgSQL server driver
*/
class PgSQL_Driver : public SQL_Driver {
public:

	PgSQL_Driver() : SQL_Driver() {
	}

	/// get api version
	int api_version() { return SQL_DRIVER_API_VERSION; }
	/// initialize driver by loading sql dynamic link library
	const char *initialize(char *dlopen_file_spec) {
		return dlopen_file_spec?
			dlink(dlopen_file_spec):"client library column is empty";
	}

	#define throwPQerror connection.services->_throw(PQerrorMessage(connection.conn))
	#define PQclear_throw(msg) { \
			PQclear(res); \
			connection.services->_throw(msg); \
		}						
	#define PQclear_throwPQerror PQclear_throw(PQerrorMessage(connection.conn))

	/**	connect
		@param url
			format: @b user:pass@host[:port]|[local]/database
	*/
	void connect(
		char *url, 
		SQL_Driver_services& services, 
		void **connection_ref ///< output: Connection*
		) {
		char *user=url;
		char *host=rsplit(user, '@');
		char *db=lsplit(host, '/');
		char *pwd=lsplit(user, ':');
		char *port=lsplit(host, ':');

		char *options=lsplit(db, '?');

		char *cstrBackwardCompAskServerToTranscode=0;

		Connection& connection=*(Connection  *)services.malloc(sizeof(Connection));
		*connection_ref=&connection;
		connection.services=&services;
		connection.cstrClientCharset=0;	
		connection.conn=PQsetdbLogin(
			(host&&strcasecmp(host, "local")==0)?NULL/* local Unix domain socket */:host, port, 
			NULL, NULL, db, user, pwd);
		if(!connection.conn)
			services._throw("PQsetdbLogin failed");
		if(PQstatus(connection.conn)!=CONNECTION_OK)  
			throwPQerror;

		char *charset=0;
		char *datestyle=0;
		isDefaultTransaction = true;

		while(options) {
			if(char *key=lsplit(&options, '&')) {
				if(*key) {
					if(char *value=lsplit(key, '=')) {
						if(strcmp(key, "ClientCharset" ) == 0) {
							toupper_str(value, value, strlen(value));
							connection.cstrClientCharset=value;
						} else if(strcasecmp(key, "charset")==0) { // left for backward compatibility, consider using ClientCharset
							cstrBackwardCompAskServerToTranscode=value;
						} else if(strcasecmp(key, "datestyle")==0) {
							datestyle=value;
						} else if(strcmp(key, "WithoutDefaultTransaction")==0) {
							if(strcmp(value, "1" ) == 0) {
								isDefaultTransaction = false;
							} else if(strcmp(value, "0" ) == 0) {
								isDefaultTransaction = true;
							} else {
								services._throw("Bad WithoutDefaultTransaction option value. Only 0 or 1 are accepted." /*value*/);
							}
						} else
							services._throw("unknown connect option" /*key*/);
					} else 
						services._throw("connect option without =value" /*key*/);
				}
			}
		}

		// if(connection.cstrClientCharset && cstrBackwardCompAskServerToTranscode)
		// 	services._throw("use 'ClientCharset' option only, "
		// 		"'charset' option is obsolete and should not be used with new 'ClientCharset' option");

		if(cstrBackwardCompAskServerToTranscode) {
			// set CLIENT_ENCODING
			char statement[MAX_STRING]="set CLIENT_ENCODING="; // win
			strncat(statement, cstrBackwardCompAskServerToTranscode, MAX_STRING);

			execute_resultless(connection, statement);
		}

		if(datestyle) {
			// set DATESTYLE
			char statement[MAX_STRING]="set DATESTYLE="; // ISO,SQL,Postgres,European,NonEuropean=US,German,DEFAULT=ISO
			strncat(statement, charset, MAX_STRING);

			execute_resultless(connection, statement);
		}

		begin_transaction(connection);
	}
	void disconnect(void *aconnection) {
		Connection& connection=*static_cast<Connection*>(aconnection);

	    PQfinish(connection.conn);
		connection.conn=0;
	}
	void commit(void *aconnection) {
		execute_transaction_cmd(aconnection, "COMMIT");
	}
	void rollback(void *aconnection) {
		execute_transaction_cmd(aconnection, "ROLLBACK");
	}

	bool ping(void *aconnection) {
		Connection& connection=*static_cast<Connection*>(aconnection);

		return PQstatus(connection.conn)==CONNECTION_OK;
	}

	const char* quote(
		void *aconnection,
		const char *from, unsigned int length) {
		Connection& connection=*static_cast<Connection*>(aconnection);

		char *result=(char*)connection.services->malloc_atomic(length*2+1);
		int err = 0;
		PQescapeStringConn (connection.conn,
                           result, from, length,
                           &err);
		return result;
	}
	
	void query(void *aconnection, 
		const char *astatement, 
		size_t placeholders_count, Placeholder* placeholders, 
		unsigned long offset, unsigned long limit,
		SQL_Driver_query_event_handlers& handlers) {
//		_asm int 3;
		Connection& connection=*static_cast<Connection*>(aconnection);
		const char* cstrClientCharset=connection.cstrClientCharset;
		SQL_Driver_services& services=*connection.services;
		PGconn *conn=connection.conn;

		const char** paramValues;
		if(placeholders_count>0){
			//services._throw("bind variables not supported (yet)");
			int binds_size=sizeof(char) * placeholders_count;
			paramValues = static_cast<const char**>(services.malloc_atomic(binds_size));
			bind_parameters(placeholders_count, placeholders, paramValues, connection);
		}

		// transcode from $request:charset to connect-string?client_charset
		if(cstrClientCharset) {
			size_t transcoded_statement_size;
			services.transcode(astatement, strlen(astatement),
				astatement, transcoded_statement_size,
				services.request_charset(),
				cstrClientCharset);
		}

		const char *statement=preprocess_statement(connection,
			astatement, offset, limit);

		PGresult *res;
		if(placeholders_count>0){
			res=PQexecParams(conn, statement, placeholders_count, NULL, paramValues, NULL, NULL, 0);
		} else {
			res=PQexec(conn, statement);
		}
		if(!res) 
			throwPQerror;

		switch(PQresultStatus(res)) {
		case PGRES_EMPTY_QUERY: 
			PQclear_throw("no query");
			break;
		case PGRES_COMMAND_OK:
			// empty result: insert|delete|update|...
			PQclear(res);
			return;
		case PGRES_TUPLES_OK: 
			break;	
		default:
			PQclear_throwPQerror;
			break;
		}
		
		int column_count=PQnfields(res);
		if(!column_count)
			PQclear_throw("result contains no columns");

		bool failed=false;
		SQL_Error sql_error;
#define CHECK(afailed) \
		if(afailed) { \
			failed=true; \
			goto cleanup; \
		}

		for(int i=0; i<column_count; i++){
			char *name=PQfname(res, i);
			size_t length=strlen(name);
			char* strm=(char*)services.malloc(length+1);
			memcpy(strm, name, length+1);
			const char* str=strm;

			// transcode to $request:charset from connect-string?client_charset
			if(cstrClientCharset) 
				services.transcode(str, length,
					str, length,
					cstrClientCharset,
					services.request_charset());

			CHECK(handlers.add_column(sql_error, str, length));
		}

		CHECK(handlers.before_rows(sql_error));

		if(unsigned long row_count=(unsigned long)PQntuples(res))
			for(unsigned long r=0; r<row_count; r++) {
				CHECK(handlers.add_row(sql_error));
				for(int i=0; i<column_count; i++){
					const char *cell=PQgetvalue(res, r, i);
					size_t length;
					const char* str;
					if(PQftype(res, i)==OIDOID) {
						// ObjectID column, read object bytes

						char *error_pos=0;
						Oid oid=cell?atoi(cell):0;
						int fd=lo_open(conn, oid, INV_READ);
						if(fd>=0) {
							// seek to end
							if(lo_lseek(conn, fd, 0, SEEK_END)<0)
								PQclear_throwPQerror;
							// get length
							int size_tell=lo_tell(conn, fd);
							if(size_tell<0)
								PQclear_throwPQerror;
							// seek to begin
							if(lo_lseek(conn, fd, 0, SEEK_SET)<0)
								PQclear_throwPQerror;
							length=(size_t)size_tell;
							if(length) {
								// read 
								char* strm=(char*)services.malloc(length+1);
								if(!lo_read_ex(conn, fd, strm, size_tell))
									PQclear_throw("lo_read can not read all bytes of object");
								strm[length]=0;
								str=strm;
							} else
								str=0;
							if(lo_close(conn, fd)<0)
								PQclear_throwPQerror;
						} else
							PQclear_throwPQerror;
					} else {
						// normal column, read it normally
						length=(size_t)PQgetlength(res, r, i);
						if(length) {
							char* strm=(char*)services.malloc(length+1);
							memcpy(strm, cell, length+1);
							str=strm;
						} else
							str=0;
					}

					if(str && length) {
						// transcode to $request:charset from connect-string?client_charset
						if(cstrClientCharset)
							services.transcode(str, length,
								str, length,
								cstrClientCharset,
								services.request_charset());
					}

					CHECK(handlers.add_row_cell(sql_error, str, length));
				}
			}
cleanup:
		PQclear(res);
		if(failed)
			services._throw(sql_error);
	}

private: // private funcs

	void bind_parameters(
		size_t placeholders_count, 
		Placeholder* placeholders, 
		const char** paramValues,
		Connection& connection
		) {
		for(size_t i=0; i<placeholders_count; i++) {
			Placeholder& ph=placeholders[i];
			size_t value_length;
			if(connection.cstrClientCharset) {
				size_t name_length;
				connection.services->transcode(ph.name, strlen(ph.name),
					ph.name, name_length,
					connection.services->request_charset(),
					connection.cstrClientCharset);

				if(ph.value) {
					connection.services->transcode(ph.value, strlen(ph.value),
						ph.value, value_length,
						connection.services->request_charset(),
						connection.cstrClientCharset);
				}
			}
			if( atoi(ph.name) <= 0 || atoi(ph.name) > placeholders_count) {
				connection.services->_throw("bad bind parameter key");
			}
			paramValues[atoi(ph.name)-1] = ph.value;
		}
	}
	
	
	void execute_transaction_cmd(void *aconnection, const char *query) {
		if(isDefaultTransaction)
		{
			Connection& connection=*static_cast<Connection*>(aconnection);
			execute_resultless(connection, query);
			begin_transaction(connection);
		}
	}
	
	/**
		Executes a query and throws the result.
	*/
	void execute_resultless(const Connection& connection, const char *query) {
		if(PGresult *res=PQexec(connection.conn, query))
			PQclear(res); // throw out the result [don't need but must call]
		else
			throwPQerror;
	}

	void begin_transaction(Connection& connection) {
		if(isDefaultTransaction)
		{
			execute_resultless(connection, "BEGIN");
		}
	}

	const char *preprocess_statement(Connection& connection,
		const char *astatement, unsigned long offset, unsigned long limit) {
		PGconn *conn=connection.conn;

		size_t statement_size=strlen(astatement);

		char *result=(char *)connection.services->malloc(statement_size
			+MAX_NUMBER*2+15 // limit # offset #
			+MAX_STRING // in case of short 'strings'
			+1);
		// offset & limit -> suffixes
		const char *o;
		if(offset || limit) {
			char *cur=result;
			memcpy(cur, astatement, statement_size); cur+=statement_size;
			if(limit)
				cur+=snprintf(cur, 7+MAX_NUMBER, " limit %u", limit);
			if(offset)
				cur+=snprintf(cur, 8+MAX_NUMBER, " offset %u", offset);
			o=result;
		} else 
			o=astatement;

		// /**xxx**/'literal' -> oid
		char *n=result;
		while(*o) {
			if(
				o[0]=='/' &&
				o[1]=='*' && 
				o[2]=='*') { // name start
				const char* saved_o=o;
				o+=3;
				while(*o)
					if(
						o[0]=='*' &&
						o[1]=='*' &&
						o[2]=='/' &&
						o[3]=='\'') { // name end
						saved_o=0; // found, marking that
						o+=4;
						Oid oid=lo_creat(conn, INV_READ|INV_WRITE);
						if(oid==InvalidOid)
							throwPQerror;
						int fd=lo_open(conn, oid, INV_WRITE);
						if(fd>=0) {
							const char *start=o;
							bool escaped=false;
							while(*o && !(o[0]=='\'' && o[1]!='\'' && !escaped)) {
								escaped=*o=='\\' || (o[0]=='\'' && o[1]=='\'');
								if(escaped) {
									// write pending, skip "\" or "'"
									if(!lo_write_ex(conn, fd, start, o-start))
										connection.services->_throw("lo_write could not write all bytes of object (1)");
									start=++o;
								} else
									o++;
							}
							if(!lo_write_ex(conn, fd, start, o-start))
								connection.services->_throw("lo_write can not write all bytes of object (2)");
							if(lo_close(conn, fd)<0)
								throwPQerror;
						} else
							throwPQerror;
						if(*o)
							o++; // skip "'"

						n+=snprintf(n, MAX_NUMBER, "%u", oid);
						break;
					} else
						o++; // /**skip**/'xxx'
				if(saved_o) {
					o=saved_o;
					*n++=*o++;
				}
			} else
				*n++=*o++;
		}
		*n=0;

		return result;
	}

private: // lo_read/write exchancements

	bool lo_read_ex(PGconn *conn, int fd, const/*paf*/ char *buf, size_t len) {
		return lo_rw_method (conn, fd, buf, len, lo_read);
	}

	bool lo_write_ex(PGconn *conn, int fd, const/*paf*/ char *buf, size_t len) {
		return lo_rw_method (conn, fd, buf, len, lo_write);
	}

	bool lo_rw_method(PGconn *conn, int fd, const/*paf*/ char *buf, size_t len, int (*lo_func)(PGconn *conn, int fd, const/*paf*/ char *buf, size_t len)) {
		int size_op;
		while(len && (size_op=lo_func(conn, fd, buf, min(LO_BUFSIZE, len)))>0) {
			buf+=size_op;
			len-=size_op;									
		}
		return len==0;
	}

private: // conn client library funcs

	typedef PGconn* (*t_PQsetdbLogin)(
		const char *pghost,
		const char *pgport,
		const char *pgoptions,
		const char *pgtty,
		const char *dbName,
		const char *login,
		const char *pwd); t_PQsetdbLogin PQsetdbLogin;
	typedef void (*t_PQfinish)(PGconn *conn);  t_PQfinish PQfinish;
	typedef char *(*t_PQerrorMessage)(const PGconn* conn); t_PQerrorMessage PQerrorMessage;
	typedef ConnStatusType (*t_PQstatus)(const PGconn *conn); t_PQstatus PQstatus;
	typedef PGresult *(*t_PQexec)(PGconn *conn,
	                 const char *query); t_PQexec PQexec;
//PQexecParams
	typedef PGresult *(*t_PQexecParams)(
					   PGconn *conn,
					   const char *query, 
					   int nParams,
                       const Oid *paramTypes,
                       const char * const *paramValues,
                       const int *paramLengths,
                       const int *paramFormats,
                       int resultFormat); t_PQexecParams PQexecParams;

	typedef ExecStatusType (*t_PQresultStatus)(const PGresult *res); t_PQresultStatus PQresultStatus;
	typedef int (*t_PQgetlength)(const PGresult *res,
					int tup_num,
					int field_num); t_PQgetlength PQgetlength;
	typedef char* (*t_PQgetvalue)(const PGresult *res,
					 int tup_num,
					 int field_num); t_PQgetvalue PQgetvalue;
	typedef int (*t_PQntuples)(const PGresult *res); t_PQntuples PQntuples;
	typedef char *(*t_PQfname)(const PGresult *res,
						int field_index); t_PQfname PQfname;
	typedef int (*t_PQnfields)(const PGresult *res); t_PQnfields PQnfields;
	typedef void (*t_PQclear)(PGresult *res); t_PQclear PQclear;

	typedef Oid	(*t_PQftype)(const PGresult *res, int field_num); t_PQftype PQftype;

	typedef size_t (*t_PQescapeStringConn)(PGconn *conn,
                           char *to, const char *from, size_t length,
                           int *error); t_PQescapeStringConn PQescapeStringConn;

	typedef int	(*t_lo_open)(PGconn *conn, Oid lobjId, int mode); t_lo_open lo_open;
	typedef int	(*t_lo_close)(PGconn *conn, int fd); t_lo_close lo_close;
	typedef int	(*t_lo_read)(PGconn *conn, int fd, const/*paf*/ char *buf, size_t len); t_lo_read lo_read;
	typedef int	(*t_lo_write)(PGconn *conn, int fd, const/*paf*/ char *buf, size_t len); t_lo_write lo_write;
	typedef int	(*t_lo_lseek)(PGconn *conn, int fd, int offset, int whence); t_lo_lseek lo_lseek;
	typedef Oid	(*t_lo_creat)(PGconn *conn, int mode); t_lo_creat lo_creat;
	typedef int	(*t_lo_tell)(PGconn *conn, int fd); t_lo_tell lo_tell;
	typedef int	(*t_lo_unlink)(PGconn *conn, Oid lobjId); t_lo_unlink lo_unlink;
	typedef Oid	(*t_lo_import)(PGconn *conn, const char *filename); t_lo_import lo_import;
	typedef int	(*t_lo_export)(PGconn *conn, Oid lobjId, const char *filename); t_lo_export lo_export;

private: // conn client library funcs linking

	const char *dlink(const char *dlopen_file_spec) {
		if(lt_dlinit())
			return lt_dlerror();
        lt_dlhandle handle=lt_dlopen(dlopen_file_spec);
        if(!handle)
			return "can not open the dynamic link module";

		#define DSLINK(name, action) \
			name=(t_##name)lt_dlsym(handle, #name); \
				if(!name) \
					action;

		#define DLINK(name) DSLINK(name, return "function " #name " was not found")
		
		DLINK(PQsetdbLogin);
		DLINK(PQerrorMessage);
		DLINK(PQstatus);
		DLINK(PQfinish);
		DLINK(PQgetvalue);
		DLINK(PQgetlength);
		DLINK(PQntuples);
		DLINK(PQfname);
		DLINK(PQnfields);
		DLINK(PQclear);
		DLINK(PQresultStatus);
		DLINK(PQexec);
		DLINK(PQexecParams);
		DLINK(PQftype);
		DLINK(PQescapeStringConn);
		DLINK(lo_open);		DLINK(lo_close);
		DLINK(lo_read);		DLINK(lo_write);
		DLINK(lo_lseek);		DLINK(lo_creat);
		DLINK(lo_tell);		DLINK(lo_unlink);
		DLINK(lo_import);		DLINK(lo_export);

		return 0;
	}

	bool isDefaultTransaction;
};

extern "C" SQL_Driver *SQL_DRIVER_CREATE() {
	return new PgSQL_Driver();
}
