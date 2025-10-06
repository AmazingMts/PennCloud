#include <mutex>
#include <vector>
#include <filesystem>
#include <regex>
#include "KVStorageCommandDispatcher.h"
#include "Tablet.h"
#include "TabletArray.h"
#include <unistd.h> // For close()
#include "SharedStructures.h"
#include "RemoteWriteRequestAssembler.h"
#include "Globals.h"
#include "IStorageService.h"
#include "SocketWriter.h"

KvStorageCommandDispatcher::KvStorageCommandDispatcher() {}

DispatcherResponse KvStorageCommandDispatcher::dispatch(const string &message)
{
    // Internal command to shut down the client
    if (message == "TQUIT")
        return {DispatcherStatusCode::QUIT, ""};

    // Check whether binary data is being received
    if (receiving_data)
    {
        // Continue reading data
        data_receiver.read_data(message);

        // Execute command if all data has been received
        if (data_receiver.finished())
        {
            NewCommand<KVServerCommand> cmd = data_receiver.get_command();
            return execute_command(cmd);
        }
        else
            return {DispatcherStatusCode::READING_DATA, ""};
    }
    else
    {
        // Parse message
        NewCommand<KVServerCommand> cmd = parse_message(message);

        // Execute command and return response
        return execute_command(cmd);
    }
}

