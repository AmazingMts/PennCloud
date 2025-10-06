#pragma once

#include "ICommandDispatcher.h"
#include "IStorageService.h"
#include <regex>
#include "SharedStructures.h"

enum class CoordinatorServerCommand
{
    KVLIST,
    QUIT,
    ERR
};

class CoordinatorCommandDispatcher : public ICommandDispatcher
{
public:
    CoordinatorCommandDispatcher();
    DispatcherResponse dispatch(const string &message) override;

private:
    std::map<int, std::vector<KVServer>> kv_servers_map_;
    map<CoordinatorServerCommand, regex> command_args_regex_map_ = {
        {CoordinatorServerCommand::KVLIST, std::regex{R"(^\s*$)", std::regex::icase}},
        {CoordinatorServerCommand::QUIT, std::regex{R"(^\s*$)", std::regex::icase}}};

    // Member functions
    Command<CoordinatorServerCommand> parse_message_(const std::string &message);
    bool validate_command_(const Command<CoordinatorServerCommand> &command);
    DispatcherResponse execute_command_(Command<CoordinatorServerCommand> cmd);
};