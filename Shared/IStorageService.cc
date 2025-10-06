#include <string>
#include <map>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <regex>
#include <iostream>
#include "IStorageService.h"
#include "RequestAssembler.h"
#include "ResponseParser.h"
#include <cstring>

int IStorageService::get(const std::string &row_key, const std::string &column_key, tablet_value &value)
{
    value.clear();
    std::string request = RequestAssembler::assemble_get(row_key, column_key);
    TryExecuteRequestParams params{
        .command = KVServerCommand::GET,
        .request = request,
        .row_key = row_key,
        .read_bytes_before_value = true};
    std::string response = group_try_execute_request_(params);
    if (response.empty())
        return 1;
    value = from_string(response);
    return 0;
}

int IStorageService::get(const std::string &row_key, const std::string &column_key, tablet_value &value, const KVServer &server)
{
    value.clear();
    std::string request = RequestAssembler::assemble_get(row_key, column_key);
    TryExecuteRequestParams params{
        .command = KVServerCommand::GET,
        .request = request,
        .row_key = row_key,
        .resolve_server_by_server = true,
        .server = server,
        .read_bytes_before_value = true,
    };
    std::string response = group_try_execute_request_(params);
    if (response.empty())
        return 1;
    value = from_string(ResponseParser::parse_get(response));
    return 0;
}

int IStorageService::put(const std::string &row_key, const std::string &column_key, const tablet_value &value)
{
    std::string request = RequestAssembler::assemble_put(row_key, column_key, value);
    TryExecuteRequestParams params{
        .command = KVServerCommand::PUT,
        .request = request,
        .row_key = row_key};
    std::string response = group_try_execute_request_(params);
    if (response.empty())
        return 1;
    return 0;
}

int IStorageService::cput(const std::string &row_key, const std::string &column_key,
                          const tablet_value &cvalue, const tablet_value &value, bool &result)
{
    std::string request = RequestAssembler::assemble_cput(row_key, column_key, cvalue, value);
    TryExecuteRequestParams params{
        .command = KVServerCommand::CPUT,
        .request = request,
        .row_key = row_key};
    std::string response = group_try_execute_request_(params);
    if (response.empty())
        return 1;
    result = ResponseParser::parse_cput(response);
    return 0;
}

int IStorageService::move(const std::string &row_key, const std::string &column_key, const std::string &new_column_key)
{
    std::string request = RequestAssembler::assemble_move(row_key, column_key, new_column_key);
    TryExecuteRequestParams params{
        .command = KVServerCommand::MOVE,
        .request = request,
        .row_key = row_key};
    std::string response = group_try_execute_request_(params);
    if (response.empty())
        return 1;
    return 0;
}

int IStorageService::remove(const std::string &row_key, const std::string &column_key)
{
    std::string request = RequestAssembler::assemble_del(row_key, column_key);
    TryExecuteRequestParams params{
        .command = KVServerCommand::DEL,
        .request = request,
        .row_key = row_key};
    std::string response = group_try_execute_request_(params);
    if (response.empty())
        return 1;
    return 0;
}

int IStorageService::list_rows(std::set<std::string> &rows)
{
    // NOTE: This is supposed to contact all replication groups!
    rows.clear();
    for (const auto &rp : kv_servers_map_)
    {
        std::string request = RequestAssembler::assemble_list_rows();
        TryExecuteRequestParams params{
            .command = KVServerCommand::LISTR,
            .request = request,
            .resolve_server_by_group_id = true,
            .group_id = rp.first,
            .read_until_termination = "\r\n.\r\n"};
        std::string response = group_try_execute_request_(params);
        if (response.empty())
            return 1;

        // Get union of rows and result
        std::set<std::string> new_rows = ResponseParser::parse_list_rows(response);
        rows.insert(new_rows.begin(), new_rows.end());
    }

    return 0;
}

