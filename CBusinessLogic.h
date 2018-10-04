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

class CBusinessLogic {
public:
    explicit CBusinessLogic()
        : placeFree_("-1")
    {}

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

        int effectedData =  dbPtr->Excute(updateQuery_sql.c_str());

        if (effectedData < 0){
            result = std::string("ERROR: last error: " + dbPtr->GetLastError());
            LOG(WARNING) << result;
            throw BusinessLogicError(result);
        }

        selectPlaceFree(dbPtr, selectQuery_sql);
    }

private:

    void selectPlaceFree(const CSQLiteDB::ptr &dbPtr, const string &selectQuery_sql){
        string result, errorMsg;

        IResult *res = dbPtr->ExcuteSelect(selectQuery_sql.c_str());

        if (nullptr == res){
            errorMsg = "ERROR: can't select 'PlaceFree'";
            LOG(WARNING) << errorMsg;
            throw BusinessLogicError(errorMsg);
        } else {
            //Data
            while (res->Next()) {
                // really only 1 column will be returned after qery
                for (int i = 0; i < res->GetColumnCount(); i++){
                    const char *tmpRes = res->ColomnData(i);
                    result += (tmpRes ? std::move(string(tmpRes)): "None");

                }
            }
            //release Result Data
            res->ReleaseStatement();

            if(result.empty()){
                result = "ERROR: db returned empty string";
                LOG(WARNING) << errorMsg;
                throw BusinessLogicError(errorMsg);
            }
//            else{
//                result.erase(result.size() - 1);
//            }

            //check, if return data is number
            if(result != "0"){
                size_t num = std::strtoull(result.c_str(), nullptr, 10 ); //this func return 0 if can't convert to size_t
                if(num <= 0 || num == ULONG_MAX){
                    errorMsg = "ERROR: can't convert '" + result + "' to number!";
                    LOG(WARNING) << errorMsg;
                    throw BusinessLogicError(errorMsg);
                }
            }


            //exclusive access to data!
            boost::unique_lock<boost::shared_mutex> lock(bl_);
            placeFree_ = result;
        }
    }

    mutable boost::shared_mutex bl_;
    string placeFree_;
};


#endif //CS_MINISQLITESERVER_CBUSINESSLOGIC_H
