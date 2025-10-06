#include "SocketWriter.h"
#include <cstring>

using namespace std;

SocketWriter::SocketWriter(int socket_fd)
{
    this->socket_fd_ = socket_fd;
}

ssize_t SocketWriter::write_message(const string &message, bool add_termination)
{
    string message_termination = message;
    if (add_termination) message_termination += "\r\n";
    size_t total_bytes_written = 0;
    size_t message_length = message_termination.size();
    const char *data = message_termination.c_str();

    while (total_bytes_written < message_length)
    {
        ssize_t bytes_written = write(this->socket_fd_, data + total_bytes_written, message_length - total_bytes_written);

        if (bytes_written == -1)
        {
            fprintf(stderr, "[%d] Error writing message to socket (%s). (expected behaviour if Ctrl+C is executed on running server)\n", this->socket_fd_, strerror(errno));
            return -1;
        }

        total_bytes_written += bytes_written;
    }

    return total_bytes_written;
}

ssize_t SocketWriter::write_message(const tablet_value &message)
{
    size_t total_bytes_written = 0;
    size_t message_length = message.size();
    const char *data = reinterpret_cast<const char *>(message.data());

    while (total_bytes_written < message_length)
    {
        ssize_t bytes_written = write(this->socket_fd_, data + total_bytes_written, message_length - total_bytes_written);

        if (bytes_written == -1)
        {
            fprintf(stderr, "[%d] Error writing binary message to socket. (expected behaviour if Ctrl+C is executed on running server)\n", this->socket_fd_);
            return -1;
        }

        total_bytes_written += bytes_written;
    }

    return total_bytes_written;
}