int IStorageService::list_rows(const KVServer &server, std::set<std::string> &rows)
{
    // NOTE: This is supposed to contact all replication groups!
    rows.clear();
    std::string request = RequestAssembler::assemble_list_rows();
    TryExecuteRequestParams params{
        .command = KVServerCommand::LISTR,
        .request = request,
        .resolve_server_by_server = true,
        .server = server,
        .read_until_termination = "\r\n.\r\n"};
    std::string response = group_try_execute_request_(params);
    if (response.empty())
        return 1;

    // Get union of rows and result
    std::set<std::string> new_rows = ResponseParser::parse_list_rows(response);
    rows.insert(new_rows.begin(), new_rows.end());

    return 0;
}

int IStorageService::list_columns(const std::string &row_key, std::set<std::string> &columns)
{
    columns.clear();
    std::string request = RequestAssembler::assemble_list_columns(row_key);
    TryExecuteRequestParams params{
        .command = KVServerCommand::LISTC,
        .request = request,
        .row_key = row_key,
        .read_until_termination = "\r\n.\r\n"};
    std::string response = group_try_execute_request_(params);
    if (response.empty())
        return 1;

    columns = ResponseParser::parse_list_columns(response);

    return 0;
}

int IStorageService::list_columns(const KVServer &server, const std::string &row_key, std::set<std::string> &columns)
{
    columns.clear();
    std::string request = RequestAssembler::assemble_list_columns(row_key);
    TryExecuteRequestParams params{
        .command = KVServerCommand::LISTC,
        .request = request,
        .row_key = row_key,
        .resolve_server_by_server = true,
        .server = server,
        .read_until_termination = "\r\n.\r\n"};
    std::string response = group_try_execute_request_(params);
    if (response.empty())
        return 1;

    columns = ResponseParser::parse_list_columns(response);

    return 0;
}

int IStorageService::create_row(const std::string &row_key)
{
    std::string request = RequestAssembler::assemble_create_row(row_key);
    TryExecuteRequestParams params{
        .command = KVServerCommand::CROW,
        .request = request,
        .row_key = row_key};
    std::string response = group_try_execute_request_(params);
    if (response.empty())
        return 1;

    return 0;
}

int IStorageService::shut_down(const KVServer &server)
{
    std::string request = RequestAssembler::assemble_shut_down();
    TryExecuteRequestParams params{
        .command = KVServerCommand::SHUT_DOWN,
        .request = request,
        .resolve_server_by_server = true,
        .server = server};
    std::string response = group_try_execute_request_(params);
    if (response.empty())
        return 1;

    return 0;
}

int IStorageService::bring_up(const KVServer &server)
{
    std::string request = RequestAssembler::assemble_bring_up();
    TryExecuteRequestParams params{
        .command = KVServerCommand::BRING_UP,
        .request = request,
        .resolve_server_by_server = true,
        .server = KVServer{
            .host = server.host,
            .port = server.port + 1, // Change to internal port
            .is_primary = server.is_primary,
            .is_alive = server.is_alive},
        .with_welcome_message = false, // No welcome message needed for internal port
    };
    std::string response = group_try_execute_request_(params);
    if (response.empty())
        return 1;

    return 0;
}

BroadcastResult IStorageService::broadcast_put(const int rg_id, const std::string message_id, const std::string &row_key, const std::string &column_key, const tablet_value &value)
{
    std::vector<KVServer> live_servers = get_live_servers_(rg_id);

    // Send out the request to all servers
    std::map<std::string, int> status_codes;
    std::map<std::string, std::string> results_strings;
    for (const auto &server : live_servers)
    {
        std::string request = RequestAssembler::assemble_put(row_key, column_key, value);
        TryExecuteRequestParams params{
            .command = KVServerCommand::PUT,
            .request = "#" + request, // Prepend with # to indicate it's a message comming from the primary node
            .row_key = row_key,
            .resolve_server_by_server = true,
            .server = server};
        std::string response = group_try_execute_request_(params);
        if (response.empty())
        {
            status_codes[server.host + ":" + std::to_string(server.port)] = 1;
            results_strings[server.host + ":" + std::to_string(server.port)] = "Error";
        }
        else
        {
            status_codes[server.host + ":" + std::to_string(server.port)] = 0;
            results_strings[server.host + ":" + std::to_string(server.port)] = response;
        }
    }

    return build_broadcast_result_(message_id, status_codes, results_strings);
}

