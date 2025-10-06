#include <random>
#include <sstream>
#include <fstream> // For std::ifstream
#include "CoordinatorCommandDispatcher.h"
#include <regex>
#include "Server.h"
#include <chrono>
#include <iomanip> // For std::put_time
#include <ctime>   // For std::localtime
#include <sstream>
#include "Globals.h"

using namespace std;

CoordinatorCommandDispatcher::CoordinatorCommandDispatcher() {
};

// Override the Dispatch method
DispatcherResponse CoordinatorCommandDispatcher::dispatch(const string &message)
{
    if (message == "TQUIT")
        return {DispatcherStatusCode::QUIT, ""};

    Command<CoordinatorServerCommand> cmd = parse_message_(message);

    if (cmd.cmd == CoordinatorServerCommand::ERR)
        return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Unknown command"};

    if (!validate_command_(cmd))
        return {DispatcherStatusCode::DISPATCHER_OK, "501 - Syntax error in parameters or arguments"};

    return execute_command_(cmd);
}

Command<CoordinatorServerCommand> CoordinatorCommandDispatcher::parse_message_(const std::string &message)
{
    Command<CoordinatorServerCommand> cmd;

    // Remove leading and trailing spaces from message
    std::regex spaceRegex("^\\s+|\\s+$");
    string trimmed_message = regex_replace(message, spaceRegex, "");

    // Command regex
    std::regex commandRegex(R"(^\s*(KVLIST|QUIT)\s*(.*)\s*$)", std::regex::icase); // Regex to split command from message
    std::smatch match;
    if (std::regex_search(trimmed_message, match, commandRegex))
    {
        // Process Command
        string command = match.str(1);
        transform(command.begin(), command.end(), command.begin(), ::toupper); // Command to upper case
        command = regex_replace(command, spaceRegex, "");                      // Remove leading and trailing spaces
        if (command == "KVLIST")
            cmd.cmd = CoordinatorServerCommand::KVLIST;
        else if (command == "QUIT")
            cmd.cmd = CoordinatorServerCommand::QUIT;

        // Process Args
        string msg = trimmed_message.substr(command.length()); // Remaining part of the message
        msg = regex_replace(msg, spaceRegex, "");              // Remove leading and trailing spaces
        cmd.args = msg;

        return cmd;
    }

    return {CoordinatorServerCommand::ERR, ""};
}

bool CoordinatorCommandDispatcher::validate_command_(const Command<CoordinatorServerCommand> &command)
{
    // Commands that need an argument (none at the moment)
    if (false)
        return false;

    // Commands that dont need an argument
    if ((command.cmd == CoordinatorServerCommand::KVLIST || (command.cmd == CoordinatorServerCommand::QUIT)) && (!command.args.empty()))
        return false;

    // Match command to appropriate regex
    std::regex rgx = command_args_regex_map_[command.cmd];
    std::smatch match;
    if (!std::regex_search(command.args, match, rgx))
    {
        return false;
    }

    return true;
}

DispatcherResponse CoordinatorCommandDispatcher::execute_command_(Command<CoordinatorServerCommand> cmd)
{
    switch (cmd.cmd)
    {
    case CoordinatorServerCommand::KVLIST:
    {
        string response;

        // Iterate over the replication groups and append to string using new lines
        std::shared_lock<std::shared_mutex> lock(coord_kv_servers_map_mtx);
        kv_servers_map_ = COORD_KV_SERVERS_MAP;
        for (const auto &pair : kv_servers_map_)
        {
            string rp_id = to_string(pair.first);
            vector<KVServer> servers = pair.second;
            response += rp_id + ":";
            for (const auto &kv_server : servers)
            {
                response += "(" + kv_server.host + ":" + to_string(kv_server.port) + "," + (kv_server.is_primary ? "true" : "false") + "," + (kv_server.is_alive ? "true" : "false") + ")";
            }

            // Add a new line for the next replication group
            response += "\n";
        }

        // No operation, just return a successful response
        return {DispatcherStatusCode::DISPATCHER_OK, response};
    }
    case CoordinatorServerCommand::QUIT:
    {
        return {DispatcherStatusCode::QUIT, "+OK Service closing transmission channel"};
    }
    default:
        return {DispatcherStatusCode::DISPATCHER_OK, "500 Syntax error, command unrecognized"};
        break;
    }
};