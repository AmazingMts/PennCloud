#pragma once

#include "ICommandDispatcher.h"
#include "IStorageService.h"
#include <regex>
#include <algorithm>

enum class Pop3ServerCommand
{
    USER,
    PASS,
    STAT,
    UIDL,
    UIDL_ALL,
    RETR,
    DELE,
    QUIT,
    LIST_ALL,
    LIST,
    RSET,
    NOOP,
    ERR
};

enum class Pop3State
{
    AUTHORIZATION,
    AUTHORIZATION_PASS,
    TRANSACTION
};

struct EmailInfo
{
    size_t size;
    bool deleted;
    std::string id;
};

class Pop3CommandDispatcher : public ICommandDispatcher
{
public:
    Pop3CommandDispatcher() {}
    DispatcherResponse dispatch(const string &message) override;

private:
    Pop3State state{Pop3State::AUTHORIZATION};
    std::string user_name;
    std::vector<EmailInfo> email_infos;

    DispatcherResponse execute_command(Command<Pop3ServerCommand> cmd);
    Command<Pop3ServerCommand> parse_message(const std::string &message);
    int get_email_infos();
};

// Inserts capitalized string (from it0 to it1) into in
inline void to_upper(const std::string::const_iterator it0, const std::string::const_iterator it1,
                     std::string &in)
{
    std::transform(it0, it1, std::back_inserter(in), ::toupper);
}