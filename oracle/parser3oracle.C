/** @file
	Parser Oracle driver.

	Copyright(c) 2001, 2003 ArtLebedev Group (http://www.artlebedev.com)

	Author: Alexandr Petrosian <paf@design.ru> (http://paf.design.ru)

	2001.07.30 using Oracle 8.1.6 [@test tested with Oracle 7.x.x]
*/

static const char *RCSId="$Id: parser3oracle.C,v 1.67 2004/12/23 16:54:52 paf Exp $"; 

#include "config_includes.h"

#include "pa_sql_driver.h"

#include <oci.h>

#define MAX_COLS 500
#define MAX_IN_LOBS 5
#define MAX_LOB_NAME_LENGTH 100
#define MAX_OUT_STRING_LENGTH 4000
#define MAX_BINDS 100

#define EMPTY_CLOB_FUNC_CALL "empty_clob()"

#include "ltdl.h"

#define MAX_STRING 0x400
#define MAX_NUMBER 20

#if _MSC_VER
#	define snprintf _snprintf
#	define strcasecmp _stricmp
#	define strncasecmp _strnicmp
#endif

#ifndef max
inline int max(int a, int b) { return a>b?a:b; }
inline int min(int a, int b){ return a<b?a:b; }
#endif

#if _MSC_VER
// interaction between '_setjmp' and C++ object destruction is non-portable
// but we forced to do that under HPUX
#pragma warning(disable:4611)   
#endif

const sb2 MAGIC_INDICATOR_VALUE_MEANING_NOT_NULL_AND_UNCHANGED=99;

