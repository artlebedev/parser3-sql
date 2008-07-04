/** @file
	Parser ODBC driver.

	Copyright(c) 2001, 2003 ArtLebedev Group (http://www.artlebedev.com)

	Author: Alexandr Petrosian <paf@design.ru> (http://paf.design.ru)
*/
static const char *RCSId="$Id: parser3odbc.C,v 1.34 2008/07/04 16:20:02 misha Exp $"; 

#ifndef _MSC_VER
#	error compile ISAPI module with MSVC [no urge for now to make it autoconf-ed (PAF)]
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

#include "pa_sql_driver.h"

#define WINVER 0x0400
#include <AFXDB.H>

// defines

#define MAX_COLS 500

#define MAX_STRING 0x400
#define MAX_NUMBER 40

// new in MSSQL2000, no MFC constants
#ifndef SQL_NVARCHAR
#define SQL_NVARCHAR (-9)
#endif
#ifndef SQL_NTEXT 
#define SQL_NTEXT (-10)
#endif
#ifndef SQL_SMALLDATETIME
#define SQL_SMALLDATETIME 11
#endif
// create table test (id int, a smalldatetime, b ntext, c nvarchar(100))

#define snprintf _snprintf
#ifndef strncasecmp
#	define strncasecmp _strnicmp
#endif
#ifndef strcasecmp
#	define strcasecmp _stricmp
#endif