DispatcherResponse KvStorageCommandDispatcher::execute_command(NewCommand<KVServerCommand> &cmd)
{
    switch (cmd.cmd)
    {
    case KVServerCommand::SYNCV:
        if (cmd.origin == CommandOrigin::REPLICA)
        {
            return {DispatcherStatusCode::TO_PT, tablets.send_remote_versions(cmd.args[0])};
        }
        else if (cmd.origin == CommandOrigin::PRIMARY)
        {
            SocketWriter writer(RECOVERY_PIPE_W);
            writer.write_message(cmd.args[1]);
            return {DispatcherStatusCode::DISPATCHER_OK, ""};
        }
        else
        {
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Internal error"};
        }

    case KVServerCommand::SYNCF:
        if (cmd.origin == CommandOrigin::REPLICA)
        {
            size_t tablet_id = std::stoul(cmd.args[1]);
            size_t version = std::stoul(cmd.args[2]);

            return {DispatcherStatusCode::TO_PT, tablets.send_remote_files(cmd.args[0], tablet_id, version)};
        }
        else if (cmd.origin == CommandOrigin::PRIMARY)
        {
            printf("KVStorageCommandDispatcher: Received SYNCF command from primary node\n");
            if (receiving_data)
            {
                receiving_data = false;

                tablet_value bin_value;
                tablet_value idx_value;
                tablet_value log_value;
                data_receiver.retrieve_data(bin_value);
                fprintf(stderr, "Received %zu bytes\n", bin_value.size());
                data_receiver.retrieve_data(idx_value);
                fprintf(stderr, "Received %zu bytes\n", idx_value.size());
                data_receiver.retrieve_data(log_value);
                fprintf(stderr, "Received %zu bytes\n", log_value.size());

                data_receiver.reset();

                if (DEBUG)
                    fprintf(stderr, "Sending recovery files through pipe\n");

                SocketWriter writer(RECOVERY_PIPE_W);
                writer.write_message(cmd.args[1] + " " + cmd.args[2] + " " + cmd.args[3] + " " + cmd.args[4] + " " + cmd.args[5]);
                writer.write_message(bin_value);
                writer.write_message(idx_value);
                writer.write_message(log_value);

                return {DispatcherStatusCode::DISPATCHER_OK, ""};
            }
            else
            {
                receiving_data = true;

                data_receiver.set_command(cmd);
                data_receiver.request_data(std::stoul(cmd.args[2]));
                data_receiver.request_data(std::stoul(cmd.args[3]));
                data_receiver.request_data(std::stoul(cmd.args[5]));

                return {DispatcherStatusCode::DISPATCHER_OK, ""};
            }

            return {DispatcherStatusCode::DISPATCHER_OK, ""};
        }
        else
        {
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Internal error"};
        }

    case KVServerCommand::BRING_UP:
        // We quit because this command will only be called by Admin console through the internal port.
        // After this command, the primary thread will close this connection (no need to keep alive)
        if (IS_ALIVE.load() && SS_FD.load() != -1)
            return {DispatcherStatusCode::QUIT, "-ERR KVStorage Node already running (quitting)"};

        // Signal Server.cc that it should bring up the server again
        IS_ALIVE.store(true);

        // NOTE: We cant wait intil SS_FD is set to -1 because this will block the server from
        // receiving data from the primary when its booting up!
        // Wait until SS_FD is set again by the Server.cc class (indicating that the server is up)
        // while (SS_FD.load() == -1)
        // {
        //     fprintf(stderr, "BRING_UP: Waiting for KVStorage Node to come back up...\n");
        //     sleep(1);
        // }

        return {DispatcherStatusCode::QUIT, "+OK KVStorage Node back up (quitting)"};

    case KVServerCommand::SHUT_DOWN:
        if (!IS_ALIVE.load() && SS_FD.load() == -1)
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR KVStorage Node already shut down"};

        // NOTE: To bring back up, we'll need to connect to the internal port number and let primary KVStorage thread set this to true again ...
        IS_ALIVE.store(false);
        close(SS_FD.load());
        SS_FD.store(-1);
        return {DispatcherStatusCode::DISPATCHER_OK, "+OK Shutting down KVStorage Node"};

    case KVServerCommand::CROW:
        if (cmd.origin == CommandOrigin::CLIENT)
        {
            // We need to forward to internal port of the primary node to start the remote write process
            string message_id = RemoteWriteRequestAssembler::create_message_id();
            return remote_write(message_id, RemoteWriteRequestAssembler::assemble_create_row(message_id, cmd.args[0]));
        }
        else if (cmd.origin == CommandOrigin::REPLICA)
        {
            // Only primary thread of primary node can receive this command. We need to send requests to all replicas and wait for their response
            std::string row_key = cmd.args[0];
            std::string host_port = cmd.args[1];
            std::string message_id = cmd.args[2];

            IStorageService storage_service(COORDINATOR_SERVICE->get_kv_servers_map());
            BroadcastResult result = storage_service.broadcast_create_row(RG_ID, message_id, row_key);

            if (DEBUG) // truncated_print("KVStorageCommandDispatcher: Finished using Forwarder to write request", result.formatted_response, message_id);
            {
                fprintf(stderr, "RW_RESULT (%s):\n %s\n", message_id.c_str(), result.formatted_response.c_str());
            }

            return {DispatcherStatusCode::TO_PT, RemoteWriteRequestAssembler::assemble_remote_write_result(host_port, result)};
        }

        switch (tablets.create_row(cmd.args[0]))
        {
        case TabletStatus::OK:
            return {DispatcherStatusCode::DISPATCHER_OK, "+OK"};
        case TabletStatus::ROW_KEY_ERR:
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Row already exists"};
        }

    case KVServerCommand::PUT:
        if (receiving_data)
        {
            receiving_data = false;
            tablet_value value;
            data_receiver.retrieve_data(value);
            data_receiver.reset();

            if (cmd.origin == CommandOrigin::CLIENT)
            {
                // We need to forward to internal port of the primary node to start the remote write process
                string message_id = RemoteWriteRequestAssembler::create_message_id();
                return remote_write(message_id, RemoteWriteRequestAssembler::assemble_put(message_id, cmd.args[0], cmd.args[1], value));
            }
            else if (cmd.origin == CommandOrigin::REPLICA)
            {

                // Only primary thread of primary node can receive this command. We need to send requests to all replicas and wait for their response
                std::string row_key = cmd.args[0];
                std::string column_key = cmd.args[1];
                std::string value_size = cmd.args[2];
                std::string host_port = cmd.args[3];
                std::string message_id = cmd.args[4];

                IStorageService storage_service(COORDINATOR_SERVICE->get_kv_servers_map());
                BroadcastResult result = storage_service.broadcast_put(RG_ID, message_id, row_key, column_key, value);

                if (DEBUG)
                {
                    fprintf(stderr, "RW_RESULT (%s):\n %s\n", message_id.c_str(), result.formatted_response.c_str());
                }

                return {DispatcherStatusCode::TO_PT, RemoteWriteRequestAssembler::assemble_remote_write_result(host_port, result)};
            }

            // If it reaches here then its a command that comes from the primary node. So we just need to execute the command on our own storage and return the response
            switch (tablets.write(cmd.args[0], cmd.args[1], value))
            {
            case TabletStatus::OK:
                return {DispatcherStatusCode::DISPATCHER_OK, "+OK"};
            case TabletStatus::ROW_KEY_ERR:
                return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Row not found"};
            case TabletStatus::SPLITTING_ERR:
                return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Tablet splitting in progress"};
            case TabletStatus::CACHING_ERR:
                return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Tablet caching in progress"};
            default:
                return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Internal error"};
            }
        }
        else
        {
            size_t value_size;
            try
            {
                value_size = std::stoul(cmd.args[2]);
            }
            catch (...)
            {
                return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Invalid size argument"};
            }

            receiving_data = true;
            data_receiver.set_command(cmd);
            data_receiver.request_data(value_size);

            return {DispatcherStatusCode::DISPATCHER_OK, ""};
        }

    case KVServerCommand::CPUT:
        if (receiving_data)
        {
            receiving_data = false;

            tablet_value conditional_value;
            data_receiver.retrieve_data(conditional_value);
            tablet_value value;
            data_receiver.retrieve_data(value);

            if (cmd.origin == CommandOrigin::CLIENT)
            {
                string message_id = RemoteWriteRequestAssembler::create_message_id();
                return remote_write(message_id, RemoteWriteRequestAssembler::assemble_cput(message_id, cmd.args[0], cmd.args[1], conditional_value, value));
            }
            else if (cmd.origin == CommandOrigin::REPLICA)
            {
                std::string row_key = cmd.args[0];
                std::string column_key = cmd.args[1];
                std::string value_size = cmd.args[2];
                std::string cvalue_size = cmd.args[3];
                std::string host_port = cmd.args[4];
                std::string message_id = cmd.args[5];

                IStorageService storage_service(COORDINATOR_SERVICE->get_kv_servers_map());
                BroadcastResult result = storage_service.broadcast_cput(RG_ID, message_id, row_key, column_key, conditional_value, value);

                if (DEBUG)
                {
                    fprintf(stderr, "RW_RESULT (%s):\n %s\n", message_id.c_str(), result.formatted_response.c_str());
                }

                return {DispatcherStatusCode::TO_PT, RemoteWriteRequestAssembler::assemble_remote_write_result(host_port, result)};
            }

            bool is_conditional_value;
            switch (tablets.conditional_write(cmd.args[0], cmd.args[1], conditional_value,
                                              value, is_conditional_value))
            {
            case TabletStatus::OK:
                if (is_conditional_value)
                    return {DispatcherStatusCode::DISPATCHER_OK, "+OK 1"};
                else
                    return {DispatcherStatusCode::DISPATCHER_OK, "+OK 0"};
            case TabletStatus::ROW_KEY_ERR:
                return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Row not found"};
            case TabletStatus::COLUMN_KEY_ERR:
                return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Column not found"};
            case TabletStatus::SPLITTING_ERR:
                return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Tablet splitting in progress"};
            case TabletStatus::CACHING_ERR:
                return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Tablet caching in progress"};
            default:
                return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Internal error"};
            }
        }
        else
        {
            size_t conditional_size;
            size_t value_size;
            try
            {
                conditional_size = std::stoul(cmd.args[2]);
                value_size = std::stoul(cmd.args[3]);
            }
            catch (...)
            {
                return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Invalid size argument"};
            }

            receiving_data = true;
            data_receiver.set_command(cmd);
            data_receiver.request_data(conditional_size);
            data_receiver.request_data(value_size);

            return {DispatcherStatusCode::DISPATCHER_OK, ""};
        }

    case KVServerCommand::MOVE:
        if (cmd.origin == CommandOrigin::CLIENT)
        {
            string message_id = RemoteWriteRequestAssembler::create_message_id();
            return remote_write(message_id, RemoteWriteRequestAssembler::assemble_move(message_id, cmd.args[0], cmd.args[1], cmd.args[2]));
        }
        else if (cmd.origin == CommandOrigin::REPLICA)
        {
            std::string row_key = cmd.args[0];
            std::string column_key = cmd.args[1];
            std::string new_column_key = cmd.args[2];
            std::string host_port = cmd.args[3];
            std::string message_id = cmd.args[4];

            IStorageService storage_service(COORDINATOR_SERVICE->get_kv_servers_map());
            BroadcastResult result = storage_service.broadcast_move(RG_ID, message_id, row_key, column_key, new_column_key);

            if (DEBUG)
            {
                fprintf(stderr, "RW_RESULT (%s):\n %s\n", message_id.c_str(), result.formatted_response.c_str());
            }

            return {DispatcherStatusCode::TO_PT, RemoteWriteRequestAssembler::assemble_remote_write_result(host_port, result)};
        }

        switch (tablets.move(cmd.args[0], cmd.args[1], cmd.args[2]))
        {
        case TabletStatus::OK:
            return {DispatcherStatusCode::DISPATCHER_OK, "+OK"};
        case TabletStatus::ROW_KEY_ERR:
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Row not found"};
        case TabletStatus::COLUMN_KEY_ERR:
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Column not found"};
        }

    case KVServerCommand::DEL:
        if (cmd.origin == CommandOrigin::CLIENT)
        {
            string message_id = RemoteWriteRequestAssembler::create_message_id();
            return remote_write(message_id, RemoteWriteRequestAssembler::assemble_del(message_id, cmd.args[0], cmd.args[1]));
        }
        else if (cmd.origin == CommandOrigin::REPLICA)
        {
            std::string row_key = cmd.args[0];
            std::string column_key = cmd.args[1];
            std::string host_port = cmd.args[2];
            std::string message_id = cmd.args[3];

            IStorageService storage_service(COORDINATOR_SERVICE->get_kv_servers_map());
            BroadcastResult result = storage_service.broadcast_remove(RG_ID, message_id, row_key, column_key);

            if (DEBUG)
            {
                fprintf(stderr, "RW_RESULT (%s):\n %s\n", message_id.c_str(), result.formatted_response.c_str());
            }

            return {DispatcherStatusCode::TO_PT, RemoteWriteRequestAssembler::assemble_remote_write_result(host_port, result)};
        }

        switch (tablets.remove(cmd.args[0], cmd.args[1]))
        {
        case TabletStatus::OK:
            return {DispatcherStatusCode::DISPATCHER_OK, "+OK"};
        case TabletStatus::ROW_KEY_ERR:
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Row not found"};
        case TabletStatus::COLUMN_KEY_ERR:
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Column not found"};
        }

    case KVServerCommand::GET:
    {
        tablet_value value;
        switch (tablets.read(cmd.args[0], cmd.args[1], value))
        {
        case TabletStatus::OK:
            return {DispatcherStatusCode::DISPATCHER_OK,
                    "+OK " + std::to_string(value.size()) + "\r\n" + Tablet::to_string(value)};
        case TabletStatus::ROW_KEY_ERR:
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Row not found"};
        case TabletStatus::COLUMN_KEY_ERR:
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Column not found"};
        }
    }

    case KVServerCommand::LISTC:
    {
        std::string msg{"+OK\r\n"};
        std::set<std::string> column_keys;
        switch (tablets.list_columns(cmd.args[0], column_keys))
        {
        case TabletStatus::OK:
            for (const auto &column_key : column_keys)
                msg += column_key + "\r\n";

            msg += ".";
            return {DispatcherStatusCode::DISPATCHER_OK, msg};
        case TabletStatus::ROW_KEY_ERR:
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Row not found"};
        }
    }

    case KVServerCommand::LISTR:
    {
        std::string msg{"+OK\r\n"};
        std::set<std::string> row_keys;
        tablets.list_rows(row_keys);
        for (const auto &row_key : row_keys)
            msg += row_key + "\r\n";
        msg += ".";
        return {DispatcherStatusCode::DISPATCHER_OK, msg};
    }

    case KVServerCommand::LISTT:
    {
        std::string msg{"+OK\r\n"};
        std::set<std::string> tablet_infos;
        tablets.list_tablets(tablet_infos);
        for (const auto &tablet_info : tablet_infos)
            msg += tablet_info + "\r\n";
        msg += ".";
        return {DispatcherStatusCode::DISPATCHER_OK, msg};
    }

    case KVServerCommand::RW_RESULT:
    {
        std::string host_port = cmd.args[0];
        std::string message_id = cmd.args[1];
        bool success = static_cast<bool>(std::stoi(cmd.args[2]));

        // Get the write fd from the pipe map
        int write_fd = -1;
        {
            std::unique_lock<std::shared_mutex> lock(pipe_map_mutex);
            write_fd = PIPE_MAP[message_id];
        }

        // Write the response to the pipe
        std::string message = (success ? "+OK RW_RESULT" : "-ERR RW_RESULT");
        SocketWriter writer(write_fd);
        writer.write_message(message);
        // ssize_t bytes_written = write(write_fd, message.c_str(), message.size());

        // Close the write fd
        close(write_fd);

        return {DispatcherStatusCode::DISPATCHER_OK, ""};
    }

    case KVServerCommand::QUIT:
        return {DispatcherStatusCode::QUIT, "+OK Disconnecting"};

    case KVServerCommand::ERR:
        if (cmd.args.size() > 0)
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR " + cmd.args[0]};
        else
            return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Unknown command"};
    }
}

