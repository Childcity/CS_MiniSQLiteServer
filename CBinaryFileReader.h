//
// Created by childcity on 20.11.18.
//

#ifndef CS_MINISQLITESERVER_CBINARYFILEREADER_H
#define CS_MINISQLITESERVER_CBINARYFILEREADER_H
#pragma once

#include <fstream>
#include <boost/scoped_array.hpp>


class CBinaryFileReader{
    enum {CHUNK_SIZE = 2097152}; // 2Mb by default
    boost::scoped_array<char> buffer_{};
    std::ifstream fileStream_;

public:
    CBinaryFileReader();

    CBinaryFileReader(const CBinaryFileReader &) = delete;

    ~CBinaryFileReader();

    bool open(const std::string &path);

    void close();

    bool nextChunk();

    const char *currentChunk() const;

    size_t chunkSize() const;

    bool isEOF() const;
};


#endif //CS_MINISQLITESERVER_CBINARYFILEREADER_H
