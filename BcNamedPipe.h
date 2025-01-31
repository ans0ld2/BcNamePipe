/**
 ******************************************************************************
 * @file    DtNamedPipe
 * @date    31.10.2024
 * @author  Heorhii Lutsenko <ans0ld2@gmail.com>
 * @brief
 ******************************************************************************
 */

#ifndef BC_NAMEDPIPE_H
#define BC_NAMEDPIPE_H

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <string>

namespace Bc {

class BcNamedPipe {
public:
    enum class Mode { Reader, Writer };

    BcNamedPipe(const std::string& path, Mode mode);
    ~BcNamedPipe();

    std::string readData();
    void writeData(const std::string& data);
    void setInternalBuffer(std::size_t bufSize);
private:
    std::string fifoPath;
    Mode mode;
    int fd;
    std::size_t _bufSize = 128;
};

} // Bc

#endif //BC_NAMEDPIPE_H
