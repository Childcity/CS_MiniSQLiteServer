#cmake_minimum_required(VERSION 3.10)
cmake_minimum_required(VERSION 2.6)
project(CS_MiniSQLiteServer)

include_directories(include)
link_directories(${CMAKE_SOURCE_DIR}/libs)
link_directories(${CMAKE_SOURCE_DIR}/libs/boost)
find_package(glog REQUIRED)
set(USED_LIBS boost_system boost_regex boost_thread glog::glog)


set(CMAKE_CXX_FLAGS "-pthread -std=c++14 -Wall -Wno-reorder")

set(SOURCES
        main.cpp CConfig.cpp CServer.cpp CClientSession.cpp CSQLiteDB.cpp
        include/sqlite3/sqlite3.c
        include/INIReaderWriter/ini.c
        include/INIReaderWriter/INIReader.cpp
        include/INIReaderWriter/INIWriter.hpp CBusinessLogic.cpp CBusinessLogic.h CBinaryFileReader.cpp CBinaryFileReader.h)

set(HEADERS
        main.h CConfig.h CServer.h CClientSession.h CSQLiteDB.h
        include/sqlite3/sqlite3.h
        include/INIReaderWriter/ini.h
        include/INIReaderWriter/INIReader.h CBusinessLogic.cpp CBusinessLogic.h CBinaryFileReader.cpp CBinaryFileReader.h)

add_executable(${PROJECT_NAME} ${HEADERS} ${SOURCES})
target_link_libraries (${PROJECT_NAME} ${USED_LIBS} ${CMAKE_DL_LIBS})