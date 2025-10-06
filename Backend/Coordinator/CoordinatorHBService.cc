#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "CoordinatorHBService.h"

CoordinatorHBService::CoordinatorHBService(std::map<int, std::vector<KVServer>> &kv_servers_map)
    : kv_servers_map_(kv_servers_map) {}

CoordinatorHBService::~CoordinatorHBService()
{
    // Destructor implementation
}

void CoordinatorHBService::start()
{
    // TODO: Watch out for the case where the KVServer can be connected to
    // but is not ready to accept connections yet (say, as it is recovering)

    int sockfd = -1;
    while (true)
    {
        try
        {
            // Sleep for a while before checking the servers again
            sleep(1);

            // Iterate over the replication groups
            for (auto &pair : kv_servers_map_)
            {
                int replication_group_id = pair.first;
                std::vector<KVServer> &servers = pair.second;
                bool rp_has_primary = false;

                // Check if the replication group has a primary server
                for (const auto &server : servers)
                {
                    if (server.is_primary)
                    {
                        rp_has_primary = true;
                        break;
                    }
                }

                // Iterate over the servers in the replication group
                for (auto &server : servers)
                {
                    sockfd = socket(AF_INET, SOCK_STREAM, 0);
                    if (sockfd < 0)
                    {
                        fprintf(stderr, "CoordinatorHBService: Error creating socket");
                        continue;
                    }

                    struct sockaddr_in server_addr;
                    server_addr.sin_family = AF_INET;
                    server_addr.sin_port = htons(server.port);
                    inet_pton(AF_INET, server.host.c_str(), &server_addr.sin_addr);

                    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
                    {
                        if (DEBUG)
                        {
                            fprintf(stderr, "CoordinatorHBService: Connection to %s:%d failed\n", server.host.c_str(), server.port);
                        }

                        if (server.is_primary)
                        {
                            // If the primary server is down, we need to elect a new primary
                            for (auto &s : servers)
                            {
                                if (s.is_alive && !s.is_primary)
                                {
                                    s.is_primary = true;
                                    rp_has_primary = true;
                                    break;
                                }
                                else
                                {
                                    rp_has_primary = false;
                                }
                            }
                        }

                        server.is_alive = false;
                        server.is_primary = false;

                        close(sockfd);
                        sockfd = -1;
                        continue;
                    }

                    // If it is the first server in the list we could connect to, default it to primary if the replication group has no primary
                    if (!rp_has_primary)
                    {
                        server.is_primary = true;
                        rp_has_primary = true;
                    }

                    server.is_alive = true;
                    close(sockfd);
                    sockfd = -1;
                }
            }

            // Set global variable to the latest state of the servers
            std::unique_lock<std::shared_mutex> lock(coord_kv_servers_map_mtx);
            COORD_KV_SERVERS_MAP = kv_servers_map_;
            lock.unlock();

            if (DEBUG)
            {
                for (const auto &pair : kv_servers_map_)
                {
                    printf("Replication Group %d:\n", pair.first);
                    for (const auto &server : pair.second)
                    {
                        printf("  Server: %s:%d, Primary: %s, Alive: %s\n", server.host.c_str(), server.port, server.is_primary ? "true" : "false", server.is_alive ? "true" : "false");
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            if (sockfd >= 0)
            {
                close(sockfd);
                sockfd = -1;
            }
            fprintf(stderr, "CoordinatorHBService: Error: %s\n", e.what());
        }
    }
}