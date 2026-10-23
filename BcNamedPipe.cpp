/**
 ******************************************************************************
 * @file    BcNamedPipe
 * @date    31.10.2024
 * @author  Heorhii Lutsenko <ans0ld2@gmail.com>
 * @brief
 ******************************************************************************
 */

#include "BcNamedPipe.h"

#include <cerrno>
#include <cstdint>
#include <limits>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Bc {

namespace {

constexpr std::size_t kFrameHeader = 4;

std::uint32_t loadLength(const char* data)
{
    const auto* raw = reinterpret_cast<const unsigned char*>(data);
    return static_cast<std::uint32_t>(raw[0])
        | (static_cast<std::uint32_t>(raw[1]) << 8)
        | (static_cast<std::uint32_t>(raw[2]) << 16)
        | (static_cast<std::uint32_t>(raw[3]) << 24);
}

void storeLength(char* data, std::uint32_t len)
{
    auto* raw = reinterpret_cast<unsigned char*>(data);
    raw[0] = static_cast<unsigned char>(len & 0xffu);
    raw[1] = static_cast<unsigned char>((len >> 8) & 0xffu);
    raw[2] = static_cast<unsigned char>((len >> 16) & 0xffu);
    raw[3] = static_cast<unsigned char>((len >> 24) & 0xffu);
}

int waitFor(int fd, short events, std::chrono::steady_clock::time_point deadline)
{
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return ETIMEDOUT;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        int waitMs = 0;
        if (remaining.count() > 0) {
            waitMs = remaining.count() > 600000 ? 600000 : static_cast<int>(remaining.count());
        }

        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = events;
        const int pr = ::poll(&pfd, 1, waitMs);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            return errno;
        }
        if (pr == 0) {
            continue;
        }
        return 0;
    }
}

int writeAll(int fd, const char* data, std::size_t size, std::chrono::steady_clock::time_point deadline)
{
    std::size_t written = 0;
    while (written < size) {
        const ssize_t n = ::write(fd, data + written, size - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                const int pr = waitFor(fd, POLLOUT, deadline);
                if (pr != 0) {
                    return pr;
                }
                continue;
            }
            return errno;
        }
        if (n == 0) {
            return EPIPE;
        }
        written += static_cast<std::size_t>(n);
    }
    return 0;
}

} // namespace

BcNamedPipe::BcNamedPipe(const std::string& path, Mode mode)
    : fifoPath(path)
    , mode(mode)
    , fd(-1)
    , ownsName(false)
    , createError(0)
    , bufferLimit(0)
{
    setInternalBuffer(128);

    if (::mkfifo(fifoPath.c_str(), 0600) == -1) {
        if (errno != EEXIST) {
            createError = errno;
        }
    } else {
        ownsName = true;
    }
}

BcNamedPipe::~BcNamedPipe()
{
    if (fd != -1) {
        ::close(fd);
        fd = -1;
    }
    if (ownsName) {
        ::unlink(fifoPath.c_str());
        ownsName = false;
    }
}

BcNamedPipe::BcNamedPipe(BcNamedPipe&& other) noexcept
    : fifoPath(std::move(other.fifoPath))
    , mode(other.mode)
    , fd(other.fd)
    , ownsName(other.ownsName)
    , createError(other.createError)
    , bufferLimit(other.bufferLimit)
    , buffer(std::move(other.buffer))
{
    other.fd = -1;
    other.ownsName = false;
    other.createError = 0;
}

BcNamedPipe& BcNamedPipe::operator=(BcNamedPipe&& other) noexcept
{
    if (this != &other) {
        if (fd != -1) {
            ::close(fd);
            fd = -1;
        }
        if (ownsName) {
            ::unlink(fifoPath.c_str());
            ownsName = false;
        }

        fifoPath = std::move(other.fifoPath);
        mode = other.mode;
        fd = other.fd;
        ownsName = other.ownsName;
        createError = other.createError;
        bufferLimit = other.bufferLimit;
        buffer = std::move(other.buffer);

        other.fd = -1;
        other.ownsName = false;
        other.createError = 0;
    }
    return *this;
}

