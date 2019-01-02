#pragma once
#ifndef CS_MINISQLITESERVER_CSQLITEDB_H
#define CS_MINISQLITESERVER_CSQLITEDB_H

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/thread/thread.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <string>

#include "sqlite3/sqlite3.h"
#include "glog/logging.h"

using std::string;
using boost::scoped_ptr;
using boost::shared_ptr;

/*Interface class for Result data of sqlQuery*/
class IResult : public boost::noncopyable {
public:

    /*This function return of count of column
      present in result set of last excueted sqlQuery*/
    virtual int	GetColumnCount() = 0;

    /*Get the next coloumn name*/
    virtual const char *NextColomnName(int iClmnCount) = 0;

    /*This function returns TRUE if still rows are
    der in result set of last excueted sqlQuery FALSE
    if no row present*/
    virtual bool  Next() = 0;

    /*Get the next coloumn data*/
    virtual const char *ColomnData(int clmNum) = 0;

    /*RExcuteSelectELEASE all result set as well as RESET all data*/
    virtual void ReleaseStatement() = 0;
};



//SQLite Wrapper Class
class CSQLiteDB : public IResult
        , public boost::enable_shared_from_this<CSQLiteDB> {
private:
    explicit CSQLiteDB(string databasePath, size_t sqlEttempts, size_t sqlWaitTime);

public:

    virtual ~CSQLiteDB();

    typedef shared_ptr<CSQLiteDB> ptr;

    /*Class factory. This method create shared pointer to CSQLiteDB.*/
    static ptr new_(string databasePath, size_t sqlEttempts = 200, size_t sqlWaitTime = 50);

    bool OpenConnection(int flags = SQLITE_OPEN_FULLMUTEX|SQLITE_OPEN_READWRITE);

    /*This Method called when SELECT sqlQuery to be excuted.
    Return RESULTSET class pointer on success else nullptr of failed*/
    IResult *ExecuteSelect(const char *sqlQuery);

    /*This Method called when INSERT/DELETE/UPDATE sqlQuery to be excuted.
    Return int count of effected data on success*/
    int Execute(const char *sqlQuery);

    /*This Method for backup Db*/
    bool BackupDb(
            const char *zFilename,                                      /* Name of file to back up to */
            const std::function<void(const int, const int)> &xProgress  /* Progress function to invoke */
    );

    /*Check Db on errors. If ok, return true, else return false and set last error str*/
    bool IntegrityCheck();

    /*Get Last Error of excution*/
    string GetLastError();

    /*Return TRUE if databse is connected else FALSE*/
    bool  isConnected() ;

    void setWaitFunction(std::function<void(size_t)> waitFunc);

protected:
    /*SQLite Connection Object*/
    struct SQLLITEConnection{
        size_t          iSQLWaitTime_;
        size_t          iSQLEttempts_;
        string          dbPath;    //Path to database
        sqlite3		    *pCon;     //SQLite Connection Object
        sqlite3_stmt    *pStmt;     //SQLite statement object
        void ReleaseStmt();
        SQLLITEConnection(string databasePath, size_t sqlEttempts, size_t sqlWaitTime);
        virtual ~SQLLITEConnection();
    };

    //SQLite Connection Details
    scoped_ptr<SQLLITEConnection> pSQLiteConn;

    std::function<void(const size_t)> fWaitFunction_;

    bool PrepareSql(const char *sqlQuery);

    int StepSql();

    bool BeginTransaction();

    bool EndTransaction();

    bool	bConnected_;      /*Is Connected To DB*/
    string  strLastError_;    /*Last Error String*/
    int     iColumnCount_;    /*No.Of Column in Result*/

private:
    /*This function return of count of column
      present in result set of last excueted sqlQuery*/
    int	GetColumnCount() override;

    /*Get the next coloumn name*/
    const char *NextColomnName(int iClmnCount) override;

    /*This function returns TRUE if still rows are
    der in result set of last excueted sqlQuery FALSE
    if no row present*/
    bool  Next() override;

    /*Get the next coloumn data*/
    const char *ColomnData(int clmNum) override;

    /*RELEASE all result set as well as RESET all data*/
    void ReleaseStatement() override;
};

#endif //CS_MINISQLITESERVER_CSQLITEDB_H
