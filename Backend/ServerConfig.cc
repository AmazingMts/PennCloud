#include "ServerConfig.h"
#include "Globals.h"
#include <unistd.h>
#include <fstream>

void ServerConfig::parse_args(int argc, char *argv[])
{
    int opt;
    while ((opt = getopt(argc, argv, "p:avc:i:w:r")) != -1)
    {
        switch (opt)
        {
        case 'p':
            portno = std::stoi(std::string(optarg));
            break;
        case 'a':
            seas_prnt = true;
            break;
        case 'v':
            debug = true;
            break;
        case 'c':
            servers_config_path = std::string(optarg);
            break;
        case 'i':
            tablet_init_dir = std::string(optarg);
            break;
        case 'w':
            tablet_work_dir = std::string(optarg);
            break;
        case 'r':
            recovery = true;
            break;
        default:
            // TODO: Print Usage message per type of server
            fprintf(stderr,
                    "Usage: \n"
                    "SMTP: \n"
                    "POP3: \n"
                    "Coordinator: \n"
                    "KVStorage: \n");
        }
    }
}

void ServerConfig::parse_servers_config(int argc, char *argv[])
{
    if (servers_config_path.empty())
    {
        fprintf(stderr, "ServerConfig: Error. No servers config file provided.\n");
        exit(1);
    }

    std::ifstream config_file(servers_config_path);
    if (!config_file.is_open())
    {
        fprintf(stderr, "ServerConfig: Error opening config file: %s\n", servers_config_path.c_str());
        exit(1);
    }

    std::string line;
    std::vector<std::string> servers;
    while (std::getline(config_file, line))
    {
        if (line.empty() || line[0] == '#')
            continue; // Skip empty lines and comments

        std::istringstream iss(line);
        servers.push_back(line);
    }

    if (servers.empty())
    {
        fprintf(stderr, "ServerConfig: Error. Server list is empty.");
        exit(1);
    }

    // First server in the file is the coordinator
    std::string coordinator_server = servers[0];
    servers.erase(servers.begin()); // Remove the coordinator server from the list
    coordinator_host = coordinator_server.substr(0, coordinator_server.find(':'));
    coordinator_port = std::stoi(coordinator_server.substr(coordinator_server.find(':') + 1));

    // The rest of the servers are the KV servers
    // Acceptable values are a single instance OR a multiple of 3
    if (!(servers.size() == 1) && (servers.size() % SERVERS_PER_RG != 0))
    {
        fprintf(stderr, "ServerConfig: Error. Number of servers in config file is not one or a multiple of %d\n", SERVERS_PER_RG);
        exit(1);
    }

    // Only the coordinator needs to parse the rest of the servers config file
    // All other servers will use the coordinator to get the list of servers
    if (server_type == ServerType::COORDINATOR)
        get_kv_servers_map_(servers);

    // KVStorage needs to know what replication group it belongs to
    // to check if it is primary or not from the coordinator
    if (server_type == ServerType::KVSTORE)
    {
        get_kv_servers_map_(servers);
        get_rg_id(servers);
    }
}

void ServerConfig::get_kv_servers_map_(const std::vector<std::string> &servers)
{
    // Calculate replication groups
    int replication_group_count = servers.size() / SERVERS_PER_RG;

    // Initialize the kv_servers_map_ with empty vectors
    kv_servers_map_.clear();
    for (int i = 0; i < replication_group_count; ++i)
    {
        kv_servers_map_[i] = std::vector<KVServer>();
    }

    // Fill the kv_servers_map_ with the servers
    int count = 0;
    for (const auto &server : servers)
    {
        std::string host = server.substr(0, server.find(':'));
        int port = std::stoi(server.substr(server.find(':') + 1));
        kv_servers_map_[count].push_back({host, port, port + 1, false, false}); // Initialize all servers as Primary = False, Alive = False

        if (kv_servers_map_[count].size() >= SERVERS_PER_RG)
        {
            count++;
        }
    }

    return;
}

void ServerConfig::get_rg_id(const std::vector<std::string> &servers)
{
    this->server_count = servers.size();
    int count = 0;
    for (const auto &server : servers)
    {
        int port = std::stoi(server.substr(server.find(':') + 1));
        if (port == portno)
        {
            // Found the server, return the replication group ID
            int id = count / SERVERS_PER_RG;
            this->rg_id = id;
            this->server_id = count;
            host = server.substr(0, server.find(':'));
            return;
        }
        count++;
    }
}