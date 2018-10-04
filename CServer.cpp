#include "CServer.h"

#include <boost/asio/io_context.hpp>
#include "glog/logging.h"

void CServer::Start()
{
	LOG(INFO) << "Server started at: " << acceptor_.local_endpoint() << std::endl;

	// init first client
	VLOG(1) << "DEBUG: init first client" << std::endl;
	CClientSession::ptr client = CClientSession::new_(io_context_, maxTimeout_, businessLogic_);

	// accept first client
	VLOG(1) << "DEBUG: accept first client";
	acceptor_.async_accept(client->sock(), bind(&CServer::do_accept, this, client, _1));

	// start listen
	VLOG(1) << "DEBUG: start listening" << std::endl;
	start_listen();

	threads.join_all(); 
}

void CServer::do_accept(CClientSession::ptr client, const boost::system::error_code & err)
{
  // if err != 0, CHECK will write to log and exit with error.
	CHECK(!err) << "\nAccepting client faild with error: " << err << ". Closing server...";

	client->start();
	CClientSession::ptr new_client = CClientSession::new_(io_context_, maxTimeout_, businessLogic_);

	VLOG(1) << "DEBUG: accept NEXT client";
	acceptor_.async_accept(new_client->sock(), bind(&CServer::do_accept, this, new_client, _1));
}

void CServer::start_listen()
{
	// run io_context in thread_num_ of threads
	for( int i = 0; i < thread_num_; ++i )
	{
		threads.create_thread(
			[this]()
			{
				this->io_context_.run();
			}
		);
	}
}
