#pragma once
#ifndef CS_MINISQLITESERVER_CCONFIG_H
#define CS_MINISQLITESERVER_CCONFIG_H

#include <string>

using string = std::string;

class CConfig {
private:

	struct KeyBindings {

        explicit KeyBindings(string exePath);

		string dbPath;
		long blockOrClusterSize;
		long waitTimeMillisec;
		long countOfEttempts;

		string ipAdress;
		long port;
		long threads;

		string logDir;
		bool logToStdErr;
		bool stopLoggingIfFullDisk;
		long verbousLog;
		long minLogLevel;

		string serviceName;

        string exeFolderPath_;
        string exeName_;
	};

	void setLocale() const;

	void setStatusOk();

	void setStatusError();

	void updateKeyBindings();

	void saveKeyBindings();

	string getConstructedNameOfLogDir() const;

public:
	CConfig(CConfig const&) = delete;
	CConfig operator=(CConfig const&) = delete;

	const char * const SETTINGS_FILE_NAME = "CS_MiniSQLiteServer.ini";

	enum Status{ LOADED_OK = 0, ERROR = -1 };

	KeyBindings keyBindings;

    explicit CConfig(string  exePath);

	void Load();

	Status getStatus() const;

	void initGlog();

private:
	Status status = ERROR;
	KeyBindings defaultKeyBindings;
};

#endif //CS_MINISQLITESERVER_CCONFIG_H