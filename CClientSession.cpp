#include "CClientSession.h"

#include <string>
#include <iostream>
#include <glog/logging.h>
#include <algorithm>

boost::recursive_mutex clients_cs;
typedef boost::shared_ptr<CClientSession> client_ptr;
typedef std::vector<client_ptr> cli_ptr_vector;
cli_ptr_vector clients;

void CClientSession::start()
{
	{
		boost::recursive_mutex::scoped_lock lk(clients_cs);
		clients.push_back(shared_from_this());
	}

	boost::recursive_mutex::scoped_lock lk(cs_);
	started_ = true;
	last_ping_ = boost::posix_time::microsec_clock::local_time();

	// first, we wait for client to login
	on_ping();
	//do_read();
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
	string inMsg(strlen(read_buffer_.get()) - sizeEndOfMsg, char(0));
	size_t cleanMsgSize = 0;
	{
		boost::recursive_mutex::scoped_lock lk(cs_);
        for (size_t i=0; i < inMsg.size(); ++i) {
        	if((read_buffer_[i] != char(0)) && (read_buffer_[i] != char(13))  && (read_buffer_[i] != char(10)))
			    inMsg[cleanMsgSize++] = read_buffer_[i];
        }
	}
	inMsg.resize(cleanMsgSize);


	VLOG(1) << "DEBUG: received msg: '" << inMsg;
	VLOG(1)	<< " DEBUG: received bytes: " << bytes;

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
	else {
		on_query(inMsg);
		//do_write(/*string(u8"FAIL: very short command:") + */inMsg + "\n");
		//LOG(WARNING) << "very short command from client " << username() << ": '" << inMsg << '\'';
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

//void do_ping()
//{
//	do_write("ping\n");
//}

//void do_ask_clients()
//{
//	do_write("ask_clients\n");
//}

void CClientSession::on_check_ping()
{
	boost::recursive_mutex::scoped_lock lk(cs_);
	boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
	if( (now - last_ping_).total_milliseconds() > max_timeout )
	{
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

void CClientSession::do_get_fibo(const size_t n)
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



void CClientSession::do_ask_db(const string query)
{
	string answer;

	// try to connect to ODBC driver
	/*ODBCDatabase::CDatabase db(L";,;");

	// if connected, send query to db
	if( db.ConnectedOk() )
	{
		db << query;

		// get answer from db
		db >> answer;
	}*/

	for (int i = 0; i < 10000; ++i) {
		i;
	}


	do_write(answer+"\n");
}

void CClientSession::on_query(const string msg)
{

	//io_context_.post(boost::bind(&CClientSession::do_ask_db, shared_from_this(), msg));
    auto self = shared_from_this();
	io_context_.post([self, this, &msg](){ do_ask_db(msg); });

	do_read();
}



void CClientSession::do_read()
{
	VLOG(1) << "DEBUG: do read" << std::endl;

	ZeroMemory(read_buffer_.get(), sizeof(char) * max_msg);

    sock_.async_receive(buffer(read_buffer_.get(), max_msg), 0,
                        boost::bind(&CClientSession::on_read, shared_from_this(), _1, _2)
    );

	/*async_read(sock_, buffer(read_buffer_.get(), max_msg),
			   boost::bind(&CClientSession::read_complete, shared_from_this(), _1, _2),
			   boost::bind(&CClientSession::on_read, shared_from_this(), _1, _2)
	);*/

	//post_check_ping();
}

void CClientSession::do_write(const string &msg)
{
	if( !started() )
		return;

	boost::recursive_mutex::scoped_lock lk(cs_);
	std::copy(msg.begin(), msg.end(), write_buffer_.get());

	sock_.async_write_some(buffer(write_buffer_.get(), msg.size()),
						   boost::bind(&CClientSession::on_write, shared_from_this(), _1, _2));
}

size_t CClientSession::read_complete(const error_code &err, size_t bytes)
{
	if( err ) {
		return 0;
	}

	string in(read_buffer_.get());
	if(!in.empty()){
	/*if(in.find("\0") < in.size())
	    in.replace(in.find("\0"),1, "\\0");
*/
    VLOG(1) <<in <<"***bytes:" << bytes <<"***strlen" <<strlen(read_buffer_.get());}else {VLOG(1)<<"EMPTY";}
	//bool found = strstr(read_buffer_, "\n") == read_buffer_ + bytes-1;
	//bool found = strstr(read_buffer_, "!e\n") == read_buffer_ + bytes - 3;
    auto read_buffer_begin = &read_buffer_[0];
    auto read_buffer_end = &read_buffer_[0] + bytes* sizeof(read_buffer_.get());
    bool found = std::search(read_buffer_begin, read_buffer_end, std::begin(endOfMsg), std::end(endOfMsg)) < read_buffer_end;
	//bool found = std::find(read_buffer_, read_buffer_ + bytes, "!e\n") < read_buffer_ + bytes;
	//bool found = std::find(read_buffer_.get(), &read_buffer_[0] + bytes, '<') < read_buffer_[0] + bytes;

	// we read one-by-one until we get to enter, no buffering
	return found ? 0 : 1;
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