#ifndef SERVERCONFIG_H
#define SERVERCONFIG_H

#include <string>
#include "Server.h"
#include <map>
#include <vector>
#include "../Shared/SharedStructures.h"

using namespace std;

enum class ServerType
{
    SMTP,
    POP3,
    COORDINATOR,
    KVSTORE,
};

struct ServerConfig
{
    // All servers
    bool debug = false;
    bool seas_prnt = false;
    int portno = -1;
    std::string host{""};
    std::string tablet_init_dir{""};
    std::string tablet_work_dir{"."};
    ServerType server_type;
    std::string servers_config_path = "servers_config.txt";
    void parse_args(int argc, char *argv[]);

    // Used by SMTP, POP3
    std::string coordinator_host = "";
    int coordinator_port = -1;

    // Used by Coordinator
    std::map<int, std::vector<KVServer>> kv_servers_map_;
    void get_kv_servers_map_(const std::vector<std::string> &servers);

    // Used by KVStorage
    int rg_id = -1; // Replication Group ID
    int server_id = -1; // Server ID
    int server_count = -1; // Total number of servers
    bool recovery = false; // Recovery flag
    int recovery_pipe_w = -1; // Pipe write fd for recovery
    int recovery_pipe_r = -1; // Pipe read fd for recovery
    void get_rg_id(const std::vector<std::string> &servers);

    // Parse the servers config file
    // Coordinator will use it to get the list of servers
    // All other servers will use it to get the host and port of coordinator
    // KVStorage will use it to get what replication group it belongs to
    void parse_servers_config(int argc, char *argv[]);
};

#endif
