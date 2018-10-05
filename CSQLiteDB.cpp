#include "CSQLiteDB.h"

CSQLiteDB::SQLLITEConnection::~SQLLITEConnection()
{
    //VLOG(1) <<"BEFORE ~SQLLITEConnection pStmt: "<<pStmt <<" pCon: " <<pCon;

    ReleaseStmt();

    if(pCon)
        sqlite3_close(pCon), pCon = nullptr;
    //VLOG(1) <<"after ~SQLLITEConnection pStmt: "<<pStmt <<" pCon: " <<pCon;
}

void CSQLiteDB::SQLLITEConnection::ReleaseStmt() {
    if(pStmt){
        sqlite3_finalize(pStmt);
        pStmt = nullptr;
    }
}

CSQLiteDB::SQLLITEConnection::SQLLITEConnection(string databasePath, size_t sqlEttempts, size_t sqlWaitTime)
        : pCon(nullptr)
        , pStmt(nullptr)
        , dbPath(std::move(databasePath))
        , iSQLEttempts_(sqlEttempts)
        , iSQLWaitTime_(sqlWaitTime)
{}

CSQLiteDB::CSQLiteDB(string databasePath, size_t sqlEttempts, size_t sqlWaitTime)
        : pSQLiteConn(new SQLLITEConnection(std::move(databasePath), sqlEttempts, sqlWaitTime))
        , bConnected_(false)
        , iColumnCount_(0)
        , fWaitFunction_([](const size_t ms){boost::this_thread::sleep(boost::posix_time::milliseconds(ms));})
{}

