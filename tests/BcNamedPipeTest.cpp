#include "BcNamedPipe.h"

#include <chrono>
#include <iostream>
#include <string>
#include <type_traits>

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

int fail(const std::string& message)
{
    std::cerr << "FAIL: " << message << std::endl;
    return 1;
}

std::string nulMessage()
{
    return std::string("ab\0cd\0ef", 8);
}

std::string longMessage()
{
    std::string message(256, '\0');
    for (std::size_t i = 0; i < message.size(); ++i) {
        message[i] = static_cast<char>('A' + static_cast<int>(i % 26));
    }
    return message;
}

int childStatus(int status)
{
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

int testRoundTrip()
{
    const std::string path = "/tmp/bc-named-pipe-rt-" + std::to_string(::getpid());
    ::unlink(path.c_str());

    const std::string withNul = nulMessage();
    const std::string longer = longMessage();
    if (withNul.size() != 8 || withNul.find('\0') == std::string::npos) {
        return fail("NUL fixture is wrong");
    }
    if (longer.size() <= 127) {
        return fail("long fixture is not longer than 127 bytes");
    }

    Bc::BcNamedPipe reader(path, Bc::BcNamedPipe::Mode::Reader);
    reader.setInternalBuffer(4096);

    ::alarm(15);
    const auto openedAt = std::chrono::steady_clock::now();
    const int openRc = reader.open(std::chrono::seconds(5));
    const auto openElapsed = std::chrono::steady_clock::now() - openedAt;
    if (openRc != 0) {
        ::alarm(0);
        return fail("reader open failed: " + std::to_string(openRc));
    }
    if (openElapsed > std::chrono::milliseconds(1000)) {
        ::alarm(0);
        return fail("reader open did not return immediately");
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::alarm(0);
        return fail("fork failed");
    }
    if (pid == 0) {
        ::alarm(10);
        int code = 0;
        {
            Bc::BcNamedPipe writer(path, Bc::BcNamedPipe::Mode::Writer);
            writer.setInternalBuffer(4096);
            const int writerOpen = writer.open(std::chrono::seconds(2));
            if (writerOpen != 0) {
                code = 2;
            } else if (writer.writeData(withNul, std::chrono::seconds(2)).error != 0) {
                code = 3;
            } else if (writer.writeData(longer, std::chrono::seconds(2)).error != 0) {
                code = 4;
            }
        }
        _exit(code);
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        ::alarm(0);
        return fail("waitpid failed");
    }
    const int code = childStatus(status);
    if (code != 0) {
        ::alarm(0);
        return fail("writer child failed: " + std::to_string(code));
    }

    const Bc::BcNamedPipe::Result gotNul = reader.readData(std::chrono::seconds(2));
    const Bc::BcNamedPipe::Result gotLong = reader.readData(std::chrono::seconds(2));
    ::alarm(0);

    if (gotNul.error != 0) {
        return fail("NUL read error: " + std::to_string(gotNul.error));
    }
    if (gotNul.bytes != withNul) {
        return fail("NUL round-trip mismatch, size " + std::to_string(gotNul.bytes.size()));
    }
    if (gotLong.error != 0) {
        return fail("long read error: " + std::to_string(gotLong.error));
    }
    if (gotLong.bytes != longer) {
        return fail("long round-trip mismatch, size " + std::to_string(gotLong.bytes.size()));
    }
    return 0;
}

int testOpenTimeout()
{
    const std::string path = "/tmp/bc-named-pipe-to-" + std::to_string(::getpid());
    ::unlink(path.c_str());

    const pid_t pid = ::fork();
    if (pid < 0) {
        return fail("timeout fork failed");
    }
    if (pid == 0) {
        ::alarm(5);
        int code = 1;
        {
            Bc::BcNamedPipe writer(path, Bc::BcNamedPipe::Mode::Writer);
            const auto started = std::chrono::steady_clock::now();
            const int rc = writer.open(std::chrono::milliseconds(250));
            const auto elapsed = std::chrono::steady_clock::now() - started;
            if (rc == ETIMEDOUT
                && elapsed >= std::chrono::milliseconds(200)
                && elapsed < std::chrono::seconds(3)) {
                code = 0;
            }
        }
        _exit(code);
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        ::unlink(path.c_str());
        return fail("timeout waitpid failed");
    }
    ::unlink(path.c_str());

    const int code = childStatus(status);
    if (code != 0) {
        return fail("writer open did not time out waiting for a peer: " + std::to_string(code));
    }
    return 0;
}

int testReadTimeout()
{
    const std::string path = "/tmp/bc-named-pipe-rd-" + std::to_string(::getpid());
    ::unlink(path.c_str());

    int ready[2] = {-1, -1};
    int release[2] = {-1, -1};
    if (::pipe(ready) != 0 || ::pipe(release) != 0) {
        return fail("pipe failed");
    }

    Bc::BcNamedPipe reader(path, Bc::BcNamedPipe::Mode::Reader);
    reader.setInternalBuffer(4096);
    ::alarm(10);
    if (reader.open(std::chrono::milliseconds(0)) != 0) {
        ::alarm(0);
        return fail("read-timeout reader open failed");
    }

    const std::string withNul = nulMessage();
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::alarm(0);
        return fail("read-timeout fork failed");
    }
    if (pid == 0) {
        ::alarm(10);
        ::close(ready[0]);
        ::close(release[1]);
        int code = 0;
        {
            Bc::BcNamedPipe writer(path, Bc::BcNamedPipe::Mode::Writer);
            writer.setInternalBuffer(4096);
            if (writer.open(std::chrono::seconds(2)) != 0) {
                code = 2;
            } else if (::write(ready[1], "R", 1) != 1) {
                code = 3;
            } else {
                char go = 0;
                if (::read(release[0], &go, 1) != 1) {
                    code = 4;
                } else if (writer.writeData(withNul, std::chrono::seconds(2)).error != 0) {
                    code = 5;
                }
            }
        }
        _exit(code);
    }

    ::close(ready[1]);
    ::close(release[0]);

    char opened = 0;
    if (::read(ready[0], &opened, 1) != 1) {
        ::alarm(0);
        return fail("writer did not stay open");
    }

    const auto started = std::chrono::steady_clock::now();
    const Bc::BcNamedPipe::Result timedOut = reader.readData(std::chrono::milliseconds(250));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (::write(release[1], "G", 1) != 1) {
        ::alarm(0);
        return fail("failed to release writer");
    }

    const Bc::BcNamedPipe::Result got = reader.readData(std::chrono::seconds(2));
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        ::alarm(0);
        return fail("read-timeout waitpid failed");
    }
    ::alarm(0);

    if (timedOut.error != ETIMEDOUT) {
        return fail("readData did not time out: " + std::to_string(timedOut.error));
    }
    if (elapsed < std::chrono::milliseconds(200) || elapsed >= std::chrono::seconds(3)) {
        return fail("readData timeout duration was unexpected");
    }
    if (childStatus(status) != 0) {
        return fail("read-timeout writer failed: " + std::to_string(childStatus(status)));
    }
    if (got.error != 0 || got.bytes != withNul) {
        return fail("fd was not usable after read timeout");
    }
    return 0;
}

