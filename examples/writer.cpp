#include "BcNamedPipe.h"

#include <chrono>
#include <iostream>
#include <string>

#include <cerrno>
#include <unistd.h>

// Writer process.
//   bc_pipe_writer <path>           write a payload that contains an embedded NUL
//   bc_pipe_writer <path> timeout   open while no reader exists; expect ETIMEDOUT

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: bc_pipe_writer <path> [timeout]\n";
        return 2;
    }

    const std::string path = argv[1];
    const bool missingPeer = argc == 3 && std::string(argv[2]) == "timeout";

    if (missingPeer) {
        ::unlink(path.c_str());
    }

    Bc::BcNamedPipe writer(path, Bc::BcNamedPipe::Mode::Writer);
    writer.setInternalBuffer(4096);

    if (missingPeer) {
        const int rc = writer.open(std::chrono::milliseconds(300));
        if (rc != ETIMEDOUT) {
            std::cerr << "expected ETIMEDOUT, got " << rc << "\n";
            return 1;
        }
        std::cout << "ETIMEDOUT\n";
        return 0;
    }

    const int opened = writer.open(std::chrono::seconds(3));
    if (opened != 0) {
        std::cerr << "open " << opened << "\n";
        return 1;
    }

    const std::string message("hello\0world", 11);
    const Bc::BcNamedPipe::Result result = writer.writeData(message, std::chrono::seconds(3));
    if (result.error != 0 || result.bytes != message) {
        std::cerr << "write " << result.error << "\n";
        return 1;
    }
    std::cout << "wrote " << message.size() << "\n";
    return 0;
}
