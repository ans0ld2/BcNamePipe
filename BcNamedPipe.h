/**
 ******************************************************************************
 * @file    BcNamedPipe
 * @date    31.10.2024
 * @author  Heorhii Lutsenko <ans0ld2@gmail.com>
 * @brief
 ******************************************************************************
 */

#ifndef BC_NAMEDPIPE_H
#define BC_NAMEDPIPE_H

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace Bc {

class BcNamedPipe {
public:
    enum class Mode { Reader, Writer };

    // error == 0: a full frame (bytes is the payload, which may be empty).
    // error == kPeerClosed: the peer closed the FIFO, or read hit EOF.
    // error > 0: errno of an I/O failure.
    struct Result {
        int error = 0;
        std::string bytes;
    };

    static constexpr int kPeerClosed = -1;
    static constexpr std::size_t kMaxBuffer = 1024 * 1024;

    BcNamedPipe(const std::string& path, Mode mode);
    ~BcNamedPipe();

    BcNamedPipe(const BcNamedPipe&) = delete;
    BcNamedPipe& operator=(const BcNamedPipe&) = delete;
    BcNamedPipe(BcNamedPipe&& other) noexcept;
    BcNamedPipe& operator=(BcNamedPipe&& other) noexcept;

    // Reader opens with O_RDONLY|O_NONBLOCK and returns at once.
    // Writer opens with O_WRONLY|O_NONBLOCK and, on ENXIO, polls until a
    // reader appears or timeout elapses. Returns 0 or an errno value.
    int open(std::chrono::milliseconds timeout);

    // Wait at most timeout for one full frame, or for the peer to accept
    // the frame. ETIMEDOUT leaves the descriptor open.
    Result readData(std::chrono::milliseconds timeout);
    Result writeData(const std::string& data, std::chrono::milliseconds timeout);
    void setInternalBuffer(std::size_t bufSize);

private:

    std::string fifoPath;
    Mode mode;
    int fd;
    bool ownsName;
    int createError;
    std::size_t bufferLimit;
    std::vector<char> buffer;
};

} // Bc

#endif //BC_NAMEDPIPE_H
