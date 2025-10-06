#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>
#include "Server.h"
#include "ClientHandler.h"
#include "ServerConfig.h"
#include "NewClientArgs.h"
#include "Globals.h"
#include "TabletArray.h"
#include <fcntl.h>

using namespace std;

void Server::startServer(const ServerConfig &config)
{
    // Start CoordinatorService if server type is NOT COORDINATOR
    if (config.server_type == ServerType::POP3 ||
        config.server_type == ServerType::SMTP ||
        config.server_type == ServerType::KVSTORE)
    {
        // Create a CoordinatorService object that will be running in the background
        pthread_t thread_id;

        // Create the service and notify interested threads that the CoordinatorService is created
        {
            std::unique_lock<std::mutex> lock(coord_service_created_mtx);
            COORDINATOR_SERVICE = new CoordinatorService(config.coordinator_host, config.coordinator_port, DEBUG); // Store the service in a global variable
        }
        COORD_SERVICE_CREATED_CV.notify_all();

        // Wrapper function for CoordinatorService::start
        auto coordinator_service_runner = [](void *arg) -> void *
        {
            // Block all signals from being received to CoordinatorService.
            sigset_t sigset;
            sigfillset(&sigset);                       // Add all signals to the set
            pthread_sigmask(SIG_BLOCK, &sigset, NULL); // Block all signals for this thread

            CoordinatorService *service = static_cast<CoordinatorService *>(arg);
            service->start(RUN_COORDINATOR_SERVICE);
            return nullptr;
        };

        if (pthread_create(&thread_id, nullptr, coordinator_service_runner, COORDINATOR_SERVICE) != 0)
        {
            fprintf(stderr, "Failed to create thread for CoordinatorService\n");
        }

        if (pthread_detach(thread_id) != 0)
        {
            perror("Failed to detach thread for CoordinatorService\n");
            return;
        }
    }

    if (config.server_type != ServerType::COORDINATOR)
    {
        COORDINATOR_SERVICE->wait_until_ready();
    }

    if (config.server_type == ServerType::KVSTORE)
    {
        tablets.load(config.recovery);
    }

    // Create a socket and bind to it
    force_bind_and_listen_(config);

    // Accept connections and create threads
    while (true)
    {
        // if (DEBUG)
        //     fprintf(stderr, "Main thread (%ld): IS_ALIVE = %d\n", pthread_self(), IS_ALIVE.load());

        if (IS_ALIVE.load() && SS_FD.load() == -1)
        {
            if (DEBUG)
                fprintf(stderr, "Main thread (%ld): Starting state syncronization...\n", pthread_self());

            // All the state synchronization should be done here
            // Only ever reached by KVStorage servers
            tablets.reset();
            tablets.load(true);

            if (DEBUG)
                fprintf(stderr, "Main thread (%ld): Finished state syncronization...\n", pthread_self());

            // We need to reboot the server.
            force_bind_and_listen_(config);
        }
        else if (!IS_ALIVE.load())
        {
            std::unique_lock<std::shared_mutex> lock(mtx);
            if (THREAD2SOCKET.empty())
            {
                // Do nothing, wait until we come back up
                lock.unlock();
                sleep(1);
                continue;
            }

            // for (const auto &pair : THREAD2SOCKET)
            // {
            //     fprintf(stderr, "Main thread (%ld): Closing socket %d...\n", pthread_self(), pair.second);
            //     pthread_kill(pair.first, SIGUSR1);
            // }

            // THREAD2SOCKET.clear(); // Clear the map of threads and sockets
            // lock.unlock();

            // If we have open clients, we need to close them.
            // Ideal process: Reader thread receives EOF, pushes
            // TQUIT to ClientHandler and finally ClientHandler
            // closes the socket and exits. That way we close off all
            // running threads.
            while (!THREAD2SOCKET.empty())
            {
                // for (const auto &pair : THREAD2SOCKET)
                // {
                //     fprintf(stderr, "Main thread (%ld): Closing socket %d...\n", pthread_self(), pair.second);
                //     pthread_kill(pair.first, SIGUSR1);
                // }
                // lock.unlock();
                for (const auto &pair : THREAD2SOCKET)
                {
                    if (pair.second != -1)
                    {
                        shutdown(pair.second, SHUT_RDWR); // Unblocks the reader thread (so it gets EOF)
                        // close(pair.second);
                    }
                }
                lock.unlock();
                sleep(1); // Gives time for threads to clean up
                lock.lock();
            }
            lock.unlock();

            continue;
        }

        sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(SS_FD.load(), (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // No connections ready to accept right now
                usleep(100000);
                continue;
            }
            else
            {
                fprintf(stderr, "Main thread (%ld): Failed to accept client connection.\n", pthread_self());
            }
        }

        // Unset flags for non-blocking mode (MACOS)
        int flags = fcntl(client_fd, F_GETFL, 0);
        flags &= ~O_NONBLOCK;
        fcntl(client_fd, F_SETFL, flags);

        // TODO: Since we have multiple types of servers, each might take different arguments
        // We should build NewClientArgs with some type of Factory. The factory would take ServerConfig
        // as an argument and populate a NewClientArgs object with necessary fields.
        NewClientArgs *nca = new NewClientArgs();
        nca->socket_fd = client_fd;
        nca->server_type = config.server_type;
        pthread_t threadId;
        if (pthread_create(&threadId, nullptr, ClientHandler::handleClientThreadWrapper, nca) != 0)
        {
            fprintf(stderr, "Failed to create thread for client %d", client_fd);
            close(client_fd);
        }
    }
}