int testWriteTimeout()
{
    const std::string path = "/tmp/bc-named-pipe-wr-" + std::to_string(::getpid());
    ::unlink(path.c_str());

    Bc::BcNamedPipe reader(path, Bc::BcNamedPipe::Mode::Reader);
    ::alarm(10);
    if (reader.open(std::chrono::milliseconds(0)) != 0) {
        ::alarm(0);
        return fail("write-timeout reader open failed");
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::alarm(0);
        return fail("write-timeout fork failed");
    }
    if (pid == 0) {
        ::alarm(10);
        int code = 1;
        {
            Bc::BcNamedPipe writer(path, Bc::BcNamedPipe::Mode::Writer);
            const std::string bulk(256 * 1024, 'Z');
            writer.setInternalBuffer(bulk.size() + 4);
            if (writer.open(std::chrono::seconds(2)) != 0) {
                code = 2;
            } else {
                const auto started = std::chrono::steady_clock::now();
                const Bc::BcNamedPipe::Result blocked = writer.writeData(bulk, std::chrono::milliseconds(250));
                const auto elapsed = std::chrono::steady_clock::now() - started;
                const auto againStarted = std::chrono::steady_clock::now();
                const Bc::BcNamedPipe::Result again = writer.writeData(std::string("x"), std::chrono::seconds(2));
                const auto againElapsed = std::chrono::steady_clock::now() - againStarted;
                if (blocked.error == ETIMEDOUT
                    && again.error == blocked.error
                    && elapsed >= std::chrono::milliseconds(200)
                    && elapsed < std::chrono::seconds(3)
                    && againElapsed < std::chrono::milliseconds(200)) {
                    code = 0;
                }
            }
        }
        _exit(code);
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        ::alarm(0);
        return fail("write-timeout waitpid failed");
    }
    ::alarm(0);

    const int code = childStatus(status);
    if (code != 0) {
        return fail("writeData did not time out while the peer never read: " + std::to_string(code));
    }
    return 0;
}

