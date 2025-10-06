#include "UpdateForwarder.h"
#include "Globals.h"
#include "SocketWriter.h"
#include <sys/socket.h>
#include <arpa/inet.h>

void UpdateForwarder::forward_update(const std::string &message)
{
    std::lock_guard<std::mutex> lock(mtx_);
    try
    {
        // Get the primary node
        primary_node_ = get_primary_node_(config_.rg_id);

        // Connect to the primary node if not already connected
        if (primary_changed_)
        {
            socket_fd_ = connect_to_server_(primary_node_);
            fprintf(stderr, "UpdateForwarder: Connected to Primary %s:%d, socket %d\n", primary_node_.host.c_str(), primary_node_.port_gc, socket_fd_);
        }

        // Send the message to the primary node
        SocketWriter writer(socket_fd_);
        writer.write_message(message, false);
        writer.write_message("\r\n", false);
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "UpdateForwarder: Exception while forwarding update: %s\n", e.what());
    }
}

KVServer UpdateForwarder::get_primary_node_(int rg_id)
{
    // Check if the current node is the primary node based on *COORDINATOR_SERVICE
    std::map<int, std::vector<KVServer>> map = COORDINATOR_SERVICE->get_kv_servers_map();
    auto it = map.find(rg_id);
    if (it != COORD_KV_SERVERS_MAP.end())
    {
        for (const auto &server : it->second)
        {
            if (server.is_primary)
            {
                // Check if the primary server changed or not
                if (primary_node_.port != server.port)
                {
                    fprintf(stderr, "UpdateForwarder: Primary node changed to %s:%d\n", server.host.c_str(), server.port_gc);
                    primary_changed_ = true;

                    // Close the old socket if it exists
                    if (socket_fd_ != -1)
                    {
                        close(socket_fd_);
                        socket_fd_ = -1;
                    }
                }
                else
                {
                    primary_changed_ = false;
                }

                return server;
            }
        }
    }
    throw std::runtime_error("UpdateForwarder: No primary node found in the replication group\n");
}

int UpdateForwarder::connect_to_server_(const KVServer &server)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        throw std::runtime_error("UpdateForwarder: Failed to create socket for server " + server.host + ":" + std::to_string(server.port_gc) + "\n");
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server.port_gc);
    inet_pton(AF_INET, server.host.c_str(), &server_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        throw std::runtime_error("UpdateForwarder: Failed to connect to server " + server.host + ":" + std::to_string(server.port_gc) + "\n");
    }

    return sockfd;
}