#pragma once

#include <string>
#include <sstream>
#include <limits>
#include <iomanip>
#include <random>

struct KVServer
{
    std::string host;
    int port;
    int port_gc;
    bool is_primary;
    bool is_alive;
};

// Domain name for PennCloud
const std::string LOCAL_DOMAIN{"penncloud.upenn.edu"};
// Length of string hashes of user names
constexpr size_t HASH_LENGTH{std::numeric_limits<size_t>::digits10 + 1};

inline std::string user_to_row(const std::string &user)
{
    const static std::hash<std::string> hash_string;

    std::stringstream ss;
    ss << std::setw(HASH_LENGTH) << std::setfill('0') << hash_string(user);
    return ss.str();
}

inline std::string num_to_row(size_t num)
{
    std::stringstream ss;
    ss << std::setw(HASH_LENGTH) << std::setfill('0') << std::to_string(num);
    return ss.str();
}

inline void truncated_print(const std::string &prefix, const std::string &message)
{
    constexpr int MAX_MESSAGE_SIZE = 200;
    if (message.size() > MAX_MESSAGE_SIZE)
    {
        fprintf(stderr, "%s: %s...[%zu]...%s\n", prefix.c_str(), message.substr(0, MAX_MESSAGE_SIZE/2).c_str(),
            message.size() - MAX_MESSAGE_SIZE, message.substr(message.size() - MAX_MESSAGE_SIZE/2).c_str());
    }
    else
    {
        fprintf(stderr, "%s: %s\n", prefix.c_str(), message.c_str());
    }
}

inline void truncated_print(const std::string &prefix, const std::string &message, int fd)
{
    constexpr int MAX_MESSAGE_SIZE = 200;
    if (message.size() > MAX_MESSAGE_SIZE)
    {
        fprintf(stderr, "%s [%d]: %s...[%zu]...%s\n", prefix.c_str(), fd, message.substr(0, MAX_MESSAGE_SIZE/2).c_str(),
            message.size() - MAX_MESSAGE_SIZE, message.substr(message.size() - MAX_MESSAGE_SIZE/2).c_str());
    }
    else
    {
        fprintf(stderr, "%s [%d]: %s\n", prefix.c_str(), fd, message.c_str());
    }
}