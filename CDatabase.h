#pragma once
#ifndef _CDATABASE_
#define _CDATABASE_

#include "main.h"

using std::wstring;

namespace ODBCDatabase
{
	using std::exception;

	class CDatabase{
	public:

		CDatabase(const wstring delim = L",");

		~CDatabase();

		bool operator<<(const wstring & os);

		bool ConnectedOk() const;
		
		void operator>>(wstring & str) const;
		
	private:

		/******************************************/
		/* Structure to store information about   */
		/* a column.
		/******************************************/

		struct Binding{
			SQLLEN         cDisplaySize;           /* size to display  */
			WCHAR               *wszBuffer;             /* display buffer   */
			SQLLEN              indPtr;                 /* size or null     */
			BOOL                fChar;                  /* character col?   */
			struct Binding		*sNext;                 /* linked list      */
		};

		void Disconnect();

		void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode);

		void CDatabase::getAnswer(SQLSMALLINT & cCols);

		bool CDatabase::allocateBindings(SQLSMALLINT & cCols);

		bool CDatabase::getTitles();

		CDatabase(CDatabase const&) = delete;
		CDatabase operator=(CDatabase const&) = delete;

	private:
		//2,147,483,647; 4,294,968,320	65536
		//1MB = 1024 * 1024 Bytes
		enum { MAX_WIDTH_OF_DATA_IN_COLOMN = 8 * 1024 * 1024 }; // in characters
		enum{ NULL_SIZE = 6 }; // size of <NULL>

		std::list<Binding>  bindings_;
		
		wstring delim_;

		wstring answer_;
		
		bool connected_;


		SQLHENV     hEnv_; // ODBC Environment handle
		SQLHDBC     hDbc_;
		SQLHSTMT    hStmt_; // ODBC Statement handle
	};

}
#endif