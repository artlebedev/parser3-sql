// Note: must include AFXDB.H first

#if _MSC_VER > 1000
#pragma once
#endif

#undef AFX_DATA
#define AFX_DATA AFX_DB_DATA

/////////////////////////////////////////////////////////////////////////////
// _AFX_DB_STATE

#undef AFX_DATA
#define AFX_DATA

class _AFX_DB_STATE : public CNoTrackObject
{
public:
	// MFC/DB global data
	HENV m_henvAllConnections;      // per-app HENV (CDatabase)
	int m_nAllocatedConnections;    // per-app reference to HENV above
};

EXTERN_PROCESS_LOCAL(_AFX_DB_STATE, _afxDbState)

#undef AFX_DATA
#define AFX_DATA

/////////////////////////////////////////////////////////////////////////////