void Server::sigint_handler(int signum)
{
    if (DEBUG)
        fprintf(stderr, "Main thread (%ld): SIGINT received, shutting down...\n", pthread_self());

    ///////////////////////////// Closing KV Primary Thread //////////////////////////////
    if (DEBUG)
        fprintf(stderr, "Main thread (%ld): Closing KVPrimaryThread...\n", pthread_self());

    RUN_KV_PRIMARY_THREAD.store(false); // Signal to the KVPrimaryThread to stop running

    while (!(UPDATE_FORWARDER == nullptr) && !RUN_KV_PRIMARY_THREAD)
    {
        usleep(100);
    }

    if (DEBUG)
        fprintf(stderr, "Main thread (%ld): Finished closing KVPrimaryThread...\n", pthread_self());

    ///////////////////////////// Closing Coordinator Service //////////////////////////////
    if (DEBUG)
        fprintf(stderr, "Main thread (%ld): Closing Coordinator Service...\n", pthread_self());

    RUN_COORDINATOR_SERVICE.store(false); // Signal to the CoordinatorService to stop running

    while (!(UPDATE_FORWARDER == nullptr) && !RUN_COORDINATOR_SERVICE)
    {
        usleep(100);
    }

    if (DEBUG)
        fprintf(stderr, "Main thread (%ld): Finished closing Coordinator Service...\n", pthread_self());

    ///////////////////////////// Closing Worker Threads //////////////////////////////
    if (DEBUG)
        fprintf(stderr, "Main thread (%ld): Cleaning Worker Threads...\n", pthread_self());

    std::shared_lock<std::shared_mutex> lock2(mtx);
    for (const auto &pair : THREAD2SOCKET)
    {
        pthread_kill(pair.first, SIGUSR1);
    }
    lock2.unlock();

    for (const auto &pair : THREAD2SOCKET)
    {
        pthread_join(pair.first, NULL);
    }

    if (DEBUG)
        fprintf(stderr, "Main thread (%ld): Finished cleaning worker threads...\n", pthread_self());
    ///////////////////////////// Closing Main Thread //////////////////////////////
    if (DEBUG)
        fprintf(stderr, "Main thread (%ld): Closing listener Socket...\n", pthread_self());

    if (SS_FD.load() != -1)
        close(SS_FD.load());

    if (DEBUG)
        fprintf(stderr, "Main thread (%ld): Exiting...\n", pthread_self());

    // TODO: Delete all the global variables

    exit(130);
}

void Server::sigusr1_handler(int signum)
{
    pthread_t thread_id = pthread_self();
    if (DEBUG)
        fprintf(stderr, "--> Worker thread (%ld): Cleaning up ...\n", thread_id);

    // Close sockets
    std::shared_lock<std::shared_mutex> lock(mtx);
    auto it = THREAD2SOCKET.find(thread_id);
    if (it != THREAD2SOCKET.end())
    {
        int socket_fd = it->second;
        string message = "-ERR Server shutting down\r\n";
        write(socket_fd, message.c_str(), message.size());
        string debug_msj = message;

        if (DEBUG)
            fprintf(stderr, "--> Worker thread (%ld): Closing socket...\n", thread_id);

        if (socket_fd != -1)
        {
            shutdown(socket_fd, SHUT_RDWR);
            close(socket_fd);
        }
    }
    lock.unlock();

    if (DEBUG)
        fprintf(stderr, "--> Worker thread (%ld): Finished cleaning up...\n", thread_id);

    return;
}

void Server::force_bind_and_listen_(const ServerConfig &config)
{
    while (SS_FD.load() == -1)
    {
        sleep(1);

        if (DEBUG)
            fprintf(stderr, "Main thread (%ld): Attempting to bind and listen on port %d...\n", pthread_self(), config.portno);

        int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket == -1)
            fprintf(stderr, "Main thread (%ld): Failed to create socket\n", pthread_self());

        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(config.portno);

        int optval = 1;
        int ret = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        if (ret == -1)
        {
            fprintf(stderr, "Main thread (%ld): Failed to set socket options\n", pthread_self());
            close(serverSocket);
        }

        if (::bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
        {
            fprintf(stderr, "Main thread (%ld): Failed to bind the socket\n", pthread_self());
            close(serverSocket);
        }

        if (listen(serverSocket, 5) == -1)
        {
            fprintf(stderr, "Main thread (%ld): Failed to listen\n", pthread_self());
            close(serverSocket);
        }

        // Store the server socket file descriptor in a shared atomic variable
        SS_FD.store(serverSocket);

        // Make the server socket non-blocking once after it's created and bound
        int flags = fcntl(SS_FD.load(), F_GETFL, 0);
        if (flags == -1)
        {
            fprintf(stderr, "Main thread (%ld): Failed to get socket flags\n", pthread_self());
        }
        if (fcntl(SS_FD.load(), F_SETFL, flags | O_NONBLOCK))
        {
            fprintf(stderr, "Main thread (%ld): Failed to set socket to non-blocking\n", pthread_self());
        }


        if (DEBUG)
            fprintf(stderr, "Main thread (%ld): Finished binding and listening on port %d...\n", pthread_self(), config.portno);
    }
}