NewCommand<KVServerCommand> KvStorageCommandDispatcher::parse_message(std::string_view message)
{
    size_t cmd_end = message.find_first_of(" ");
    std::string cmd_str(message.substr(0, cmd_end));
    CommandOrigin origin = get_command_origin(message);
    string message_id = "";

    if (origin == CommandOrigin::PRIMARY || origin == CommandOrigin::REPLICA || origin == CommandOrigin::COORDINATOR)
    {
        // Remove the special character from the command
        cmd_str = cmd_str.substr(1);
    }

    cmd_str = to_upper(cmd_str);

    std::vector<std::string>
        args;
    if (cmd_end != std::string::npos)
        parse_arguments(message.substr(cmd_end), args);

    if (cmd_str == "CROW")
    {
        if (!(args.size() == 1 || args.size() == 3))
            return {KVServerCommand::ERR, {"Invalid number of arguments"}};
        return {KVServerCommand::CROW, std::move(args), origin};
    }
    if (cmd_str == "PUT")
    {
        if (!(args.size() == 3 || args.size() == 5))
            return {KVServerCommand::ERR, {"Invalid number of arguments"}};
        return {KVServerCommand::PUT, std::move(args), origin};
    }
    if (cmd_str == "CPUT")
    {
        if (!(args.size() == 4 || args.size() == 6))
            return {KVServerCommand::ERR, {"Invalid number of arguments"}};
        return {KVServerCommand::CPUT, std::move(args), origin};
    }
    if (cmd_str == "MOVE")
    {
        if (!(args.size() == 3 || args.size() == 5))
            return {KVServerCommand::ERR, {"Invalid number of arguments"}};
        return {KVServerCommand::MOVE, std::move(args), origin};
    }
    else if (cmd_str == "DEL")
    {
        if (!(args.size() == 2 || args.size() == 4))
            return {KVServerCommand::ERR, {"Invalid number of arguments"}};
        return {KVServerCommand::DEL, std::move(args), origin};
    }
    else if (cmd_str == "GET")
    {
        if (args.size() != 2)
            return {KVServerCommand::ERR, {"Invalid number of arguments, 2 expected"}};
        return {KVServerCommand::GET, std::move(args), origin};
    }
    else if (cmd_str == "LISTC")
    {
        if (args.size() != 1)
            return {KVServerCommand::ERR, {"Invalid number of arguments, 1 expected"}};
        return {KVServerCommand::LISTC, std::move(args), origin};
    }
    else if (cmd_str == "LISTR")
    {
        if (args.size() != 0)
            return {KVServerCommand::ERR, {"Invalid number of arguments, none expected"}};
        return {KVServerCommand::LISTR, std::move(args), origin};
    }
    else if (cmd_str == "LISTT")
    {
        if (args.size() != 0)
            return {KVServerCommand::ERR, {"Invalid number of arguments, none expected"}};
        return {KVServerCommand::LISTT, std::move(args), origin};
    }
    else if (cmd_str == "QUIT")
    {
        if (args.size() != 0)
            return {KVServerCommand::ERR, {"Invalid number of arguments, none expected"}};
        return {KVServerCommand::QUIT, std::move(args), origin};
    }
    else if (cmd_str == "SHUT_DOWN")
    {
        if (args.size() != 0)
            return {KVServerCommand::ERR, {"Invalid number of arguments, none expected"}};
        return {KVServerCommand::SHUT_DOWN, std::move(args), origin};
    }
    else if (cmd_str == "BRING_UP")
    {
        if (args.size() != 0)
            return {KVServerCommand::ERR, {"Invalid number of arguments, none expected"}};
        return {KVServerCommand::BRING_UP, std::move(args), origin};
    }
    else if (cmd_str == "RW_RESULT")
    {
        if (args.size() != 3)
            return {KVServerCommand::ERR, {"Invalid number of arguments, three expected"}};
        return {KVServerCommand::RW_RESULT, std::move(args), origin};
    }
    else if (cmd_str == "SYNCV")
    {
        // if (args.size() != 3)
        //     return {KVServerCommand::ERR, {"Invalid number of arguments, three expected"}};
        return {KVServerCommand::SYNCV, std::move(args), origin};
    }
    else if (cmd_str == "SYNCF")
    {
        // if (args.size() != 6)
        //     return {KVServerCommand::ERR, {"Invalid number of arguments, six expected"}};
        return {KVServerCommand::SYNCF, std::move(args), origin};
    }

    return {KVServerCommand::ERR, {"Unknown command"}, origin};
}

