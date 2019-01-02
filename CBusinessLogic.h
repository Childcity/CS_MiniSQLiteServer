//
// Created by childcity on 03.10.18.
//

#ifndef CS_MINISQLITESERVER_CBUSINESSLOGIC_H
#define CS_MINISQLITESERVER_CBUSINESSLOGIC_H
#pragma once

#include "main.h"
#include "CSQLiteDB.h"
#include "CBinaryFileReader.h"
#include "glog/logging.h"

#include <memory>
#include <string>
#include <fstream>
#include <utility>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <mutex>

using std::string;
using namespace boost::asio;

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
    explicit CBusinessLogic();

     //~CBusinessLogic(){/*VLOG(1) <<"DEBUG: free CBusinessLogic";*/}

    CBusinessLogic(CBusinessLogic const&) = delete;
    CBusinessLogic operator=(CBusinessLogic const&) = delete;

    void checkPlaceFree(const CSQLiteDB::ptr &dbPtr, const string &selectQuery_sql);

    string getCachedPlaceFree() const;

    void updatePlaceFree(const CSQLiteDB::ptr &dbPtr, const string &updateQuery_sql, const string &selectQuery_sql);

    int backupDb(const CSQLiteDB::ptr &dbPtr, const string &backupPath);

    int getBackUpProgress() const;

    bool isBackupExist(const string &backupPath) const;

    void resetBackUpProgress();


    void setTimeoutOnNextBackupCmd(io_context &io_context, size_t ms);

    bool prepareBeforeRestore(const string &mainDbPath, const string &restoreDbPath);

    void restoreDbFromFile(const string &mainDbPath, const string &restoreDbPath);

    int getRestoreProgress() const;

    bool isRestoreExecuting() const;

    void resetRestoreProgress();

    // throws BuisnessLogicErro
    // This method select saved querys, while backup was active, and execute theirs in main db
    static void SyncDbWithTmp(const string &mainDbPath, const std::function<void(const size_t)> &waitFunc);

    // throws BusinessLogicError
    static void CreateOrUseOldTmpDb();

    // throws BusinessLogicError
    static int SaveQueryToTmpDb(const string &query);

private:

    void selectPlaceFree(const CSQLiteDB::ptr &dbPtr, const string &selectQuery_sql);


    static const string getTmpDbPath();

private:
    mutable boost::shared_mutex business_logic_mtx_, restore_mtx_;
    string placeFree_;

    int backupProgress_;
    int restoreProgress_;

    std::unique_ptr<deadline_timer> backupTimer_;

};


#endif //CS_MINISQLITESERVER_CBUSINESSLOGIC_H
