#include "CClientSession.h"
#include <utility>

boost::recursive_mutex clients_cs;
typedef boost::shared_ptr<CClientSession> client_ptr;
typedef std::vector<client_ptr> cli_ptr_vector;
cli_ptr_vector clients;

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
    db->Execute(string(/*"PRAGMA journal_mode = WAL; */"PRAGMA encoding = \"UTF-8\"; "
                                                      "PRAGMA foreign_keys = 1; PRAGMA page_size = " + std::to_string(blockOrClusterSize) + "; PRAGMA cache_size = -3000;").c_str());

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

        VLOG(1) << "DEBUG: stop client: " << username() << std::endl;

        started_ = false;
        sock_.close();
    }

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
        //LOG_IF(INFO,(len <7)||len>max_msg) <<"LENGH: " <<len <<" "<<read_buffer_.get();
        if((len < 7)||(len > MAX_READ_BUFFER))
            len = 1;

        string inMsg(len, char(0));
        size_t cleanMsgSize = 0;
        {
            boost::recursive_mutex::scoped_lock lk(cs_);
            for (size_t i=0; i < inMsg.size(); ++i) {
                //continue if read_buffer_[i] == one of (\r, \n, NULL)
                if((read_buffer_[i] != char(0)) && (read_buffer_[i] != char(13))  && (read_buffer_[i] != char(10)))
                    inMsg[cleanMsgSize++] = read_buffer_[i];
            }
        }
        inMsg.resize(cleanMsgSize);


        VLOG(1) << "DEBUG: received msg '" << inMsg << "\nDEBUG: received bytes from user '" <<username() <<"' bytes: " << bytes
                << " delay: " <<(boost::posix_time::microsec_clock::local_time() - last_ping_).total_milliseconds();

        if(0 == inMsg.find(u8"UPDATE Config SET PlaceFree")){
            businessLogic_->updatePlaceFree(db, inMsg, "select PlaceFree from Config;");
            do_write("NONE");

        }else if(0 == inMsg.find(u8"get_place_free")) {
            try {
                businessLogic_->checkPlaceFree(db, "select PlaceFree from Config;");
                do_write(businessLogic_->getCachedPlaceFree());
            } catch (BusinessLogicError &e){
                // if an error occur, send last PlaceFree. If last PlaceFree == -1, send 0
                do_write(businessLogic_->getCachedPlaceFree() == "-1" ? "0" : businessLogic_->getCachedPlaceFree());
            }

        }else if(0 == inMsg.find(u8"backup_db")){
            auto self = shared_from_this();
            io_context_.post([self, this](){ //async call
                do_db_backup();
            });

        }else if(0 == inMsg.find(u8"get_db_backup_progress")){
            do_ask_db_backup_progress();

        }else if(0 == inMsg.find(u8"get_db_backup")){

            bool isBackUpExist = false;
            {// next thread will wait here, until current thread reseting backUpStatus if it 100%
                boost::recursive_mutex::scoped_lock lk(clients_cs);
                isBackUpExist = businessLogic_->getBackUpProgress() == 100; // backup exists if getBackUpProgress == 100%. Backup can be sent to a client only one time

                if(isBackUpExist){
                    businessLogic_->resetBackUpProgress();
                }
            }

            if(! isBackUpExist){
                LOG(INFO) <<"Client sent 'get_db_backup', but backup doesn't exist or have been sent to another client";
                do_write("NONE: Backup doesn't exist, you can send 'backup_db' to create new and 'get_db_backup_progress' to check backup progress!");
                return;
            }

            //TODO: sending  bak

            if(! backupReader_.open(bakDbPath)){
                string errMsg("can't open backup file [" + bakDbPath + "]");
                LOG(WARNING) << errMsg;
                do_write("ERROR: " + errMsg);
                return;
            }

            if(backupReader_.isEOF()){ //file is empty
                do_write("");
                return;
            }

            auto self = shared_from_this();
            sock_.async_write_some(buffer(backupReader_.nextChunk(), backupReader_.chunkSize()),
                                   [this, self](const error_code &err, size_t bytes)
                                   {
                                       const char *nextChunk = backupReader_.nextChunk();
                                       if( ! started() || ! nextChunk )
                                           return;

                                       sock_.async_write_some(buffer(backupReader_.nextChunk(), backupReader_.chunkSize()),
                                                              [this, self](const error_code &err, size_t bytes)
                                                              {

                                                              }
                                   }
            );

            io_context_.post([self, this](){ //async call

            });

            // after this server 'think' that backup doesn't exist!

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
        LOG(WARNING) <<"BusinessLogic error [" <<e.what() <<"]";
        do_write(e.what());
    }

}

void CClientSession::on_login(const string &msg)
{
    boost::recursive_mutex::scoped_lock lk(cs_);
    std::istringstream in(msg);

    in >> username_ >> username_;

    VLOG(1) << "DEBUG: logged in: " << username_ << std::endl;

    do_write(string("login ok\n"));
    update_clients_changed();
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

    //for( cli_ptr_vector::const_iterator b = copy.begin(), e = copy.end(); b != e; ++b )
    //	msg += (*b)->username() + " ";

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

void CClientSession::on_write(const error_code &err, size_t bytes)
{
    do_read();
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

    do_write(string("fibo: " + std::to_string(b) + "\n"));
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
                VLOG(1) <<"DEBUG: insert to tmp while backuping. Effected data: " <<effectedData;
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
    do_write(answer);
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

    sock_.async_receive(buffer(read_buffer_.get(), MAX_READ_BUFFER), 0,
                        bind(&CClientSession::on_read, shared_from_this(), _1, _2));

}

void CClientSession::do_write(const string &msg)
{
    if( !started() )
        return;

    {
        boost::recursive_mutex::scoped_lock lk(cs_);
        ZeroMemory(write_buffer_.get(), sizeof(char) * MAX_WRITE_BUFFER);
    }

    std::copy(msg.begin(), msg.end(), write_buffer_.get());

    sock_.async_write_some(buffer(write_buffer_.get(), msg.size()),
                           bind(&CClientSession::on_write, shared_from_this(), _1, _2));
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
    }else if(100 == backUpStatus){

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

void update_clients_changed()
{
    cli_ptr_vector clients_copy;
    {
        boost::recursive_mutex::scoped_lock lk(clients_cs);
        clients_copy = clients;
    }

    for(const auto &it : clients_copy )
        it->set_clients_changed();

    //old variant
    //for( cli_ptr_vector::iterator b = copy.begin(), e = copy.end(); b != e; ++b )
    //	(*b)->set_clients_changed();
}