int testEmptyFrame()
{
    const std::string path = "/tmp/bc-named-pipe-empty-" + std::to_string(::getpid());
    ::unlink(path.c_str());

    Bc::BcNamedPipe reader(path, Bc::BcNamedPipe::Mode::Reader);
    ::alarm(10);
    if (reader.open(std::chrono::milliseconds(0)) != 0) {
        ::alarm(0);
        return fail("empty-frame reader open failed");
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::alarm(0);
        return fail("empty-frame fork failed");
    }
    if (pid == 0) {
        ::alarm(5);
        int code = 1;
        {
            Bc::BcNamedPipe writer(path, Bc::BcNamedPipe::Mode::Writer);
            if (writer.open(std::chrono::seconds(2)) == 0
                && writer.writeData(std::string(), std::chrono::seconds(2)).error == 0) {
                code = 0;
            }
        }
        _exit(code);
    }

    const Bc::BcNamedPipe::Result got = reader.readData(std::chrono::seconds(2));
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        ::alarm(0);
        return fail("empty-frame waitpid failed");
    }
    ::alarm(0);
    if (childStatus(status) != 0) {
        return fail("empty-frame writer failed");
    }
    if (got.error != 0 || !got.bytes.empty()) {
        return fail("empty frame did not round-trip, error " + std::to_string(got.error)
            + " size " + std::to_string(got.bytes.size()));
    }
    return 0;
}

int testReaderWaitsForWriter()
{
    const std::string path = "/tmp/bc-named-pipe-wait-" + std::to_string(::getpid());
    ::unlink(path.c_str());
    const std::string payload("late\0writer", 11);

    Bc::BcNamedPipe reader(path, Bc::BcNamedPipe::Mode::Reader);
    reader.setInternalBuffer(4096);
    ::alarm(10);
    if (reader.open(std::chrono::milliseconds(0)) != 0) {
        ::alarm(0);
        return fail("wait-for-writer reader open failed");
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::alarm(0);
        return fail("wait-for-writer fork failed");
    }
    if (pid == 0) {
        ::alarm(5);
        ::poll(nullptr, 0, 200);
        int code = 1;
        {
            Bc::BcNamedPipe writer(path, Bc::BcNamedPipe::Mode::Writer);
            writer.setInternalBuffer(4096);
            if (writer.open(std::chrono::seconds(2)) == 0
                && writer.writeData(payload, std::chrono::seconds(2)).error == 0) {
                code = 0;
            }
        }
        _exit(code);
    }

    const Bc::BcNamedPipe::Result got = reader.readData(std::chrono::seconds(2));
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        ::alarm(0);
        return fail("wait-for-writer waitpid failed");
    }
    ::alarm(0);
    if (childStatus(status) != 0) {
        return fail("wait-for-writer child failed: " + std::to_string(childStatus(status)));
    }
    if (got.error != 0 || got.bytes != payload) {
        return fail("readData did not wait for the late writer, error " + std::to_string(got.error));
    }
    return 0;
}