CommandOrigin KvStorageCommandDispatcher::get_command_origin(std::string_view message)
{
    if (message[0] == '#')
        return CommandOrigin::PRIMARY;
    else if (message[0] == '?')
        return CommandOrigin::REPLICA;
    else if (message[0] == '$')
        return CommandOrigin::COORDINATOR;
    else
        return CommandOrigin::CLIENT;
}

void KvStorageCommandDispatcher::parse_arguments(std::string_view message, std::vector<std::string> &args)
{
    size_t start{0}, end{0};
    while (true)
    {
        start = message.find_first_not_of(" ", end);
        if (start == std::string::npos)
            return;

        end = message.find_first_of(" ", start);
        if (end == std::string::npos)
        {
            args.push_back(std::string{message.substr(start, end - start)});
            return;
        }
        else
            args.push_back(std::string{message.substr(start, end - start)});
    }
}

NewCommand<KVServerCommand> DataReceiver::get_command() const
{
    if (cmd.cmd == KVServerCommand::ERR)
        return {KVServerCommand::ERR, {"Internal DataReceiver error"}};
    else
        return cmd;
}

void DataReceiver::request_data(size_t size)
{
    data_requests.insert(data_requests.begin(), size);
    _finished = false;
}

void DataReceiver::retrieve_data(std::vector<std::byte> &buffer)
{
    buffer.clear();
    if (data.size() == 0)
        return;

    buffer = std::move(data.back());
    data.pop_back();
    data_requests.pop_back();

    if (data_requests.size() == 0)
        cmd = {KVServerCommand::ERR, {"Internal DataReceiver error"}};
}