int BcNamedPipe::open(std::chrono::milliseconds timeout)
{
    if (createError != 0) {
        return createError;
    }
    if (fd != -1) {
        return 0;
    }

    if (mode == Mode::Reader) {
        int opened = -1;
        do {
            opened = ::open(fifoPath.c_str(), O_RDONLY | O_NONBLOCK);
        } while (opened < 0 && errno == EINTR);
        if (opened < 0) {
            return errno;
        }
        fd = opened;
        return 0;
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        int opened = -1;
        do {
            opened = ::open(fifoPath.c_str(), O_WRONLY | O_NONBLOCK);
        } while (opened < 0 && errno == EINTR);
        if (opened >= 0) {
            fd = opened;
            return 0;
        }
        if (errno != ENXIO) {
            return errno;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return ETIMEDOUT;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        int waitMs = remaining.count() > 50 ? 50 : static_cast<int>(remaining.count());
        if (waitMs < 0) {
            waitMs = 0;
        }

        const int pr = ::poll(nullptr, 0, waitMs);
        if (pr < 0 && errno != EINTR) {
            return errno;
        }
    }
}

BcNamedPipe::Result BcNamedPipe::readData(std::chrono::milliseconds timeout)
{
    if (mode != Mode::Reader || fd < 0) {
        return Result{EBADF, {}};
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (buffer.size() >= kFrameHeader) {
            const std::uint32_t len = loadLength(buffer.data());
            if (bufferLimit < kFrameHeader
                || static_cast<std::size_t>(len) > bufferLimit - kFrameHeader) {
                return Result{EMSGSIZE, {}};
            }
            const std::size_t frame = static_cast<std::size_t>(len) + kFrameHeader;
            if (buffer.size() >= frame) {
                Result out;
                out.error = 0;
                out.bytes.assign(buffer.data() + kFrameHeader, buffer.data() + frame);
                buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(frame));
                return out;
            }
        }

        if (buffer.size() >= bufferLimit) {
            return Result{EMSGSIZE, {}};
        }

        const std::size_t room = bufferLimit - buffer.size();
        const std::size_t chunk = room < 4096 ? room : static_cast<std::size_t>(4096);
        const std::size_t old = buffer.size();
        buffer.resize(old + chunk);

        const ssize_t n = ::read(fd, buffer.data() + old, chunk);
        if (n < 0) {
            buffer.resize(old);
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                const int pr = waitFor(fd, POLLIN, deadline);
                if (pr != 0) {
                    return Result{pr, {}};
                }
                continue;
            }
            return Result{errno, {}};
        }
        if (n == 0) {
            buffer.resize(old);
            return Result{kPeerClosed, {}};
        }
        buffer.resize(old + static_cast<std::size_t>(n));
    }
}

BcNamedPipe::Result BcNamedPipe::writeData(const std::string& data, std::chrono::milliseconds timeout)
{
    if (mode != Mode::Writer || fd < 0) {
        return Result{EBADF, {}};
    }
    if (data.size() > std::numeric_limits<std::uint32_t>::max()
        || bufferLimit < kFrameHeader
        || data.size() > bufferLimit - kFrameHeader) {
        return Result{EMSGSIZE, {}};
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    char header[kFrameHeader];
    storeLength(header, static_cast<std::uint32_t>(data.size()));

    const int headerError = writeAll(fd, header, kFrameHeader, deadline);
    if (headerError != 0) {
        return Result{headerError, {}};
    }
    if (!data.empty()) {
        const int payloadError = writeAll(fd, data.data(), data.size(), deadline);
        if (payloadError != 0) {
            return Result{payloadError, {}};
        }
    }
    return Result{0, data};
}

void BcNamedPipe::setInternalBuffer(std::size_t bufSize)
{
    if (bufSize == 0) {
        return;
    }
    if (bufSize > kMaxBuffer) {
        bufSize = kMaxBuffer;
    }

    bufferLimit = bufSize;
    std::vector<char> next;
    next.reserve(bufferLimit);
    const std::size_t keep = buffer.size() < bufferLimit ? buffer.size() : bufferLimit;
    next.insert(next.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(keep));
    buffer.swap(next);
}

} // Bc
