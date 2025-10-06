#ifndef SOCKETREADER_H
#define SOCKETREADER_H

#include "ThreadSafeQueue.h"
#include "Globals.h"

class SocketReader
{
public:
    SocketReader(int socket_fd_, ThreadSafeQueue &queue);
    ~SocketReader() = default;

    // Continuously read data from socket and pushes new messages to ThreadSafeQueue, if not it blocks
    void read_data();

private:
    int socket_fd_;
    ThreadSafeQueue &message_queue_;
};

#endif
