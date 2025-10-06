#pragma once

#include <shared_mutex>
#include "../Shared/CoordinatorService.h"
#include "UpdateForwarder.h"
#include <unordered_map>
#include <atomic>

extern int PORT_NO;      // Port number of the server
extern std::string HOST; // Hostname of the server
extern std::shared_mutex mtx;
extern std::unordered_map<pthread_t, int> THREAD2SOCKET; // Maps thread IDs to client socket FDs;
extern std::atomic<int> SS_FD;                           // Listener socket descriptor (to access inside SIGINT handler)
extern bool DEBUG;
extern std::mutex coord_service_created_mtx;             // Mutex to protect the Coordinator created condition variable
extern std::condition_variable COORD_SERVICE_CREATED_CV; // Condition variable to signal when the CoordinatorService is created
extern CoordinatorService *COORDINATOR_SERVICE;          // Pointer to CoordinatorService to have access to KVStorage Servers
extern std::shared_mutex coord_kv_servers_map_mtx;
extern std::map<int, std::vector<KVServer>> COORD_KV_SERVERS_MAP; // Used by the Coordinator to keep track of the KVStorage servers (through Heartbeat)
extern std::atomic<bool> IS_ALIVE;                                // Used by KVStorage servers to check if the server is alive
extern UpdateForwarder *UPDATE_FORWARDER;                         // Used by KVStorage servers to forward updates to the primary node
extern std::shared_mutex pipe_map_mutex;                          // Mutex to protect the global pipe map
extern std::map<string, int> PIPE_MAP;                            // Used by KVStorage servers to write response back to update request initiator
extern int RECOVERY_PIPE_R;  // Used by KVStorage servers to read data from the recovery pipe
extern int RECOVERY_PIPE_W;  // Used by KVStorage servers to write data to the recovery pipe

extern int SERVERS_PER_RG;      // Number of servers per replication group
extern int RG_ID;               // Replication group ID
extern int SERVER_ID;           // Global server ID
extern int SERVER_COUNT;        // Total number of servers
extern std::atomic<bool> RUN_KV_PRIMARY_THREAD;                   // Used to check if the KVPrimaryThread should run
extern std::atomic<bool> RUN_COORDINATOR_SERVICE;                 // Used to check if the Coordinator Service should run