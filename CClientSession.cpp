#include "CClientSession.h"

boost::recursive_mutex clients_cs;
typedef boost::shared_ptr<CClientSession> client_ptr;
typedef std::vector<client_ptr> cli_ptr_vector;
cli_ptr_vector clients;

CClientSession::CClientSession(io_context &io_context, const size_t maxTimeout,
                               CClientSession::businessLogic_ptr businessLogic)
        : sock_(io_context)
        , started_(false)
        , timer_(io_context)
        , maxTimeout_(maxTimeout)
        , clients_changed_(false)
        , username_("user")
        , io_context_(io_context)
        , write_buffer_({ new char[MAX_WRITE_BUFFER] })
        , read_buffer_({ new char[MAX_READ_BUFFER] })
        , businessLogic_(std::move(businessLogic))
{}

CClientSession::~CClientSession() { /*VLOG(1) << "DEBUG: ~CClientSession()";*/ }

void CClientSession::start()
{
    started_ = true;

    {
        boost::recursive_mutex::scoped_lock lk(clients_cs);
        clients.push_back(shared_from_this());
    }

    db = CSQLiteDB::new_(dbPath, sqlCountOfAttempts, sqlWaitTime);
    db->setWaitFunction([=](size_t ms){
        // Construct a timer without setting an expiry time.
        deadline_timer timer(io_context_);
        // Set an expiry time relative to now.
        timer.expires_from_now(boost::posix_time::millisec(ms));
        // Wait for the timer to expire.
        timer.wait();
    });
    LOG_IF(WARNING, ! db->OpenConnection()) << "ERROR: can't connect to db: " <<db->GetLastError();
    IResult *res = db->ExecuteSelect(string("PRAGMA journal_mode = WAL; PRAGMA encoding = \"UTF-8\"; "
                                                      "PRAGMA foreign_keys = 1; PRAGMA page_size = " + std::to_string(blockOrClusterSize) + "; PRAGMA cache_size = -3000;").c_str());
    res->ReleaseStatement();

    last_ping_ = boost::posix_time::microsec_clock::local_time();

    do_read();
}

CClientSession::ptr CClientSession::new_(io_context& io_context, const size_t maxTimeout, businessLogic_ptr businessLogic)
{
    ptr new_(new CClientSession(io_context, maxTimeout, std::move(businessLogic)));
    return new_;
}

void CClientSession::stop()
{
    {
        boost::recursive_mutex::scoped_lock lk(cs_);

        if( !started_ )
            return;

        VLOG(1) << "DEBUG: stop client: " << username();

        started_ = false;
        sock_.cancel();
        //There is a bug: https://svn.boost.org/trac10/ticket/7611#no1
        //so in multithread mode we mustn't stop socket, because asio in some time can run async_read/write on socket exactly when we close socket
        //and OS send SIGSEGV to server :(
        //To prevent SIGSEGV, I don't close socket. Socket will be closed automaticaly, after destructor CClientSession::~CClientSession()
        //sock_.close();
    }

    VLOG(1) << "DEBUG: socket was stopped for client: " << username();

    ptr self = shared_from_this();

    {
        boost::recursive_mutex::scoped_lock lk(clients_cs);
        auto it = std::find(clients.begin(), clients.end(), self);
        if(it != clients.end())
            clients.erase(it);
    }
    update_clients_changed();
}

bool CClientSession::started() const
{
    boost::recursive_mutex::scoped_lock lk(cs_);
    return started_;
}

ip::tcp::socket& CClientSession::sock()
{
    boost::recursive_mutex::scoped_lock lk(cs_);
    return sock_;
}

string CClientSession::username() const
{
    boost::recursive_mutex::scoped_lock lk(cs_);
    return username_;
}

void CClientSession::set_clients_changed()
{
    boost::recursive_mutex::scoped_lock lk(cs_);
    clients_changed_ = true;
}

