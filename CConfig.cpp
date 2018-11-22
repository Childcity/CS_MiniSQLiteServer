#include "CConfig.h"
#include "INIReaderWriter/INIReader.h"
#include "INIReaderWriter/INIWriter.hpp"

#include <ctime>
#include <sys/stat.h>
#include "glog/logging.h"

using INIWriter = samilton::INIWriter;

CConfig::KeyBindings::KeyBindings(const string exePath)
{
	size_t found = exePath.find_last_of("/\\");
	exeName_ = exePath.substr(found + 1);
	exeFolderPath_ = exePath.substr(0, (exePath.size() - exeName_.size()));

    dbPath = exeFolderPath_ + "defaultEmptyDb.sqlite3";
	bakDbPath = exeFolderPath_ + "backup.sqlite3";
	restoreDbPath = exeFolderPath_ + "restore.sqlite3";
	newBackupTimeoutMillisec = 30 * 60 * 1000; //30 min
	blockOrClusterSize = 4096;
	waitTimeMillisec = 50;
	countOfEttempts = 200;

	ipAdress = "127.0.0.1";
	port = 65043;
	threads = 10;
    countOfEttempts = 200;
    timeoutToDropConnection = 5 * 60 * 1000; //5 min

	logDir = exeFolderPath_ + "logs";
	logToStdErr = false;
	stopLoggingIfFullDisk = true;
	verbousLog = 0;
	minLogLevel = 0;

	serviceName = "CS_MiniSQLiteServerSvc";
};

CConfig::CConfig(string exePath)
		: keyBindings(exePath)
		, defaultKeyBindings(exePath) {
}

void CConfig::Load()
{
	try{
		setLocale();
		updateKeyBindings();
		initGlog();

		LOG(INFO) <<"Log lines have next format:"
		          <<"\nLmmdd hh:mm:ss.uuuuuu threadid file:line] msg...";
	}catch (std::exception &e){
		LOG(FATAL) <<"Unexpected error: " <<e.what();
	}
}

CConfig::Status CConfig::getStatus() const
{
	return status;
}

void CConfig::setLocale() const
{
	//std::locale cp1251_locale("ru_RU.CP866");
	//std::locale::global(cp1251_locale);
	setlocale(LC_CTYPE, "");
}

void CConfig::setStatusOk()
{
	status = LOADED_OK;
}

void CConfig::setStatusError()
{
	status = ERROR;
}

string CConfig::getConstructedNameOfLogDir() const
{
	std::time_t t = std::time(nullptr);   // get time now
	std::tm *now = std::localtime(&t);

	std::ostringstream nameStream;
	nameStream << now->tm_mday << "-"
			   << (now->tm_mon + 1) << "-"
			   << (now->tm_year + 1900) << "_"
			   << now->tm_hour << "."
			   << now->tm_min << "."
			   << now->tm_sec;

	return string(nameStream.str());
}