CSQLiteDB::ptr CSQLiteDB::new_(string databasePath, size_t sqlEttempts, size_t sqlWaitTime) {
    ptr new_ = ptr(new CSQLiteDB(std::move(databasePath), sqlEttempts, sqlWaitTime));
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

bool CSQLiteDB::OpenConnection()
{
    if(bConnected_)
        return bConnected_;

    bConnected_ = true;

    int rc = sqlite3_open_v2(pSQLiteConn->dbPath.c_str(), &pSQLiteConn->pCon, SQLITE_OPEN_FULLMUTEX|SQLITE_OPEN_READWRITE, nullptr );

    strLastError_ .clear();

    if(SQLITE_OK != rc)
    {
        if(strLastError_.find("not an error") == string::npos){
            strLastError_ = sqlite3_errmsg(pSQLiteConn->pCon);
            pSQLiteConn->pCon = nullptr;
            bConnected_ = false;
        }
    }

    //VLOG(1) <<"OpenCon pStmt: "<<pSQLiteConn->pStmt <<" pCon: " <<pSQLiteConn->pCon;

    //sqlite3_busy_timeout(pSQLiteConn->pCon, busyTimeout);

    return bConnected_;
}

IResult *CSQLiteDB::ExecuteSelect(const char *sqlQuery)
{
    if( ! isConnected())
        return nullptr;

    strLastError_.clear();
    iColumnCount_ = 0;

    if( ! PrepareSql(sqlQuery) ) {
        strLastError_ = "prepare statement error/timeout: " + string(sqlite3_errmsg(pSQLiteConn->pCon));
        LOG(WARNING) << "SQLITE: prepare statement error/timeout on handle(" << pSQLiteConn->pStmt <<") (" << sqlite3_errmsg(pSQLiteConn->pCon) <<")";
        return nullptr;
    }

    iColumnCount_ = sqlite3_column_count(pSQLiteConn->pStmt);

    return static_cast<IResult *>(this);
}


int CSQLiteDB::Execute(const char *sqlQuery)
{
    if(!isConnected())
        return -1;

    //Waiting, while DB become not busy
    for (size_t triesToBeginTrans = 0; ! BeginTransaction(); ++triesToBeginTrans) {
        VLOG(1) << "DEBUG: DB is busy! tries to begin transaction = " << triesToBeginTrans << ". Handle(" << pSQLiteConn->pStmt <<")";
        fWaitFunction_(128);
    }


    if( !PrepareSql(sqlQuery) ) {
        /** Timeout or error --> exit **/
        strLastError_ = "error while executing statement, (prepare statement error/timeout): " + string(sqlite3_errmsg(pSQLiteConn->pCon));
        LOG(WARNING) << "SQLITE: error while executing statement (" << sqlite3_errmsg(pSQLiteConn->pCon) <<")";
        EndTransaction();
        return 0;
    }

    int rc = StepSql();
    if( (rc != SQLITE_DONE) &&  (rc != SQLITE_ROW) ) {
        /** Timeout or error --> exit **/
        strLastError_ = "while executing statement, sqlite3_step returned with error_code(" + std::to_string(rc) +"): " + string(sqlite3_errmsg(pSQLiteConn->pCon));
        LOG(WARNING) << "SQLITE: sqlite3_step returned with error_code(" << rc <<") on handle(" << pSQLiteConn->pStmt <<"): " << sqlite3_errmsg(pSQLiteConn->pCon) << std::endl
                     << "Statement: " <<sqlQuery;
        pSQLiteConn->ReleaseStmt();
        EndTransaction();
        return 0;
    }

    pSQLiteConn->ReleaseStmt();

    EndTransaction();

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

bool CSQLiteDB::Next()
{
    int rc = 0;
    if( (rc = StepSql()) == SQLITE_MISUSE ){
        strLastError_ = "sqlite3_step returned missuse!";
        LOG(WARNING) << "SQLITE: sqlite3_step returned missuse on handle(" << pSQLiteConn->pStmt <<")";
        pSQLiteConn->ReleaseStmt();
        return false;
    }
    else if( rc == SQLITE_DONE ){
        return false;
    }
    else if( rc != SQLITE_ROW ){
        strLastError_ = "sqlite3_step returned with error_code(" + std::to_string(rc) +")";
        LOG(WARNING) << "SQLITE: sqlite3_step returned with error_code(" << rc <<" on handle(" << pSQLiteConn->pStmt <<")";
        pSQLiteConn->ReleaseStmt();
        return false;
    }

    return true;
}

const char *CSQLiteDB::ColomnData(int clmNum)
{
    if(clmNum > iColumnCount_)
        return nullptr;

    return (const char *)sqlite3_column_text(pSQLiteConn->pStmt, clmNum);
}

void CSQLiteDB::ReleaseStatement()
{
    //VLOG(1) <<"before Release() pStmt: "<<pSQLiteConn->pStmt <<" pCon: " <<pSQLiteConn->pCon;
    pSQLiteConn->ReleaseStmt();
    //VLOG(1) <<"after Release() pStmt: "<<pSQLiteConn->pStmt <<" pCon: " <<pSQLiteConn->pCon;
}

void CSQLiteDB::setWaitFunction(std::function<void(const size_t)> waitFunc) {
    fWaitFunction_ = std::move(waitFunc);
}

bool CSQLiteDB::PrepareSql(const char *sqlQuery) {
    int rc = 0; size_t n = 0;

    do
    {
        rc = sqlite3_prepare_v2(pSQLiteConn->pCon, sqlQuery, -1, &pSQLiteConn->pStmt, nullptr);

        if( (rc == SQLITE_BUSY) || (rc == SQLITE_LOCKED) )
        {
            n++;
            fWaitFunction_(pSQLiteConn->iSQLWaitTime_);
        }
    }while( (n < pSQLiteConn->iSQLEttempts_) && ((rc == SQLITE_BUSY) || (rc == SQLITE_LOCKED)));

    if( rc != SQLITE_OK)
    {
        LOG(WARNING) << "SQLITE: prepare statement error. Returned with error_code(" << rc <<") (" << sqlite3_errmsg(pSQLiteConn->pCon) << ")" << std::endl
                     << "Statement: " <<sqlQuery;
        return false;
    }

    return true;
}

int CSQLiteDB::StepSql() {
    int rc = 0; size_t n = 0;

    do
    {
        rc = sqlite3_step(pSQLiteConn->pStmt);

        if( rc == SQLITE_LOCKED )
        {
            rc = sqlite3_reset(pSQLiteConn->pStmt); /** Note: This will return SQLITE_LOCKED as well... **/
            n++;
            fWaitFunction_(pSQLiteConn->iSQLWaitTime_);
        }
        else if( rc == SQLITE_BUSY )
        {
            fWaitFunction_(pSQLiteConn->iSQLWaitTime_);
            n++;
        }
    }while( (n < pSQLiteConn->iSQLEttempts_) && ((rc == SQLITE_BUSY) || (rc == SQLITE_LOCKED)));

    if( n == pSQLiteConn->iSQLEttempts_ ) {
        VLOG(1) << "DEBUG: timeout on handle(" << pSQLiteConn->pStmt <<"): (" << rc << ")";
    }

    if( n > 2 ) {
        VLOG(1) << "DEBUG: tries on handle(" << pSQLiteConn->pStmt <<"): (" << n << ")";
    }

    if( rc == SQLITE_MISUSE ){
        LOG(WARNING) << "SQLITE: missuse ?? on handle(" << pSQLiteConn->pStmt <<")";
    }

    return(rc);
}

bool CSQLiteDB::BeginTransaction() {

    if( ! isConnected() ){
        strLastError_ = "no DB connection!";
        LOG(WARNING) << "SQLITE: " << strLastError_;
        OpenConnection();
        return false;
    }

    if( !PrepareSql("BEGIN EXCLUSIVE TRANSACTION;") ) {
        LOG(WARNING) << "SQLITE: begin Transaction error/timeout";
        return false;
    }

    int rc = StepSql();

    pSQLiteConn->ReleaseStmt();

    if( rc != SQLITE_DONE ){
        LOG(WARNING) << "SQLITE: begin Transaction error/timeout on handle(" << pSQLiteConn->pStmt <<"): (" << rc << ") " << sqlite3_errmsg(pSQLiteConn->pCon);
        return false;
    }

    return true;
}

bool CSQLiteDB::EndTransaction() {

    if( ! isConnected() ){
        strLastError_ = "no DB connection!";
        LOG(WARNING) << "SQLITE: " << strLastError_;
        return false;
    }

    if( !PrepareSql("COMMIT;") ) {
        strLastError_ = "end Transaction error";
        LOG(WARNING) << "SQLITE: " << strLastError_ << " on handle(" << pSQLiteConn->pStmt <<")";
        return false;
    }

    int rc = StepSql();

    pSQLiteConn->ReleaseStmt();

    if( rc != SQLITE_DONE ){
        strLastError_ = "end Transaction error/timeout";
        LOG(WARNING) << "SQLITE: " << strLastError_ << " on handle(" << pSQLiteConn->pStmt <<"): (" << rc << ") " << sqlite3_errmsg(pSQLiteConn->pCon);
        return false;
    }

    return true;
}
