#include <numeric>
#include "Pop3CommandDispatcher.h"
#include "Server.h"
#include "Globals.h"
#include "SharedStructures.h"

DispatcherResponse Pop3CommandDispatcher::dispatch(const string &message)
{
    if (message == "TQUIT")
        return {DispatcherStatusCode::QUIT, ""};

    Command<Pop3ServerCommand> cmd = parse_message(message);

    if (cmd.cmd == Pop3ServerCommand::ERR)
        return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Unknown command"};

    return execute_command(cmd);
}

DispatcherResponse Pop3CommandDispatcher::execute_command(Command<Pop3ServerCommand> cmd)
{
    switch (cmd.cmd)
    {
    case Pop3ServerCommand::QUIT:
        // Only update emails if in transaction state
        if (state == Pop3State::TRANSACTION)
        {
            for (auto &email_info : email_infos)
            {
                if (email_info.deleted)
                {
                    IStorageService storage(COORDINATOR_SERVICE->get_kv_servers_map());
                    storage.remove(user_to_row(user_name) + "-MAILBOX", email_info.id);
                }
            }
        }

        return {DispatcherStatusCode::QUIT, "+OK Disconnecting"};

    case Pop3ServerCommand::STAT:
    {
        if (state != Pop3State::TRANSACTION)
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Bad command sequence"};

        // Get total number of octets
        size_t total_size{std::accumulate(
            email_infos.begin(),
            email_infos.end(),
            0u,
            [](size_t sz, EmailInfo &ei)
            { return sz + (ei.deleted ? 0u : ei.size); })};

        return {DispatcherStatusCode::DISPATCHER_OK,
                "+OK " + std::to_string(email_infos.size()) + " " + std::to_string(total_size)};
    }

    case Pop3ServerCommand::UIDL_ALL:
    {
        if (state != Pop3State::TRANSACTION)
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Bad command sequence"};

        // Print all uidls
        std::string msg{"+OK\r\n"};
        for (int i{0}; i < email_infos.size(); ++i)
        {
            if (!email_infos[i].deleted)
                msg += std::to_string(i + 1) + " " + email_infos[i].id + "\r\n";
        }
        msg += ".";

        return {DispatcherStatusCode::DISPATCHER_OK, msg};
    }

    case Pop3ServerCommand::UIDL:
        if (state != Pop3State::TRANSACTION)
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Bad command sequence"};

        try
        {
            int email_idx = std::stoi(cmd.args);

            if ((email_idx < 1) || (email_idx > email_infos.size()) || (email_infos[email_idx - 1].deleted))
                throw std::invalid_argument("");

            return {DispatcherStatusCode::DISPATCHER_OK,
                    "+OK " + std::to_string(email_idx) + " " + email_infos[email_idx - 1].id};
        }
        catch (const std::invalid_argument &error)
        {
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Invalid argument"};
        }

    case Pop3ServerCommand::RETR:
        if (state != Pop3State::TRANSACTION)
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Bad command sequence"};

        try
        {
            int email_idx = std::stoi(cmd.args) - 1;

            if ((email_idx < 0) || (email_idx >= email_infos.size()) || (email_infos[email_idx].deleted))
                throw std::invalid_argument("");

            std::string email;
            std::string header;

            // Retrieve email
            tablet_value email_data;
            IStorageService storage(COORDINATOR_SERVICE->get_kv_servers_map());
            if (storage.get(user_to_row(user_name) + "-MAILBOX", email_infos[email_idx].id, email_data))
                return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Failed to retrieve email"};

            email = IStorageService::to_string(email_data);

            // Print email
            return {DispatcherStatusCode::DISPATCHER_OK, "+OK\r\n" + email + "\r\n."};
        }
        catch (const std::invalid_argument &error)
        {
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Invalid argument"};
        }

    case Pop3ServerCommand::RSET:
        if (state != Pop3State::TRANSACTION)
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Bad command sequence"};

        for (auto &email_info : email_infos)
            email_info.deleted = false;

        return {DispatcherStatusCode::DISPATCHER_OK, "+OK Reset all emails"};

    case Pop3ServerCommand::DELE:
        if (state != Pop3State::TRANSACTION)
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Bad command sequence"};

        try
        {
            int email_idx = std::stoi(cmd.args);

            if ((email_idx < 1) || (email_idx > email_infos.size()) || (email_infos[email_idx - 1].deleted))
                throw std::invalid_argument("");

            email_infos[email_idx - 1].deleted = true;

            return {DispatcherStatusCode::DISPATCHER_OK, "+OK Marked for deletion"};
        }
        catch (const std::invalid_argument &error)
        {
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Invalid argument"};
        }

    case Pop3ServerCommand::LIST_ALL:
    {
        if (state != Pop3State::TRANSACTION)
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Bad command sequence"};

        size_t num_emails{std::accumulate(
            email_infos.begin(),
            email_infos.end(),
            0u,
            [](size_t num, EmailInfo &ei)
            { return num + 1 - ei.deleted; })};

        std::string msg{"+OK " + std::to_string(num_emails) + " emails\r\n"};
        for (int i{0}; i < email_infos.size(); ++i)
        {
            if (!email_infos[i].deleted)
                msg += std::to_string(i + 1) + " " + std::to_string(email_infos[i].size) + "\r\n";
        }
        msg += ".";

        return {DispatcherStatusCode::DISPATCHER_OK, msg};
    }
    case Pop3ServerCommand::LIST:
        if (state != Pop3State::TRANSACTION)
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Bad command sequence"};

        try
        {
            int email_idx = std::stoi(cmd.args);

            if ((email_idx < 1) || (email_idx > email_infos.size()) || (email_infos[email_idx - 1].deleted))
                throw std::invalid_argument("");

            return {DispatcherStatusCode::DISPATCHER_OK,
                    "+OK " + std::to_string(email_idx) + " " + std::to_string(email_infos[email_idx - 1].size)};
        }
        catch (const std::invalid_argument &error)
        {
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Invalid argument"};
        }

    case Pop3ServerCommand::NOOP:
        if (state != Pop3State::TRANSACTION)
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Bad command sequence"};

        return {DispatcherStatusCode::DISPATCHER_OK, "+OK"};

    case Pop3ServerCommand::USER:
    {
        if (state != Pop3State::AUTHORIZATION)
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Bad command sequence"};

        // Check if user exists
        std::set<std::string> users;
        IStorageService storage(COORDINATOR_SERVICE->get_kv_servers_map());
        if (storage.list_columns(user_to_row("ACCOUNTS"), users))
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Failed to retrieve available users"};

        if (users.count(cmd.args) == 0)
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Never heard of mailbox name"};

        user_name = cmd.args;
        state = Pop3State::AUTHORIZATION_PASS;

        return {DispatcherStatusCode::DISPATCHER_OK, "+OK Name is a valid mailbox"};
    }

    case Pop3ServerCommand::PASS:
    {
        if (state != Pop3State::AUTHORIZATION_PASS)
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Bad command sequence"};

        // Check if password is correct
        tablet_value password_data;
        IStorageService storage(COORDINATOR_SERVICE->get_kv_servers_map());
        if (storage.get(user_to_row("ACCOUNTS"), user_name, password_data))
        {
            state = Pop3State::AUTHORIZATION;
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Failed to check password"};
        }

        if (IStorageService::to_string(password_data) != cmd.args)
        {
            state = Pop3State::AUTHORIZATION;
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Nice try, but that's wrong!"};
        }

        // populate email_infos
        if (get_email_infos())
        {
            state = Pop3State::AUTHORIZATION;
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Failed to read mailbox"};
        }

        state = Pop3State::TRANSACTION;
        return {DispatcherStatusCode::DISPATCHER_OK, "+OK Unlocked mailbox"};
    }

    case Pop3ServerCommand::ERR:
        return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Unknown command"};
    }
}

