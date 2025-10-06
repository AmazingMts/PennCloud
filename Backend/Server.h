#ifndef SERVER_H
#define SERVER_H

#include <atomic>
#include <pthread.h>
#include <shared_mutex>
#include <unordered_map>
#include "ServerConfig.h"
#include <memory>
#include "../Shared/CoordinatorService.h"

using namespace std;

struct ServerConfig; // Forward declaration of ServerConfig

/*
 * Class for handling server logic
 */
class Server
{
public:
    // Start the server and accept connections
    void startServer(const ServerConfig &config);
    // Signal handler for SIGINT (main listener/dispatcher thread cleanup)
    static void sigint_handler(int signum);
    // Signal handler for SIGUSR1 (reader + worker threads cleanup)
    static void sigusr1_handler(int signum);

private:
    // Binds to server port and listens for incoming connections (wrapped in while loop until success)
    void force_bind_and_listen_(const ServerConfig &config);
};

#endif
