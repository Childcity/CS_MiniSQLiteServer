#include <boost/asio.hpp>
#include <iostream>
#include <fstream>

#include "glog/logging.h"
#include "sqlite3/sqlite3.h"

#include "main.h"
#include "CSQLiteDB.h"
#include "CServer.h"
#include "CConfig.h"

#ifdef WIN32
	#include "Service.h" //For Windows Service
#endif // WIN32


//Global variable declared in main.h
std::string dbPath;
std::string bakDbPath;
size_t sqlWaitTime;
size_t sqlCountOfAttempts;
long blockOrClusterSize;

static int running_from_service = 0;

int main(int argc, char *argv[])
{
	static CConfig cfg(argv[0]);

	if( ! running_from_service )
	{
		cfg.Load();
		LOG_IF(FATAL, CConfig::Status::ERROR == cfg.getStatus()) <<"Check settings file and RESTART" ;

		running_from_service = 1;
#ifdef WIN32
		if( service_register(argc, argv, (LPSTR)cfg.keyBindings.serviceName.c_str()) )
		{
			VLOG(1) << "DEBUG: We've been called as a service. Register service and exit this thread.";
			/* We've been called as a service. Register service
			* and exit this thread. main() would be called from
			* service.c next time.
			*
			* Note that if service_register() succeedes it does
			* not return until the service is stopped.
			* That is why we should set running_from_service
			* before calling service_register and unset it
			* afterwards.
			*/
			return 0;
		}
#endif // WIN32

		LOG(INFO) <<"Started as console application";

		running_from_service = 0;
	}

	try
	{
		boost::asio::io_context io_context;

		// try connect to db and check sqlite settings
        TestSqlite3Settings(&cfg);

        dbPath = cfg.keyBindings.dbPath;
        bakDbPath = cfg.keyBindings.bakDbPath;
        blockOrClusterSize = cfg.keyBindings.blockOrClusterSize;
        sqlWaitTime = static_cast<size_t>(cfg.keyBindings.waitTimeMillisec);
        sqlCountOfAttempts = static_cast<size_t>(cfg.keyBindings.countOfEttempts);

        if(cfg.keyBindings.ipAdress.empty()){
            CServer Server(io_context,
						   static_cast<const size_t>(cfg.keyBindings.timeoutToDropConnection),
						   static_cast<unsigned short>(cfg.keyBindings.port),
						   static_cast<unsigned short>(static_cast<short>(cfg.keyBindings.threads)));
        }
        else {
			CServer Server(io_context,
						   static_cast<const size_t>(cfg.keyBindings.timeoutToDropConnection),
						   cfg.keyBindings.ipAdress, static_cast<unsigned short>(cfg.keyBindings.port),
						   static_cast<unsigned short>(cfg.keyBindings.threads));
		}

	} catch(std::exception &e) {
		LOG(FATAL) << "Server has been crashed: " << e.what() << std::endl;
	}
	return 0;
}

void TestSqlite3Settings(CConfig *cfg){

    VLOG(1) <<"DEBUG: sqlite version: " <<sqlite3_version;
	VLOG(1) <<"DEBUG: using db: " <<cfg->keyBindings.dbPath;
    VLOG(1) <<"DEBUG: using db backup file: " <<cfg->keyBindings.bakDbPath;

	std::ofstream testFile(cfg->keyBindings.bakDbPath, std::ios::in |std::ios::out | std::ios::app | std::ios::binary);
	LOG_IF(FATAL, ! testFile.is_open()) <<"Can't open backup file! (Check permission)";
	testFile.close();

    LOG_IF(FATAL, sqlite3_threadsafe() == 0 ) <<"Sqlite compiled without 'threadsafe' mode";

    CSQLiteDB::ptr db = CSQLiteDB::new_(cfg->keyBindings.dbPath);
	LOG_IF(FATAL, ! db->OpenConnection()) <<"Can't connect to '" << cfg->keyBindings.dbPath << "', check permission or file does not exist. System error: " << db->GetLastError();

	// check tmp db or create new
	CBusinessLogic::CreateOrUseOldTmpDb();

	VLOG(1) <<"DEBUG: connection to db checked - everything is OK";
}

void SafeExit()
{
	LOG(INFO) <<"Server stopped safely.";

	//We just exit from program. All connections wrapped in shared_ptr, so they will be closed soon
	//We don't need to watch them

	google::ShutdownGoogleLogging();
}