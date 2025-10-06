#include "ServerConfig.h"
#include "KVStorageCommandDispatcher.h"

class KVPrimaryThread
{
public:
    KVPrimaryThread(const ServerConfig &config);
    ~KVPrimaryThread();

    void start();

private:
    ServerConfig config_;
    KvStorageCommandDispatcher command_dispatcher_;
    int socket_fd_{-1};
    KVServer primary_node_;
    std::map<string, int> port_to_private_fd_map_; // Maps public host and port of a replica to the socket open to its internal port

    void bind_and_listen_();
    std::string read_data_(const int sockfd);
    KVServer get_primary_node_();

    // Connect to the specified server
    int connect_to_server_(const KVServer &server);
};