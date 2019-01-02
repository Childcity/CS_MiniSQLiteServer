#pragma once

#define ZeroMemory(Destination,Length) memset((Destination),0,(Length))

//#define BOOST_ASIO_ENABLE_HANDLER_TRACKING // for asio debuging
//#define GOOGLE_STRIP_LOG 0 // cut all glog strings from .exe

#include <string>
#include <boost/thread/pthread/recursive_mutex.hpp>
#include "CConfig.h"

extern std::string dbPath;
extern std::string bakDbPath;
extern std::string restoreDbPath;
extern size_t newBackupTimeout;
extern size_t sqlWaitTime;
extern size_t sqlCountOfAttempts;
extern long blockOrClusterSize;

extern boost::recursive_mutex clients_cs;

int main(int argc, char *argv[]);

class CConfig;
void TestSqlite3Settings(CConfig *cfg);

void SafeExit();

template <typename T, std::size_t N>
constexpr std::size_t countof(T const (&)[N]) noexcept
{
    return N;
}

#ifdef WIN32
#include <codecvt>
template<class A, class B>
static B ConverterUTF8_UTF16(A str1)
{
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converterUTF8_UTF16;

	if constexpr (std::is_same_v<A, std::wstring>){
		return converterUTF8_UTF16.to_bytes(str1);
	}
	else{
		return converterUTF8_UTF16.from_bytes(str1);
	}
}
#endif // WIN32