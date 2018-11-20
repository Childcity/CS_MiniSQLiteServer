//
// Created by childcity on 20.11.18.
//

#include "CBinaryFileReader.h"

CBinaryFileReader::CBinaryFileReader()
        : buffer_(nullptr){}

CBinaryFileReader::~CBinaryFileReader() { close(); }

bool CBinaryFileReader::open(const std::string &path) {
    close();

    try{
        buffer_.reset({ new char[CHUNK_SIZE] });
    } catch( ... ){
        return false;
    }

    fileStream_.open(path, std::ios::in | std::ios::binary);

    return fileStream_.is_open();
}

void CBinaryFileReader::close() {
    buffer_.reset();

    if(fileStream_.is_open())
        fileStream_.close();
}

bool CBinaryFileReader::nextChunk() {
    if(fileStream_.eof())
        return false;

    fileStream_.read(buffer_.get(), CHUNK_SIZE);

    return true;
}

const char *CBinaryFileReader::currentChunk() const {
    return static_cast<const char*>(buffer_.get());
}

size_t CBinaryFileReader::chunkSize() const {
    return static_cast<size_t>(fileStream_.gcount());
}

bool CBinaryFileReader::isEOF() const {
    return fileStream_.eof();
}
