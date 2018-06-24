#include "CSQLiteDB.h"
//#include "glog/logging.h"

CSQLiteDB::SQLLITEConnection::~SQLLITEConnection()
{
    //VLOG(1) <<"BEFORE ~SQLLITEConnection pStmt: "<<pStmt <<" pCon: " <<pCon;
    if(pCon)
        sqlite3_close(pCon), pCon = nullptr;

    FinalizeRes();
    //VLOG(1) <<"after ~SQLLITEConnection pStmt: "<<pStmt <<" pCon: " <<pCon;
}

void CSQLiteDB::SQLLITEConnection::FinalizeRes() {
    if(pStmt){
        sqlite3_finalize(pStmt);
        pStmt = nullptr;
    }
}



CSQLiteDB::ptr CSQLiteDB::new_() {
    ptr new_ = ptr(new CSQLiteDB);
    return new_;
}

string CSQLiteDB::GetLastError()
{
    return strLastError_;
}

bool   CSQLiteDB::isConnected()
{
    return bConnected_;
}

bool CSQLiteDB::OpenConnection(string databasePath, int busyTimeout)
{
    if(bConnected_)
        return bConnected_;

    pSQLiteConn->dbPath = std::move(databasePath);

    bConnected_ = true;

    int rc = sqlite3_open_v2(pSQLiteConn->dbPath.c_str(), &pSQLiteConn->pCon, SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX|SQLITE_OPEN_READWRITE, nullptr );

    strLastError_ = (string)sqlite3_errmsg(pSQLiteConn->pCon);

    if(SQLITE_OK != rc)
    {
        if(strLastError_.find("not an error") == string::npos){
            pSQLiteConn->pCon = nullptr;
            bConnected_ = false;
        }
    }

    //VLOG(1) <<"OpenCon pStmt: "<<pSQLiteConn->pStmt <<" pCon: " <<pSQLiteConn->pCon;

    sqlite3_busy_timeout(pSQLiteConn->pCon, busyTimeout);

    return bConnected_;
}

void CSQLiteDB::BeginTransaction()
{
    sqlite3_exec(pSQLiteConn->pCon, "BEGIN TRANSACTION", nullptr, nullptr,nullptr);
}

void CSQLiteDB::CommitTransection()
{
    sqlite3_exec(pSQLiteConn->pCon, "COMMIT TRANSACTION", nullptr, nullptr,nullptr);
}

IResult *CSQLiteDB::ExcuteSelect(const char *query)
{
    if(!isConnected())
        return nullptr;

    iColumnCount_ = 0;

    if( SQLITE_OK != sqlite3_prepare_v2(pSQLiteConn->pCon, query, -1, &pSQLiteConn->pStmt, nullptr))
    {
        strLastError_= sqlite3_errmsg(pSQLiteConn->pCon);
        //VLOG(1) <<"Bad ExcuteSelect pStmt: "<<pSQLiteConn->pStmt <<" pCon: " <<pSQLiteConn->pCon;
        return nullptr;
    }
    else
    {
        iColumnCount_ = sqlite3_column_count(pSQLiteConn->pStmt);
        //VLOG(1) <<"Good ExcuteSelect pStmt: "<<pSQLiteConn->pStmt <<" pCon: " <<pSQLiteConn->pCon;

        IResult *ires=this;
        return ires;
    }
}


int CSQLiteDB::Excute(const char *query)
{
    if(!isConnected())
        return -1;

    strLastError_.clear();

    char *pErrMsg = nullptr;

    if(SQLITE_OK != sqlite3_exec(pSQLiteConn->pCon, query, nullptr, nullptr, &pErrMsg))
    {
        //strLastError_ = (string)sqlite3_errmsg(pSQLiteConn->pCon);
        strLastError_ = (string)pErrMsg;
        //VLOG(1) <<"asas"<<pErrMsg;
        sqlite3_free(pErrMsg);
        return 0;
    }

    return sqlite3_total_changes(pSQLiteConn->pCon);
}

/*Result Set Definations*/
int	CSQLiteDB::GetColumnCount()
{
    return iColumnCount_;
}

const char *CSQLiteDB::NextColomnName(int iClmnCount)
{
    if(iClmnCount > iColumnCount_)
        return nullptr;

    return sqlite3_column_name(pSQLiteConn->pStmt, iClmnCount);
}

bool CSQLiteDB:: Next()
{
    return SQLITE_ROW == sqlite3_step(pSQLiteConn->pStmt);
}

const char *CSQLiteDB::ColomnData(int clmNum)
{
    if(clmNum > iColumnCount_)
        return nullptr;

    return (const char *)sqlite3_column_text(pSQLiteConn->pStmt,clmNum);
}

void CSQLiteDB::Release()
{
    //VLOG(1) <<"before Release() pStmt: "<<pSQLiteConn->pStmt <<" pCon: " <<pSQLiteConn->pCon;
    pSQLiteConn->FinalizeRes();
    //VLOG(1) <<"after Release() pStmt: "<<pSQLiteConn->pStmt <<" pCon: " <<pSQLiteConn->pCon;
}