BroadcastResult IStorageService::broadcast_cput(const int rg_id, const std::string message_id, const std::string &row_key, const std::string &column_key, const tablet_value &cvalue,
                                                const tablet_value &value)
{
    std::vector<KVServer> live_servers = get_live_servers_(rg_id);

    // Send out the request to all servers
    std::map<std::string, int> status_codes;
    std::map<std::string, std::string> results_strings;
    for (const auto &server : live_servers)
    {
        std::string request = RequestAssembler::assemble_cput(row_key, column_key, cvalue, value);
        TryExecuteRequestParams params{
            .command = KVServerCommand::CPUT,
            .request = "#" + request, // Prepend with # to indicate it's a message comming from the primary node
            .row_key = row_key,
            .resolve_server_by_server = true,
            .server = server};
        std::string response = group_try_execute_request_(params);
        if (response.empty())
        {
            status_codes[server.host + ":" + std::to_string(server.port)] = 1;
            results_strings[server.host + ":" + std::to_string(server.port)] = "Error";
        }
        else
        {
            status_codes[server.host + ":" + std::to_string(server.port)] = 0;
            results_strings[server.host + ":" + std::to_string(server.port)] = response;
        }
    }

    return build_broadcast_result_(message_id, status_codes, results_strings);
}

BroadcastResult IStorageService::broadcast_move(const int rg_id, const std::string message_id, const std::string &row_key, const std::string &column_key, const std::string &new_column_key)
{
    std::vector<KVServer> live_servers = get_live_servers_(rg_id);

    // Send out the request to all servers
    std::map<std::string, int> status_codes;
    std::map<std::string, std::string> results_strings;
    for (const auto &server : live_servers)
    {
        std::string request = RequestAssembler::assemble_move(row_key, column_key, new_column_key);
        TryExecuteRequestParams params{
            .command = KVServerCommand::MOVE,
            .request = "#" + request, // Prepend with # to indicate it's a message comming from the primary node
            .row_key = row_key,
            .resolve_server_by_server = true,
            .server = server};
        std::string response = group_try_execute_request_(params);
        if (response.empty())
        {
            status_codes[server.host + ":" + std::to_string(server.port)] = 1;
            results_strings[server.host + ":" + std::to_string(server.port)] = "Error";
        }
        else
        {
            status_codes[server.host + ":" + std::to_string(server.port)] = 0;
            results_strings[server.host + ":" + std::to_string(server.port)] = response;
        }
    }

    return build_broadcast_result_(message_id, status_codes, results_strings);
}

BroadcastResult IStorageService::broadcast_remove(const int rg_id, const std::string message_id, const std::string &row_key, const std::string &column_key)
{
    std::vector<KVServer> live_servers = get_live_servers_(rg_id);

    // Send out the request to all servers
    std::map<std::string, int> status_codes;
    std::map<std::string, std::string> results_strings;
    for (const auto &server : live_servers)
    {
        std::string request = RequestAssembler::assemble_del(row_key, column_key);
        TryExecuteRequestParams params{
            .command = KVServerCommand::DEL,
            .request = "#" + request, // Prepend with # to indicate it's a message comming from the primary node
            .row_key = row_key,
            .resolve_server_by_server = true,
            .server = server};
        std::string response = group_try_execute_request_(params);
        if (response.empty())
        {
            status_codes[server.host + ":" + std::to_string(server.port)] = 1;
            results_strings[server.host + ":" + std::to_string(server.port)] = "Error";
        }
        else
        {
            status_codes[server.host + ":" + std::to_string(server.port)] = 0;
            results_strings[server.host + ":" + std::to_string(server.port)] = response;
        }
    }

    return build_broadcast_result_(message_id, status_codes, results_strings);
}

