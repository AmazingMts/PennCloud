#ifndef SMTPCOMMANDDISPATCHER_H
#define SMTPCOMMANDDISPATCHER_H

#include <random>
#include <sstream>
#include "SmtpCommandDispatcher.h"
#include <regex>
#include "Server.h"
#include <chrono>
#include <iomanip> // For std::put_time
#include <ctime>   // For std::localtime
#include <sstream>
#include "Globals.h"
#include "SocketWriter.h"
#include "relay.h"
#include "SharedStructures.h"

using namespace std;

namespace uuid
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);

    std::string generate_uuid_v4()
    {
        std::stringstream ss;
        int i;
        ss << std::hex;
        for (i = 0; i < 8; i++)
        {
            ss << dis(gen);
        }
        ss << "-";
        for (i = 0; i < 4; i++)
        {
            ss << dis(gen);
        }
        ss << "-4";
        for (i = 0; i < 3; i++)
        {
            ss << dis(gen);
        }
        ss << "-";
        ss << dis2(gen);
        for (i = 0; i < 3; i++)
        {
            ss << dis(gen);
        }
        ss << "-";
        for (i = 0; i < 12; i++)
        {
            ss << dis(gen);
        };
        return ss.str();
    }
}

string get_current_timestamp_()
{
    std::ostringstream oss;
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::chrono::system_clock::duration d = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
    oss << millis;
    return oss.str();
}

// Override the Dispatch method
DispatcherResponse SmtpCommandDispatcher::dispatch(const string &message)
{
    if (current_state_ == SmtpState::DATA_RECEIVING)
        return handle_data_receiving_(message);

    if (message == "TQUIT")
        return {DispatcherStatusCode::QUIT, ""};

    Command<SmtpServerCommand> cmd = parse_message_(message);

    if (cmd.cmd == SmtpServerCommand::ERR)
        return {DispatcherStatusCode::DISPATCHER_OK, "500 Unknown command"};

    if (!validate_command_(cmd))
        return {DispatcherStatusCode::DISPATCHER_OK, "501 Syntax error in parameters or arguments"};

    return execute_command_(cmd);
}

Command<SmtpServerCommand> SmtpCommandDispatcher::parse_message_(const std::string &message)
{
    Command<SmtpServerCommand> cmd;

    // Remove leading and trailing spaces from message
    std::regex spaceRegex("^\\s+|\\s+$");
    string trimmed_message = regex_replace(message, spaceRegex, "");

    // Command regex
    std::regex commandRegex(R"(^\s*(HELO|MAIL|RCPT|DATA|RSET|NOOP|QUIT)\s*(.*)\s*$)", std::regex::icase); // Regex to split command from message
    std::smatch match;
    if (std::regex_search(trimmed_message, match, commandRegex))
    {
        // Process Command
        string command = match.str(1);
        transform(command.begin(), command.end(), command.begin(), ::toupper); // Command to upper case
        command = regex_replace(command, spaceRegex, "");                      // Remove leading and trailing spaces
        if (command == "HELO")
            cmd.cmd = SmtpServerCommand::HELO;
        else if (command == "MAIL")
            cmd.cmd = SmtpServerCommand::MAIL;
        else if (command == "RCPT")
            cmd.cmd = SmtpServerCommand::RCPT;
        else if (command == "DATA")
            cmd.cmd = SmtpServerCommand::DATA;
        else if (command == "RSET")
            cmd.cmd = SmtpServerCommand::RSET;
        else if (command == "NOOP")
            cmd.cmd = SmtpServerCommand::NOOP;
        else if (command == "QUIT")
            cmd.cmd = SmtpServerCommand::QUIT;

        // Process Args
        string msg = trimmed_message.substr(command.length()); // Remaining part of the message
        msg = regex_replace(msg, spaceRegex, "");              // Remove leading and trailing spaces
        cmd.args = msg;

        return cmd;
    }

    return {SmtpServerCommand::ERR, ""};
}

bool SmtpCommandDispatcher::validate_command_(const Command<SmtpServerCommand> &command)
{
    // Commands that need an argument
    if ((command.cmd == SmtpServerCommand::HELO || command.cmd == SmtpServerCommand::MAIL || command.cmd == SmtpServerCommand::RCPT) && command.args.empty())
        return false;

    // Commands that dont need an argument
    if ((command.cmd == SmtpServerCommand::DATA || command.cmd == SmtpServerCommand::RSET || command.cmd == SmtpServerCommand::NOOP || command.cmd == SmtpServerCommand::QUIT) && !command.args.empty())
        return false;

    // Match command to appropriate regex
    std::regex rgx = command_args_regex_map_[command.cmd];
    std::smatch match;
    if (!std::regex_search(command.args, match, rgx))
    {
        return false;
    }

    // TODO: If its a MAIL or RCPT command, check if the user and domain are valid

    return true;
}

