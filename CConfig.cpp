#include "CConfig.h"
#include "INIReaderWriter/INIReader.h"
#include "INIReaderWriter/INIWriter.hpp"

#include <ctime>
#include <sys/stat.h>
#include <glog/logging.h>

using INIWriter = samilton::INIWriter;

CConfig::KeyBindings::KeyBindings(const string exePath)
{
	size_t found = exePath.find_last_of("/\\");
	exeName_ = exePath.substr(found + 1);
	exeFolderPath_ = exePath.substr(0, (exePath.size() - exeName_.size()));

	dbPath = "defaultEmptyDb.sqlite3";
	ipAdress = "127.0.0.1";
	port = 65043;
	threads = 10;

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
	std::time_t t = std::time(0);   // get time now
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
	FLAGS_v = keyBindings.verbousLog;
	FLAGS_minloglevel = keyBindings.minLogLevel;

	int ret = mkdir(keyBindings.logDir.c_str(),  S_IRWXU | S_IRWXG |  S_IRWXO);
	if((0 != ret) && (EEXIST != errno)){
		//log directory not exist or permission denied or other error
		LOG(FATAL) <<"Error: can't create or use log dir '" <<keyBindings.logDir <<"': "<<strerror(errno);
	}

	ret = mkdir(newFolder.c_str(),  S_IRWXU | S_IRWXG |  S_IRWXO);
	if((0 != ret) && (EEXIST != errno)){
		//log directory not exist or permission denied or other error
		LOG(FATAL) <<"Error: can't create or use log dir '" <<newFolder <<"': "<<strerror(errno);
	}
	/*CreateDirectoryW(keyBindings.logDir.c_str(), NULL);
    CreateDirectoryW(newFolder.c_str(), NULL);*/


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
		//path to db
		keyBindings.dbPath = settings.Get("DatabaseSettings", "PathToDatabaseFile", "_a");
		//Log settings
		keyBindings.logDir = settings.Get("LogSettings", "LogDir", "_a");
		keyBindings.logToStdErr = settings.GetBoolean("LogSettings", "LogToStdErr", false);
		keyBindings.stopLoggingIfFullDisk = settings.GetBoolean("LogSettings", "StopLoggingIfFullDisk", false);
		keyBindings.verbousLog = settings.GetInteger("LogSettings", "DeepLogging", 0L);
		keyBindings.minLogLevel = settings.GetInteger("LogSettings", "MinLogLevel", 0L);
		//Service settings (only for windows)
		keyBindings.serviceName = settings.Get("ServiceSettings", "ServiceName", "_a");

		if (keyBindings.port == -1L || keyBindings.threads == -1L || keyBindings.ipAdress == "0" || keyBindings.dbPath == "_a"
			|| keyBindings.logDir == "_a" || keyBindings.serviceName == "_a" ) {
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
	//DB settings
	settings["DatabaseSettings"]["PathToDatabaseFile"] = defaultKeyBindings.dbPath;
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
			"; LogDir = logs\n";

	//!!! This log massage go to stderr ONLY, because GLOG is not initialized yet !
	LOG(WARNING) << "Default settings saved to'" << pathToSettings << "'";
	file.close();
}