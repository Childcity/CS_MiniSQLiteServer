//
// Created by childcity on 03.10.18.
//

#ifndef CS_MINISQLITESERVER_CBUSINESSLOGIC_H
#define CS_MINISQLITESERVER_CBUSINESSLOGIC_H
#pragma once

#include "CSQLiteDB.h"
#include "glog/logging.h"

#include <string>
#include <boost/shared_ptr.hpp>
#include <utility>
#include <boost/thread/pthread/recursive_mutex.hpp>
#include <mutex>

using std::string;

class BusinessLogicError: public std::exception {
public:
    explicit BusinessLogicError(string errorMsg) noexcept
            : msg_(std::move(errorMsg))
    {}
    explicit BusinessLogicError(std::string &&errorMsg) noexcept
            : msg_(std::move(errorMsg))
    {}
    explicit BusinessLogicError(const char *errorMsg) noexcept
            : msg_(std::move(std::string(errorMsg)))
    {}
    const char *what() const noexcept override{
        return msg_.c_str();
    }
private:
    std::string msg_;
};

class CBusinessLogic : public boost::enable_shared_from_this<CBusinessLogic>
        , boost::noncopyable {

public:
    explicit CBusinessLogic()
        : placeFree_("-1")
        , backupProgress_(-1)
    {/*static int instCount = 0; instCount++;VLOG(1) <<instCount;*/}

     //~CBusinessLogic(){/*VLOG(1) <<"DEBUG: free CBusinessLogic";*/}

    CBusinessLogic(CBusinessLogic const&) = delete;
    CBusinessLogic operator=(CBusinessLogic const&) = delete;

    void checkPlaceFree(const CSQLiteDB::ptr &dbPtr, const string &selectQuery_sql) {
        if(placeFree_ == "-1"){
            //selectPlaceFree(dbPtr, "select PlaceFree from Config");
            selectPlaceFree(dbPtr, selectQuery_sql);
        }
    }

    string getCachedPlaceFree() const{
        //NOT exclusive access to data! Allows only read, not write!
        boost::shared_lock< boost::shared_mutex > lock(bl_);
        return placeFree_;
    }

    void updatePlaceFree(const CSQLiteDB::ptr &dbPtr, const string &updateQuery_sql, const string &selectQuery_sql){
        string result;

        int effectedData =  dbPtr->Execute(updateQuery_sql.c_str());

        if (effectedData < 0){
            result = std::string("ERROR: effected data < 0! : " + dbPtr->GetLastError());
            LOG(WARNING) << result;
            throw BusinessLogicError(result);
        }

        selectPlaceFree(dbPtr, selectQuery_sql);
        //VLOG(1) <<"Update PL Free result: " <<effectedData <<" Now PlFree: " <<getCachedPlaceFree();
    }

    int backupDb(const CSQLiteDB::ptr &dbPtr, const string &backupPath){
        bool backupStatus;

        //If someone of existing clients start backup process, return progress3
        {
            boost::shared_lock< boost::shared_mutex > lock(bl_); //NOT exclusive access to data! Allows only read, not write!
            if(backupProgress_ != -1){
                return backupProgress_;
            }
        }

        {
            boost::unique_lock<boost::shared_mutex> lock(bl_);
            backupProgress_ = 0;
        }


        auto self = shared_from_this();
        backupStatus = dbPtr->BackupDb(backupPath.c_str(), [self, this](const int remaining, const int total){
                //exclusive access to data!
                boost::unique_lock<boost::shared_mutex> lock(bl_);
                backupProgress_ = static_cast<const int>(100) * (total - remaining) / total;
                if(backupProgress_ == 100)
                    backupProgress_ = 99;
                VLOG(1) << "DEBUG: backup in progress [" <<backupProgress_ <<"%]";
            });

        if(! backupStatus){
            resetBackUpProgress();
            return -1;
        }

        // check backup on error (integrity check)
        const auto backUpDb = CSQLiteDB::new_(backupPath);
        if(! backUpDb->OpenConnection()){
            LOG(WARNING) << "ERROR: can't connect to backup db for 'integrity check': " <<backUpDb->GetLastError();
            resetBackUpProgress();
            return -1;
        }

        if(! backUpDb->IntegrityCheck()){
            LOG(WARNING) << "ERROR: 'integrity check' failed ";
            resetBackUpProgress();
            return -1;
        }

        boost::unique_lock<boost::shared_mutex> lock(bl_);
        backupProgress_ = 100;
        return 100;
    }

    int getBackUpProgress() const{
        boost::shared_lock< boost::shared_mutex > lock(bl_); //NOT exclusive access to data! Allows only read, not write!
        return backupProgress_;
    }

    void resetBackUpProgress(){
        boost::unique_lock<boost::shared_mutex> lock(bl_);
        backupProgress_ = -1;
    }

    // throws BuisnessLogicErro
    // This method select saved querys, while backup was active, and execute theirs in main db
    static void SyncDbWithTmp(const string mainDbPath, const std::function<void(const size_t)> &waitFunc){

        static std::recursive_mutex sync_;

        std::unique_lock<std::recursive_mutex> lock1(sync_, std::try_to_lock);

        if(! lock1){
            std::unique_lock<std::recursive_mutex> lock2(sync_, std::try_to_lock);
            if(! lock2){
                VLOG(1) <<"ERROROROROROROROROROR";
                return;
            }
        }

        VLOG(1) <<"BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
        const auto tmpDb = CSQLiteDB::new_(getTmpDbPath());
        const auto mainDb = CSQLiteDB::new_(mainDbPath);

        if(! tmpDb->OpenConnection()) {
            string errMsg("can't connect to " + getTmpDbPath() + ": " + tmpDb->GetLastError());
            LOG(WARNING) <<"BUSINESS_LOGIC: " <<errMsg;
            throw BusinessLogicError(errMsg);
        }

        if(! mainDb->OpenConnection()) {
            string errMsg("can't connect to " + mainDbPath + ": " + mainDb->GetLastError());
            LOG(WARNING) <<"BUSINESS_LOGIC: " <<errMsg;
            throw BusinessLogicError(errMsg);
        }

        IResult *res;

        // start execute querys from tmp db
        do{
            res = nullptr;
            waitFunc(200); //sleep to give other connections executed

            res = tmpDb->ExecuteSelect("SELECT rowid, * FROM `tmp_querys` ORDER BY rowid ASC LIMIT 1;");

            if (nullptr == res){
                string errorMsg = "can't select row from 'tmp_querys'";
                LOG(WARNING) << "BUSINESS_LOGIC: " <<errorMsg;
                throw BusinessLogicError(errorMsg);
            }

            //no querys in tmp db, sync is done success
            if (! res->Next()) {
                //release Result Data
                res->ReleaseStatement();
                break;
            }

            // 3 column will be returned
            const string rowid = res->ColomnData(0);
            const string query = res->ColomnData(1);

            //release Result Data
            res->ReleaseStatement();

            if(rowid.empty() || query.empty()){
                string errorMsg = "BUSINESS_LOGIC: rowid or query empty";
                LOG(WARNING) << errorMsg;
                continue;
            }

            //VLOG(1) <<"DEBUG: rowid: " <<rowid <<" query: " <<query;

            // executing query from tmp table
            int queryResult = mainDb->Execute(query.c_str());

            if(queryResult < 0){
                string errorMsg = "BUSINESS_LOGIC: can't execute query '" + query + "' from tmp db: " + mainDb->GetLastError();
                LOG(WARNING) << errorMsg;
            }

            //VLOG(1) <<"DEBUG: query executed OK: " <<queryResult;

            // delete query from tmp db
            int deleteResult = tmpDb->Execute(string("DELETE FROM `tmp_querys` WHERE rowid = '" + rowid + "';").c_str());

            if(deleteResult < 0){ //TODO: can be cicling!! If newer delete current row
                string errorMsg = "BUSINESS_LOGIC: can't delete row from tmp db: " + tmpDb->GetLastError();
                LOG(WARNING) << errorMsg;
            }

            // VLOG(1) <<"DEBUG: delete row OK: " <<deleteResult;

        }while(true);
    }

    // throws BusinessLogicError
    static void CreateOrUseOldTmpDb(){

        const auto tmpDb = CSQLiteDB::new_(getTmpDbPath());

        // check if tmp db exists
        if(! tmpDb->OpenConnection(SQLITE_OPEN_FULLMUTEX|SQLITE_OPEN_READWRITE)){
            LOG(INFO) <<"Can't connect to " <<getTmpDbPath() <<": " <<tmpDb->GetLastError();
            LOG(INFO) <<"Trying to create new " <<getTmpDbPath();

            //create new db. If can't create, return;
            if(! tmpDb->OpenConnection(SQLITE_OPEN_FULLMUTEX|SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE)){
                LOG(WARNING) <<"BUSINESS_LOGIC: can't create or connect to " <<getTmpDbPath() <<": " <<tmpDb->GetLastError();
                throw BusinessLogicError("Temporary database can't be opened or created. Check permissions and free place on disk");
            }

            //create table for temporary query strings
            int res = tmpDb->Execute("CREATE TABLE tmp_querys(\n"
                           "   query          TEXT    NOT NULL,\n"
                           "   timestamp DATETIME DEFAULT CURRENT_TIMESTAMP\n"
                           ");");

            if(res < 0){
                string errMsg("Can't create table for temporary database");
                LOG(WARNING) <<"BUSINESS_LOGIC: " <<errMsg;
                throw BusinessLogicError(errMsg);
            }

            //created new db. Created table
        }

    }

    // throws BuisnessLogicError
    static int SaveQueryToTmpDb(const string &query){

        const auto tmpDb = CSQLiteDB::new_(getTmpDbPath());

        if(! tmpDb->OpenConnection()) {
            string errMsg("can't connect to " + getTmpDbPath() + ": " + tmpDb->GetLastError());
            LOG(WARNING) <<"BUSINESS_LOGIC: " <<errMsg;
            throw BusinessLogicError(errMsg);
        }

        return tmpDb->Execute(string("INSERT INTO `tmp_querys` (query) VALUES ('" + query + "');").c_str());
    }

private:

    void selectPlaceFree(const CSQLiteDB::ptr &dbPtr, const string &selectQuery_sql){
        string result, errorMsg;

        IResult *res = dbPtr->ExecuteSelect(selectQuery_sql.c_str());

        if (nullptr == res){
            errorMsg = "can't select 'PlaceFree'";
            LOG(WARNING) << "BUSINESS_LOGIC: " << errorMsg;
            throw BusinessLogicError(errorMsg);
        } else {
            //Data
            while (res->Next()) {
                // really only 1 column will be returned after qery
                for (int i = 0; i < res->GetColumnCount(); i++){
                    const char *tmpRes = res->ColomnData(i);
                    result += (tmpRes ? std::move(string(tmpRes)): "NONE");

                }
            }
            //release memory for Result Data
            res->ReleaseStatement();

            if(result.empty()){
                result = "ERROR: db returned empty string";
                LOG(WARNING) << errorMsg;
                throw BusinessLogicError(errorMsg);
            }

            //check, if return data is number
            if(result != "0"){
                size_t num = std::strtoull(result.c_str(), nullptr, 10 ); //this func return 0 if can't convert to size_t
                if(num <= 0 || num == ULONG_MAX){
                    errorMsg = "can't convert '" + result + "' to number!";
                    LOG(WARNING) << "BUSINESS_LOGIC: " << errorMsg;
                    throw BusinessLogicError(errorMsg);
                }
            }


            //exclusive access to data!
            boost::unique_lock<boost::shared_mutex> lock(bl_);
            placeFree_ = result;
        }
    }


    static const string getTmpDbPath(){
        static const string tmpDbPath("temp_db.sqlite3");
        return tmpDbPath;
    }

private:
    mutable boost::shared_mutex bl_;
    string placeFree_;

    int backupProgress_;

};


#endif //CS_MINISQLITESERVER_CBUSINESSLOGIC_H
