#include "BcNamedPipe.h"

#include <chrono>
#include <iostream>
#include <string>

#include <cerrno>
#include <poll.h>
#include <unistd.h>

// Reader process. Creates the FIFO, reads one frame, prints embedded NULs as \0.
// Usage: bc_pipe_reader <path>

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: bc_pipe_reader <path>\n";
        return 2;
    }

    const std::string path = argv[1];
    ::unlink(path.c_str());

    Bc::BcNamedPipe reader(path, Bc::BcNamedPipe::Mode::Reader);
    reader.setInternalBuffer(4096);
    const int opened = reader.open(std::chrono::milliseconds(0));
    if (opened != 0) {
        std::cerr << "open " << opened << "\n";
        return 1;
    }

    // Until a writer opens the FIFO, read reports EOF. Keep the fd open and
    // retry until the frame arrives or the deadline passes.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    Bc::BcNamedPipe::Result result{ETIMEDOUT, {}};
    while (std::chrono::steady_clock::now() < deadline) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        result = reader.readData(left);
        if (result.error != Bc::BcNamedPipe::kPeerClosed) {
            break;
        }
        ::poll(nullptr, 0, 20);
    }
    if (result.error != 0) {
        std::cerr << "read " << result.error << "\n";
        return 1;
    }

    std::cout << "size=" << result.bytes.size() << "\n";
    for (const char byte : result.bytes) {
        if (byte == '\0') {
            std::cout << "\\0";
        } else {
            std::cout << byte;
        }
    }
    std::cout << "\n";
    return 0;
}
