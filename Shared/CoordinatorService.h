#pragma once

#include <string>
#include <vector>
#include <map>
#include "SharedStructures.h"
#include <shared_mutex>
#include <atomic>

class CoordinatorService
{
public:
    CoordinatorService(std::string coordinator_host, int coordinator_port, bool debug);
    ~CoordinatorService();

    void start(std::atomic<bool> &run);

    // Retruns a copy of the KVStorage servers map
    std::map<int, std::vector<KVServer>> get_kv_servers_map();
    // Blocks until the CoordinatorService is ready
    void wait_until_ready();

private:
    std::string coordinator_host_;
    int coordinator_port_;
    bool debug_;
    std::shared_mutex kv_map_mutex_;
    std::map<int, std::vector<KVServer>> kv_servers_map_; // Map to store the replication groups and their servers

    // Condition variable to signal when the CoordinatorService is ready
    std::mutex coord_service_ready_mutex_;
    std::condition_variable ready_cv_;
    bool coord_service_ready_;

    // Performs TCP call to Coordinator Server and updates the kv_servers_map_
    void get_kv_servers_();
    // std::string read_response_(int sockfd);
};