int testMidFrameEof()
{
    const std::string path = "/tmp/bc-named-pipe-mid-" + std::to_string(::getpid());
    ::unlink(path.c_str());

    Bc::BcNamedPipe reader(path, Bc::BcNamedPipe::Mode::Reader);
    reader.setInternalBuffer(4096);
    ::alarm(10);
    if (reader.open(std::chrono::milliseconds(0)) != 0) {
        ::alarm(0);
        return fail("mid-frame reader open failed");
    }

    const pid_t torn = ::fork();
    if (torn < 0) {
        ::alarm(0);
        return fail("mid-frame fork failed");
    }
    if (torn == 0) {
        ::alarm(5);
        const int fd = ::open(path.c_str(), O_WRONLY);
        if (fd < 0) {
            _exit(2);
        }
        const char partial[2] = {0x01, 0x00};
        const ssize_t n = ::write(fd, partial, 2);
        ::close(fd);
        _exit(n == 2 ? 0 : 3);
    }

    int tornStatus = 0;
    if (::waitpid(torn, &tornStatus, 0) < 0) {
        ::alarm(0);
        return fail("mid-frame waitpid failed");
    }
    if (childStatus(tornStatus) != 0) {
        ::alarm(0);
        return fail("mid-frame writer failed: " + std::to_string(childStatus(tornStatus)));
    }

    const auto started = std::chrono::steady_clock::now();
    const Bc::BcNamedPipe::Result tornRead = reader.readData(std::chrono::seconds(2));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (tornRead.error != Bc::BcNamedPipe::kPeerClosed || elapsed >= std::chrono::milliseconds(500)) {
        ::alarm(0);
        return fail("mid-frame EOF was not sticky peer close, error " + std::to_string(tornRead.error));
    }

    const pid_t next = ::fork();
    if (next < 0) {
        ::alarm(0);
        return fail("mid-frame second fork failed");
    }
    if (next == 0) {
        ::alarm(5);
        int code = 1;
        {
            Bc::BcNamedPipe writer(path, Bc::BcNamedPipe::Mode::Writer);
            writer.setInternalBuffer(4096);
            if (writer.open(std::chrono::seconds(2)) == 0
                && writer.writeData(std::string("next"), std::chrono::seconds(2)).error == 0) {
                code = 0;
            }
        }
        _exit(code);
    }

    const Bc::BcNamedPipe::Result glued = reader.readData(std::chrono::seconds(1));
    int nextStatus = 0;
    ::waitpid(next, &nextStatus, 0);
    ::alarm(0);
    if (glued.error != Bc::BcNamedPipe::kPeerClosed || glued.bytes == "next") {
        return fail("mid-frame EOF was glued to the next frame, error " + std::to_string(glued.error));
    }
    return 0;
}