BroadcastResult IStorageService::broadcast_create_row(const int rg_id, const std::string message_id, const std::string &row_key)
{
    std::vector<KVServer> live_servers = get_live_servers_(rg_id);

    // Send out the request to all servers
    std::map<std::string, int> status_codes;
    std::map<std::string, std::string> results_strings;
    for (const auto &server : live_servers)
    {
        std::string request = RequestAssembler::assemble_create_row(row_key);
        TryExecuteRequestParams params{
            .command = KVServerCommand::CROW,
            .request = "#" + request, // Prepend with # to indicate it's a message comming from the primary node
            .row_key = row_key,
            .resolve_server_by_server = true,
            .server = server};
        std::string response = group_try_execute_request_(params);
        if (response.empty())
        {
            status_codes[server.host + ":" + std::to_string(server.port)] = 1;
            results_strings[server.host + ":" + std::to_string(server.port)] = "Error";
        }
        else
        {
            status_codes[server.host + ":" + std::to_string(server.port)] = 0;
            results_strings[server.host + ":" + std::to_string(server.port)] = response;
        }
    }

    return build_broadcast_result_(message_id, status_codes, results_strings);
}

std::string IStorageService::to_string(const tablet_value &data)
{
    return std::string{reinterpret_cast<const char *>(data.data()), data.size()};
}

tablet_value IStorageService::from_string(const std::string &data)
{
    return tablet_value(reinterpret_cast<const std::byte *>(data.c_str()),
                        reinterpret_cast<const std::byte *>(data.c_str() + data.size()));
}

std::string IStorageService::group_try_execute_request_(TryExecuteRequestParams &params)
{
    const auto read_bytes_before_value = params.read_bytes_before_value;
    const auto read_until_termination = params.read_until_termination;

    int try_count = 0;
    int max_tries = 2; // NOTE: For all three attempts we will use a different server (picked randomly from the group)
    int sockfd;
    while (try_count < max_tries)
    {
        try
        {
            // Get the server for the command
            KVServer server = resolve_server_(params);

            // Connect
            sockfd = connect_to_server_(server);

            // Welcome message (if it has one)
            std::string welcome_response;
            if (params.with_welcome_message)
            {
                welcome_response = read_response_(sockfd);
            }

            // Send request
            size_t bytes_sent = write_message_(sockfd, params.request);
            if (bytes_sent == -1)
                throw std::runtime_error("IStorageService: Failed sending data to KVStorage: " + std::string(strerror(errno)));

            if (read_bytes_before_value)
            {
                // Read Bytes Response
                std::string bytes_response = read_response_(sockfd);
                if (!is_success_response_(bytes_response))
                    throw std::runtime_error("IStorageService: Received error response from KVStorage: " + bytes_response);

                std::size_t bytes = ResponseParser::parse_bytes(bytes_response);

                // Read Value Response
                const std::string response = read_response_(sockfd, bytes);

                close(sockfd);

                return response;
            }
            else
            {
                // Read Response
                const std::string response = read_response_(sockfd, read_until_termination);

                // Validate response
                if (!is_success_response_(response))
                    throw std::runtime_error("IStorageService: Received error response from KVStorage: " + response);

                close(sockfd);

                return response;
            }
        }
        catch (const std::runtime_error &e)
        {
            std::cerr << "Runtime error occurred: " << e.what() << "\n";

            ++try_count;

            // Sleep for 100 ms (for cases where tablet splitting is in progress)
            usleep(100000);

            if (sockfd > 0)
            {
                // Attempt to close the socket if it was opened
                close(sockfd);
            }
        }
    }

    fprintf(stderr, "IStorageService: Failed to execute request after %d attempts\n", max_tries);
    return "";
}

