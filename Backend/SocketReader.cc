#include <unistd.h>
#include <string.h>
#include "Server.h" // Needed for DEBUG flag global variable
#include "SocketReader.h"

SocketReader::SocketReader(int socket_fd, ThreadSafeQueue &message_queue) : message_queue_(message_queue)
{
    this->socket_fd_ = socket_fd;
}

void SocketReader::read_data()
{
    string message;
    char buffer[2048];
    ssize_t bytes_read;

    while (true)
    {
        bytes_read = read(this->socket_fd_, buffer, sizeof(buffer));

        if (bytes_read > 0)
        {
            message.append(buffer, bytes_read);

            size_t newline_pos;
            while ((newline_pos = message.find("\r\n")) != std::string::npos)
            {
                string complete_message = message.substr(0, newline_pos);
                this->message_queue_.push(complete_message);
                message.erase(0, newline_pos + 2);

                // Optionally clear the buffer explicitly, though it's generally overwritten with the next read
                // Clear the buffer (for safety)
                // memset(buffer, 0, sizeof(buffer));
            }
        }
        else if (bytes_read == 0)
        {
            if (DEBUG)
            {
                fprintf(stderr, "[%d] (SocketReader): Connection closed by the client\n", this->socket_fd_);
            }
            // TODO: Handle this with a singal to the clientHandler thread
            // to shut down the connection.

            // Notify clientHandler to exit processing loop
            this->message_queue_.push("TQUIT");
            return;
        }
        else
        {
            if (errno == ECONNRESET || errno == EPIPE)
            {
                fprintf(stderr, "[%d] (SocketReader): Connection reset by peer\n", this->socket_fd_);
            }
            else
            {
                // When a client requests to close connection with QUIT, this might be printed
                // because clientHandler closed connection and read command got "Bad file descriptor" error.
                fprintf(stderr, "[%d] (SocketReader): Error reading data from socket: %s\n", this->socket_fd_, strerror(errno));
            }
            this->message_queue_.push("TQUIT");
            return;
        }
    }
}