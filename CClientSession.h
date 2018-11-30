﻿#ifndef CS_MINISQLITESERVER_CCLIENTSESSION_H
#define CS_MINISQLITESERVER_CCLIENTSESSION_H
#pragma once

#include "main.h"
#include "CSQLiteDB.h"
#include "glog/logging.h"
#include "CBusinessLogic.h"
#include "CBinaryFileReader.h"

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <string>
#include <algorithm>
#include <utility>

using namespace boost::asio;
using namespace boost::posix_time;
using boost::scoped_array;
using std::string;
using std::move;

void update_clients_changed();

class CClientSession : public boost::enable_shared_from_this<CClientSession>
							, boost::noncopyable{
private:
	typedef boost::system::error_code error_code;
	using businessLogic_ptr = boost::shared_ptr<CBusinessLogic>;

    explicit CClientSession(io_context &io_context, size_t maxTimeout, businessLogic_ptr businessLogic);

public:

	typedef boost::shared_ptr<CClientSession> ptr;

	// init and start do_read()
	void start();

	// class factory. scoped_array = Return ptr to this class
	static ptr new_(io_context& io_context, size_t maxTimeout, businessLogic_ptr businessLogic);

	// stop working with current client and remove it from clients
	void stop();

	// return started
	bool started() const;

	// return link to socket of current client
	ip::tcp::socket& sock();

	// get user name
	string username() const;
	
	// set flag clients changed to true, then client session notify	
	// its client, that clients list was changed
	void set_clients_changed();

private:
	void on_read(const error_code &err, size_t bytes);

	void on_login(const string &msg);

	void on_ping();

	void on_clients();

	void on_check_ping();

	void post_check_ping();

	void on_write(const error_code &err, size_t bytes);

    void on_backup_chunk_write(const CClientSession::error_code &err, size_t bytes);

		void do_get_fibo(const size_t &n);

		void on_fibo(const string &msg);

	void do_ask_db(string &query);

	void on_query(const string &msg);

	void do_read();

	void do_write(const string &msg);

	void do_backup_chunk_write();

	void do_db_backup();

	void do_ask_db_backup_progress();

	void do_get_db_backup();

private:

	mutable boost::recursive_mutex cs_;
	enum{ MAX_WRITE_BUFFER = 20971520, MAX_READ_BUFFER = 500*1024 };
	const size_t maxTimeout_;
    //const char endOfMsg[0] = {};
	const size_t sizeEndOfMsg = 1;
	scoped_array<char> read_buffer_;
	scoped_array<char>  write_buffer_;
	io_context &io_context_;
	ip::tcp::socket sock_;
	bool started_;

	boost::posix_time::ptime last_ping_;
	deadline_timer timer_;

	string username_;
	bool clients_changed_;

	mutable  boost::recursive_mutex db_;
	CSQLiteDB::ptr db;
	const char separator = '|';

    businessLogic_ptr businessLogic_;
    CBinaryFileReader backupReader_;
};

#endif //CS_MINISQLITESERVER_CCLIENTSESSION_H