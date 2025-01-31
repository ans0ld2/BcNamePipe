/**
 ******************************************************************************
 * @file    DtNamedPipe
 * @date    31.10.2024
 * @author  Heorhii Lutsenko <ans0ld2@gmail.com>
 * @brief
 ******************************************************************************
 */

#include "BcNamedPipe.h"

namespace Bc {

BcNamedPipe::BcNamedPipe(const std::string& path, Mode mode)
        : fifoPath(path), mode(mode), fd(-1)
{
    if (mkfifo(this->fifoPath.c_str(), 0666) == -1 && errno != EEXIST) {
        std::cerr << "Failed to create FIFO" << std::endl;
    }

    if (mode == Mode::Reader) {
        this->fd = open(this->fifoPath.c_str(), O_RDONLY);
    } else if (mode == Mode::Writer) {
        this->fd = open(this->fifoPath.c_str(), O_WRONLY);
    }

    if (this->fd == -1) {
        std::cerr << "Error opening FIFO" << std::endl;
    }
}

BcNamedPipe::~BcNamedPipe()
{
    if (this->fd != -1) {
        close(this->fd);
    }
}

std::string BcNamedPipe::readData()
{
    if (this->mode != Mode::Reader) {
        std::cerr << "This NamedPipe is not configured to read" << std::endl;
        return "";
    }

    char buffer[this->_bufSize];
    int bytesRead = read(this->fd, buffer, sizeof(buffer) - 1);
    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        return std::string(buffer);
    }
    return "";
}

void BcNamedPipe::writeData(const std::string& data)
{
    if (this->mode != Mode::Writer) {
        std::cerr << "This NamedPipe is not configured for writing" << std::endl;
        return;
    }

    write(this->fd, data.c_str(), data.size() + 1);
}

void BcNamedPipe::setInternalBuffer(std::size_t bufSize)
{
    if (bufSize > 0) {
        this->_bufSize = bufSize;
    }
}




} // Dt