static char *lsplit(char *string, char delim){
	if(string){
		if(char* v=strchr(string, delim)){
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

static void toupper_str(char *out, const char *in, size_t size){
	while(size--)
		*out++=(char)toupper(*in++);
}

struct modified_statement {
	const char* statement;
	bool limit;
	bool offset;
};

// todo: MySQL, SQLite, PgSQL (add LIMIT at the end of statement)
struct SQL {
	enum SQLEnum {
		Undefined,
		MSSQL,
		Pervasive,
		FireBird
	};
};

struct Connection {
	SQL_Driver_services* services;

	CDatabase* db;
	const char* client_charset;
	SQL::SQLEnum sql_specific;
	bool autocommit;
};

/**
	ODBC server driver
*/
class ODBC_Driver : public SQL_Driver {
public:

	ODBC_Driver() : SQL_Driver() {
	}

	/// get api version
	int api_version() { return SQL_DRIVER_API_VERSION; }

	const char *initialize(char *dlopen_file_spec) { return 0; }
	/**	connect
		@param url
			format: @b DSN=dsn;UID=user;PWD=password? (ODBC connect string)
				ClientCharset=charset&	// transcode with parser
				autocommit=1&		// 0 -- disable auto commit
				FastOffsetSearch=0
			WARNING: must be used only to connect, for buffer doesn't live long
	*/

	void connect(
			char *url, 
			SQL_Driver_services& services, 
			void **connection_ref ///< output: Connection*
	){
		Connection& connection=*(Connection *)services.malloc(sizeof(Connection));
		*connection_ref=&connection;
		connection.services=&services;
		connection.client_charset=0;
		connection.sql_specific=SQL::Undefined;
		connection.autocommit=true;

		size_t url_length=strlen(url);
		char *options=lsplit(url, '?');

		// todo: analize connect string and if 'SQL Server' found, modify query and add TOP into SELECTs

		while(options){
			if(char *key=lsplit(&options, '&')){
				if(*key){
					if(char *value=lsplit(key, '=')){
						if(strcmp(key, "ClientCharset")==0){
							toupper_str(value, value, strlen(value));
							connection.client_charset=value;
						} else if(strcasecmp(key, "autocommit")==0){
							if(atoi(value)==0)
								connection.autocommit=false;
						} else if(strcmp(key, "SQL")==0){
							if(strcasecmp(value, "MSSQL")==0){
								connection.sql_specific=SQL::MSSQL;
							} else if(strcasecmp(value, "Pervasive")==0){
								connection.sql_specific=SQL::Pervasive;
							} else if(strcasecmp(value, "FireBird")==0){
								connection.sql_specific=SQL::FireBird;
							} else {
								services._throw("unknown value of SQL option was specified" /*key*/);
							}
						} else
							services._throw("unknown connect option" /*key*/);
					} else 
						services._throw("connect option without =value" /*key*/);
				}
			}
		}

		TRY {
			connection.db=new CDatabase();
			connection.db->OpenEx(url, CDatabase::noOdbcDialog);
			connection.db->BeginTrans();
		} 
		CATCH_ALL (e) {
			_throw(services, e);
		}
		END_CATCH_ALL
	}

	void disconnect(void *aconnection){
		Connection& connection=*static_cast<Connection*>(aconnection);
		TRY
			delete connection.db;
			connection.db=0;
		CATCH_ALL (e) {
			// nothing
		}
		END_CATCH_ALL
	}

	void commit(void *aconnection){
		Connection& connection=*static_cast<Connection*>(aconnection);
		TRY
			connection.db->CommitTrans();
			connection.db->BeginTrans();
		CATCH_ALL (e) {
			_throw(*connection.services, e);
		}
		END_CATCH_ALL
	}

	void rollback(void *aconnection){
		Connection& connection=*static_cast<Connection*>(aconnection);
		TRY
			connection.db->Rollback();
			connection.db->BeginTrans();
		CATCH_ALL (e) {
			_throw(*connection.services, e);
		}
		END_CATCH_ALL
	}

	bool ping(void *connection){
		return true;
	}

	const char* quote(void *aconnection, const char *from, unsigned int length){
		Connection& connection=*static_cast<Connection*>(aconnection);
		char *result=(char*)connection.services->malloc_atomic(length*2+1);
		char *to=result;
		while(length--){
			if(*from=='\'') { // ' -> ''
				*to++='\'';
			}
			*to++=*from++;
		}
		*to=0;
		return result;
	}

	void query(void *aconnection, 
			const char *astatement, 
			size_t placeholders_count,
			Placeholder* placeholders, 
			unsigned long offset,
			unsigned long limit,
			SQL_Driver_query_event_handlers& handlers
	){
		Connection& connection=*static_cast<Connection*>(aconnection);
		CDatabase *db=connection.db;
		SQL_Driver_services& services=*connection.services;

		if(placeholders_count>0)
			services._throw("bind variables not supported (yet)");

		while(isspace((unsigned char)*astatement)) 
			astatement++;

		modified_statement mstatement=_preprocess_statement(connection, astatement, offset, limit);
		const char* statement=mstatement.statement;

		const char* client_charset=connection.client_charset;
		const char* request_charset=services.request_charset();
		bool transcode_needed=(client_charset && strcmp(client_charset, request_charset)!=0);
		if(transcode_needed){
			// transcode query from $request:charset to ?ClientCharset
			size_t length=strlen(statement);
			services.transcode(statement, length,
				statement, length,
				request_charset,
				client_charset);
		}

		TRY {
			// mk:@MSITStore:C:\Program%20Files\Microsoft%20SQL%20Server\80\Tools\Books\adosql.chm::/adoprg02_4g33.htm
			// or http://msdn.microsoft.com/en-us/library/aa905899(SQL.80).aspx
			// Server cursors are created only for statements that begin with: 
			// SELECT
			// EXEC[ute] procedure_name
			// call procedure_name
			// mk:@MSITStore:C:\Program%20Files\Microsoft%20SQL%20Server\80\Tools\Books\odbcsql.chm::/od_6_035_5dnp.htm
			// The ODBC CALL escape sequence for calling a procedure is:
			// {[?=]call procedure_name[([parameter][,[parameter]]...)]}
			if(
				strncasecmp(statement, "SELECT", 6)==0
				|| strncasecmp(statement, "EXEC", 4)==0
				|| strncasecmp(statement, "call", 4)==0
				|| strncasecmp(statement, "{", 1)==0
			){
				CRecordset rs(db);
				DWORD options=CRecordset::executeDirect|CRecordset::readOnly;
				TRY {
					rs.Open(
						CRecordset::forwardOnly,
						statement,
						options
					);
				} CATCH_ALL (e) {
					// could not fetch a table
					TRY {
						// then try resultless query
						db->ExecuteSQL(statement);
						// OK then
						return;
					} CATCH_ALL (e2) {
						// still nothing good
						_throw(services, e); // throw ORIGINAL exception
					} END_CATCH_ALL
				} END_CATCH_ALL

				int column_count=rs.GetODBCFieldCount();
				if(!column_count)
					services._throw("result contains no columns");

				if(column_count>MAX_COLS)
					column_count=MAX_COLS;

				SWORD column_types[MAX_COLS];
				bool transcode_column[MAX_COLS];

				SQL_Error sql_error;
#define CHECK(afailed) if(afailed) services._throw(sql_error)

				for(int i=0; i<column_count; i++){
					CString string;
					CODBCFieldInfo fieldinfo;
					rs.GetODBCFieldInfo(i, fieldinfo);
					column_types[i]=fieldinfo.m_nSQLType;
					switch(fieldinfo.m_nSQLType){
						case SQL_NUMERIC:
						case SQL_DECIMAL:
						case SQL_INTEGER:
						case SQL_SMALLINT:
						case SQL_FLOAT:
						case SQL_REAL:
						case SQL_DOUBLE:
						case SQL_DATETIME:
						case SQL_SMALLDATETIME:
						case SQL_BIGINT:
						case SQL_TINYINT:
							transcode_column[i]=false;
							break;
						default:
							transcode_column[i]=transcode_needed;
							break;
					}
					size_t length=fieldinfo.m_strName.GetLength();
					char *str=0;
					if(length){
						str=(char*)services.malloc_atomic(length+1);
						memcpy(str, (char*)LPCTSTR(fieldinfo.m_strName), length+1);

						// transcode column name from ?ClientCharset to $request:charset
						if(transcode_needed){
							services.transcode(str, length,
								str, length,
								client_charset,
								request_charset);
						}
					}
					CHECK(handlers.add_column(sql_error, str, length));
				}

				CHECK(handlers.before_rows(sql_error));
				
				// skip offset rows
				if(offset && !mstatement.offset){
					unsigned long row=offset;
					while(!rs.IsEOF() && row--)
						rs.MoveNext();
				}

				unsigned long row=0;
				CDBVariant v;
				CString s;
				while(!rs.IsEOF() && (limit==SQL_NO_LIMIT || row<limit)){
					CHECK(handlers.add_row(sql_error));
					for(int i=0; i<column_count; i++){
						size_t length;
						char* str;
						switch(column_types[i]){
							//case xBOOL:
							//case SQL_DATETIME: << default: handles that more properly (?)
							case SQL_BINARY: 
							case SQL_VARBINARY:
							case SQL_LONGVARBINARY:
							case SQL_SMALLDATETIME:
							//case SQL_NVARCHAR:	// mfc 7.1 has errors with nvarchar(length): SQLGetData in dbcore.cpp truncates last byte for unknown reason.
													// could be fixed by uncommenting this and handing DBVT_WSTRING inside, but it's UNICODE
								rs.GetFieldValue(i, v);
								getFromDBVariant(services, v, str, length);
								break;
							default:
								rs.GetFieldValue(i, s);
								getFromString(services, s, str, length);
								break;
						}

						// transcode cell value from ?ClientCharset to $request:charset
						if(length && transcode_column[i]){
							services.transcode(str, length,
								str, length,
								client_charset,
								request_charset);
						}

						CHECK(handlers.add_row_cell(sql_error, str, length));
					}
					rs.MoveNext();
					row++;
				}
				
				rs.Close();
			} else {
				db->ExecuteSQL(statement);
			}
		} CATCH_ALL (e) {
			_throw(services, e);
		} END_CATCH_ALL

		if(connection.autocommit)
			commit(aconnection);
	}

private:
	void getFromDBVariant(SQL_Driver_services& services, CDBVariant& v, char*& str, size_t& length){
		switch(v.m_dwType){
		case DBVT_BINARY: /* << would cause problems with current String implementation
				  now falling into NULL case, effectively ignoring such columns [not failing]
			{
				if(length=v.m_pbinary->m_dwDataLength) {
					str=services.malloc_atomic(length+1);
					memcpy(ptr, ::GlobalLock(v.m_pbinary->m_hData), length);
					::GlobalUnlock(v.m_pbinary->m_hData);
				} else 
					str=0;
				break;
			}*/ 
		case DBVT_NULL: // No union member is valid for access. 
			str=0;
			length=0;
			break;
/*		case DBVT_BOOL:
			ptr=v.m_boolVal?"1":"0";
			length=1;
			break;*/
/*		case DBVT_UCHAR:
			length=strlen(ptr=v.m_chVal);
			break;
		case DBVT_SHORT:
			char buf[MAX_NUMBER];
			length=snprintf(HEAPIZE buf, "%d", v.m_iVal);
			break;
*/
/*		case DBVT_LONG: 
			{
				char local_buf[MAX_NUMBER];
				length=snprintf(local_buf, MAX_NUMBER, "%ld", v.m_lVal);
				ptr=services.malloc_atomic(length);
				memcpy(ptr, local_buf, length);
				break;
			}
*/
/*
		case DBVT_SINGLE:
			m_fltVal 
			break;
		case DBVT_DOUBLE m_dblVal 
		case DBVT_STRING m_pstring
*/ 
		case DBVT_DATE:
			{
				char local_buf[MAX_STRING];
				length=snprintf(local_buf, MAX_STRING, 
					"%04d-%02d-%02d %02d:%02d:%02d.%03d",
					v.m_pdate->year, 
					v.m_pdate->month,
					v.m_pdate->day,
					v.m_pdate->hour,
					v.m_pdate->minute,
					v.m_pdate->second,
					v.m_pdate->fraction/1000000); // lexical parser of INCOMING literal choked on times like hh:mm:ss.123000000
				str=(char*)services.malloc_atomic(length+1);
				memcpy(str, local_buf, length+1);
				break;
			}
		default:
			char msg[MAX_STRING];
			snprintf(msg, MAX_STRING, "unknown column return variant type (%d)",
				v.m_dwType);
			services._throw(msg);
		}
	}

	void getFromString(SQL_Driver_services& services, CString& s, char*& astr, size_t& length){
		if(s.IsEmpty()){
			astr=0;
			length=0;
		} else {
			const char *cstr=LPCTSTR(s);
			length=strlen(cstr); //string.GetLength() works wrong with non-string types: 
			astr=(char*)services.malloc_atomic(length+1);
			memcpy(astr, cstr, length+1);
		}
	}

	modified_statement _preprocess_statement(
			Connection& connection, 
			const char* astatement,
			unsigned long offset,
			unsigned long limit
	){
		modified_statement result={astatement, false, false};

		if(limit!=SQL_NO_LIMIT && connection.sql_specific!=SQL::Undefined && strncasecmp(astatement, "select", 6)==0){
			switch(connection.sql_specific){
				case SQL::MSSQL:
				case SQL::Pervasive: // uses TOP as well
					{
						// add ' TOP limit+offset' after 'SELECT'
						char* statement_limited=(char *)connection.services->malloc_atomic(
								strlen(astatement)
								+MAX_NUMBER
								+5/* TOP */
								+1/*terminator*/
							);

						result.limit=true; // with TOP we can't skip offset records easily
						result.statement=statement_limited;

						snprintf(statement_limited, MAX_NUMBER+11, "SELECT TOP %u", (limit)?limit+offset:0/*no reasons to skip something if we need 0 rows*/);

						astatement+=6;/*skip 'select'*/
						strcat(statement_limited, astatement);

						//connection.services->_throw(result.statement);
						break;
					}
				case SQL::FireBird:
					{
						// add ' FIRST (limit) SKIP (offset)' after 'SELECT'
						char* statement_limited=(char *)connection.services->malloc_atomic(
								strlen(astatement)
								+MAX_NUMBER*2
								+9/* FIRST ()*/
								+offset?8:0/* SKIP ()*/
								+1/*terminator*/
							);

						result.limit=true;
						result.offset=true;
						result.statement=statement_limited;

						statement_limited+=snprintf(statement_limited, MAX_NUMBER+15, "SELECT FIRST (%u)", limit);
						if(offset && limit/*no reasons to skip something if we need 0 rows*/)
							statement_limited+=snprintf(statement_limited, MAX_NUMBER+8, " SKIP (%u)", offset);

						astatement+=6;/*skip 'select'*/
						strcat((char*)result.statement, astatement);

						//connection.services->_throw(result.statement);
						break;
					}
				default:
					connection.services->_throw("Unknown SQL specifics");
			}
		}
		return result;
	}

	void _throw(SQL_Driver_services& services, CException *e){
		char szCause[MAX_STRING];
		szCause[0]=0;
		e->GetErrorMessage(szCause, MAX_STRING);
		char msg[MAX_STRING];
		snprintf(msg, MAX_STRING, "%s: %s",
			e->GetRuntimeClass()->m_lpszClassName,
			*szCause?szCause:"unknown");
		services._throw(msg);
	}

	void _throw(Connection& connection, long value){
		char msg[MAX_STRING];
		snprintf(msg, MAX_STRING, "%u", value);
		connection.services->_throw(msg);
	}

};

extern "C" SQL_Driver *SQL_DRIVER_CREATE() {
	return new ODBC_Driver();
}
