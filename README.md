# BcNamedPipe

Contract for one POSIX named pipe between one reader and one writer. Linux only.

## Name

The constructor stores the path and mode and calls `mkfifo` with mode `0600`. It does not open the descriptor.

The instance whose `mkfifo` succeeds creates the name. If the name is already taken, `mkfifo` returns `EEXIST`: that instance did not create the name and does not own it.

The destructor closes the descriptor. Only the owner of the name calls `unlink`. The destructor does not remove a name it does not own.

## Frame

A frame is a `uint32` length in little-endian order followed by a payload of that length. An embedded NUL is part of the payload and does not end the message.

`readData` copies bytes until one full frame is assembled. `writeData` writes the frame to completion or returns an error.

If a write sends part of a frame and then fails, if a read sees EOF in the middle of a frame, or if the length does not fit in the buffer (`EMSGSIZE`), that error sticks. Later `readData` and `writeData` return the same error and transfer no more bytes. Close both ends.

## Sides

The pipe has one `Mode::Reader` and one `Mode::Writer`.

Several writers mix frames: bytes from different writers land in the pipe interleaved, and the reader no longer sees each writer's messages intact.

## Waiting

`open`, `readData`, and `writeData` take a `std::chrono::milliseconds` timeout.

The reader opens with `O_RDONLY|O_NONBLOCK` and returns immediately. The writer opens with `O_WRONLY|O_NONBLOCK` and, on `ENXIO`, waits for a reader no longer than the timeout.

`readData` waits for one frame, including while no writer has opened, but not longer than the timeout. EOF with nothing buffered waits out the timeout and returns `ETIMEDOUT`. `writeData` checks the deadline before each write, so a slow peer cannot stretch the call by accepting one byte at a time. When time runs out, the call returns `ETIMEDOUT` and leaves the descriptor open. A timeout after any byte of the current frame was sent sticks.

`Result.error == 0` means `bytes` holds one complete frame, which may be empty. `kPeerClosed` (`-1`) means the peer closed in the middle of a frame; that value sticks and is not an `errno`. A positive value is `errno`.
