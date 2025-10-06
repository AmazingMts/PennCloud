#include <sys/socket.h>
#include "SimpleSocketReader.h"
#include <cstring>
#include <stdexcept>

namespace SimpleSocketReader
{
    std::string read(int sfd, const std::string termination)
    {
        constexpr size_t BUFFER_SIZE = 1024;
        std::string response;
        char buffer[BUFFER_SIZE];

        while (true)
        {
            fprintf(stderr, "SimpleSocketReader: Reading data from socket %d\n", sfd);
            // Peek at the data without removing it from the kernel buffer
            ssize_t peek_bytes_received = recv(sfd, buffer, BUFFER_SIZE, MSG_PEEK);
            if (peek_bytes_received == 0)
                throw std::runtime_error("SimpleSocketReader: Connection closed on socket " + std::to_string(sfd) +
                                         " before message could be peeked at\n");

            if (peek_bytes_received < 0)
                throw std::runtime_error("SimpleSocketReader: Failed reading bytes from socket " + std::to_string(sfd) +
                                         " (" + strerror(errno) + ")\n");

            std::string_view chunk(buffer, peek_bytes_received);

            if (chunk.length() < termination.length())
                continue;
            // Check if termination exists in current chunk
            size_t term_pos = chunk.find(termination);
            if (term_pos != std::string::npos)
            {
                // Calculate how many bytes to actually read (termination + its length)
                size_t bytes_to_read = term_pos + termination.length();

                size_t bytes_received = 0;
                while (bytes_received < bytes_to_read)
                {
                    // Read the remaining bytes
                    ssize_t read = recv(sfd, buffer, bytes_to_read - bytes_received, 0);
                    if (read == 0)
                        throw std::runtime_error("SimpleSocketReader: Connection closed on socket " + std::to_string(sfd) +
                                                 " before message could be read\n");

                    if (read < 0)
                        throw std::runtime_error("SimpleSocketReader: Failed reading bytes from socket " + std::to_string(sfd) +
                                                 " (" + strerror(errno) + ")\n");

                    response.append(buffer, read);
                    bytes_received += read;
                }
                break;
            }
            else
            {
                // Read (almost) all the peeked data since no termination found (termination could cut off at the end)
                size_t bytes_to_read = chunk.length() - termination.length() + 1;

                size_t bytes_received = 0;
                while (bytes_received < bytes_to_read)
                {
                    // Read the remaining bytes
                    ssize_t read = recv(sfd, buffer, std::min(BUFFER_SIZE, bytes_to_read - bytes_received), 0);
                    if (read == 0)
                        throw std::runtime_error("SimpleSocketReader: Connection closed on socket " + std::to_string(sfd) +
                                                 " before message could be read\n");

                    if (read < 0)
                        throw std::runtime_error("SimpleSocketReader: Failed reading bytes from socket " + std::to_string(sfd) +
                                                 " (" + strerror(errno) + ")\n");

                    response.append(buffer, read);
                    bytes_received += read;
                }
            }
        }
        response.resize(response.size() - termination.length());
        return response;
    }
}