void CClientSession::on_read(const error_code &err, size_t bytes)
{
    if( err )
        stop();

    if( !started() )
        return;

    try {
        // process the msg]

        // we must make copy of read_buffer_, for quick unlock cs_ mutex
        size_t len = strlen(read_buffer_.get()) - sizeEndOfMsg;

        if((len < 7)||(len > MAX_READ_BUFFER))
            len = 1;

        string inMsg(len, char(0));
        size_t cleanMsgSize = 0;
        {
            boost::recursive_mutex::scoped_lock lk(cs_);
            for (size_t i = 0; i < inMsg.size(); ++i) {
                //continue if read_buffer_[i] == one of (\r, \n, NULL)
                if((read_buffer_[i] != char(0)) && (read_buffer_[i] != char(13))  && (read_buffer_[i] != char(10)))
                    inMsg[cleanMsgSize++] = read_buffer_[i];
            }
        }
        inMsg.resize(cleanMsgSize);


        VLOG(1) << "DEBUG: received msg '" << inMsg << "\nDEBUG: received bytes from user '" <<username() <<"' bytes: " << bytes
                << " delay: " <<(boost::posix_time::microsec_clock::local_time() - last_ping_).total_milliseconds() <<"ms";

        if(businessLogic_->isRestoreExecuting()){
            VLOG(1) <<"DEBUG: Server is busy at the moment. ";
            do_write("Server is busy at the moment. Database restore progress [" + std::to_string(businessLogic_->getRestoreProgress()) + "%]", false);
            stop();

        }else if(0 == inMsg.find(u8"UPDATE Config SET PlaceFree")){
            int progress = businessLogic_->getBackUpProgress();
            string msg;

            if(progress > -1 && progress <100) {
                msg = "'UPDATE Config SET PlaceFree...'. Backup in progress [" + std::to_string(progress) + "%]";
            }else{
                businessLogic_->updatePlaceFree(db, inMsg, "select PlaceFree from Config;");
                msg = "NONE";
            }

            do_write(msg);

        }else if(0 == inMsg.find(u8"get_place_free")) {
            try {
                businessLogic_->checkPlaceFree(db, "select PlaceFree from Config;");
                do_write(businessLogic_->getCachedPlaceFree());
            } catch (BusinessLogicError &e){
                // if an error occur, send last PlaceFree. If last PlaceFree == -1, send 0
                do_write(businessLogic_->getCachedPlaceFree() == "-1" ? "0" : businessLogic_->getCachedPlaceFree());
            }

        }else if(0 == inMsg.find(u8"restore_db")){
            do_restore_db();

        }else if(0 == inMsg.find(u8"backup_db")){
            auto self = shared_from_this();
            io_context_.post([self, this](){ //async call
                do_db_backup();
            });

        }else if(0 == inMsg.find(u8"get_db_backup_progress")){
            do_ask_db_backup_progress();

        }else if(0 == inMsg.find(u8"get_db_backup")){
            do_get_db_backup();

        }else if(0 == inMsg.find(u8"login ")){
            on_login(inMsg);

        }else if(0 == inMsg.find(u8"ping")){
            on_ping();

        }else if(0 == inMsg.find(u8"who")){
            on_clients();

        }else if(0 == inMsg.find(u8"fibo ")){
            on_fibo(inMsg);

        }else if(0 == inMsg.find(u8"exit")){
            stop();

        }else if(inMsg.size() > 10) {
            on_query(inMsg);

        }else{
            do_write(string(u8"ERROR: very short command:") + inMsg + "\n");
            LOG(WARNING) << "very short command from client " << username() << ": '" << inMsg << '\'';
        }

    }catch (BusinessLogicError &e){
        LOG(WARNING) <<"BusinessLogic [" <<e.what() <<"]";
        do_write(e.what());
    }catch (...){
        stop();
    }

}

void CClientSession::on_login(const string &msg)
{
    boost::recursive_mutex::scoped_lock lk(cs_);
    std::istringstream in(msg);

    in >> username_ >> username_;

    VLOG(1) << "DEBUG: logged in: " << username_ << std::endl;

    do_write(string("login ok\n"));
    //update_clients_changed(); // this caused bug with dead lock when restore or backup db, I didn't tested fixed it or not
}

void CClientSession::on_ping()
{
    boost::recursive_mutex::scoped_lock lk(cs_);
    do_write(clients_changed_ ? string("ping client_list_changed\n") : string(u8"ping OK\n"));

    // we have notified client, that clients list was changed yet,
    // so clients_changed_ should be false
    clients_changed_ = false;
}

void CClientSession::on_clients()
{
    cli_ptr_vector clients_copy;
    {
        boost::recursive_mutex::scoped_lock lk(clients_cs);
        clients_copy = clients;
    }

    string msg;

    for(const auto &it : clients_copy )
        msg += it->username() + " ";

    do_write(string("clients: " + msg + "\n"));
}

void CClientSession::on_check_ping()
{
    boost::recursive_mutex::scoped_lock lk(cs_);
    boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
    time_duration::tick_type delay = (now - last_ping_).total_milliseconds();

    if( delay >= time_duration::tick_type(maxTimeout_ - 1) ){
        VLOG(1) << "DEBUG: stopping: " << username_ << " - no ping in time " <<delay;
        stop();
    }

    last_ping_ = boost::posix_time::microsec_clock::local_time();
}

void CClientSession::post_check_ping()
{
    boost::recursive_mutex::scoped_lock lk(cs_);
    timer_.expires_from_now(boost::posix_time::millisec(maxTimeout_));
    timer_.async_wait(bind(&CClientSession::on_check_ping, shared_from_this()));
}

