/**
 * @file BcNamedPipe.h
 * @brief One Linux FIFO between one reader and one writer.
 */

#ifndef BC_NAMEDPIPE_H
#define BC_NAMEDPIPE_H

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace Bc {

/**
 * @brief Move-only endpoint of a POSIX named pipe.
 *
 * The constructor stores the path and mode and creates the FIFO with mode
 * 0600. It does not open the descriptor. Only the instance that created the
 * name unlinks it. One reader and one writer. Several writers mix frames.
 *
 * If a frame is torn on write, torn on read, or rejected with EMSGSIZE, that
 * error sticks. Later readData and writeData return it and transfer no more
 * bytes. Close both ends.
 */
class BcNamedPipe {
public:
    /**
     * @brief Which end of the FIFO this object uses.
     */
    enum class Mode { Reader, Writer };

    /**
     * @brief Bytes transferred and the error code for one read or write.
     *
     * error == 0 is one full frame, which may be empty. error == kPeerClosed
     * means the peer closed in the middle of a frame; that value then sticks.
     * A positive error is errno, including ETIMEDOUT and EMSGSIZE. EMSGSIZE
     * and a timeout or I/O error after a partial frame also stick.
     */
    struct Result {
        int error = 0;
        std::string bytes;
    };

    /** @brief Peer closed mid-frame. Not an errno value. Sticky once returned. */
    static constexpr int kPeerClosed = -1;
    /** @brief Upper bound for the internal frame buffer, in bytes. */
    static constexpr std::size_t kMaxBuffer = 1024 * 1024;

    /**
     * @brief Store the path and mode and create the FIFO name.
     * @param path Filesystem path of the FIFO.
     * @param mode Reader or writer role of this endpoint.
     * @return No value. mkfifo uses mode 0600. EEXIST means this instance does not own the name.
     */
    BcNamedPipe(const std::string& path, Mode mode);

    /**
     * @brief Close the descriptor and unlink the name if this instance created it.
     * @return No value.
     */
    ~BcNamedPipe();

    /**
     * @brief Copy construction is deleted.
     * @param other Unused source endpoint.
     * @return No value.
     */
    BcNamedPipe(const BcNamedPipe&) = delete;

    /**
     * @brief Copy assignment is deleted.
     * @param other Unused source endpoint.
     * @return No value.
     */
    BcNamedPipe& operator=(const BcNamedPipe&) = delete;

    /**
     * @brief Take the descriptor and name ownership from another endpoint.
     * @param other Source endpoint. Its descriptor is set to -1.
     * @return No value.
     */
    BcNamedPipe(BcNamedPipe&& other) noexcept;

    /**
     * @brief Move-assign the descriptor and name ownership.
     * @param other Source endpoint. Its descriptor is set to -1.
     * @return This endpoint.
     */
    BcNamedPipe& operator=(BcNamedPipe&& other) noexcept;

    /**
     * @brief Open the FIFO.
     * @param timeout How long a writer waits for a reader. A reader returns immediately.
     * @return 0 on success. Otherwise errno, including ETIMEDOUT when no reader appears.
     */
    int open(std::chrono::milliseconds timeout);

    /**
     * @brief Read one little-endian length-prefixed frame.
     * @param timeout Maximum wait for a full frame, including while no writer is open.
     * @return Result with the payload on success. EOF with nothing buffered waits out the timeout and returns ETIMEDOUT. EOF mid-frame returns sticky kPeerClosed. EMSGSIZE sticks. The descriptor stays open.
     */
    Result readData(std::chrono::milliseconds timeout);

    /**
     * @brief Write one little-endian length-prefixed frame.
     * @param data Payload bytes. An embedded NUL is preserved.
     * @param timeout Maximum wait for the peer to accept every byte. Checked before each write.
     * @return Result with the payload on success, or errno. If any byte of the frame was sent and the frame was not finished, that errno sticks and later calls transfer nothing. EMSGSIZE sticks. The descriptor stays open.
     */
    Result writeData(const std::string& data, std::chrono::milliseconds timeout);

    /**
     * @brief Set the capacity of the internal frame buffer.
     * @param bufSize Requested capacity in bytes. Zero is ignored. Values above kMaxBuffer are clamped. A size below the bytes already buffered is ignored.
     * @return No value.
     */
    void setInternalBuffer(std::size_t bufSize);

private:
    Result failSticky(int error);

    std::string fifoPath;
    Mode mode;
    int fd;
    bool ownsName;
    int createError;
    int stickyError;
    std::size_t bufferLimit;
    std::vector<char> buffer;
};

} // Bc

#endif //BC_NAMEDPIPE_H
