#pragma once

#include <string>
#include <map>
#include <set>
#include <cstddef>
#include <vector>
#include "SharedStructures.h"
#include "KVServerCommand.h"

typedef std::vector<std::byte> tablet_value;

struct BroadcastResult
{
    std::string initiator_address;                      // Address of the server that initiated the update request
    std::string message_id;                             // Message ID of the broadcast
    bool success;                                       // Indicates if all broadcast was successful (based on the status codes)
    std::string formatted_response;                     // Formatted response from the server
    std::map<std::string, int> status_codes;            // Map of server address to status code
    std::map<std::string, std::string> results_strings; // Map of server address to result strings
};

struct TryExecuteRequestParams
{
    const KVServerCommand command; // Mandatory
    const std::string &request;    // Mandatory
    const std::string row_key = "";

    // RESOLVE SERVERS BY PARAMETERS
    const bool resolve_server_by_group_id = false;
    const int group_id = -1; // Mandatory if resolve_server_by_group_id is true
    const bool resolve_server_by_server = false;
    const KVServer server; // Mandatory if resolve_server_by_server is true

    const bool read_bytes_before_value = false;
    const std::string read_until_termination = "\r\n";
    const bool with_welcome_message = true;
};

/*
 * Class to interact with storage service.
 */
class IStorageService
{
private:
    // Map to store the replication groups and their servers
    std::map<int, std::vector<KVServer>> kv_servers_map_;

    // Executes the command on the server inside a try catch block (3 attempts)
    std::string group_try_execute_request_(TryExecuteRequestParams &params);

    // Resolves the server based on the request parameters.
    KVServer resolve_server_(const TryExecuteRequestParams &params);

    // Picks a random server for a given row key
    KVServer resolve_server_(const std::string &row_key);

    // Picks a random server for a given replication group
    KVServer resolve_server_(const int group_id);

    // Connects to the server and returns the socket file descriptor
    int connect_to_server_(const KVServer &server);

    // Reads the response from the server until termination
    std::string read_response_(const int sockfd, std::string termination = "\r\n");

    // Reads the response from the server until a specified number of bytes
    std::string read_response_(const int sockfd, const size_t bytes);

    // Checks if the response is a success response (true if the response starts with "+OK")
    bool is_success_response_(const std::string &response);

    // Based on the status codes and results of a broadcast, formats the broadcast result
    BroadcastResult build_broadcast_result_(const std::string message_id, const std::map<std::string, int> &status_codes, std::map<std::string, std::string> results_strings);

    // Gets live servers for a given replication group from the internal map
    std::vector<KVServer> get_live_servers_(const int rg_id);

    // Write a message to the provided socket
    ssize_t write_message_(const int sockfd, const std::string &message);

public:
    // Constructor
    IStorageService(std::map<int, std::vector<KVServer>> kv_servers_map)
        : kv_servers_map_(std::move(kv_servers_map))
    {
    }

    // Destructor
    ~IStorageService() = default;

    // Gets value from a given row and column key
    // Returns 0 if successful
    // Returns 1 if error
    int get(const std::string &row_key, const std::string &column_key, tablet_value &value);

    // Gets value from a given row and column key from the provided server
    // Returns 0 if successful
    // Returns 1 if error
    int get(const std::string &row_key, const std::string &column_key, tablet_value &value, const KVServer &server);

    // Puts value into a given row and column key
    // Returns 0 if successful
    // Returns 1 if error
    int put(const std::string &row_key, const std::string &column_key, const tablet_value &value);

    // Conditional put updates value only if current value is equal to cvalue
    // Returns 0 if successful
    // Returns 1 if error
    int cput(const std::string &row_key, const std::string &column_key, const tablet_value &cvalue,
             const tablet_value &value, bool &result);

    // Moves value from one column to another
    // Returns 0 if successful
    // Returns 1 if error
    int move(const std::string &row_key, const std::string &column_key, const std::string &new_column_key);

    // Removes value for a given row and column key
    // Returns 0 if successful
    // Returns 1 if error
    int remove(const std::string &row_key, const std::string &column_key);

    // Retrieves all row keys as a set
    // Returns 0 if successful
    // Returns 1 if error
    int list_rows(std::set<std::string> &rows);

    // Retrieves all row keys as a set for a specific server
    // Returns 0 if successful
    // Returns 1 if error
    int list_rows(const KVServer &server, std::set<std::string> &rows);

    // Retrieves all column keys for a given row key as a set
    // Returns 0 if successful
    // Returns 1 if error
    int list_columns(const std::string &row_key, std::set<std::string> &columns);

    // Retrieves all column keys for a given row key as a set for a specific server
    // Returns 0 if successful
    // Returns 1 if error
    int list_columns(const KVServer &server, const std::string &row_key, std::set<std::string> &columns);

    // Creates a new row with the given row key
    // Returns 0 if successful
    // Returns 1 if error
    int create_row(const std::string &row_key);

    // Shuts down the server
    // Returns 0 if successful
    // Returns 1 if error
    int shut_down(const KVServer &server);

    // Brings the server back up
    // Returns 0 if successful
    // Returns 1 if error
    int bring_up(const KVServer &server);

    ////////////////////// Broadcasting functions ////////////////////////
    // Broadcasts a put command to all alive servers in the replication group
    BroadcastResult broadcast_put(const int rg_id, const std::string message_id, const std::string &row_key, const std::string &column_key, const tablet_value &value);

    // Broadcasts a conditional put command to all alive servers in the replication group
    BroadcastResult broadcast_cput(const int rg_id, const std::string message_id, const std::string &row_key, const std::string &column_key, const tablet_value &cvalue,
                                   const tablet_value &value);

    // Broadcasts a move command to all alive servers in the replication group
    BroadcastResult broadcast_move(const int rg_id, const std::string message_id, const std::string &row_key, const std::string &column_key, const std::string &new_column_key);

    // Broadcasts a delete command to all alive servers in the replication group
    BroadcastResult broadcast_remove(const int rg_id, const std::string message_id, const std::string &row_key, const std::string &column_key);

    // Broadcasts a create row command to all alive servers in the replication group
    BroadcastResult broadcast_create_row(const int rg_id, const std::string message_id, const std::string &row_key);

    ////////////////////// Other helper functions ////////////////////////
    // Convenience function to convert tablet_value to string
    static std::string to_string(const tablet_value &value);

    // Convenience function to convert string to tablet_value
    static tablet_value from_string(const std::string &value);
};