// Parse the given string command
Command<Pop3ServerCommand> Pop3CommandDispatcher::parse_message(const std::string &message)
{
    Command<Pop3ServerCommand> cmd;

    std::smatch m;
    std::string strCmd;

    // simple argument command regex
    std::regex_match(message, m, std::regex{"^(USER|PASS|RETR|LIST|DELE|UIDL)[ \\t]+(.+?)[ \\t]*$", std::regex::icase});
    to_upper(m[1].first, m[1].second, strCmd);
    if (!m.empty())
    {
        cmd.args = m[2];

        if (strCmd == "USER")
            cmd.cmd = Pop3ServerCommand::USER;
        else if (strCmd == "PASS")
            cmd.cmd = Pop3ServerCommand::PASS;
        else if (strCmd == "RETR")
            cmd.cmd = Pop3ServerCommand::RETR;
        else if (strCmd == "LIST")
            cmd.cmd = Pop3ServerCommand::LIST;
        else if (strCmd == "DELE")
            cmd.cmd = Pop3ServerCommand::DELE;
        else if (strCmd == "UIDL")
            cmd.cmd = Pop3ServerCommand::UIDL;

        return cmd;
    }

    // no argument commands regex
    std::regex_match(message, m, std::regex{"^(QUIT|NOOP|RSET|STAT|UIDL|LIST)[ \\t]*", std::regex::icase});
    to_upper(m[1].first, m[1].second, strCmd);
    if (!m.empty())
    {
        cmd.args = "";

        if (strCmd == "QUIT")
            cmd.cmd = Pop3ServerCommand::QUIT;
        else if (strCmd == "NOOP")
            cmd.cmd = Pop3ServerCommand::NOOP;
        else if (strCmd == "RSET")
            cmd.cmd = Pop3ServerCommand::RSET;
        else if (strCmd == "STAT")
            cmd.cmd = Pop3ServerCommand::STAT;
        else if (strCmd == "UIDL")
            cmd.cmd = Pop3ServerCommand::UIDL_ALL;
        else if (strCmd == "LIST")
            cmd.cmd = Pop3ServerCommand::LIST_ALL;

        return cmd;
    }

    return {Pop3ServerCommand::ERR, ""};
}

int Pop3CommandDispatcher::get_email_infos()
{

    std::set<std::string> email_ids;
    tablet_value email_data;
    IStorageService storage(COORDINATOR_SERVICE->get_kv_servers_map());
    if (storage.list_columns(user_to_row(user_name) + "-MAILBOX", email_ids)) return 1;

    for (auto &email_id : email_ids)
    {
        if (storage.get(user_to_row(user_name) + "-MAILBOX", email_id, email_data))
            return 1;

        email_infos.push_back({email_data.size(), false, email_id});
    }

    return 0;
}