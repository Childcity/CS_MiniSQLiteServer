#pragma once
#ifndef CS_MINISQLITESERVER_CSERVER_H
#define CS_MINISQLITESERVER_CSERVER_H

#include "CClientSession.h"
#include "CBusinessLogic.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/thread.hpp>
#include <boost/make_shared.hpp>

using namespace boost::asio;
using boost::asio::ip::tcp;

class CServer{
public:
	explicit CServer(io_context& io_context, const size_t maxTimeout, unsigned short port, unsigned short thread_num)
		: acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
		, io_context_(io_context)
		, thread_num_(thread_num)
		, maxTimeout_(maxTimeout)
        , businessLogic_(boost::make_shared<CBusinessLogic>())
	{ Start(); }

	explicit CServer(io_context& io_context, const size_t maxTimeout, const std::string &ipAddress, unsigned short port, unsigned short thread_num)
		: acceptor_(io_context, tcp::endpoint(ip::address::from_string(ipAddress), port))
		, io_context_(io_context)
		, thread_num_(thread_num)
		, maxTimeout_(maxTimeout)
        , businessLogic_(boost::make_shared<CBusinessLogic>())
	{ Start(); }
	
	CServer(CServer const&) = delete;
	CServer operator=(CServer const&) = delete;

private:
	void Start();

	void do_accept(CClientSession::ptr client, const boost::system::error_code& err);

	void start_listen();

private:
	io_context &io_context_;
	tcp::acceptor acceptor_;
	boost::thread_group threads;
	short thread_num_;
	const size_t maxTimeout_;

    boost::shared_ptr<CBusinessLogic> businessLogic_;
};

#endif //CS_MINISQLITESERVER_CSERVER_H