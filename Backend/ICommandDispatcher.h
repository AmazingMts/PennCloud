#ifndef ICOMMANDDISPACHER_H
#define ICOMMANDDISPACHER_H

#include <string>

using namespace std;

enum class DispatcherStatusCode
{
    QUIT,
    DISPATCHER_OK,
    TO_PT,
    READING_DATA
};

typedef pair<DispatcherStatusCode, string> DispatcherResponse;

template <typename T>
struct Command
{
    T cmd;
    std::string args;
};

class ICommandDispatcher
{
public:
    virtual ~ICommandDispatcher() = default; // Virtual destructor for proper cleanup in derived classes

    // Gets a message from client, performs necessary server specific parsing (i.e., SMTP, POP3, etc)
    // and returns a string response to be sent to client through the ClientHandler class.
    virtual DispatcherResponse dispatch(const string &message) = 0; // Pure virtual function
};

#endif