#include "BcNamedPipe.h"

#include <chrono>
#include <iostream>
#include <string>
#include <type_traits>

#include <cerrno>
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
            } else if (writer.writeData(withNul).error != 0) {
                code = 3;
            } else if (writer.writeData(longer).error != 0) {
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

    const Bc::BcNamedPipe::Result gotNul = reader.readData();
    const Bc::BcNamedPipe::Result gotLong = reader.readData();
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
    if (const int rc = testRoundTrip()) {
        return rc;
    }
    std::cout << "ok" << std::endl;
    return 0;
}
