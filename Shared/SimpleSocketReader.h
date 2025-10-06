#pragma once

#include <string>

namespace SimpleSocketReader
{
    // A stateless reader function that reads data from the socket and returns it without the termination
    // Throws runtime error if any error occurs
    std::string read(int fd, const std::string termination = "\r\n");
}