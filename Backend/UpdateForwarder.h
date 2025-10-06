#pragma once

#include "ServerConfig.h"

/*
 * This class is going to be a singleton class.
 * It will be used by any thread of the KVStorage
 * server to forward updates to the primary node
 * of the replication group.
 */
class UpdateForwarder
{
public:
    UpdateForwarder(const ServerConfig &config) : config_(config), socket_fd_(-1), primary_changed_(true) {}
    ~UpdateForwarder();

    // Thread safe method that finds the primary node of the replication group, connects to its internal port (if not already connected to it) and forwards the update message.
    void forward_update(const std::string &message);

private:
    ServerConfig config_;
    KVServer primary_node_;
    int socket_fd_;
    bool primary_changed_;
    std::mutex mtx_;

    // Gets the primary node of the replication group
    KVServer get_primary_node_(int rg_id);

    // Connects to the servers internal port and returns the socket file descriptor
    int connect_to_server_(const KVServer &server);
};