void DataReceiver::reset()
{
    data.clear();
    data_requests.clear();
    _finished = false;
    _finished_data = false;
}

void DataReceiver::read_data(const string &message)
{
    size_t idx;
    if (data.size() == 0)
    {
        data.push_front(tablet_value{});
        idx = data_requests.size() - 1;
    }
    else
    {
        idx = data_requests.size() - data.size();
        // if (data.front().size() == data_requests[idx] && !(data_requests[idx] > 0))
        if (_finished_data)
        {
            data.push_front(tablet_value{});
            _finished_data = false;
            --idx;
        }
    }

    data.front().insert(data.front().end(),
                        reinterpret_cast<const std::byte *>(message.data()),
                        reinterpret_cast<const std::byte *>(message.data() + message.size()));

    if (data.front().size() < data_requests[idx])
    {
        // Add CLRF that was removed during read process
        data.front().push_back(static_cast<std::byte>('\r'));
        data.front().push_back(static_cast<std::byte>('\n'));
    }
    else
    {
        if (data.front().size() == data_requests[idx])
        {
            _finished_data = true;
        }

        if (data.size() == data_requests.size())
        {
            _finished = true;
        }
    }
}

std::string random_reader(int fd)
{
    std::string message;
    char buffer[1024];
    ssize_t bytes_received{1};

    while (bytes_received > 0)
    {
        bytes_received = ::read(fd, buffer, sizeof(buffer));

        if (bytes_received < 0)
        {
            fprintf(stderr, "Error reading from pipe: %s\n", strerror(errno));
            return "";
        }
        message.append(buffer, bytes_received);
    }

    return message.substr(0, message.size() - 2); // Remove the last CRLF
}