/// @todo small memory leaks here
static int pa_setenv(const char *name, const char *value, bool do_append) {
	const char *prev_value=0;
	if(do_append)
		prev_value=getenv(name);
#ifdef HAVE_PUTENV
    // MEM_LEAK_HERE. refer to EOF man putenv
	char *buf=(char *)::malloc(strlen(name)
		+1
		+(prev_value?strlen(prev_value):0)
		+strlen(value)
		+1);
	strcpy(buf, name);
	strcat(buf, "=");
	if(prev_value)
		strcat(buf, prev_value);
	strcat(buf, value);
/*
	if(FILE *f=fopen("f", "at")) {
		fprintf(f, "****************************%s\n", buf);
//		for (char **env = environ; env != NULL && *env != NULL; env++)
//			fputs(*env, f);
	
		fclose(f);
	}
*/	
	return putenv(buf);
#else 
	//#ifdef HAVE_SETENV
	if(value) {
		if(prev_value) {
			// MEM_LEAK_HERE
			char *buf=(char *)::malloc(strlen(prev_value)
				+strlen(value)
				+1);
			strcpy(buf, prev_value);
			strcat(buf, value);
			value=buf;
		}
		return setenv(name, value, 1/*overwrite*/); 
	} else {
		unsetenv(name);
		return 0;
	}
#endif
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

#ifndef DOXYGEN
struct Connection {
	SQL_Driver_services *services;

	jmp_buf mark; char error[MAX_STRING];
	SQL_Error sql_error;
	OCIEnv *envhp;
	OCIServer *srvhp;
	OCIError *errhp;
	OCISvcCtx *svchp;
	OCISession *usrhp;

	char* fetch_buffers[MAX_COLS];
	char* bind_buffers[MAX_BINDS];

	struct Options {
		bool bLowerCaseColumnNames;
		const char* cstrClientCharset;
	} options;
};

struct Query_lobs {
	struct return_rows {
		struct return_row {
			OCILobLocator *locator; ub4 len;
			int ind;		
			ub2 rcode;
		} *row;
		int count;
	};
	struct Item {
		const char *name_ptr; size_t name_size;
		char *data_ptr; size_t data_size;
		OCILobLocator *locator;
		OCIBind *bind;
		return_rows rows;
		Connection *connection;
	} items[MAX_IN_LOBS];
	int count;
};

#endif

// forwards
static void faile(Connection& connection, const char *msg);
static void check(Connection& connection, const char *step, sword status);
static void check(Connection& connection, bool error);
static sb4 cbf_no_data(
					   dvoid *ctxp, 
					   OCIBind *bindp, 
					   ub4 iter, ub4 index, 
					   dvoid **bufpp, 
					   ub4 *alenpp, 
					   ub1 *piecep, 
					   dvoid **indpp);
static sb4 cbf_get_data(dvoid *ctxp, 
						OCIBind *bindp, 
						ub4 iter, ub4 index, 
						dvoid **bufpp, 
						ub4 **alenp, 
						ub1 *piecep, 
						dvoid **indpp, 
						ub2 **rcodepp);
static void tolower_str(char *out, const char *in, size_t size);
static void toupper_str(char *out, const char *in, size_t size);

static const char *options2env(char *s, Connection::Options* options) {
	while(s) {
		if(char *key=lsplit(&s, '&')) {
			if(*key) {
				if(char *value=lsplit(key, '=')) {
					if( strcmp( key, "ClientCharset" ) == 0 ) {
						if(options) {
							toupper_str(value, value, strlen(value));
							options->cstrClientCharset = value;
						}
						continue;
					}

					if( strcmp( key, "LowerCaseColumnNames" ) == 0 ) {
						if(options)
							options->bLowerCaseColumnNames = atoi(value)!=0;
						continue;
					}

					bool do_append=key[strlen(key)-1]=='+'; // PATH+=
					if(do_append)
						key[strlen(key)-1]=0; // remove trailing +
					if(strncmp(key, "ORACLE_", 7)==0  // ORACLE_HOME & co
						|| strncmp(key, "ORA_", 4)==0 // ORA_ENCRYPT_LOGIN & co
						|| strncmp(key, "NLS_", 4)==0 // NLS_LANG & co
						|| do_append
						) {
						if(pa_setenv(key, value, do_append)!=0)
							return "problem changing process environment" /*key*/;
					} else
						return "unknown option" /*key*/;
				} else 
					return "option without =value" /*key*/;
			}
		}
	}
	return 0;
}

/**
	OracleSQL server driver
*/
class OracleSQL_Driver : public SQL_Driver {
public:

	OracleSQL_Driver() : SQL_Driver() {
	}

	/// get api version
	int api_version() { return SQL_DRIVER_API_VERSION; }
	/** initialize driver by loading sql dynamic link library
		@todo ?objects=1 which would turn on OCI_OBJECT init flag
	*/
	const char *initialize(char *dlopen_file_spec) {
		char *options=lsplit(dlopen_file_spec, '?');

		const char *error=dlopen_file_spec?
			dlink(dlopen_file_spec):"client library column is empty";
		if(!error) {
			error=options2env(options, 0);

			if(!error)
				OCIInitialize((ub4)OCI_THREADED/*| OCI_OBJECT*/, (dvoid *)0, 
					(dvoid * (*)(void *, unsigned int))0, 
					(dvoid * (*)(void*, void*, unsigned int))0,  
					(void (*)(void*, void*))0 
				);
		}

		return error;
	}

	/**	connect
		@param url
			format: @b user:pass@service?
			   ORACLE_HOME=/u01/app/oracle/product/8.1.5&
			   ORA_NLS33=/u01/app/oracle/product/8.1.5/ocommon/nls/admin/data&
			   NLS_LANG=RUSSIAN_AMERICA.CL8MSWIN1251&
			   ORA_ENCRYPT_LOGIN=TRUE

		@todo environment manupulation doesnt look thread safe
		@todo allocate 'aused_only_in_connect_url' on gc heap, so it can be manipulated directly
	*/
	void connect(
		char *url, 
		SQL_Driver_services& services, 
		void **connection_ref ///< output: Connection *
		) 
	{
		// connections are cross-request, do not use services._alloc [linked with request]
		Connection& connection=*(Connection  *)services.malloc(sizeof(Connection));
		connection.services=&services;
		connection.options.bLowerCaseColumnNames = true;
		*connection_ref=&connection;

		char *user=url;
		char *service=rsplit(user, '@');
		char *pwd=lsplit(user, ':');
		char *options=lsplit(service, '?');

		if(!(user && pwd && service))
			services._throw("mailformed connect part, must be 'user:pass@service'");

		if(const char *error=options2env(options, &connection.options))
			services._throw(error);

		if(setjmp(connection.mark))
			services._throw(connection.error);

		// Allocate and initialize OCIError handle, attempt #1
		/*
			grabbed from sample 
			/server.804/a58234/oci_func.htm#446192
			but doc 
			/server.804/a58234/oci_func.htm#446100
			doesnt have this param listed as allowed
			8.1.6 client library barks as OCI_INVALID_HANDLE
			and debugging revealed that OCI_HTYPE_ENV param value is invalid
			later in doc 
			/server.804/a58234/oci_func.htm#446192
			on OCIEnvInit thay say
			"No changes are done to an already initialized handle"
			think, this is some sort of backward compatibility wonder.
			leaving as it is, and without check()
		*/
		OCIHandleAlloc((dvoid *)NULL, (dvoid **) &connection.envhp, (ub4)OCI_HTYPE_ENV, 0, 0);
		// Initialize an environment handle, attempt #2
		check(connection, "EnvInit", OCIEnvInit(
			&connection.envhp, (ub4)OCI_DEFAULT, 0, 0));		
		// Allocate and initialize OCIError handle
		check(connection, "HandleAlloc errhp", OCIHandleAlloc( 
			(dvoid *)connection.envhp, (dvoid **) &connection.errhp, (ub4)OCI_HTYPE_ERROR, 0, 0));
		// Allocate and initialize OCIServer handle
		check(connection, "HandleAlloc srvhp", OCIHandleAlloc( 
			(dvoid *)connection.envhp, (dvoid **) &connection.srvhp, (ub4)OCI_HTYPE_SERVER, 0, 0));		
		// Attach to a 'service'; initialize server context handle  
		check(connection, "ServerAttach", OCIServerAttach( 
			connection.srvhp, connection.errhp, (text *)service, (sb4)strlen(service), (ub4)OCI_DEFAULT));
		// Allocate and initialize OCISvcCtx handle
		check(connection, "HandleAlloc svchp", OCIHandleAlloc( 
			(dvoid *)connection.envhp, (dvoid **) &connection.svchp, (ub4)OCI_HTYPE_SVCCTX, 0, 0));		
		// set attribute server context in the service context
		check(connection, "AttrSet server-service", OCIAttrSet( 
			(dvoid *)connection.svchp, (ub4)OCI_HTYPE_SVCCTX, 
			(dvoid *)connection.srvhp, (ub4)0, 
			(ub4)OCI_ATTR_SERVER, (OCIError *)connection.errhp));		
		// allocate a user context handle
		check(connection, "HandleAlloc usrhp", OCIHandleAlloc(
			(dvoid *)connection.envhp, (dvoid **)&connection.usrhp, (ub4)OCI_HTYPE_SESSION, 0, 0));
		// set 'user' name
		check(connection, "AttrSet user-session", OCIAttrSet(
			(dvoid *)connection.usrhp, (ub4)OCI_HTYPE_SESSION, 
			(dvoid *)user, (ub4)strlen(user), 
			OCI_ATTR_USERNAME, connection.errhp));		
		// set 'pwd' password
		check(connection, "AttrSet pwd-session", OCIAttrSet(
			(dvoid *)connection.usrhp, (ub4)OCI_HTYPE_SESSION, 
			(dvoid *)pwd, (ub4)strlen(pwd), 
			OCI_ATTR_PASSWORD, connection.errhp));
		// Authenticate a user  
		check(connection, "SessionBegin", OCISessionBegin(
			connection.svchp, connection.errhp, connection.usrhp, 
			OCI_CRED_RDBMS, OCI_DEFAULT));
		// remember connection in session
		check(connection, "AttrSet service-session", OCIAttrSet(
			(dvoid *)connection.svchp, (ub4)OCI_HTYPE_SVCCTX, 
			(dvoid *)connection.usrhp, (ub4)0, 
			OCI_ATTR_SESSION, connection.errhp));
	}
	void disconnect(void *aconnection) {
	    Connection& connection=*static_cast<Connection *>(aconnection);

		// free fetch buffers. leave that to GC [no such services func. yet?]
		/*
		for(int i=0; i<MAX_COLS; i++) {
			if(void* fetch_buffer=connection.fetch_buffers[i])
				connection.services->free(fetch_buffer);
			else
				break;
		}
		*/

		// Terminate a user session
		OCISessionEnd(
			connection.svchp, connection.errhp, connection.usrhp, (ub4)OCI_DEFAULT);
		// Detach from a server; uninitialize server context handle
		OCIServerDetach(
			connection.srvhp, connection.errhp, (ub4)OCI_DEFAULT);
		// Free a previously allocated handles
		/* 
		oci will free them up as belonging to env
		OCIHandleFree(
			(dvoid *)connection.srvhp, (ub4)OCI_HTYPE_SERVER);
		OCIHandleFree(
			(dvoid *)connection.svchp, (ub4)OCI_HTYPE_SVCCTX);
		OCIHandleFree(
			(dvoid *)connection.errhp, (ub4)OCI_HTYPE_ERROR);
		*/
		OCIHandleFree(
			(dvoid *)connection.envhp, (ub4)OCI_HTYPE_ENV);

		// free connection. leave that to GC [no such services func. yet?]
		// connection.services->free(&connection);
	}
	void commit(void *aconnection) {
	    Connection& connection=*static_cast<Connection *>(aconnection);
		if(setjmp(connection.mark))
			connection.services->_throw(connection.error);

		check(connection, "commit", OCITransCommit(connection.svchp, connection.errhp, 0));
	}
	void rollback(void *aconnection) {
	    Connection& connection=*static_cast<Connection *>(aconnection);
		if(setjmp(connection.mark))
			connection.services->_throw(connection.error);

		// sometimes rollback is done in context when this yields error which masks previous error
		// consider consequent errors not very important to report, reporting first one
		/*check(connection, "rollback", */OCITransRollback(connection.svchp, connection.errhp, 0)/*)*/;
	}

	bool ping(void* /*connection*/) {
		// maybe OCIServerVersion?
		// select 0 from dual
		return true;
	}

	const char* quote(void *aconnection,
		const char *from, unsigned int length) 
	{
		Connection& connection=*static_cast<Connection *>(aconnection);
		char *result=(char*)connection.services->malloc_atomic(length*2+1);
		char *to=result;
		while(length--) {
			switch(*from) {
			case '\'': // "'" -> "''"
				*to++='\'';
				break;
			}
			*to++=*from++;
		}
		*to=0;
		return result;
	}
	void query(void* aconnection, 
		const char* astatement, 
		size_t placeholders_count, Placeholder* placeholders, 
		unsigned long offset, unsigned long limit,
		SQL_Driver_query_event_handlers& handlers) 
	{

		Connection& connection=*static_cast<Connection *>(aconnection);
		const char* cstrClientCharset=connection.options.cstrClientCharset;
		Query_lobs lobs={{0}, 0};
		OCIStmt *stmthp=0;

		SQL_Driver_services& services=*connection.services;

		// transcode from $request:charset to connect-string?client_charset
		if(cstrClientCharset) {
			size_t transcoded_xxx_size;
			services.transcode(astatement, strlen(astatement),
				astatement, transcoded_xxx_size,
				services.request_charset(),
				cstrClientCharset);
		}

		bool failed=false;
		if(setjmp(connection.mark)) {
			failed=true;
			goto cleanup;
		} else {
			if(placeholders_count>MAX_BINDS)
				fail(connection, "too many bind variables");

			const char *statement=preprocess_statement(connection, astatement, lobs);

			check(connection, "HandleAlloc STMT", OCIHandleAlloc( 
				(dvoid *)connection.envhp, (dvoid **) &stmthp, (ub4)OCI_HTYPE_STMT, 0, 0));
			check(connection, "syntax", 
				OCIStmtPrepare(stmthp, connection.errhp, (unsigned char *)statement, 
				(ub4)strlen((char *)statement), 
				(ub4)OCI_NTV_SYNTAX, (ub4)OCI_DEFAULT));

			struct Bind_info {
					OCIBind *bind;
					sb2 indicator;
			};

			int binds_size=sizeof(Bind_info) * placeholders_count;
			// we DO store OCIBind* into ATOMIC gc memory, 
			// but we do not allocate/free it, that's done automatically from oracle [using environment handles]
			// so we don't have to bother with that
			Bind_info* binds=static_cast<Bind_info*>(services.malloc_atomic(binds_size));
			{
				for(size_t i=0; i<placeholders_count; i++) {
					Placeholder& ph=placeholders[i];
					Bind_info& bi=binds[i];
					bi.bind=0;
					// http://i/docs/oracle/server.804/a58234/basics.htm#422173
					bi.indicator=ph.is_null? -1: MAGIC_INDICATOR_VALUE_MEANING_NOT_NULL_AND_UNCHANGED;

					size_t value_length;

					if(cstrClientCharset) {
						size_t name_length;
						services.transcode(ph.name, strlen(ph.name),
							ph.name, name_length,
							services.request_charset(),
							cstrClientCharset);

						if(ph.value)
							services.transcode(ph.value, strlen(ph.value),
								ph.value, value_length,
								services.request_charset(),
								cstrClientCharset);
					} else {
						value_length=ph.value? strlen(ph.value): 0;
					}

					// clone value for possible output binds
					// note: even empty input can be replaced by huge output
					char*& value_buf=connection.bind_buffers[i]; // get cached buffer
					if(!value_buf) // allocate if needed, caching it
						value_buf=(char *)services.malloc_atomic(MAX_OUT_STRING_LENGTH+1/*terminator*/);
					if(value_length)
						memcpy(value_buf, ph.value, value_length+1);
					else
						value_buf[0]=0;

					char name_buf[MAX_STRING];
					sb4 placeh_len=snprintf(name_buf, sizeof(name_buf), ":%s", ph.name);
					char check_step_buf[MAX_STRING];
					snprintf(check_step_buf, sizeof(check_step_buf), "bind by name :%s", ph.name);
					check(connection, check_step_buf, OCIBindByName(stmthp, 
						&bi.bind, connection.errhp, 
						(text*)name_buf, placeh_len,
						(dvoid *)value_buf, (sword)(MAX_OUT_STRING_LENGTH+1), SQLT_STR, (dvoid *)&bi.indicator, 
						(ub2 *)0, (ub2 *)0, (ub4)0, (ub4 *)0, OCI_DEFAULT));
				}

				for(int i=0; i<lobs.count; i++) {
					Query_lobs::Item &item=lobs.items[i];
					check(connection, "alloc output var desc", OCIDescriptorAlloc(
						(dvoid *)connection.envhp, (dvoid **)&item.locator, (ub4)OCI_DTYPE_LOB, 0, 0));

					char placeholder_buf[MAX_STRING];
					sb4 placeh_len=snprintf(placeholder_buf, sizeof(placeholder_buf), 
						":%.*s", item.name_size, item.name_ptr);
					check(connection, "bind lob", OCIBindByName(stmthp, 
						&item.bind, connection.errhp, 
						(text*)placeholder_buf, placeh_len,
						(dvoid *)&item.locator, 
						(sword)sizeof (item.locator), SQLT_CLOB, (dvoid *)0, 
						(ub2 *)0, (ub2 *)0, (ub4)0, (ub4 *)0, OCI_DATA_AT_EXEC));

					item.rows.count=0;
					item.connection=&connection;
					check(connection, "bind dynamic", OCIBindDynamic(
						item.bind, connection.errhp, 
						(dvoid *) &item, cbf_no_data, 
						(dvoid *) &item, cbf_get_data));
				}
			}

			execute_prepared(connection, 
				statement, stmthp, lobs, 
				offset, limit, handlers);

			{
				for(size_t i=0; i<placeholders_count; i++) {
					Bind_info& bi=binds[i];
					if(bi.indicator==MAGIC_INDICATOR_VALUE_MEANING_NOT_NULL_AND_UNCHANGED/*unchanged*/)
						continue;

					Placeholder& ph=placeholders[i];
					if(bi.indicator==-1)
						ph.is_null=true;
					else
						if(bi.indicator==0)
							ph.is_null=false;
						else
							fail(connection, bi.indicator<0?
								"output bind buffer overflow, additionally size too big to be returned in 'indicator'"
								: "output bind buffer overflow");

					ph.were_updated=true;
					const char* bind_buffer=connection.bind_buffers[i];
					if( size_t value_length=strlen(bind_buffer) ) {
						char* returned_value=(char*)services.malloc_atomic(value_length+1/*terminator*/);
						memcpy(returned_value, bind_buffer, value_length+1 );
						ph.value=returned_value;

						if(cstrClientCharset) {
							services.transcode(ph.value, value_length,
								ph.value, value_length/*<this new value is not used afterwards, actually*/,
								cstrClientCharset,
								services.request_charset());
						}
					} else {
						ph.value=0;
					}
				}
			}
		}
cleanup: // no check call after this point!
		{
			for(int i=0; i<lobs.count; i++) {
				/* free var locator */
				if(OCILobLocator *locator=lobs.items[i].locator)
					OCIDescriptorFree((dvoid *)locator, (ub4)OCI_DTYPE_LOB);

				/* free rows descriptors */
				Query_lobs::return_rows &rows=lobs.items[i].rows;
				for(int r=0; r<rows.count; r++)
					OCIDescriptorFree((dvoid *)rows.row[r].locator, (ub4)OCI_DTYPE_LOB);
			}
		}
		if(stmthp)
			OCIHandleFree((dvoid *)stmthp, (ub4)OCI_HTYPE_STMT);

		if(failed) {
			if(connection.sql_error.defined())
				services._throw(connection.sql_error);
			services._throw(connection.error);
		}
	}

private: // private funcs

	const char *preprocess_statement(Connection& connection, 
		const char *astatement, Query_lobs &lobs) {
		size_t statement_size=strlen(astatement);
		SQL_Driver_services& services=*connection.services;

		char *result=(char *)services.malloc_atomic(statement_size
			+MAX_STRING // in case of short 'strings'
			+11/* returning */+6/* into */+(MAX_LOB_NAME_LENGTH+2/*:, */)*2/*ret into*/*MAX_IN_LOBS
			+1);
		const char *o=astatement;

		// /**xxx**/'literal' -> EMPTY_CLOB_FUNC_CALL
		char *n=result;
		while(*o) {
			if(
				o[0]=='/' &&
				o[1]=='*' && 
				o[2]=='*') { // name start
				const char* saved_o=o;
				o+=3;
				const char *name_begin=o;
				while(*o)
					if(
						o[0]=='*' &&
						o[1]=='*' &&
						o[2]=='/' &&
						o[3]=='\'') { // name end
						saved_o=0; // found, marking that
						const char *name_end=o;
						o+=4;
						Query_lobs::Item &item=lobs.items[lobs.count++];
						item.name_ptr=name_begin; item.name_size=name_end-name_begin;
						item.data_ptr=(char *)services.malloc_atomic(statement_size/*max*/); item.data_size=0;

						const char *start=o;
						bool escaped=false;
						while(*o && !(o[0]=='\'' && o[1]!='\'' && !escaped)) {
							escaped=o[0]=='\'' && o[1]=='\'';
							if(escaped) {
								// write pending, skip "\" or "'"
								if(size_t size=o-start) {
									memcpy(item.data_ptr+item.data_size, start, size);
									item.data_size+=size;
								}
								start=++o;
							} else
								o++;
						}
						if(size_t size=o-start) {
							memcpy(item.data_ptr+item.data_size, start, size);
							item.data_size+=size;
						}
						if(*o)
							o++; // skip "'"

						n+=sprintf(n, EMPTY_CLOB_FUNC_CALL);
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

		if(lobs.count) {
			int i;
			n+=sprintf(n, " returning ");
			for(i=0; i<lobs.count; i++) {
				if(i)
					*n++=',';
				n+=sprintf(n, "%.*s", lobs.items[i].name_size, lobs.items[i].name_ptr);
			}
			n+=sprintf(n, " into ");
			for(i=0; i<lobs.count; i++) {
				if(i)
					*n++=',';
				n+=sprintf(n, ":%.*s", lobs.items[i].name_size, lobs.items[i].name_ptr);
			}
		}

		return result;
	}

	void execute_prepared(
		Connection& connection, 
		const char *statement, OCIStmt *stmthp, Query_lobs &lobs, 
		unsigned long offset, unsigned long limit, 
		SQL_Driver_query_event_handlers& handlers) {

		ub2 stmt_type=0; // UNKNOWN
	/*
		//gpfs on sun. paf 000818
		//Zanyway, this is needed before. 
		check(connection, "get stmt type", OCIAttrGet(
			(dvoid *)stmthp, (ub4)OCI_HTYPE_STMT, (ub1 *)&stmt_type, 
			(ub4 *)0, OCI_ATTR_STMT_TYPE, connection.errhp));
	*/

		while(isspace((unsigned char)*statement)) 
			statement++;
		if(strncasecmp(statement, "select", 6)==0) 
			stmt_type=OCI_STMT_SELECT;
		else if(strncasecmp(statement, "insert", 6)==0)
			stmt_type=OCI_STMT_INSERT;
		else if(strncasecmp(statement, "update", 6)==0)
			stmt_type=OCI_STMT_UPDATE;

		sword status=OCIStmtExecute(connection.svchp, stmthp, connection.errhp, 
			(ub4)stmt_type==OCI_STMT_SELECT?0:1, (ub4)0, 
			(OCISnapshot *)NULL, 
			(OCISnapshot *)NULL, (ub4)OCI_DEFAULT);

		if(status!=OCI_NO_DATA)
			check(connection, "execute", status);

		{
			for(int i=0; i<lobs.count; i++) 
				if(ub4 bytes_to_write=lobs.items[i].data_size) {
					Query_lobs::return_rows *rows=&lobs.items[i].rows;
					for(int r=0; r<rows->count; r++) {
						OCILobLocator *locator=rows->row[r].locator;
						check(connection, "lobwrite", OCILobWrite (
							connection.svchp, connection.errhp, 
							locator, &bytes_to_write, 1, 
							(dvoid *)lobs.items[i].data_ptr, (ub4)bytes_to_write, OCI_ONE_PIECE, 
							(dvoid *)0, 0, (ub2)0, 
							(ub1) SQLCS_IMPLICIT));
					}
				}
		}
		
		switch(stmt_type) {
		case OCI_STMT_SELECT:
			fetch_table(connection,
				stmthp, offset, limit, 
				handlers);
			break;
		default:
		/*
		case OCI_STMT_INSERT:
		case OCI_STMT_UPDATE:
		*/
			break;
		}
	}

	void fetch_table(Connection& connection, 
		OCIStmt *stmthp, unsigned long offset, unsigned long limit, 
		SQL_Driver_query_event_handlers& handlers) 
	{
		const char* cstrClientCharset=connection.options.cstrClientCharset;
		SQL_Driver_services& services=*connection.services;

		ub4 prefetch_rows=100;
		check(connection, "AttrSet prefetch-rows", OCIAttrSet( 
			(dvoid *)stmthp, (ub4)OCI_HTYPE_STMT, 
			(dvoid *)&prefetch_rows, (ub4)0, 
			(ub4)OCI_ATTR_PREFETCH_ROWS, (OCIError *)connection.errhp));

		ub4 prefetch_mem_size=100*0x400;
		check(connection, "AttrSet prefetch-memory", OCIAttrSet( 
			(dvoid *)stmthp, (ub4)OCI_HTYPE_STMT, 
			(dvoid *)&prefetch_mem_size, (ub4)0, 
			(ub4)OCI_ATTR_PREFETCH_MEMORY, (OCIError *)connection.errhp));

		OCIParam          *mypard;
		ub2                    dtype;
		const char*           col_name;

		struct Col {
			ub2 type;
			char *str;
			OCILobLocator *var;
			OCIDefine *def;
			sb2 indicator;
		} cols[MAX_COLS]={0};
		int column_count=0;

		bool failed=false;
		jmp_buf saved_mark; memcpy(saved_mark, connection.mark, sizeof(jmp_buf));
		if(setjmp(connection.mark)) {
			failed=true;
			goto cleanup;
		} else {
			// idea of preincrementing is that at error time all handles would free up
			while(++column_count<=MAX_COLS) {
				/* get next descriptor, if there is one */
				if(OCIParamGet(stmthp, OCI_HTYPE_STMT, connection.errhp, (void **)&mypard, 
					(ub4) column_count)!=OCI_SUCCESS) {
					--column_count;
					break;
				}
				
				/* Retrieve the data type attribute */
				check(connection, "get type", OCIAttrGet(
					(dvoid*) mypard, (ub4)OCI_DTYPE_PARAM, 
					(dvoid*) &dtype, (ub4 *)0, (ub4)OCI_ATTR_DATA_TYPE, 
					(OCIError *)connection.errhp));
				
				/* Retrieve the column name attribute */
				ub4 col_name_len;
				check(connection, "get name", OCIAttrGet(
					(dvoid*) mypard, (ub4)OCI_DTYPE_PARAM, 
					(dvoid**) &col_name, (ub4 *) &col_name_len, (ub4)OCI_ATTR_NAME, 
					(OCIError *)connection.errhp));
				// transcode to $request:charset from connect-string?client_charset
				if(cstrClientCharset) {
					services.transcode(col_name, col_name_len,
						col_name, col_name_len,
						cstrClientCharset,
						services.request_charset());
				}

				Col& col=cols[column_count-1];
				{
					size_t length=(size_t)col_name_len;
					char *ptr=(char *)services.malloc_atomic(length+1);
					if( connection.options.bLowerCaseColumnNames ) 
						tolower_str(ptr, col_name, length);
					else
						memcpy(ptr, col_name, length);						
					ptr[length]=0;
					check(connection, handlers.add_column(connection.sql_error, ptr, length));
				}
				
				ub2 coerce_type=dtype;
				sb4 size=0;
				void *ptr;
				
				switch(dtype) {
				case SQLT_CLOB: 
					{
						check(connection, "alloc output var desc", OCIDescriptorAlloc(
							(dvoid *)connection.envhp, (dvoid **)(ptr=&col.var), 
							(ub4)OCI_DTYPE_LOB, 
							0, (dvoid **)0));
						
						size=0;
						break;
					}
				default:
					coerce_type=SQLT_STR;
					char*& buf=connection.fetch_buffers[column_count-1];
					ptr=buf; // get cached buffer
					if(!ptr) // allocate if needed, caching it
						ptr=buf=(char *)services.malloc_atomic(MAX_OUT_STRING_LENGTH+1/*terminator*/);
					col.str=(char*)ptr;
					size=MAX_OUT_STRING_LENGTH;
					break;
				}
				
				col.type=coerce_type;
				
				// http://i/docs/oracle/server.804/a58234/oci_func.htm#449680
				//   this call implicitly allocates the define handle
				// http://sunsite.eunnet.net/documentation/oracle.8.0.4/server.804/a58234/basics.htm
				//   when a statement handle is freed, any bind and define handles associated with it 
				//   are also freed
				col.def=0; check(connection, "DefineByPos", OCIDefineByPos(
					stmthp, &col.def, connection.errhp, 
					column_count, (ub1 *) ptr, size, 
					coerce_type, (dvoid *) &col.indicator, 
					(ub2 *)0, (ub2 *)0, OCI_DEFAULT));
			}
			
			check(connection, handlers.before_rows(connection.sql_error));
			
			for(unsigned long row=0; !limit||row<offset+limit; row++) {
				sword status=OCIStmtFetch(stmthp, connection.errhp, (ub4)1,  (ub4)OCI_FETCH_NEXT, 
					(ub4)OCI_DEFAULT);
				if(status==OCI_NO_DATA)
					break;
				check(connection, "fetch", status);

				if(row>=offset) {
					check(connection, handlers.add_row(connection.sql_error));
					for(int i=0; i<column_count; i++) {
						size_t length=0;
						char* strm=0;

						sb2 indicator=cols[i].indicator;
						if(indicator!=-1) { // not NULL
							if(indicator!=0)
								fail(connection, indicator<0?
									"column return buffer overflow, additionally size too big to be returned in 'indicator'"
									: "column return buffer overflow");

							switch(cols[i].type) {
							case SQLT_CLOB: 
								{
									ub4   offset=1;
									OCILobLocator *var=(OCILobLocator *)cols[i].var;
									size_t read_size=0;
									strm=(char*)services.malloc_atomic(1/*for terminator*/); // set type of memory block
									do {
										char buf[MAX_STRING*10];
										ub4   amtp=0/*to be read in stream mode*/;
										// http://i/docs/oracle/server.804/a58234/oci_func.htm#427818
										status=OCILobRead(connection.svchp, connection.errhp, 
											var, &amtp, offset, (dvoid *)buf, 
											sizeof(buf), 
											(dvoid *)0, 0, 
											(ub2)0, (ub1)SQLCS_IMPLICIT);
                                        if(status!=OCI_SUCCESS && status!=OCI_NEED_DATA)
											check(connection, "lobread", status);

										strm=(char*)services.realloc(strm, read_size+amtp+1/*for termintator*/);
										memcpy(strm+read_size, buf, amtp);
										read_size+=amtp;
										offset+=amtp;
									} while(status==OCI_NEED_DATA);

									length=(size_t)read_size;
									strm[length]=0;
									break;
								}
							default:
								if(const char *value=cols[i].str) {
									length=strlen(value);
									strm=(char*)services.malloc_atomic(length+1);
									memcpy(strm, value, length+1);
								} else {
									length=0;
									strm=0;
								}
								break;
							}
						}

						const char* str=strm;
						if(str && length)
						{
							// transcode to $request:charset from connect-string?client_charset
							if(cstrClientCharset)
								services.transcode(str, length,
									str, length,
									cstrClientCharset,
									services.request_charset());
						}

						check(connection, handlers.add_row_cell(connection.sql_error, str, length));
					}
				}
			}
		}

cleanup: // no check call after this point!
		for(int i=0; i<column_count; i++) {
			switch(cols[i].type) {
			case SQLT_CLOB:
				/* free var locator */
				OCIDescriptorFree((dvoid *) cols[i].var, (ub4)OCI_DTYPE_LOB);
				break;
			default:
				break;
			}
		}

		if(failed) // need rethrow?
			longjmp(saved_mark, 1);
	}

private: // conn client library funcs
	
	friend void fail(Connection& connection, const char *msg);
	friend void check(Connection& connection, const char *step, sword status);
	friend sb4 cbf_get_data(dvoid *ctxp, 
		OCIBind *bindp, 
		ub4 iter, ub4 index, 
		dvoid **bufpp, 
		ub4 **alenp, 
		ub1 *piecep, 
		dvoid **indpp, 
		ub2 **rcodepp);


#define OCI_DECL(name, params) \
	typedef sword (*t_OCI##name)params; t_OCI##name OCI##name

	OCI_DECL(Initialize, (ub4 mode, dvoid *ctxp, 
		dvoid * (*malocfp)(dvoid *ctxp, size_t size),
		dvoid * (*ralocfp)(dvoid *ctxp, dvoid *memptr, size_t newsize),
		void (*mfreefp)(dvoid *ctxp, dvoid *memptr) ));

	OCI_DECL(EnvInit, (OCIEnv **envp, ub4 mode, 
		size_t xtramem_sz, dvoid **usrmempp)); 
		
	OCI_DECL(AttrGet, (CONST dvoid *trgthndlp, ub4 trghndltyp, 
		dvoid *attributep, ub4 *sizep, ub4 attrtype, 
		OCIError *errhp));
	
	OCI_DECL(AttrSet, (dvoid *trgthndlp, ub4 trghndltyp, dvoid *attributep,
								ub4 size, ub4 attrtype, OCIError *errhp));

	OCI_DECL(BindByPos, (OCIStmt *stmtp, OCIBind **bindp, OCIError *errhp,
		ub4 position, dvoid *valuep, sb4 value_sz,
		ub2 dty, dvoid *indp, ub2 *alenp, ub2 *rcodep,
		ub4 maxarr_len, ub4 *curelep, ub4 mode));

	OCI_DECL(BindByName, (OCIStmt *stmtp, OCIBind **bindp, OCIError *errhp,
		text* placeholder, sb4 placeh_len, dvoid *valuep, sb4 value_sz,
		ub2 dty, dvoid *indp, ub2 *alenp, ub2 *rcodep,
		ub4 maxarr_len, ub4 *curelep, ub4 mode));

	OCI_DECL(BindDynamic, (OCIBind *bindp, OCIError *errhp, dvoid *ictxp,
		OCICallbackInBind icbfp, dvoid *octxp,
		OCICallbackOutBind ocbfp));
	
	OCI_DECL(DefineByPos, (OCIStmt *stmtp, OCIDefine **defnp, OCIError *errhp,
		ub4 position, dvoid *valuep, sb4 value_sz, ub2 dty,
		dvoid *indp, ub2 *rlenp, ub2 *rcodep, ub4 mode));
	
	OCI_DECL(DescriptorAlloc, (CONST dvoid *parenth, dvoid **descpp, 
		CONST ub4 type, CONST size_t xtramem_sz, 
		dvoid **usrmempp));
	
	OCI_DECL(DescriptorFree, (dvoid *descp, CONST ub4 type));

	
	OCI_DECL(ErrorGet, (dvoid *hndlp, ub4 recordno, OraText *sqlstate,
                       sb4 *errcodep, OraText *bufp, ub4 bufsiz, ub4 type));

	OCI_DECL(HandleAlloc, (CONST dvoid *parenth, dvoid **hndlpp, CONST ub4 type, 
                       CONST size_t xtramem_sz, dvoid **usrmempp));

	OCI_DECL(HandleFree, (dvoid *hndlp, CONST ub4 type));
					   
	OCI_DECL(LobGetLength, (OCISvcCtx *svchp, OCIError *errhp, 
                          OCILobLocator *locp,
                          ub4 *lenp));

	OCI_DECL(LobRead, (OCISvcCtx *svchp, OCIError *errhp, OCILobLocator *locp,
                     ub4 *amtp, ub4 offset, dvoid *bufp, ub4 bufl, 
                     dvoid *ctxp, sb4 (*cbfp)(dvoid *ctxp, 
                                              CONST dvoid *bufp, 
                                              ub4 len, 
                                              ub1 piece),
                     ub2 csid, ub1 csfrm));

	OCI_DECL(LobWrite, (OCISvcCtx *svchp, OCIError *errhp, OCILobLocator *locp,
                      ub4 *amtp, ub4 offset, dvoid *bufp, ub4 buflen, 
                      ub1 piece, dvoid *ctxp, 
                      sb4 (*cbfp)(dvoid *ctxp, 
                                  dvoid *bufp, 
                                  ub4 *len, 
                                  ub1 *piece),
                      ub2 csid, ub1 csfrm));

	OCI_DECL(ParamGet, (CONST dvoid *hndlp, ub4 htype, OCIError *errhp, 
                     dvoid **parmdpp, ub4 pos));

	OCI_DECL(ServerAttach, (OCIServer *srvhp, OCIError *errhp,
                          CONST OraText *dblink, sb4 dblink_len, ub4 mode));

	OCI_DECL(ServerDetach, (OCIServer *srvhp, OCIError *errhp, ub4 mode));

	OCI_DECL(SessionBegin, (OCISvcCtx *svchp, OCIError *errhp, OCISession *usrhp,
                          ub4 credt, ub4 mode));

	OCI_DECL(SessionEnd, (OCISvcCtx *svchp, OCIError *errhp, OCISession *usrhp, 
                         ub4 mode));

	OCI_DECL(StmtExecute, (OCISvcCtx *svchp, OCIStmt *stmtp, OCIError *errhp, 
                         ub4 iters, ub4 rowoff, CONST OCISnapshot *snap_in, 
                         OCISnapshot *snap_out, ub4 mode));

	OCI_DECL(StmtFetch, (OCIStmt *stmtp, OCIError *errhp, ub4 nrows, 
                        ub2 orientation, ub4 mode));

	OCI_DECL(StmtPrepare, (OCIStmt *stmtp, OCIError *errhp, CONST OraText *stmt,
                          ub4 stmt_len, ub4 language, ub4 mode));

	OCI_DECL(TransCommit, (OCISvcCtx *svchp, OCIError *errhp, ub4 flags));

	OCI_DECL(TransRollback, (OCISvcCtx *svchp, OCIError *errhp, ub4 flags));

private: // conn client library funcs linking

	const char *dlink(const char *dlopen_file_spec) {
		if(lt_dlinit())
			return lt_dlerror();
        lt_dlhandle handle=lt_dlopen(dlopen_file_spec);
        if(!handle)
			return lt_dlerror(); //"can not open the dynamic link module";

		#define DSLINK(name, action) \
			name=(t_##name)lt_dlsym(handle, #name); \
				if(!name) \
					action;

		#define OCI_LINK(name) DSLINK(OCI##name, return "function OCI" #name " was not found")
		
		OCI_LINK(Initialize);
		OCI_LINK(EnvInit);
		OCI_LINK(AttrGet);		OCI_LINK(AttrSet);
		OCI_LINK(BindByPos);		OCI_LINK(BindByName);	OCI_LINK(BindDynamic);
		OCI_LINK(DefineByPos);
		OCI_LINK(DescriptorAlloc);		OCI_LINK(DescriptorFree);
		OCI_LINK(ErrorGet);
		OCI_LINK(HandleAlloc);		OCI_LINK(HandleFree);
		OCI_LINK(LobGetLength);
		OCI_LINK(LobRead);		OCI_LINK(LobWrite);
		OCI_LINK(ParamGet);
		OCI_LINK(ServerAttach);		OCI_LINK(ServerDetach);
		OCI_LINK(SessionBegin);		OCI_LINK(SessionEnd);
		OCI_LINK(StmtExecute);		OCI_LINK(StmtFetch);		OCI_LINK(StmtPrepare);
		OCI_LINK(TransCommit);		OCI_LINK(TransRollback);

		return 0;
	}

} *OracleSQL_driver;

void check(Connection& connection, const char *step, sword status) {

	const char *msg;
	char reason[MAX_STRING/2];

	switch (status) {
	case OCI_SUCCESS: // hurrah
	case OCI_SUCCESS_WITH_INFO:		// ignoring. example: count(column) when column contains NULLs, 
														// count() not counting them and gives that status
		return;
	case OCI_ERROR:
		{
			sb4 errcode;
			if(OracleSQL_driver->OCIErrorGet((dvoid *)connection.errhp, (ub4)1, (text *)NULL, &errcode, 
				(text *)reason, (ub4)sizeof(reason), OCI_HTYPE_ERROR)==OCI_SUCCESS) {
				msg=reason;

				// transcode to $request:charset from connect-string?client_charset
				if(const char* cstrClientCharset=connection.options.cstrClientCharset) {
					if(msg) {
						if(size_t msg_length=strlen(msg)) {
							connection.services->transcode(msg, msg_length,
								msg, msg_length,
								cstrClientCharset,
								connection.services->request_charset());
						}
					}
				}
			} else
				msg="[can not get error description]";
			break;
		}
	case OCI_NEED_DATA:
		msg="NEED_DATA"; break;
	case OCI_NO_DATA:
		msg="NODATA"; break;
	case OCI_INVALID_HANDLE:
		msg="INVALID_HANDLE"; break;
	case OCI_STILL_EXECUTING:
		msg="STILL_EXECUTE"; break;
	case OCI_CONTINUE:
		msg="CONTINUE"; break;
	default:
		msg="unknown"; break;
	}

	snprintf(connection.error, sizeof(connection.error), "%s (%s, %d)", 
		msg, step, (int)status);
	longjmp(connection.mark, 1);
}

void fail(Connection& connection, const char* msg) {
	snprintf(connection.error, sizeof(connection.error), "%s", msg);
	longjmp(connection.mark, 1);
}

void check(Connection& connection, bool error) {
	if(error)
		longjmp(connection.mark, 1);
}

/* ----------------------------------------------------------------- */
/* Intbind callback that does not do any data input.                 */
/* ----------------------------------------------------------------- */
sb4 cbf_no_data(
				dvoid* /*ctxp*/, 
				OCIBind* /*bindp*/, 
				ub4 /*iter*/, ub4 /*index*/, 
				dvoid **bufpp, 
				ub4 *alenpp, 
				ub1 *piecep, 
				dvoid **indpp) {
	*bufpp=(dvoid *)0;
	*alenpp=0;
	static sb2 null_ind=-1;
	*indpp=(dvoid *) &null_ind;
	*piecep=OCI_ONE_PIECE;
	
	return OCI_CONTINUE;
}

/* ----------------------------------------------------------------- */
/* Outbind callback for returning data.                              */
/* ----------------------------------------------------------------- */
static sb4 cbf_get_data(dvoid *ctxp, 
				 OCIBind *bindp, 
				 ub4 /*iter*/, ub4 index, 
				 dvoid **bufpp, 
				 ub4 **alenp, 
				 ub1 *piecep, 
				 dvoid **indpp, 
				 ub2 **rcodepp) {
	Query_lobs::Item& context=*static_cast<Query_lobs::Item*>(ctxp);

	if(index==0) {
		static ub4  rows;
		check(*context.connection, "AttrGet cbf_get_data ROWS_RETURNED", 
			OracleSQL_driver->OCIAttrGet(
				(CONST dvoid *) bindp, OCI_HTYPE_BIND, (dvoid *)&rows, 
				(ub4 *)sizeof(ub2), OCI_ATTR_ROWS_RETURNED, context.connection->errhp))	;
		context.rows.count=(ub2)rows;
		context.rows.row=(Query_lobs::return_rows::return_row *)
			context.connection->services->malloc_atomic(sizeof(Query_lobs::return_rows::return_row)*rows);
	}

	Query_lobs::return_rows::return_row &var=context.rows.row[index];

	check(*context.connection, "alloc output var desc dynamic", OracleSQL_driver->OCIDescriptorAlloc(
		(dvoid *) context.connection->envhp, (dvoid **)&var.locator, 
		(ub4)OCI_DTYPE_LOB, 
		0, (dvoid **)0));

	*bufpp=var.locator;
	*alenp=&var.len;
	*indpp=(dvoid *) &var.ind;
	*piecep=OCI_ONE_PIECE;
	*rcodepp=&var.rcode;
	
	return OCI_CONTINUE;
}

static void tolower_str(char *out, const char *in, size_t size) {
	while(size--)
		*out++=(char)tolower(*in++);
}
static void toupper_str(char *out, const char *in, size_t size) {
	while(size--)
		*out++=(char)toupper(*in++);
}


extern "C" SQL_Driver *SQL_DRIVER_CREATE() {
	return OracleSQL_driver=new OracleSQL_Driver();
}
