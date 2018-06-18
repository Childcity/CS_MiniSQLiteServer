cmake_minimum_required(VERSION 3.10)
project(CS_MiniSQLiteServer)

include_directories("include")

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)

find_package(glog 0.3.5 REQUIRED)
find_package(Boost 1.67 COMPONENTS REQUIRED system thread regex)

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    link_directories(${Boost_LIBRARY_DIRS})
endif(Boost_FOUND)

set(USED_LIBS ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${Boost_REGEX_LIBRARY})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-std=c++17 -Wall -Wno-reorder")


set(SOURCES
        main.cpp CConfig.cpp CServer.cpp CClientSession.cpp CRunAsync.cpp
        include/INIReaderWriter/ini.c
        include/INIReaderWriter/INIReader.cpp
        include/INIReaderWriter/INIWriter.hpp)

set(HEADERS
        main.h CConfig.h CServer.h CClientSession.h CRunAsync.h
        include/INIReaderWriter/ini.h
        include/INIReaderWriter/INIReader.h)

add_executable(${PROJECT_NAME} ${HEADERS} ${SOURCES})
target_link_libraries (${PROJECT_NAME} glog::glog ${USED_LIBS})