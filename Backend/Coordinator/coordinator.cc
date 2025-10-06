#include "Server.h"
#include "ServerConfig.h"
#include <signal.h>
#include "Globals.h"
#include "CoordinatorHBService.h" // Ensure this header is included

/**
 * Main function to parse command-line arguments,
 * and set up signal handlers and start the server.
 */
int main(int argc, char *argv[])
{
    /* Step 1: Parse configs */
    ServerConfig config = ServerConfig();
    config.server_type = ServerType::COORDINATOR;

    config.parse_args(argc, argv);
    config.parse_servers_config(argc, argv);

    if (config.seas_prnt)
    {
        fprintf(stderr, "Franco Canova (fcanova) \n");
        return 0;
    }

    // Set Globals
    DEBUG = config.debug;

    COORD_KV_SERVERS_MAP = config.kv_servers_map_;

    if (DEBUG)
    {
        fprintf(stderr, "Starting server on port %d\n", config.portno);
        fprintf(stderr, "Listener ThreadID: %ld\n", pthread_self());
    }

    /* Step 2: Handle signal interrupts for main and child threads */
    struct sigaction sa;

    // Setup SIGINT handler for the main thread
    sa.sa_handler = Server::sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Ensures SA_RESTART is not set, system calls should return EINTR
    sigaction(SIGINT, &sa, NULL);

    // Setup SIGUSR1 handler for worker threads
    sa.sa_handler = Server::sigusr1_handler;
    sigaction(SIGUSR1, &sa, NULL);

    // Initialize CoordinatorHBService in a background thread
    pthread_t thread_id;
    CoordinatorHBService hb_service(config.kv_servers_map_);
    auto coordinator_hb_service_runner = [](void *arg) -> void * // Wrapper function for CoordinatorService::start
    {
        CoordinatorHBService *hb_service = static_cast<CoordinatorHBService *>(arg);
        hb_service->start();
        return nullptr;
    };

    if (pthread_create(&thread_id, nullptr, coordinator_hb_service_runner, &hb_service) != 0)
    {
        fprintf(stderr, "Failed to create thread for CoordinatorHBService\n");
        return 1;
    }

    if (pthread_detach(thread_id) != 0)
    {
        perror("Failed to detach thread for CoordinatorHBService\n");
        return 1;
    }

    /* Step 3: Start the server */
    Server server = Server();
    server.startServer(config);

    return 0;
}