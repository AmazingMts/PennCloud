#pragma once

#include <list>
#include <algorithm>
#include "ICommandDispatcher.h"
#include "Tablet.h"
#include "KVServerCommand.h"
#include "Globals.h"

enum class CommandOrigin
{
    CLIENT,
    PRIMARY,
    REPLICA,
    COORDINATOR,
};

template <typename T>
struct NewCommand
{
    T cmd;
    std::vector<std::string> args;
    CommandOrigin origin;
};

class DataReceiver
{
private:
    std::vector<size_t> data_requests{};
    std::list<std::vector<std::byte>> data{};
    bool _finished{false};
    bool _finished_data{false};
    NewCommand<KVServerCommand> cmd{KVServerCommand::ERR, {"Internal DataReceiver error"}};

public:
    // Returns true if all data has been received
    inline bool finished() const { return _finished; }
    // Cache current command
    inline void set_command(NewCommand<KVServerCommand> cmd) { this->cmd = cmd; }
    // Returns cached command
    NewCommand<KVServerCommand> get_command() const;
    // Request data by number of bytes, pushed onto queue
    inline void request_data(size_t size);
    // Returns data from queue
    void retrieve_data(std::vector<std::byte> &buffer);
    // Resets data receiver
    void reset();
    // Reads and distributes data to arguments
    void read_data(const string &message);
};

class KvStorageCommandDispatcher : public ICommandDispatcher
{
public:
    KvStorageCommandDispatcher();
    DispatcherResponse dispatch(const string &message) override;

private:
    bool receiving_data{false};
    size_t server_id{0};
    DataReceiver data_receiver{};

    // Parse message and return command with arguments
    NewCommand<KVServerCommand> parse_message(std::string_view message);
    // Parse arguments from message
    void parse_arguments(std::string_view message, std::vector<std::string> &args);
    // Execute command and return response
    DispatcherResponse execute_command(NewCommand<KVServerCommand> &cmd);
    // Performs remote write operation
    DispatcherResponse remote_write(const std::string message_id, const std::string &remote_write_request);
    // Based on the message, gets the origin of the command
    CommandOrigin get_command_origin(std::string_view message);
};

// Inserts capitalized string in into out
inline std::string to_upper(std::string_view in)
{
    std::string out;
    std::transform(in.cbegin(), in.cend(), std::back_inserter(out), ::toupper);
    return out;
}