//
// Created by childcity on 20.11.18.
//

#include "CBinaryFileReader.h"

CBinaryFileReader::CBinaryFileReader()
        : buffer_(nullptr)
        , fileSize_(-1l)
        , readerProgress_(-1l)
        , bytesRead_(0l){}

CBinaryFileReader::~CBinaryFileReader() { close(); }

bool CBinaryFileReader::open(const std::string &path) {
    close();

    try{
        buffer_.reset({ new char[CHUNK_SIZE] });
    } catch( ... ){
        return false;
    }

    fileStream_.open(path, std::ios::in | std::ios::binary);
    bool isOpen = fileStream_.is_open();

    if(isOpen){
        fileStream_.seekg(0, std::ios_base::end);
        fileSize_ = fileStream_.tellg();
        fileStream_.seekg(0, std::ios_base::beg);
    }

    return isOpen;
}

void CBinaryFileReader::close() {
    buffer_.reset();

    fileSize_ = readerProgress_ = -1l;
    bytesRead_ = 0;

    if(fileStream_.is_open())
        fileStream_.close();
}

bool CBinaryFileReader::nextChunk() {
    if(isEOF())
        return false;

    bytesRead_ += fileStream_.read(buffer_.get(), CHUNK_SIZE).gcount();
    readerProgress_ = 100l * bytesRead_ / fileSize_;

    return true;
}

const char *CBinaryFileReader::getCurrentChunk() const {
    return static_cast<const char*>(buffer_.get());
}

size_t CBinaryFileReader::getCurrentChunkSize() const {
    return static_cast<size_t>(fileStream_.gcount());
}

bool CBinaryFileReader::isEOF() const {
    return fileStream_.eof();
}

long CBinaryFileReader::getFileSize() const {
    return fileSize_;
}

long CBinaryFileReader::getProgress() const {
    return readerProgress_;
}
