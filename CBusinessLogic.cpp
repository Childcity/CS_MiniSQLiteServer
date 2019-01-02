//
// Created by childcity on 03.10.18.
//

#include "CBusinessLogic.h"

CBusinessLogic::CBusinessLogic()
        : placeFree_("-1")
        , backupProgress_(-1)
        , restoreProgress_(-1)
{/*static int instCount = 0; instCount++;VLOG(1) <<instCount;*/}

void CBusinessLogic::checkPlaceFree(const CSQLiteDB::ptr &dbPtr, const string &selectQuery_sql) {
    if(placeFree_ == "-1"){
        //selectPlaceFree(dbPtr, "select PlaceFree from Config");
        selectPlaceFree(dbPtr, selectQuery_sql);
    }
}

string CBusinessLogic::getCachedPlaceFree() const {
    //NOT exclusive access to data! Allows only read, not write!
    boost::shared_lock< boost::shared_mutex > lock(business_logic_mtx_);
    return placeFree_;
}

void CBusinessLogic::updatePlaceFree(const CSQLiteDB::ptr &dbPtr, const string &updateQuery_sql,
                                     const string &selectQuery_sql) {
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

int CBusinessLogic::backupDb(const CSQLiteDB::ptr &dbPtr, const string &backupPath) {
    bool backupStatus;

    //If someone of existing clients start backup process, return progress3
    {
        boost::shared_lock< boost::shared_mutex > lock(business_logic_mtx_); //NOT exclusive access to data! Allows only read, not write!
        if(backupProgress_ != -1){
            return backupProgress_;
        }
    }

    {
        boost::unique_lock<boost::shared_mutex> lock(business_logic_mtx_);
        backupProgress_ = 0;
    }

    auto self = shared_from_this();
    backupStatus = dbPtr->BackupDb(backupPath.c_str(), [self, this](const int remaining, const int total){
        //exclusive access to data!
        boost::unique_lock<boost::shared_mutex> lock(business_logic_mtx_);
        //SQLite BUG: total can be 0!!!! So we should check it on zero
        backupProgress_ = static_cast<int>(100 * abs(total - remaining) / (total ? total:1));
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
        LOG(WARNING) << "ERROR: 'integrity check' failed: \n" <<backUpDb->GetLastError();
        resetBackUpProgress();
        return -1;
    }

    VLOG(1) <<"DEBUG: integrity check OK";

    boost::unique_lock<boost::shared_mutex> lock(business_logic_mtx_);
    backupProgress_ = 100;
    return 100;
}

int CBusinessLogic::getBackUpProgress() const {
    boost::shared_lock< boost::shared_mutex > lock(business_logic_mtx_); //NOT exclusive access to data! Allows only read, not write!
    return backupProgress_;
}

bool CBusinessLogic::isBackupExist(const string &backupPath) const {
    return /*(getBackUpProgress() == 100) &&*/ std::ifstream{backupPath}.good() ;
}

void CBusinessLogic::resetBackUpProgress() {
    boost::unique_lock<boost::shared_mutex> lock(business_logic_mtx_);
    backupProgress_ = -1;
}

void CBusinessLogic::   setTimeoutOnNextBackupCmd(io_context &io_context, const size_t ms) {
    if(backupTimer_)
        return;

    auto self = shared_from_this();
    backupTimer_ = std::make_unique<deadline_timer>(io_context, boost::posix_time::millisec(ms));
    backupTimer_->async_wait([self, this](const boost::system::error_code &error){
        if(error){
            LOG(WARNING) <<"BUSINESS_LOGIC: backup timer error: " << error;
        }
        resetBackUpProgress();
        backupTimer_.reset();
    });
}

bool CBusinessLogic::prepareBeforeRestore(const string &mainDbPath, const string &restoreDbPath) {
    string errMsg;
    CBinaryFileReader fileReader;

    {//exclusive access to data!
        boost::unique_lock<boost::shared_mutex> lock(restore_mtx_);
        restoreProgress_ = 0;
    }

    std::ofstream fileWriter(mainDbPath, std::ios::binary);

    if((! fileReader.open(restoreDbPath)) && (! fileReader.open(restoreDbPath))){
        errMsg = "can't open for read restore file '" + restoreDbPath + "'";
        LOG(WARNING) << "BUSINESS_LOGIC: prepare for db restoring error: " <<errMsg;
        //BusinessLogicError()
        resetRestoreProgress();
        return false;
    }

    if(! fileWriter.is_open()){
        errMsg = "can't open for write current db file '" + mainDbPath + "'";
        LOG(WARNING) << "BUSINESS_LOGIC: prepare for db restoring error: " <<errMsg;
        //BusinessLogicError()
        resetRestoreProgress();
        return false;
    }

    CSQLiteDB::ptr restoreDb = CSQLiteDB::new_(restoreDbPath);

    if(! restoreDb->OpenConnection()){
        errMsg = "can't open restore db '" + restoreDbPath + "': " + restoreDb->GetLastError();
        LOG(WARNING) << "BUSINESS_LOGIC: prepare for db restoring error: " <<errMsg;
        //BusinessLogicError()
        resetRestoreProgress();
        return false;
    }

    VLOG(1) <<"DEBUG: (restore db) - integrity checking...";
    if(! restoreDb->IntegrityCheck()){
        errMsg = "integrity check failed for '" + restoreDbPath + "': \n" + restoreDb->GetLastError();
        LOG(WARNING) << "BUSINESS_LOGIC: prepare for db restoring error: " <<errMsg;
        //BusinessLogicError()
        resetRestoreProgress();
        return false;
    }else{
        LOG(INFO) <<"Restore db: integrity check: OK";
    }

    return true;
}

void CBusinessLogic::restoreDbFromFile(const string &mainDbPath, const string &restoreDbPath) {
    string errMsg;
    CBinaryFileReader fileReader;
    std::ofstream fileWriter(mainDbPath, std::ios::binary);

    if((! fileReader.open(restoreDbPath)) && (! fileReader.open(restoreDbPath))){
        errMsg = "can't open for read restore file '" + restoreDbPath + "'";
        LOG(WARNING) << "BUSINESS_LOGIC: restore db error: " <<errMsg;
        resetRestoreProgress();
        return;
    }

    if(! fileWriter.is_open()){
        errMsg = "can't open for write current db file '" + mainDbPath + "'";
        LOG(WARNING) << "BUSINESS_LOGIC: restore db error: " <<errMsg;
        resetRestoreProgress();
        return;
    }

    try {
        long progress = -1;
        while(fileReader.nextChunk()){
            fileWriter.write(fileReader.getCurrentChunk(), fileReader.getCurrentChunkSize());
            progress = fileReader.getProgress();

            if(progress == 100l)
                progress = 99l;

            VLOG(1) << "DEBUG: restore db in progress [" <<progress <<"%]";

            {//exclusive access to data!
                boost::unique_lock<boost::shared_mutex> lock(restore_mtx_);
                restoreProgress_ = static_cast<int>(progress);
            }

        }
    }catch (...){
        LOG(WARNING) <<"BUSINESS_LOGIC: restore db error: unknown system error";
    }


    CSQLiteDB::ptr mainDb = CSQLiteDB::new_(mainDbPath);

    if(! mainDb->OpenConnection()){
        errMsg = "can't open main db '" + mainDbPath + "': " + mainDb->GetLastError();
        LOG(WARNING) << "BUSINESS_LOGIC: main db error: " <<errMsg;
    }

    VLOG(1) <<"DEBUG: (main db) - integrity checking...";
    if(! mainDb->IntegrityCheck()){
        errMsg = "integrity check failed for '" + mainDbPath + "': \n" + mainDb->GetLastError();
        LOG(WARNING) << "BUSINESS_LOGIC: main db error: " <<errMsg;
    }else{
        LOG(INFO) << "Main db: integrity check: OK";
    }

    LOG(INFO) << "Restore db complete [100%]";

    resetRestoreProgress();
}

int CBusinessLogic::getRestoreProgress() const {
    boost::shared_lock< boost::shared_mutex > lock(restore_mtx_);
    return restoreProgress_;
}

bool CBusinessLogic::isRestoreExecuting() const {
    boost::shared_lock< boost::shared_mutex > lock(restore_mtx_);
    return restoreProgress_ > -1;
}

void CBusinessLogic::resetRestoreProgress() {
    boost::unique_lock<boost::shared_mutex> lock(restore_mtx_);
    restoreProgress_ = -1;
}

void CBusinessLogic::SyncDbWithTmp(const string &mainDbPath, const std::function<void(const size_t)> &waitFunc) {

    static std::recursive_mutex sync_;

    std::unique_lock<std::recursive_mutex> lock1(sync_, std::try_to_lock);

    if(! lock1){
        std::unique_lock<std::recursive_mutex> lock2(sync_, std::try_to_lock);
        if(! lock2){
            return;
        }
    }

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

    // start execute queries ONE BY ONE from tmp db
    for(;;){
        res = nullptr;
        waitFunc(200); //sleep to give other connections executed

        // if main db is not connected, try to reconnect
        for (int j = 0; (! mainDb->isConnected()) && j < 20; ++j) {
            LOG(WARNING) <<"Main db is not connected, trying to reconnect [" << (j+1) << "]";
            waitFunc(500);
            mainDb->OpenConnection();
        }

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

        // 3 columns will be returned
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

        //VLOG(1) <<"DEBUG: delete row OK: " <<deleteResult;

    }
}

void CBusinessLogic::CreateOrUseOldTmpDb() {

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

int CBusinessLogic::SaveQueryToTmpDb(const string &query) {

    const auto tmpDb = CSQLiteDB::new_(getTmpDbPath());

    if(! tmpDb->OpenConnection()) {
        string errMsg("can't connect to " + getTmpDbPath() + ": " + tmpDb->GetLastError());
        LOG(WARNING) <<"BUSINESS_LOGIC: " <<errMsg;
        throw BusinessLogicError(errMsg);
    }

    const string insertQuery("INSERT INTO `tmp_querys` (query) VALUES ('" + boost::replace_all_copy(query, "'", "''") + "');"); //replace ' to ''
    return tmpDb->Execute(insertQuery.c_str());
}

void CBusinessLogic::selectPlaceFree(const CSQLiteDB::ptr &dbPtr, const string &selectQuery_sql) {
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
            errorMsg = "ERROR: db returned empty string";
            LOG(WARNING) << errorMsg;
            throw BusinessLogicError(errorMsg);
        }

        //check, if return data is number
        if(result != "0"){
            long long num = std::strtoull(result.c_str(), nullptr, 10 ); //this func return 0 if can't convert to size_t
            if(num <= 0 || num == std::numeric_limits<long long>::max()){
                errorMsg = "can't convert '" + result + "' to number!";
                LOG(WARNING) << "BUSINESS_LOGIC: " << errorMsg;
                throw BusinessLogicError(errorMsg);
            }
        }


        //exclusive access to data!
        boost::unique_lock<boost::shared_mutex> lock(business_logic_mtx_);
        placeFree_ = std::move(result);
    }
}

const string CBusinessLogic::getTmpDbPath() {
    static const string tmpDbPath("temp_db.sqlite3");
    return tmpDbPath;
}
