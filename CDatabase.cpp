#include "CDatabase.h"


namespace ODBCDatabase
{

	// mutex to call SQLDriverConnectW. This func crashes in multythread
	boost::recursive_mutex driverConnect;

	CDatabase::CDatabase(const wstring delim)
	{
		connected_ = false;
		delim_ = delim;
		answer_ = L"";
		RETCODE retCode;
		hEnv_ = NULL;
		hDbc_ = NULL;
		hStmt_ = NULL;

		// Allocate an environment
		if( SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv_) == SQL_ERROR )
		{
			LOG(WARNING) << "Unable to allocate an environment handle (can't connect to database)!" << std::endl;
			return;
		}

		// Register this as an application that expects 3.x behavior,
		// you must register something if you use AllocHandle
		retCode = SQLSetEnvAttr(hEnv_, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
		if( retCode != SQL_SUCCESS && retCode != SQL_SUCCESS_WITH_INFO )
		{
			HandleDiagnosticRecord(hEnv_, SQL_HANDLE_ENV, retCode);
			Disconnect();
			return;
		}

		// Allocate a connection
		retCode = SQLAllocHandle(SQL_HANDLE_DBC, hEnv_, &hDbc_);
		if( retCode != SQL_SUCCESS && retCode != SQL_SUCCESS_WITH_INFO )
		{
			HandleDiagnosticRecord(hEnv_, SQL_HANDLE_ENV, retCode);
			Disconnect();
			return;
		}

		// Connect to the driver.  Use the connection string if supplied
		// on the input, otherwise let the driver manager prompt for input.
		
		{
			boost::recursive_mutex::scoped_lock lk(driverConnect);	
			retCode = SQLDriverConnectW(hDbc_, hWnd, (SQLWCHAR *)ConectionString, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
		}
		if( retCode != SQL_SUCCESS && retCode != SQL_SUCCESS_WITH_INFO ){
			HandleDiagnosticRecord(hDbc_, SQL_HANDLE_DBC, retCode);
			Disconnect();
			return;
		}

		connected_ = true;
		VLOG(1) << "DEBUG: ODBC: Connected to db!" << std::endl; 
	}

	void CDatabase::Disconnect()
	{
		connected_ = false;

		VLOG(1) << "DEBUG: ODBC: Disconnected from db!" << std::endl;

		try{
			for( auto & thisBinding : bindings_ )
			{
				//????? ?????? ????????? ?????? ??? wszBuffer, ????? ???????? ??????? free
				if( thisBinding.wszBuffer != NULL )
				{
					//VLOG(1) << "ODBC: free bind!" << std::endl;
					free(thisBinding.wszBuffer);
					thisBinding.wszBuffer = NULL;
				}
			}

			bindings_.clear();

		}catch(exception& e)
		{ 
			LOG(WARNING) << "ERROR: " << e.what();
		}
		

		if( hStmt_ != NULL )
		{
			SQLFreeHandle(SQL_HANDLE_STMT, hStmt_);
		}

		if( hDbc_ != NULL )
		{
			SQLDisconnect(hDbc_);
			SQLFreeHandle(SQL_HANDLE_DBC, hDbc_);
		}

		if( hEnv_ != NULL )
		{
			SQLFreeHandle(SQL_HANDLE_ENV, hEnv_);
		}
	}

	void CDatabase::getAnswer(SQLSMALLINT& cCols)
	{
		try{
			RETCODE	retCode = SQL_SUCCESS;

			bool result;

			// allocate memory for each column 

			result = allocateBindings(cCols);
			if( !result )
			{
				return;
			}

			// get the titles

			/*result = getTitles();
			if( !result )
			{
				return;
			}*/

			// Fetch and display the data

			bool fNoData = false;

			do
			{
				// Fetch a row

				retCode = SQLFetch(hStmt_);

				if( retCode == SQL_ERROR || retCode == SQL_INVALID_HANDLE )
				{
					HandleDiagnosticRecord(hStmt_, SQL_HANDLE_STMT, retCode);
					Disconnect();
					fNoData = true;
				} else if( retCode == SQL_NO_DATA_FOUND )
				{
					fNoData = true;
				} else
				{

					// Display the data.   Ignore truncations

					for(auto & thisBinding: bindings_ )
					{
						if( thisBinding.indPtr != SQL_NULL_DATA )
						{
							answer_ += wstring(thisBinding.wszBuffer) + delim_;
						} else
						{
							answer_ += wstring(L"<NULL>" + delim_);
						}
					}
					answer_.push_back(L'\n');

					// Cleaning up wszBuffer before next Fetch

					for( auto & thisBinding : bindings_ )
					{
						ZeroMemory(thisBinding.wszBuffer, thisBinding.cDisplaySize);
					}
				}
			} while( !fNoData );

			/*for( auto & thisBinding : bindings_ )
			{
				if( thisBinding.wszBuffer )
				{
					free(thisBinding.wszBuffer);
				}
			}

			bindings_.clear();*/

		} catch( exception& e )
		{
			LOG(WARNING) << "ERROR: " << e.what();
		}
	}

	/************************************************************************
	/* allocateBindings:  Get column information and allocate bindings_
	/* for each column.
	/*
	/* Parameters:
	/*      cCols       Number of columns in the result set
	/************************************************************************/
	
	bool CDatabase::allocateBindings(SQLSMALLINT & cCols)
	{
		try {
			Binding         ThisBinding;
			SQLLEN          cchDisplay, ssType;
			//SQLSMALLINT     cchColumnNameLength; // column name length in cheracters
			RETCODE			retCode;

			for( SQLSMALLINT iCol = 1; iCol <= cCols; iCol++ )
			{
				cchDisplay = 0;

				// Figure out the display length of the column (we will
				// bind to char since we are only displaying data, in general
				// you should bind to the appropriate C type if you are going
				// to manipulate data since it is much faster...)
				//
				// PS: SQL_DESC_LENGTH - A numeric value that is either the maximum or actual character length of
				// a character string or binary data type.It is the maximum character length for a
				// fixed - length data type, or the actual character length for a variable - length
				// data type.Its value always excludes the null - termination byte that ends the character string
				// SQL_DESC_DISPLAY_SIZE - count of character to display. WAS IN SIMPLE FROM MICROSOFT

				retCode = SQLColAttributeW(hStmt_, iCol, SQL_DESC_LENGTH /*SQL_DESC_DISPLAY_SIZE*/, NULL, 0, NULL, &cchDisplay);
				if( retCode != SQL_SUCCESS )
				{
					HandleDiagnosticRecord(hStmt_, SQL_HANDLE_STMT, retCode);
					if( retCode == SQL_ERROR || retCode == SQL_INVALID_HANDLE )
					{
						Disconnect();
						return false;
					}
				}

				// Figure out if this is a character or numeric column; this is
				// used to determine if we want to display the data left- or right-
				// aligned.

				// SQL_DESC_CONCISE_TYPE maps to the 1.x SQL_COLUMN_TYPE. 
				// This is what you must use if you want to work
				// against a 2.x driver.

				retCode = SQLColAttributeW(hStmt_, iCol, SQL_DESC_CONCISE_TYPE, NULL, 0, NULL, &ssType);
				if( retCode != SQL_SUCCESS )
				{
					HandleDiagnosticRecord(hStmt_, SQL_HANDLE_STMT, retCode);
					if( retCode == SQL_ERROR || retCode == SQL_INVALID_HANDLE )
					{
						Disconnect();
						return false;
					}
				}

				ThisBinding.fChar = (ssType == SQL_WCHAR ||
									 ssType == SQL_WVARCHAR ||
									 ssType == SQL_WLONGVARCHAR);


				// Arbitrary limit on display size
				if( cchDisplay > MAX_WIDTH_OF_DATA_IN_COLOMN || cchDisplay < 0)
				{
					LOG_FIRST_N(WARNING, 1) << "Colomn '" << iCol <<"' needs: '" << cchDisplay << "' characters for longest value in it. This is unpredictable number. "
								<< "The maximum number of characters was changed to: '" << MAX_WIDTH_OF_DATA_IN_COLOMN <<"'";
					cchDisplay = MAX_WIDTH_OF_DATA_IN_COLOMN;
				}

				// Allocate a buffer big enough to hold the text representation
				// of the data.  Add one character for the null terminator

				ThisBinding.wszBuffer = (WCHAR *)malloc((cchDisplay + 1) * sizeof(WCHAR));

				if( !(ThisBinding.wszBuffer) )
				{
					LOG(WARNING) << "Out of memory!" << std::endl;
					Disconnect();
					return false;
				}

				ZeroMemory(ThisBinding.wszBuffer, cchDisplay + 1);

				// Map this buffer to the driver's buffer.   At Fetch time,
				// the driver will fill in this data.  Note that the size is 
				// count of bytes (for Unicode).  All ODBC functions that take
				// SQLPOINTER use count of bytes; all functions that take only
				// strings use count of characters.

				retCode = SQLBindCol(hStmt_, iCol, SQL_C_WCHAR, (SQLPOINTER)ThisBinding.wszBuffer, (SQLINTEGER)(cchDisplay + 1) * sizeof(WCHAR), &ThisBinding.indPtr);
				if( retCode != SQL_SUCCESS )
				{
					HandleDiagnosticRecord(hStmt_, SQL_HANDLE_STMT, retCode);
					if( retCode == SQL_ERROR || retCode == SQL_INVALID_HANDLE )
					{
						Disconnect();
						return false;
					}
				}

				// Now set the display size that we will use to display
				// the data.   Figure out the length of the column name

				/*retCode = SQLColAttributeW(hStmt_, iCol, SQL_DESC_NAME, NULL, 0, &cchColumnNameLength, NULL);
				if( retCode != SQL_SUCCESS )
				{
					HandleDiagnosticRecord(hStmt_, SQL_HANDLE_STMT, retCode);
					if( retCode == SQL_ERROR || retCode == SQL_INVALID_HANDLE )
					{
						Disconnect();
						return false;
					}
				}

				ThisBinding.cDisplaySize = max((SQLSMALLINT)cchDisplay, cchColumnNameLength);
	*/

				ThisBinding.cDisplaySize = cchDisplay + 1;

				//if( ThisBinding.cDisplaySize < NULL_SIZE )
				//	ThisBinding.cDisplaySize = NULL_SIZE;

				bindings_.push_back(ThisBinding);
			}

			return true;

		} catch( exception& e )
		{
			LOG(WARNING) << "ERROR: " << e.what();
			return false;
		}
	}

	bool CDatabase::getTitles()
	{
			RETCODE			retCode;
			WCHAR           wszTitle[MAX_WIDTH_OF_DATA_IN_COLOMN];
			SQLSMALLINT     iCol = 1;

		try {
			for( auto& thisBinding : bindings_ )
			{
				ZeroMemory(wszTitle, MAX_WIDTH_OF_DATA_IN_COLOMN);

				retCode = SQLColAttributeW(hStmt_, iCol++, SQL_DESC_NAME, wszTitle, sizeof(wszTitle) /*Note count of bytes!*/, NULL, NULL);
				if( retCode != SQL_SUCCESS )
				{
					HandleDiagnosticRecord(hStmt_, SQL_HANDLE_STMT, retCode);
					if( retCode == SQL_ERROR || retCode == SQL_INVALID_HANDLE )
					{
						Disconnect();
						return false;
					}
				}
 
					size_t len = 0;
					for(auto& ch: wszTitle )
					{
						if( ch == 0 )
						{
							break;
						}

						len++;
					}

					answer_ += wstring(wszTitle, len) + delim_;
				
			}	
			answer_ += wstring(L"\n\n");

			return true;

		} catch( exception& e )
		{
			LOG(WARNING) << "ERROR: " << e.what();
			return false;
		}
	}

	void CDatabase::HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
	{
		SQLSMALLINT	  iRec = 0;
		SQLINTEGER    iError;
		SQLCHAR       wszMessage[1000];
		SQLCHAR       wszState[SQL_SQLSTATE_SIZE + 1];

		if( RetCode == SQL_INVALID_HANDLE )
		{
			LOG(WARNING) << "ODBC: Invalid handle with type identifier: '" <<hType <<"' !";
			return;
		}

		bool noNameError = true;

		while( SQLGetDiagRecA(hType,
			  hHandle,
			  ++iRec,
			  wszState,
			  &iError,
			  wszMessage,
			  (SQLSMALLINT)countof(wszMessage),
			  (SQLSMALLINT *)NULL) == SQL_SUCCESS )
		{
			// Hide data truncated..
			if( strncmp((CHAR *)wszState, "01004", 5) )
			{
				LOG(WARNING) <<"ODBC: [" << wszState <<"] " << wszMessage << " (" << iError <<")" <<std::endl;
			}

			noNameError = false;
		}


		if( noNameError && (RetCode == SQL_ERROR) )
		{
			LOG(WARNING) << "ODBC: NoName error";
		}

	}

	bool CDatabase::operator<<(const wstring & query)
	{	
		try {
			RETCODE     retCode;
			SQLSMALLINT sNumResults;

			bool result = false;
		
			// Execute the query

			if( query.empty() || (! connected_) )
			{
				return result;
			}

			retCode = SQLAllocHandle(SQL_HANDLE_STMT, hDbc_, &hStmt_);
			if( retCode != SQL_SUCCESS )
			{
				HandleDiagnosticRecord(hDbc_, SQL_HANDLE_DBC, retCode);
				Disconnect();
				return result;
			}

			//wcscpy_s(wszInput, L"SELECT  Events.colEventTime, A_55.colAccountNumber, Events.colCardNumber, Events.colHolderID, colHolderType, Holder.colSurname, Holder.colName, Holder.colMiddlename, colStateCode, colComment, colDepartmentID, Holder.colDepartment, Holder.colTabNumber,  G_55.colName AS colAccessGroupName, CP.colID AS colObjectID, CP.colType AS colObjectType, CP.colName AS colObjectName, Events.colDirection, ZoneStart.colName AS colStartAreaName, ZoneTarget.colName AS colTargetAreaName, colEventCode FROM [StopNet4].[dbo].[tblEvents_55] AS Events WITH (NOLOCK) LEFT JOIN [StopNet4].[dbo].fnGetControlPoints() AS CP ON Events.colControlPointID = CP.colID LEFT JOIN (			SELECT				colID,				colSurname,				colName,				colMiddlename,  '' AS colStateCode,     colDepartmentID,				colDepartment,				colTabNumber,  colComment,				'1' AS colHolderType			FROM				[StopNet4].[dbo].[vwEmployees] WITH (NOLOCK)								UNION ALL						SELECT				colID,				colSurname,				colName,				colMiddlename,  '' AS colStateCode,  NULL,				'',				'',  colComment, 				'2' AS colHolderType			FROM				[StopNet4].[dbo].[vwVisitors] WITH (NOLOCK)      UNION ALL      SELECT				colID,				'',				'',				'',  colStateCode,  NULL,				'',				'',  colComment,				'3' AS colHolderType			FROM				[StopNet4].[dbo].[vwVehicles] WITH (NOLOCK)		) AS Holder ON Events.colHolderID = Holder.colID		LEFT JOIN [StopNet4].[dbo].[vwAccounts_55] AS A_55 WITH (NOLOCK) ON Events.colAccountID = A_55.colAccountID LEFT JOIN (			SELECT				colID,				colName			FROM				[StopNet4].[dbo].[tblGroupRightsEmployees_55] WITH (NOLOCK)							UNION ALL						SELECT				colID,				colName			FROM				[StopNet4].[dbo].[tblGroupRightsVisitors_55] WITH (NOLOCK)	UNION ALL						SELECT				colID,				colName			FROM				[StopNet4].[dbo].[tblGroupRightsVehicleEmployees_55] WITH (NOLOCK)	     UNION ALL						SELECT				colID,				colName			FROM				[StopNet4].[dbo].[tblGroupRightsVehicleVisitors_55] WITH (NOLOCK) ) AS G_55 ON [A_55].colAccessGroupRightsID = G_55.colID LEFT JOIN [StopNet4].[dbo].[tblZones_55] AS ZoneStart WITH (NOLOCK) ON Events.colStartZoneID = ZoneStart.colID LEFT JOIN [StopNet4].[dbo].[tblZones_55] AS ZoneTarget WITH (NOLOCK) ON  Events.colTargetZoneID = ZoneTarget.colID ");
			retCode = SQLExecDirectW(hStmt_, (SQLWCHAR *)query.c_str(), SQL_NTS);

			switch( retCode )
			{
				case SQL_SUCCESS_WITH_INFO:
					{
						HandleDiagnosticRecord(hStmt_, SQL_HANDLE_STMT, retCode);
						// fall through
					}
				case SQL_SUCCESS:
					{
						// If this is a row-returning query, display
						// results

						retCode = SQLNumResultCols(hStmt_, &sNumResults);

						if( retCode != SQL_SUCCESS ){
							HandleDiagnosticRecord(hStmt_, SQL_HANDLE_STMT, retCode);
							if( retCode == SQL_ERROR || retCode == SQL_INVALID_HANDLE ){
								Disconnect();
								break; //We must free hStmt_ before return
							}
						}

						if( sNumResults > 0 ){
							//get answer on query from db
							getAnswer(sNumResults);
						} else
						{
							SQLLEN cRowCount;

							retCode = SQLRowCount(hStmt_, &cRowCount);
							if( retCode != SQL_SUCCESS ){
								HandleDiagnosticRecord(hStmt_, SQL_HANDLE_STMT, retCode);
								if( retCode == SQL_ERROR || retCode == SQL_INVALID_HANDLE ){
									Disconnect();
									break; //We must free hStmt_ before return
								}
							}

							if( cRowCount >= 0 )
							{
								answer_ += wstring(cRowCount + cRowCount == 1 ? L"row" : L"rows");
							}

						}

						result = true;
						break;
					}

				case SQL_ERROR:
					{
						HandleDiagnosticRecord(hStmt_, SQL_HANDLE_STMT, retCode);
						break;
					}

				default:
					LOG(WARNING) << "ODBC: Unexpected return code: !" <<retCode << std::endl;

			}

			retCode = SQLFreeStmt(hStmt_, SQL_CLOSE);
			if( retCode != SQL_SUCCESS ){ 
				HandleDiagnosticRecord(hStmt_, SQL_HANDLE_STMT, retCode);
			} 

			return result;

		} catch( exception& e )
		{
			LOG(WARNING) << "ERROR: " << e.what();
			return false;
		}
	}

	CDatabase::~CDatabase()
	{
		Disconnect();
	}

	bool CDatabase::ConnectedOk() const
	{	
		return connected_;
	}

	void CDatabase::operator>>(wstring & str) const
	{
		str = answer_;
	}

}