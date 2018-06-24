#pragma once
#ifndef CS_MINISQLITESERVER_CSQLITEDB_H
#define CS_MINISQLITESERVER_CSQLITEDB_H

#include <boost/enable_shared_from_this.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <string>

#include "sqlite3/sqlite3.h"

using std::string;
using boost::scoped_ptr;
using boost::shared_ptr;

/*Interface class for Result data of query*/
class IResult : public boost::enable_shared_from_this<IResult>
        , boost::noncopyable {
public:
    /*Shared pointer to IResult*/
    typedef shared_ptr<IResult> ptr;

    /*This function return of count of column
      present in result set of last excueted query*/
    virtual int	GetColumnCount() = 0;

    /*Get the next coloumn name*/
    virtual const char *NextColomnName(int iClmnCount) = 0;

    /*This function returns TRUE if still rows are
    der in result set of last excueted query FALSE
    if no row present*/
    virtual bool  Next() = 0;

    /*Get the next coloumn data*/
    virtual const char *ColomnData(int clmNum) = 0;

    /*RExcuteSelectELEASE all result set as well as RESET all data*/
    virtual void Release() = 0;
};



//SQLite Wrapper Class
class CSQLiteDB : public IResult
        , boost::enable_shared_from_this<CSQLiteDB> {
private:
    explicit CSQLiteDB()
            : pSQLiteConn((new SQLLITEConnection()))
            , bConnected_(false)
    {}

public:

    typedef shared_ptr<CSQLiteDB> ptr;

    /*Class factory. This method create shared pointer to CSQLiteDB.*/
    static ptr new_();;

    /*Open Connection*/
    bool OpenConnection(string databasePath, int busyTimeout = 0);

    /*Query Wrapper*/
    /*For large insert operation Memory Insert option for SQLLITE dbJournal*/
    void BeginTransaction();
    void CommitTransection();

    /*This Method called when SELECT Query to be excuted.
    Return RESULTSET class pointer on success else nullptr of failed*/
    IResult *ExcuteSelect(const char *query);

    /*This Method called when INSERT/DELETE/UPDATE Query to be excuted.
    Return int count of effected data on success*/
    int Excute(const char *query);

    /*Get Last Error of excution*/
    string GetLastError();

    /*Return TRUE if databse is connected else FALSE*/
    bool  isConnected() ;


protected:
    /*SQLite Connection Object*/
    struct SQLLITEConnection
    {
        string          dbPath;    //Path to database
        sqlite3		    *pCon;     //SQLite Connection Object
        sqlite3_stmt    *pStmt;     //SQLite statement object
        void FinalizeRes();
        SQLLITEConnection()
                : pCon(nullptr)
                , pStmt(nullptr){}
        virtual ~SQLLITEConnection();
    };

    //SQLite Connection Details
     scoped_ptr<SQLLITEConnection> pSQLiteConn;

    bool	bConnected_;      /*Is Connected To DB*/
    string  strLastError_;    /*Last Error String*/
    int     iColumnCount_;    /*No.Of Column in Result*/


public:
    /*This function return of count of column
      present in result set of last excueted query*/
    int	GetColumnCount() override;

    /*Get the next coloumn name*/
    const char *NextColomnName(int iClmnCount) override;

    /*This function returns TRUE if still rows are
    der in result set of last excueted query FALSE
    if no row present*/
    bool  Next() override;

    /*Get the next coloumn data*/
    const char *ColomnData(int clmNum) override;

    /*RELEASE all result set as well as RESET all data*/
    void Release() override;

};

#endif //CS_MINISQLITESERVER_CSQLITEDB_H
