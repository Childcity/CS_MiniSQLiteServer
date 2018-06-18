#pragma once
#ifndef _CSERVER_
#define _CSERVER_

#include "CClientSession.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/thread.hpp>

using namespace boost::asio;
using boost::asio::ip::tcp;

class CServer{
public:
	explicit CServer(io_context& io_context, unsigned short port, unsigned short thread_num)
		: acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
		, io_context_(io_context)
		, thread_num_(thread_num)
	{ Start(); }

	explicit CServer(io_context& io_context, const std::string &ipAddress, unsigned short port, unsigned short thread_num)
		: acceptor_(io_context, tcp::endpoint(ip::address::from_string(ipAddress), port))
		, io_context_(io_context)
		, thread_num_(thread_num)
	{ Start(); }
	
	CServer(CServer const&) = delete;
	CServer operator=(CServer const&) = delete;

private:
	void Start();

	void do_accept(CClientSession::ptr client, const boost::system::error_code& err);

	void start_listen();

	io_context &io_context_;
	tcp::acceptor acceptor_;
	boost::thread_group threads;
	short thread_num_;
};
#endif