std::vector<KVServer> IStorageService::get_live_servers_(const int rg_id)
{
    std::vector<KVServer> live_servers;
    for (const auto &server : kv_servers_map_[rg_id])
    {
        if (server.is_alive)
        {
            live_servers.push_back(server);
        }
    }
    return live_servers;
}

KVServer IStorageService::resolve_server_(const TryExecuteRequestParams &params)
{
    if (params.resolve_server_by_server)
    {
        return params.server;
    }
    else if (params.resolve_server_by_group_id)
    {
        return resolve_server_(params.group_id);
    }

    // Default is to resolve by row_key
    return resolve_server_(params.row_key);
}

KVServer IStorageService::resolve_server_(const std::string &row_key)
{
    if (kv_servers_map_.size() == 0)
    {
        throw std::runtime_error("IStorageService: No replication groups available\n");
    }

    size_t rg_count = kv_servers_map_.size();
    size_t max = std::numeric_limits<size_t>::max();
    size_t hash = row_key.find('-') == std::string::npos ? std::stoull(row_key) : std::stoull(row_key.substr(0, row_key.find('-')));
    int replication_group_id = hash / (max / rg_count);

    // Get the replication group
    std::vector<KVServer> servers = kv_servers_map_[replication_group_id];

    // Filter out inactive servers
    std::vector<KVServer> alive_servers;
    for (const auto &server : servers)
    {
        if (server.is_alive)
        {
            alive_servers.push_back(server);
        }
    }

    if (alive_servers.empty())
    {
        throw std::runtime_error("IStorageService: No alive servers found for replication group " + std::to_string(replication_group_id) + "\n");
    }

    // Randomly select a server from the alive servers
    int random_index = rand() % alive_servers.size();

    return alive_servers[random_index];
}

KVServer IStorageService::resolve_server_(const int group_id)
{
    // Get the replication group
    std::vector<KVServer> servers = kv_servers_map_[group_id];

    // Filter out inactive servers
    std::vector<KVServer> alive_servers;
    for (const auto &server : servers)
    {
        if (server.is_alive)
        {
            alive_servers.push_back(server);
        }
    }

    if (alive_servers.empty())
    {
        throw std::runtime_error("IStorageService: No alive servers found for replication group " + std::to_string(group_id) + "\n");
    }

    // Randomly select a server from the alive servers
    int random_index = rand() % alive_servers.size();

    return alive_servers[random_index];
}

int IStorageService::connect_to_server_(const KVServer &server)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
        throw std::runtime_error("IStorageService: Failed to create socket for server " + server.host + ":" + std::to_string(server.port) + "\n");

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server.port);
    inet_pton(AF_INET, server.host.c_str(), &server_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        throw std::runtime_error("IStorageService: Failed to connect to server " + server.host + ":" + std::to_string(server.port) + "\n");

    return sockfd;
}