DispatcherResponse SmtpCommandDispatcher::execute_command_(Command<SmtpServerCommand> cmd)
{
    switch (cmd.cmd)
    {
    case SmtpServerCommand::NOOP:
    {
        // No operation, just return a successful response
        return {DispatcherStatusCode::DISPATCHER_OK, "211 System status, or system help reply"};
    }
    case SmtpServerCommand::RSET:
    {
        // Can only occur after HELO, so state should NOT be INITIAL. If not, its a bad sequence of commands.
        if (current_state_ != SmtpState::HELO_RECEIVED)
            return {DispatcherStatusCode::DISPATCHER_OK, "503 Bad sequence of commands"};

        // Reset state to Initial
        reset_state_();
        current_state_ = SmtpState::INITIAL;
        return {DispatcherStatusCode::DISPATCHER_OK, "250 OK"};
    }
    case SmtpServerCommand::QUIT:
    {
        reset_state_();
        current_state_ = SmtpState::INITIAL;
        return {DispatcherStatusCode::QUIT, "221 Service closing transmission channel"};
    }
    case SmtpServerCommand::HELO:
    {
        // Either state is INITIAL or we have received a HELO before
        if (!(current_state_ == SmtpState::INITIAL || current_state_ == SmtpState::HELO_RECEIVED))
            return {DispatcherStatusCode::DISPATCHER_OK, "503 Bad sequence of commands"};

        reset_state_();
        current_state_ = SmtpState::HELO_RECEIVED;
        return {DispatcherStatusCode::DISPATCHER_OK, "250 OK"};
    }
    case SmtpServerCommand::MAIL:
    {
        if (current_state_ != SmtpState::HELO_RECEIVED)
            return {DispatcherStatusCode::DISPATCHER_OK, "503 Bad sequence of commands"};

        // Extract group 2 and 3 from the regex
        std::smatch match;
        regex_match(cmd.args, match, command_args_regex_map_[SmtpServerCommand::MAIL]);
        string user = match[2];
        string domain = match[3];

        // TODO: User and domain format checks???
        // // Check that the domain is localhost
        // if (domain != "localhost")
        // {
        //     return "501 - Syntax error in parameters or arguments";
        // }

        // // Check that the user is valid syntax
        // regex validUserRegex(R"(^([a-zA-Z]([a-zA-Z0-9-#]*[a-zA-Z0-9])?(\.[a-zA-Z]([a-zA-Z0-9-#]*[a-zA-Z0-9])?)?)+$)");
        // if (!regex_search(user, validUserRegex))
        // {
        //     return "501 - Syntax error in parameters or arguments";
        // }

        // TODO: Check if user exists in the storage. How can this be done?
        // // Check that the user is in the mailbox directory
        // if (DEBUG)
        // {
        //     fprintf(stderr, "Checking if user %s is in the mailbox directory\n", user.c_str());
        // }

        // List of users from accoutns storage and check it it exists
        // bool exists = IMailboxStorageService::checkMailboxExists(mailbox_path_ + user + ".mbox");
        // if (!exists)
        // {
        //     return "551 User not local";
        // }

        // Clear the forward path and mail data buffers
        // forward_path_.clear();
        mail_data_.clear();
        recipients_.clear();

        // Store the reverse path
        sender_ = {user, domain};
        // reverse_path_ = cmd.args;
        // user_from_ = user;
        current_state_ = SmtpState::MAILFROM_RECEIVED;

        return {DispatcherStatusCode::DISPATCHER_OK, "250 OK"};
    }
    case SmtpServerCommand::RCPT:
    {
        if (!(current_state_ == SmtpState::MAILFROM_RECEIVED || current_state_ == SmtpState::RCPT_RECEIVING))
            return {DispatcherStatusCode::DISPATCHER_OK, "503 Bad sequence of commands"};

        // Extract group 2 and 3 from the regex
        std::smatch match;
        regex_match(cmd.args, match, command_args_regex_map_[SmtpServerCommand::RCPT]);
        string user = match[2];
        string domain = match[3];

        // // Check that the domain is localhost
        // if (domain != "localhost")
        // {
        //     return "501 - Syntax error in parameters or arguments";
        // }

        // // Check that the user is valid syntax
        // regex validUserRegex(R"(^([a-zA-Z]([a-zA-Z0-9-#]*[a-zA-Z0-9])?(\.[a-zA-Z]([a-zA-Z0-9-#]*[a-zA-Z0-9])?)?)+$)");
        // if (!regex_search(user, validUserRegex))
        // {
        //     return "501 - Syntax error in parameters or arguments";
        // }

        // // Check that the user is in the mailbox directory
        // if (DEBUG)
        // {
        //     fprintf(stderr, "Checking if user %s is in the mailbox directory\n", user.c_str());
        // }

        // bool exists = IMailboxStorageService::checkMailboxExists(mailbox_path_ + user + ".mbox");
        // if (!exists)
        // {
        //     return "550 User not local";
        // }

        // Check that the user is not already in the forward path so we dont send the email twice to the same user
        // for (const string &recipient : users_to_)
        // {
        //     if (recipient == user)
        //     {
        //         return {DispatcherStatusCode::DISPATCHER_OK, "250 OK"};
        //     }
        // }

        // TODO: Check if local user exists in storage
        recipients_.insert({user, domain});

        current_state_ = SmtpState::RCPT_RECEIVING;

        // forward_path_.push_back(cmd.args);
        // users_to_.push_back(user);

        return {DispatcherStatusCode::DISPATCHER_OK, "250 OK"};
    }
    case SmtpServerCommand::DATA:
    {
        if (current_state_ != SmtpState::RCPT_RECEIVING)
            return {DispatcherStatusCode::DISPATCHER_OK, "503 Bad sequence of commands"};

        // Set the state to DATA_RECEIVING
        current_state_ = SmtpState::DATA_RECEIVING;
        return {DispatcherStatusCode::DISPATCHER_OK, "354 Start mail input; end with <CRLF>.<CRLF>"};
    }
    default:
        return {DispatcherStatusCode::DISPATCHER_OK, "500 Syntax error, command unrecognized"};
        break;
    }
}