DispatcherResponse KvStorageCommandDispatcher::remote_write(const std::string message_id, const std::string &remote_write_request)
{
    // Open pipe
    int pipe_fds[2];
    if (pipe(pipe_fds) == -1)
    {
        return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Remote write pipe creation error"};
    }

    int read_fd = pipe_fds[0];
    int write_fd = pipe_fds[1];

    // Save the write end of the pipe in a global map so the primary thread can write the response back
    {
        std::unique_lock<std::shared_mutex> lock(pipe_map_mutex);
        PIPE_MAP[message_id] = write_fd;
    }

    if (DEBUG) truncated_print("KVStorageCommandDispatcher: Using Forwarder to write request", remote_write_request);

    // Sends the request to the primary thread of the primary node (who will coordinate the write operation)
    UPDATE_FORWARDER->forward_update(remote_write_request);

    if (DEBUG) truncated_print("KVStorageCommandDispatcher: Finished using Forwarder to write request", remote_write_request);

    // Block until a response is available from the primary thread
    // char buffer[4096];
    // ssize_t bytes_read = read(read_fd, buffer, sizeof(buffer));
    // if (bytes_read <= 0)
    // {
    //     close(read_fd);
    //     return {DispatcherStatusCode::DISPATCHER_OK, "-ERR Remote write pipe reading error"};
    // }

    // // Convert buffer to string
    // std::string response_str(buffer, bytes_read);

    std::string response_str = random_reader(read_fd);

    // Clean up
    close(read_fd);
    {
        std::unique_lock<std::shared_mutex> lock(pipe_map_mutex);
        PIPE_MAP.erase(message_id);
    }

    if (DEBUG) truncated_print("KVStorageCommandDispatcher: Finished using Forwarder to write request", remote_write_request);

    return {DispatcherStatusCode::DISPATCHER_OK, response_str};
}
