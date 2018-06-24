﻿#include "CClientSession.h"

#include <string>
#include <iostream>
#include <algorithm>

#include "glog/logging.h"

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

	db = CSQLiteDB::new_();
	LOG_IF(WARNING, ! db->OpenConnection(dbPath, busyTimeout)) << "ERROR: can't connect to db: " <<db->GetLastError();
	db->Excute(string("PRAGMA journal_mode = WAL; PRAGMA encoding = \"UTF-8\"; "
	           "PRAGMA foreign_keys = 1; PRAGMA page_size = " + std::to_string(blockOrClusterSize) + "; PRAGMA cache_size = 2000;").c_str());

	last_ping_ = boost::posix_time::microsec_clock::local_time();

	// first, we send 'ping OK' and wait for client to login
	on_ping();
}

CClientSession::ptr CClientSession::new_(io_context& io_context)
{
	ptr new_(new CClientSession(io_context));
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

	// process the msg]

	// we must make copy of read_buffer_, for quick unlock cs_ mutex
    size_t len = strlen(read_buffer_.get()) - sizeEndOfMsg;
    LOG_IF(INFO,(len <7)||len>max_msg) <<"LENGH" <<len;
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


	VLOG(1) << "DEBUG: received msg '" << inMsg <<"'\n"	<< " DEBUG: received bytes from user '" <<username() <<"': " << bytes;

	if(0 == inMsg.find(u8"login ")){
		on_login(inMsg);
	}
	else if(0 == inMsg.find(u8"ping")){
		on_ping();
	}
	else if(0 == inMsg.find(u8"who")){
		on_clients();
	}
	else if(0 == inMsg.find(u8"fibo ")){
		on_fibo(inMsg);
	}
	else if(0 == inMsg.find(u8"exit")){
		stop();
	}
	else if(inMsg.size() > 10) {
		on_query(inMsg);
	}
	else{
		do_write(string(u8"ERROR: very short command:") + inMsg + "\n");
		LOG(WARNING) << "very short command from client " << username() << ": '" << inMsg << '\'';
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

	if( (now - last_ping_).total_milliseconds() >= (max_timeout-1) ){
		VLOG(1) << "DEBUG: stopping: " << username_ << " - no ping in time" << std::endl;
		stop();
	}

	last_ping_ = boost::posix_time::microsec_clock::local_time();
}

void CClientSession::post_check_ping()
{
	boost::recursive_mutex::scoped_lock lk(cs_);
	timer_.expires_from_now(boost::posix_time::millisec((size_t)max_timeout));
	timer_.async_wait(boost::bind(&CClientSession::on_check_ping, shared_from_this()));
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

	string answer("");

	{
		boost::recursive_mutex::scoped_lock bd_;

		if(! db->isConnected()){
			if(! db->OpenConnection(dbPath, busyTimeout)){
				answer = "ERROR: can't connect to db - " + db->GetLastError();
				LOG(WARNING) << answer;
			}
		}
		else{
			//check if query is 'select' or 'insert/update...'
			if((query.find("select") < 10) || (query.find("SELECT") < 10)){
				//Get Data From DB
				IResult *res = db->ExcuteSelect(query.c_str());

				if (nullptr == res){
					answer = "ERROR: " + db->GetLastError();
					LOG(WARNING) << answer;
				}
				else {
					//Colomn Name
					/*for (int i = 0; i < db->GetColumnCount(); ++i) {
							const char *tmpRes = res->NextColomnName(i);
							answer += (tmpRes ? std::move(string(tmpRes)): "NULL")+ separator;
					}
					answer += '\n';*/

					//Data
					while (res->Next()) {
						for (int i = 0; i < db->GetColumnCount(); i++){
							const char *tmpRes = res->ColomnData(i);
							answer += (tmpRes ? std::move(string(tmpRes)): "NULL")+ separator;

						}
						answer.resize(answer.size() - 1);
						answer += '\n';
					}
					//release Result Data
					res->Release();

					if(answer.empty())
						answer = "EMPTY";
				}
			}
			else{
				db->BeginTransaction();
				int effectedData =  db->Excute(query.c_str());
				db->CommitTransection();

				if (effectedData < 0){
					answer = "ERROR: " + db->GetLastError();
					LOG(WARNING) << answer;
				}
				else{
					answer = "OK: count of effected data - " + std::to_string(effectedData);
				}
			}
		}
	}
	//VLOG(1) <<answer;
	do_write(answer+endOfMsg);
}

void CClientSession::on_query(const string &msg)
{
	if( !started() )
		return;

    io_context_.post(boost::bind(&CClientSession::do_ask_db, shared_from_this(), msg));

	do_read();
}



void CClientSession::do_read()
{
	//VLOG(1) << "DEBUG: do read" << std::endl;

	{
		boost::recursive_mutex::scoped_lock lk(cs_);
		ZeroMemory(read_buffer_.get(), sizeof(char) * max_msg);
	}

	post_check_ping();

    sock_.async_receive(buffer(read_buffer_.get(), max_msg), 0,
                        boost::bind(&CClientSession::on_read, shared_from_this(), _1, _2));

}

void CClientSession::do_write(const string &msg)
{
	if( !started() )
		return;

	boost::recursive_mutex::scoped_lock lk(cs_);
    ZeroMemory(write_buffer_.get(), sizeof(char) * max_msg);
	std::copy(msg.begin(), msg.end(), write_buffer_.get());

	sock_.async_write_some(buffer(write_buffer_.get(), msg.size()),
						   boost::bind(&CClientSession::on_write, shared_from_this(), _1, _2));
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