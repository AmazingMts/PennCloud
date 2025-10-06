#pragma once

#include "ICommandDispatcher.h"
#include "IStorageService.h"
#include <regex>
#include <algorithm>
#include <sstream>
#include <netinet/in.h>

enum class SmtpServerCommand
{
    HELO,
    MAIL,
    RCPT,
    DATA,
    RSET,
    NOOP,
    QUIT,
    ERR
};

enum class SmtpState
{
    INITIAL,
    HELO_RECEIVED,
    MAILFROM_RECEIVED,
    RCPT_RECEIVING,
    DATA_RECEIVED,
    DATA_RECEIVING,
};

struct EmailAddress
{
    std::string user;   // User receiving the email
    std::string domain; // Domain of the user

    // Implement compare function so struct can be used in set
    bool operator<(const EmailAddress &other) const
    {
        return std::tie(user, domain) < std::tie(other.user, other.domain);
    }
};

namespace uuid
{
    std::string generate_uuid_v4();
}

class SmtpCommandDispatcher : public ICommandDispatcher
{
public:
    SmtpCommandDispatcher() {}
    DispatcherResponse dispatch(const string &message) override;

private:
    SmtpState current_state_ = SmtpState::INITIAL;           // Current state of the SMTP transaction
    map<SmtpServerCommand, regex> command_args_regex_map_ = {// Map from command to regex to parse the command arguments
                                                             {SmtpServerCommand::HELO, std::regex{R"(^(([a-zA-Z]([a-zA-Z0-9-#]*[a-zA-Z0-9])?(\.[a-zA-Z]([a-zA-Z0-9-#]*[a-zA-Z0-9])?)?)+|\[([0-9]{1,3}(\.[0-9]{1,3}){3})\])$)", std::regex::icase}},
                                                             {SmtpServerCommand::MAIL, std::regex{R"(^FROM:<((.*)@(.*))>$)", std::regex::icase}},
                                                             {SmtpServerCommand::RCPT, std::regex{R"(^TO:<((.*)@(.*))>$)", std::regex::icase}},
                                                             {SmtpServerCommand::DATA, std::regex{R"(^\s*$)", std::regex::icase}},
                                                             {SmtpServerCommand::RSET, std::regex{R"(^\s*$)", std::regex::icase}},
                                                             {SmtpServerCommand::NOOP, std::regex{R"(^\s*$)", std::regex::icase}},
                                                             {SmtpServerCommand::QUIT, std::regex{R"(^\s*$)", std::regex::icase}}};

    // Buffers to store the state during a mail transaction
    // string user_from_;                              // User sending the email
    // string reverse_path_;                           // MAIL command argument
    EmailAddress sender_; // Sender email address
    // vector<string> users_to_;                       // Users receiving argument
    // vector<string> forward_path_;                   // RCPT command argument
    set<EmailAddress> recipients_;
    vector<string> mail_data_;                      // DATA command content
    map<string, vector<sockaddr_in>> mail_servers_; // Map to list of mail servers for each domain

    // Basic dispatcher functions
    Command<SmtpServerCommand> parse_message_(const std::string &message); // Parse a message into a command
    bool validate_command_(const Command<SmtpServerCommand> &command);     // Validate a command using regex
    DispatcherResponse execute_command_(Command<SmtpServerCommand> cmd);   // Execute a command and return a response

    // Other helper functions for command handling
    DispatcherResponse handle_data_receiving_(const std::string &message);
    void reset_state_(); // Return to the initial state (the state after a successful HELO)
    bool write_mail_();  // Write the email to the storage
    bool send_remote_mail_(const EmailAddress &user_from, const EmailAddress &recipient,
                           const sockaddr_in remote_server, const std::string &mail_content);
};

string get_current_timestamp_();