void CConfig::initGlog()
{

	if( this->ERROR == getStatus() ){
		keyBindings = defaultKeyBindings;
	}

	string newFolder = keyBindings.logDir + "/" + getConstructedNameOfLogDir();

	fLS::FLAGS_log_dir = newFolder;
	FLAGS_logtostderr = keyBindings.logToStdErr;
	FLAGS_stop_logging_if_full_disk = keyBindings.stopLoggingIfFullDisk;
	FLAGS_v = static_cast<google::int32>(keyBindings.verbousLog);
	FLAGS_minloglevel = static_cast<google::int32>(keyBindings.minLogLevel);


	#ifdef WIN32
	CreateDirectoryW(ConverterUTF8_UTF16<std::string, std::wstring>(keyBindings.logDir).c_str(), NULL);
	CreateDirectoryW(ConverterUTF8_UTF16<std::string, std::wstring>(newFolder).c_str(), NULL);
	#else
		int ret = mkdir(keyBindings.logDir.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
		if ((0 != ret) && (EEXIST != errno)) {
			//log directory not exist or permission denied or other error
			LOG(FATAL) << "Error: can't create or use log dir '" << keyBindings.logDir << "': " << strerror(errno);
		}

		ret = mkdir(newFolder.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
		if ((0 != ret) && (EEXIST != errno)) {
			//log directory not exist or permission denied or other error
			LOG(FATAL) << "Error: can't create or use log dir '" << newFolder << "': " << strerror(errno);
		}
	#endif // WIN32

	google::InitGoogleLogging(defaultKeyBindings.exeName_.c_str());
}

void CConfig::updateKeyBindings() {
	string pathToSettings = defaultKeyBindings.exeFolderPath_ + SETTINGS_FILE_NAME;
	INIReader settings(pathToSettings);

	if (settings.ParseError() < 0) {
		//!!! This log massage go to stderr ONLY, because GLOG is not initialized yet !
		LOG(WARNING) << "Can't load '" << pathToSettings << "', creating default bindings";
		saveKeyBindings();
	} else {
		//Server settings
		keyBindings.port = settings.GetInteger("ServerSettings", "Port", -1L);
		keyBindings.threads = settings.GetInteger("ServerSettings", "Threads", -1L);
		keyBindings.ipAdress = settings.Get("ServerSettings", "IpAddress", "0");
		keyBindings.timeoutToDropConnection = settings.GetInteger("ServerSettings", "TimeoutToDropConnection", -1L);
		//DB settings
		keyBindings.dbPath = settings.Get("DatabaseSettings", "PathToDatabaseFile", "_a");
		keyBindings.bakDbPath = settings.Get("DatabaseSettings", "PathToDatabaseBackupFile", "_a");
		keyBindings.newBackupTimeoutMillisec = settings.GetInteger("DatabaseSettings", "NewBackupTimeMillisec", -1L);
		keyBindings.restoreDbPath = settings.Get("DatabaseSettings", "PathToDatabaseRestoreFile", "_a");
		keyBindings.blockOrClusterSize = settings.GetInteger("DatabaseSettings", "BlockOrClusterSize", -1L);
		keyBindings.waitTimeMillisec = settings.GetInteger("DatabaseSettings", "WaitTimeMillisec", -1L);
		keyBindings.countOfEttempts = settings.GetInteger("DatabaseSettings", "CountOfAttempts", -1L);
		//Log settings
		keyBindings.logDir = settings.Get("LogSettings", "LogDir", "_a");
		keyBindings.logToStdErr = settings.GetBoolean("LogSettings", "LogToStdErr", false);
		keyBindings.stopLoggingIfFullDisk = settings.GetBoolean("LogSettings", "StopLoggingIfFullDisk", false);
		keyBindings.verbousLog = settings.GetInteger("LogSettings", "DeepLogging", 0L);
		keyBindings.minLogLevel = settings.GetInteger("LogSettings", "MinLogLevel", 0L);
		keyBindings.logDir = settings.Get("LogSettings", "LogDir", "_a");
		//Service settings (only for windows)
		keyBindings.serviceName = settings.Get("ServiceSettings", "ServiceName", "_a");

		if (keyBindings.port <= 0L || keyBindings.threads <= 0L || keyBindings.ipAdress == "0"
			|| keyBindings.blockOrClusterSize == -1L || keyBindings.countOfEttempts <= 0L
			|| keyBindings.waitTimeMillisec <= 0L
			|| keyBindings.timeoutToDropConnection <= 0L
			|| keyBindings.newBackupTimeoutMillisec <= 0L
			|| keyBindings.dbPath == "_a" || keyBindings.dbPath.empty()
			|| keyBindings.restoreDbPath == "_a"
			|| keyBindings.bakDbPath == "_a"
			|| keyBindings.logDir == "_a" || keyBindings.logDir.empty()
			|| keyBindings.serviceName == "_a" || keyBindings.serviceName.empty()) {
			//!!! This log massage go to stderr ONLY, because GLOG is not initialized yet !
			LOG(WARNING) << "Format of settings is not correct. Trying to save settings by default...";
			saveKeyBindings();
			return;
		}

		if(keyBindings.logDir.empty()){
			keyBindings.logDir = defaultKeyBindings.logDir;
		}

		if(keyBindings.serviceName.empty()){
			keyBindings.serviceName = defaultKeyBindings.serviceName;
		}

		if(keyBindings.restoreDbPath.empty()){
			keyBindings.restoreDbPath = defaultKeyBindings.restoreDbPath;
		}

		if(keyBindings.bakDbPath.empty()){
			keyBindings.bakDbPath = defaultKeyBindings.bakDbPath;
		}

		//If we |here|, settings loaded correctly and we can continue
		setStatusOk();
	}
}

void CConfig::saveKeyBindings() {

	setStatusError();

	INIWriter settings(INIWriter::INIcommentType::windowsType, true);

	//Server settings
	settings["ServerSettings"]["Port"] = defaultKeyBindings.port;
	settings["ServerSettings"]["Threads"] = defaultKeyBindings.threads;
	settings["ServerSettings"]["IpAddress"] = defaultKeyBindings.ipAdress;
	settings["ServerSettings"]["TimeoutToDropConnection"]("5 min") = defaultKeyBindings.timeoutToDropConnection;
	//DB settings
	settings["DatabaseSettings"]["PathToDatabaseFile"] = defaultKeyBindings.dbPath;
	settings["DatabaseSettings"]["PathToDatabaseBackupFile"] = defaultKeyBindings.bakDbPath;
	settings["DatabaseSettings"]["PathToDatabaseRestoreFile"] = defaultKeyBindings.restoreDbPath;
	settings["DatabaseSettings"]["NewBackupTimeMillisec"]("Timeout before next backup can be created") = defaultKeyBindings.newBackupTimeoutMillisec;
	settings["DatabaseSettings"]["BlockOrClusterSize"]("Set, according to your file system block/cluster size. This make sqlite db more faster") = defaultKeyBindings.blockOrClusterSize;
	settings["DatabaseSettings"]["WaitTimeMillisec"]("Time, that thread waiting before next attempt to begin 'write transaction'") = defaultKeyBindings.waitTimeMillisec;
	settings["DatabaseSettings"]["CountOfAttempts"]("Number of attempts to begin 'write transaction'") = defaultKeyBindings.countOfEttempts;
	//Log settings
	settings["LogSettings"]["LogDir"] = defaultKeyBindings.logDir;
	settings["LogSettings"]["LogToStdErr"] = defaultKeyBindings.logToStdErr;
	settings["LogSettings"]["StopLoggingIfFullDisk"] = defaultKeyBindings.stopLoggingIfFullDisk;
	settings["LogSettings"]["DeepLogging"] = defaultKeyBindings.verbousLog;
	settings["LogSettings"]["MinLogLevel"] = defaultKeyBindings.minLogLevel;
	//Service settings (only for windows)
	settings["ServiceSettings"]["ServiceName"]("Only for Windows! In *NIX this parameter will be missed") = defaultKeyBindings.serviceName;


	string pathToSettings = defaultKeyBindings.exeFolderPath_ + SETTINGS_FILE_NAME;
	std::ofstream file(pathToSettings, std::ios::trunc);

	if(file.bad()){
		//!!! This log massage go to stderr ONLY, because GLOG is not initialized yet !
		LOG(WARNING) << "Can't open '" + pathToSettings +"' for writing. Default settings did not saved!";
		return;
	}

	file << settings << std::endl
		 << "\n; *** Log parameters ***\n"
			"; Log lines have this form:\n"
			"; \n"
			"; Lmmdd hh : mm:ss.uuuuuu threadid file : line] msg...\n"
			"; \n"
			"; where the fields are defined as follows :\n"
			"; \n"
			"; L                A single character, representing the log level\n"
			";                     (eg 'I' for INFO)\n"
			"; mm               The month(zero padded; ie May is '05')\n"
			"; dd               The day(zero padded)\n"
			"; hh:mm:ss.uuuuuu  Time in hours, minutes and fractional seconds\n"
			"; threadid         The space - padded thread ID as returned by GetTID()\n"
			";                     (this matches the PID on Linux)\n"
			"; file             The file name\n"
			"; line             The line number\n"
			"; msg              The user - supplied message\n"
			"; \n"
			"; Example:\n"
			"; \n"
			"; I1103 11 : 57 : 31.739339 24395 google.cc : 2341] Command line : . / some_prog\n"
			"; I1103 11 : 57 : 31.739403 24395 google.cc : 2342] Process id 24395\n"
			"; \n"
			"; NOTE: although the microseconds are useful for comparing events on\n"
			"; a single machine, clocks on different machines may not be well\n"
			"; synchronized.Hence, use caution when comparing the low bits of\n"
			"; timestamps from different machines.\n"
			";\n"
			"; Log messages to stderr(console)\t instead of logfiles\n"
			"; LogToStdErr = false\n"
			";\n"
			"; This is ierarchy of errors: FATAL<-WARNINGS<-INFO\n"
			"; Log message at or above this level. Default 0, that represent INFO and above\n"
			"; MinLogLevel = 0\n"
			";\n"
			"; Deep log for debuging. 0 = off, 1 = on\n"
			"; DeepLogging = 0\n"
			"\n"
			"; Log folder. Default tmp directory\n"
			"; LogDir = logs\n"
            "\n"
            "; true - print log to standard error stream (by default it is console)\n"
            "; false - print log to log file"
            "; LogToStdErr = true";

	//!!! This log massage go to stderr ONLY, because GLOG is not initialized yet !
	LOG(WARNING) << "Default settings saved to'" << pathToSettings << "'";
	file.close();
}