void CClientSession::do_get_fibo(const size_t &n)
{
    //return n<=2 ? n: get_fibo(n-1) + get_fibo(n-2);
    size_t a = 1, b = 1;
    for( size_t i = 3; i <= n; i++ )
    {
        size_t c = a + b;
        a = b; b = c;
    }

    do_write(string("fibo: " + std::to_string(b) + "\n"), false);
}

void CClientSession::on_fibo(const string &msg)
{
    std::istringstream in(msg);
    in.ignore(5);
    size_t n;
    in >> n;
    //msg.substr()
    auto self = shared_from_this();
    io_context_.post([self, this, &n](){ do_get_fibo(n); });

    do_read();
}



void CClientSession::do_ask_db(string &query)
{
    if( ! started() )
        return;

    boost::recursive_mutex::scoped_lock bd_;

    string answer;

    if(! db->isConnected()){
        if(! db->OpenConnection()){
            answer = "ERROR: " + db->GetLastError();
        }
    }else{
        //check if query is 'select' or 'insert/update...'
        if((query.find("select") < 10) || (query.find("SELECT") < 10)){
            //Get Data From DB
            IResult *res = db->ExecuteSelect(query.c_str());

            if (nullptr == res){
                answer = "ERROR: undefined";
                LOG(WARNING) << answer;
            } else {
                //Colomn Name
                /*for (int i = 0; i < db->GetColumnCount(); ++i) {
                        const char *tmpRes = res->NextColomnName(i);
                        answer += (tmpRes ? std::move(string(tmpRes)): "NULL")+ separator;
                }
                answer += '\n';*/

                //Data
                while (res->Next()) {
                    for (int i = 0; i < res->GetColumnCount(); i++){
                        const char *tmpRes = res->ColomnData(i);
                        answer += (tmpRes ? std::move(string(tmpRes)): "None")+ separator;

                    }
                    answer.resize(answer.size() - 1);
                    answer += '\n';
                }
                //release Result Data
                res->ReleaseStatement();

                if(answer.empty()){
                    answer = "NONE";
                }else{
                    answer.erase(answer.size() - 1);
                }
            }
        }else{

            int effectedData = 0;
            int backUpProgress = businessLogic_->getBackUpProgress();

            if(backUpProgress < 0 || backUpProgress == 100){
                effectedData =  db->Execute(query.c_str());
            }else{
                effectedData = businessLogic_->SaveQueryToTmpDb(query);
                VLOG(1) <<"DEBUG: insert to tmp db while backuping. Effected data: " <<effectedData;
            }


            if (effectedData < 0){
                answer = std::string("ERROR: effected data < 0! : " + db->GetLastError());
                LOG(WARNING) << answer;
            }else{
                //answer = "OK: count of effected data(" + std::to_string(effectedData) +")";
                answer = "NONE";
            }
        }
    }

    //VLOG(1) <<(int)answer[0]<<(int)answer[1];
    do_write(answer, false);
}

void CClientSession::on_query(const string &msg)
{
    if( !started() )
        return;

    io_context_.post(bind(&CClientSession::do_ask_db, shared_from_this(), msg));

    do_read();
}



void CClientSession::do_read()
{
    //VLOG(1) << "DEBUG: do read" << std::endl;

    {
        boost::recursive_mutex::scoped_lock lk(cs_);
        ZeroMemory(read_buffer_.get(), sizeof(char) * MAX_READ_BUFFER);
    }

    post_check_ping();

    async_read(sock_, buffer(read_buffer_.get(), MAX_READ_BUFFER), boost::asio::transfer_at_least(1),
                        bind(&CClientSession::on_read, shared_from_this(), _1, _2));

}


void CClientSession::do_write(const string &msg, bool read_on_write)
{
    if( !started() )
        return;

    {
        boost::recursive_mutex::scoped_lock lk(cs_);
        ZeroMemory(write_buffer_.get(), sizeof(char) * MAX_WRITE_BUFFER);
    }

    std::copy(msg.begin(), msg.end(), write_buffer_.get());

    auto self(shared_from_this());

    async_write(sock_, buffer(write_buffer_.get(), msg.size()),
                [this, self, read_on_write](error_code, size_t){
                    if(read_on_write){
                        do_read();
                    }
                });

}

