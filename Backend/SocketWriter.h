#ifndef WRITER_H
#define WRITER_H
#include <string>
#include <unistd.h>
#include <cstddef>
#include <vector>

using namespace std;

typedef std::vector<std::byte> tablet_value;

class SocketWriter
{
public:
    SocketWriter(int socket_fd);
    ~SocketWriter() = default;

    // Writes message to the socket used at object instantiation
    ssize_t write_message(const string &message, bool add_termination = true);
    ssize_t write_message(const tablet_value &message);

private:
    int socket_fd_;
};

#endif