DispatcherResponse SmtpCommandDispatcher::handle_data_receiving_(const std::string &message)
{
    // Check if the message is the termination message
    if (message == ".")
    {
        // Store the email in the storage
        bool success = write_mail_();

        // Reset state to right after HELO Command
        reset_state_();
        current_state_ = SmtpState::HELO_RECEIVED;

        // Return if it was successfull or not
        if (success)
            return {DispatcherStatusCode::DISPATCHER_OK, "250 OK"};

        return {DispatcherStatusCode::DISPATCHER_OK, "550 Requested action not taken: Failed to store (some) email (IStorageService = 10)"};
    }

    // Append the message to the mail data buffer
    mail_data_.push_back(message);

    // NO_RESPONSE will prompt clientHandler to not write a response to the client
    return {DispatcherStatusCode::DISPATCHER_OK, ""};
}

bool SmtpCommandDispatcher::write_mail_()
{
    if (DEBUG)
        printf("Sending mail to recipients\n");

    // Generate a unique ID for the email
    std::string email_id = uuid::generate_uuid_v4();

    // Join the mail data into a single string
    std::string mail_to_store;
    for (const auto &line : mail_data_)
    {
        mail_to_store += line + "\r\n";
    }

    // Send message to each recipient
    bool success = true;
    std::vector<EmailAddress> remote_recipients;
    for (const auto &recipient : recipients_)
    {
        if (recipient.domain == LOCAL_DOMAIN)
        {
            IStorageService storage(COORDINATOR_SERVICE->get_kv_servers_map());
            if (storage.put(user_to_row(recipient.user) + "-MAILBOX", email_id, IStorageService::from_string(mail_to_store)))
                ; // TODO: handle failure
        }
        else
        {
            remote_recipients.push_back(recipient);
        }

        // if (response_code == 10)
        //     fprintf(stderr, "Error saving message with id %s to recipient: %s\n", email_id.c_str(), recipient.c_str());

        // success = success && (response_code == 0);
    }

    if (!remote_recipients.empty())
    {
        RemoteEmail *email = new RemoteEmail;
        email->user_from = sender_;
        email->recipients = std::move(remote_recipients);
        email->mail_content = std::move(mail_to_store);

        if (DEBUG)
            fprintf(stdout, "Sending mail to remote recipients\n");

        // Launch thread for mail relay
        pthread_t thread_id;
        if (pthread_create(&thread_id, nullptr, relay_main, email))
        {
            fprintf(stderr, "Failed to create thread for mail relay\n");
            delete email;
            return success;
        }

        if (pthread_detach(thread_id))
        {
            perror("Failed to detach thread for mail relay\n");
            delete email;
            return success;
        }
    }

    current_state_ = SmtpState::DATA_RECEIVED;
    return success;
}

void SmtpCommandDispatcher::reset_state_()
{
    // user_from_.clear();
    // reverse_path_.clear();
    // users_to_.clear();
    // forward_path_.clear();
    sender_.user.clear();
    sender_.domain.clear();
    recipients_.clear();
    mail_data_.clear();
}

#endif