void CClientSession::do_db_backup() {
    int backUpStatus = businessLogic_->getBackUpProgress();

    //check if if backuping is executing. (!= -1). If not, send 0% and start backup
    if(-1 == backUpStatus){
        string startBackupMsg = "backup in progress [0%]";
        VLOG(1) << "DEBUG: " <<startBackupMsg;
        do_write(startBackupMsg);
        //block this async func and make backup
        backUpStatus = businessLogic_->backupDb(db, bakDbPath);
    }

    string msg;
    if(-1 == backUpStatus){
        //this will be executed after backup is finished with error
        msg = "ERROR: db was not backuped: " + db->GetLastError();
        LOG(WARNING) << msg;
        do_write(msg, false);
        return;
    }else if(100 == backUpStatus){
        businessLogic_->setTimeoutOnNextBackupCmd(io_context_, newBackupTimeout);

        // start executing query from tmp db in background
        auto self = shared_from_this();
        io_context_.post([self, this](){ //async call
            try {
                    businessLogic_->SyncDbWithTmp(dbPath, [=](size_t ms) {
                        // Construct a timer without setting an expiry time.
                        deadline_timer timer(io_context_);
                        // Set an expiry time relative to now.
                        timer.expires_from_now(boost::posix_time::millisec(ms));
                        // Wait for the timer to expire.
                        timer.wait();
                    });
            }catch (BusinessLogicError &e){
                LOG(WARNING) <<"Sync Error [" <<e.what() <<"]";
            }
        });

        msg = "backup db complete [100%]";
        LOG(INFO) << "DEBUG: " <<msg;
    }else if(backUpStatus > 0 && backUpStatus < 100){
        msg = "backup in progress [" + std::to_string(businessLogic_->getBackUpProgress()) + "%]";
        //VLOG(1) << "DEBUG: " <<msg;
    }

    do_write(msg);
}

void CClientSession::do_ask_db_backup_progress() {
    string msg;
    int progress = businessLogic_->getBackUpProgress();

    if(progress == -1){
        msg = "backup not started";
    }else{
        msg = "backup in progress [" + std::to_string(progress) + "%]";
    }

    VLOG(1) << "DEBUG: " <<msg;
    do_write(msg);
}

void CClientSession::on_backup_chunk_write(const CClientSession::error_code &err, size_t bytes) {
    //VLOG(1) <<"sanded bytes: " <<bytes << " delay: " <<(boost::posix_time::microsec_clock::local_time() - last_ping_).total_milliseconds();
    if( err ){
        LOG(WARNING) <<"ERROR: can't send file to client: " <<err;
        do_write("ERROR: " + err.message());
        return;
    }

    post_check_ping();

    do_backup_chunk_write();
}

void CClientSession::do_backup_chunk_write() {
    if( ! started() )
        return;

    if( ! backupReader_.nextChunk() ){
        backupReader_.close();
        do_read();
        return;
    }

    async_write(sock_, buffer(backupReader_.getCurrentChunk(), backupReader_.getCurrentChunkSize()),
                           bind(&CClientSession::on_backup_chunk_write, shared_from_this(), _1, _2));
}

void CClientSession::do_get_db_backup() {

    if(! businessLogic_->isBackupExist(bakDbPath)){
        LOG(INFO) <<"Backup doesn't exist";
        do_write("NONE : Backup doesn't exist, you can send 'backup_db' to create new and 'get_db_backup_progress' to check backup progress!");
        return;
    }

    if(! backupReader_.open(bakDbPath)){
        string errMsg("can't open backup file [" + bakDbPath + "]");
        LOG(WARNING) << errMsg;
        do_write("ERROR: " + errMsg);
        return;
    }

    if(backupReader_.isEOF()){ //file is empty. Send empty string
        do_write("");
        return;
    }

    do_backup_chunk_write();
}

void CClientSession::do_restore_db() {
    string msg;
    int progress = businessLogic_->getBackUpProgress();

    if(progress > -1 && progress < 100){
        msg = "Restore can't be executed. Backup in progress [" + std::to_string(progress) + "%]";
        LOG(WARNING) <<msg;
    }else{
        if(! businessLogic_->prepareBeforeRestore(dbPath, restoreDbPath)){
            msg = "Restore can't be executed. System error or restore db corrupted";
            LOG(WARNING) <<msg;
        }else{
            auto self = shared_from_this();
            cli_ptr_vector clients_copy;
            {
                boost::recursive_mutex::scoped_lock lk(clients_cs);
                clients_copy = clients;
            }

            // stop all clients except current
            for(const auto &it : clients_copy ){
                if(it != self)
                    it->stop();
            }

            io_context_.post([self, this](){ //async call
                // wait, before all existing call to db will be complete, and clients go away
                deadline_timer timer(io_context_);
                timer.expires_from_now(boost::posix_time::millisec(5000));
                timer.wait();

                businessLogic_->restoreDbFromFile(dbPath, restoreDbPath);
                stop(); // stopping current client
                return;
            });

            msg = "Restore db in progress [0%]";
            LOG(INFO) <<msg;
        }
    }

    do_write(msg);
}

void update_clients_changed()
{
    cli_ptr_vector clients_copy;
    {
        boost::recursive_mutex::scoped_lock lk(clients_cs);
        clients_copy = clients;
    }

    for(const auto &it : clients_copy )
        it->set_clients_changed();
}
