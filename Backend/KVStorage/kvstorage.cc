#include "Server.h"
#include "ServerConfig.h"
#include <signal.h>
#include "Globals.h"
#include "KVPrimaryThread.h"
#include "TabletArray.h"
#include "unistd.h"

/**
 * Main function to parse command-line arguments,
 * and set up signal handlers and start the server.
 */
int main(int argc, char *argv[])
{
    /* Step 1: Parse configs */
    ServerConfig config = ServerConfig();
    config.server_type = ServerType::KVSTORE;

    config.parse_args(argc, argv);
    config.parse_servers_config(argc, argv);

    if (config.seas_prnt)
    {
        fprintf(stderr, "Franco Canova (fcanova) \n");
        return 0;
    }

    // Open pipe between KVPrimaryThread and Listener thread for recovery
    int pipe_fds[2];
    if (pipe(pipe_fds) == -1)
    {
        fprintf(stderr, "Failed to create recovery pipe\n");
    }

    // Set Globals
    DEBUG = config.debug;
    SERVER_ID = config.server_id;
    RG_ID = config.rg_id;
    SERVER_COUNT = config.server_count;

    UPDATE_FORWARDER = new UpdateForwarder(config);
    PORT_NO = config.portno;
    HOST = config.host;

    RECOVERY_PIPE_R = pipe_fds[0];
    RECOVERY_PIPE_W = pipe_fds[1];

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

    config.server_type = ServerType::KVSTORE;

    // Initialize KVPrimaryStorageThread in a background thread
    pthread_t thread_id;
    KVPrimaryThread primary_thread(config);
    auto primary_thread_runner = [](void *arg) -> void * // Wrapper function for CoordinatorService::start
    {
        // Block all signals from being received to KVPrimaryThread.
        // ClientHandler should be the only one to receive SIGUSR1.
        sigset_t sigset;
        sigfillset(&sigset);                       // Add all signals to the set
        pthread_sigmask(SIG_BLOCK, &sigset, NULL); // Block all signals for this thread

        // Wait until the coordinator service is created
        std::unique_lock<std::mutex> lock(coord_service_created_mtx);
        COORD_SERVICE_CREATED_CV.wait(lock, []
                                      { return COORDINATOR_SERVICE != nullptr; });

        KVPrimaryThread *primary_thread = static_cast<KVPrimaryThread *>(arg);
        primary_thread->start();
        return nullptr;
    };

    if (pthread_create(&thread_id, nullptr, primary_thread_runner, &primary_thread) != 0)
    {
        fprintf(stderr, "Failed to create thread for KVPrimaryThread\n");
        return 1;
    }

    if (pthread_detach(thread_id) != 0)
    {
        perror("Failed to detach thread for KVPrimaryThread\n");
        return 1;
    }

    // Initialize the tablet array
    tablets.init(config.tablet_init_dir, config.tablet_work_dir);

    fprintf(stderr, "Server initialized\n");
    /* Step 3: Start the server */
    Server server = Server();
    server.startServer(config);

    return 0;
}