std::string IStorageService::read_response_(const int sockfd, std::string termination)
{
    // \r\n or for example \r\n.\r\n

    std::string response;
    char buffer[1024];
    size_t term_pos = std::string::npos;

    while (true)
    {
        fprintf(stderr, "IStorageService: Reading response from socket %d\n", sockfd);
        // Peek at the data without removing it from the kernel buffer
        ssize_t peek_bytes_received = recv(sockfd, buffer, sizeof(buffer), MSG_PEEK);
        if (peek_bytes_received < 0)
            throw std::runtime_error("IStorageService: Failed reading bytes from socket " + std::to_string(sockfd) + "\n");

        if (peek_bytes_received == 0)
        {
            fprintf(stderr, "IStorageService: Connection closed on socket %d\n", sockfd);
            break; // Connection closed
        }

        std::string_view chunk(buffer, peek_bytes_received);

        // Check if the chunk is an error
        if (chunk.length() > 4 && chunk.substr(0, 4) == "-ERR")
        {
            termination = "\r\n";
        }

        // Check if termination exists in current chunk
        term_pos = chunk.find(termination);
        if (term_pos != std::string::npos)
        {
            // Calculate how many bytes to actually read (termination + its length)
            size_t bytes_to_read = term_pos + termination.length();

            size_t bytes_received = 0;
            while (bytes_received < bytes_to_read)
            {
                // Read the remaining bytes
                ssize_t read = recv(sockfd, buffer, bytes_to_read - bytes_received, 0);
                if (read < 0)
                {
                    throw std::runtime_error("IStorageService: Failed reading bytes from socket " + std::to_string(sockfd) + "\n");
                }
                response.append(buffer, read);
                bytes_received += read;
            }
            break;
        }
        else
        {
            // Read all the peeked data since no termination found
            size_t bytes_to_read = chunk.length() - termination.length() + 1;

            size_t bytes_received = 0;
            while (bytes_received < bytes_to_read)
            {
                // Read the remaining bytes
                ssize_t read = recv(sockfd, buffer, bytes_to_read - bytes_received, 0);
                if (read < 0)
                {
                    throw std::runtime_error("IStorageService: Failed reading bytes from socket " + std::to_string(sockfd) + "\n");
                }
                response.append(buffer, read);
                bytes_received += read;
            }
        }
    }
    response.resize(response.size() - termination.length());
    return response;
}

std::string IStorageService::read_response_(const int sockfd, const size_t bytes)
{
    char buffer[1024];
    std::string response;
    size_t total_bytes = 0;

    while (total_bytes < bytes + 2)
    {
        // int bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        int bytes_received = recv(sockfd, buffer, std::min(sizeof(buffer), 2 + bytes - total_bytes), 0);
        if (bytes_received < 0)
        {
            throw std::runtime_error("IStorageService: Failed reading bytes from socket " + std::to_string(sockfd) + "\n");
        }

        total_bytes += bytes_received;
        // buffer[bytes_received] = '\0'; // Null-terminate the received data
        // response += buffer;
        response.append(buffer, bytes_received);

        // Check if we have received the expected number of bytes
        // if (total_bytes >= bytes)
        // {
        //     break;
        // }
    }

    return response.substr(0, response.size() - 2);
}

bool IStorageService::is_success_response_(const std::string &response)
{
    return response.substr(0, 3) == "+OK";
}

BroadcastResult IStorageService::build_broadcast_result_(const std::string message_id, const std::map<std::string, int> &status_codes, std::map<std::string, std::string> results_strings)
{
    // Check if all status codes are 1
    bool all_success = true;
    for (const auto &status_code : status_codes)
    {
        // If a single status code is 1, then the broadcast was not successful
        if (status_code.second == 1)
        {
            all_success = false;
            break;
        }
    }

    // Build formatted_response
    std::ostringstream oss;
    for (const auto &[server, code] : status_codes)
    {
        const auto &result_str = results_strings.count(server) ? results_strings.at(server) : "N/A";
        oss << "[" << server << "] => Status: " << code << ", Result: " << result_str << "\n";
    }

    return {
        .message_id = message_id,
        .success = all_success,
        .formatted_response = oss.str(),
        .status_codes = status_codes,
        .results_strings = results_strings};
}

ssize_t IStorageService::write_message_(const int sockfd, const std::string &message)
{
    // string message_termination = message + "\r\n";
    size_t total_bytes_written = 0;
    size_t message_length = message.size();
    const char *data = message.c_str();

    while (total_bytes_written < message_length)
    {
        ssize_t bytes_written = write(sockfd, data + total_bytes_written, message_length - total_bytes_written);

        if (bytes_written == -1)
        {
            fprintf(stderr, "[%d] IStorageService: Error writing message to socket (%s).\n", sockfd, strerror(errno));
            return -1;
        }

        total_bytes_written += bytes_written;
    }

    return total_bytes_written;
}
