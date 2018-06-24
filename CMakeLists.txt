#cmake_minimum_required(VERSION 3.10)
cmake_minimum_required(VERSION 2.6)
project(CS_MiniSQLiteServer)

include_directories(include)
link_directories(${CMAKE_SOURCE_DIR}/libs)
link_directories(${CMAKE_SOURCE_DIR}/libs/boost)
set(USED_LIBS boost_system boost_regex boost_thread glog sqlite3)

#[[set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set( Boost_USE_STATIC_RUNTIME ON )
set( Boost_DEBUG ON )]]

#find_package(glog REQUIRED)
#[[
find_package(Boost COMPONENTS REQUIRED system thread regex)

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    link_directories(${Boost_LIBRARY_DIRS})
endif(Boost_FOUND)

set(USED_LIBS
        ${Boost_SYSTEM_LIBRARY}
        ${Boost_THREAD_LIBRARY}

        ${Boost_REGEX_LIBRARY} )]]



#include sqlite3 and glog (static lib)
#[[if(WIN32)
    set(USED_LIBS ${USED_LIBS} libsqlite3.lib glog.lib)
else()
    set(USED_LIBS ${USED_LIBS} glog sqlite3)
endif()]]

#set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_CXX_FLAGS "-std=c++17 -Wall -Wno-reorder")
set(CMAKE_CXX_FLAGS "-pthread -Wall -Wno-reorder")

set(SOURCES
        main.cpp CConfig.cpp CServer.cpp CClientSession.cpp CSQLiteDB.cpp
        include/INIReaderWriter/ini.c
        include/INIReaderWriter/INIReader.cpp
        include/INIReaderWriter/INIWriter.hpp)

set(HEADERS
        main.h CConfig.h CServer.h CClientSession.h CSQLiteDB.h
        include/INIReaderWriter/ini.h
        include/INIReaderWriter/INIReader.h)

add_executable(${PROJECT_NAME} ${HEADERS} ${SOURCES})
target_link_libraries (${PROJECT_NAME} ${USED_LIBS} ${CMAKE_DL_LIBS})