int testEmsgsizeSticky()
{
    const std::string path = "/tmp/bc-named-pipe-big-" + std::to_string(::getpid());
    ::unlink(path.c_str());

    Bc::BcNamedPipe reader(path, Bc::BcNamedPipe::Mode::Reader);
    reader.setInternalBuffer(128);
    ::alarm(10);
    if (reader.open(std::chrono::milliseconds(0)) != 0) {
        ::alarm(0);
        return fail("emsgsize reader open failed");
    }

    const std::string payload(200, 'A');
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::alarm(0);
        return fail("emsgsize fork failed");
    }
    if (pid == 0) {
        ::alarm(5);
        int code = 1;
        {
            Bc::BcNamedPipe writer(path, Bc::BcNamedPipe::Mode::Writer);
            writer.setInternalBuffer(payload.size() + 4);
            if (writer.open(std::chrono::seconds(2)) == 0
                && writer.writeData(payload, std::chrono::seconds(2)).error == 0) {
                code = 0;
            }
        }
        _exit(code);
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        ::alarm(0);
        return fail("emsgsize waitpid failed");
    }
    ::alarm(0);
    if (childStatus(status) != 0) {
        return fail("emsgsize writer failed");
    }

    const Bc::BcNamedPipe::Result first = reader.readData(std::chrono::seconds(2));
    const Bc::BcNamedPipe::Result second = reader.readData(std::chrono::seconds(2));
    if (first.error != EMSGSIZE || second.error != EMSGSIZE || !second.bytes.empty()) {
        return fail("EMSGSIZE was not sticky, first " + std::to_string(first.error)
            + " second " + std::to_string(second.error));
    }
    return 0;
}

int testLargeConcurrent()
{
    const std::string path = "/tmp/bc-named-pipe-large-" + std::to_string(::getpid());
    ::unlink(path.c_str());
    const std::size_t payloadSize = 256 * 1024;
    std::string payload(payloadSize, '\0');
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>(i & 0xff);
    }

    Bc::BcNamedPipe reader(path, Bc::BcNamedPipe::Mode::Reader);
    reader.setInternalBuffer(payloadSize + 4);
    ::alarm(15);
    if (reader.open(std::chrono::milliseconds(0)) != 0) {
        ::alarm(0);
        return fail("large reader open failed");
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::alarm(0);
        return fail("large fork failed");
    }
    if (pid == 0) {
        ::alarm(10);
        int code = 1;
        {
            Bc::BcNamedPipe writer(path, Bc::BcNamedPipe::Mode::Writer);
            writer.setInternalBuffer(payloadSize + 4);
            if (writer.open(std::chrono::seconds(2)) == 0
                && writer.writeData(payload, std::chrono::seconds(5)).error == 0) {
                code = 0;
            }
        }
        _exit(code);
    }

    const Bc::BcNamedPipe::Result got = reader.readData(std::chrono::seconds(5));
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        ::alarm(0);
        return fail("large waitpid failed");
    }
    ::alarm(0);
    if (childStatus(status) != 0) {
        return fail("large writer failed: " + std::to_string(childStatus(status)));
    }
    if (got.error != 0 || got.bytes != payload) {
        return fail("256 KiB concurrent round-trip failed, error " + std::to_string(got.error)
            + " size " + std::to_string(got.bytes.size()));
    }
    return 0;
}

} // namespace

int main()
{
    static_assert(!std::is_copy_constructible<Bc::BcNamedPipe>::value, "copy construct");
    static_assert(!std::is_copy_assignable<Bc::BcNamedPipe>::value, "copy assign");
    static_assert(std::is_move_constructible<Bc::BcNamedPipe>::value, "move construct");
    static_assert(std::is_move_assignable<Bc::BcNamedPipe>::value, "move assign");

    if (const int rc = testOpenTimeout()) {
        return rc;
    }
    if (const int rc = testReadTimeout()) {
        return rc;
    }
    if (const int rc = testWriteTimeout()) {
        return rc;
    }
    if (const int rc = testRoundTrip()) {
        return rc;
    }
    if (const int rc = testEmptyFrame()) {
        return rc;
    }
    if (const int rc = testReaderWaitsForWriter()) {
        return rc;
    }
    if (const int rc = testMidFrameEof()) {
        return rc;
    }
    if (const int rc = testEmsgsizeSticky()) {
        return rc;
    }
    if (const int rc = testLargeConcurrent()) {
        return rc;
    }
    std::cout << "ok" << std::endl;
    return 0;
}
