#include "KVPrimaryThread.h"
#include <unistd.h>
#include <sys/socket.h>
#include <csignal>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "SocketWriter.h"
#include <algorithm>
// #include <poll.h>
#include <fcntl.h>
#include "SharedStructures.h"

KVPrimaryThread::KVPrimaryThread(const ServerConfig &config)
    : config_(config), command_dispatcher_()
{
}

KVPrimaryThread::~KVPrimaryThread()
{
    if (socket_fd_ != -1)
    {
        close(socket_fd_);
    }
}

void KVPrimaryThread::start()
{
    // Wait until the coordinator service is running
    COORDINATOR_SERVICE->wait_until_ready();

    bind_and_listen_();

    if (DEBUG)
        fprintf(stderr, "KVPrimaryThread THIS IS MY SOCKET %d: Listening on %s:%d\n", socket_fd_, config_.host.c_str(), config_.portno + 1);

    fd_set read_fd_set, master_fd_set;
    FD_ZERO(&master_fd_set);
    FD_SET(socket_fd_, &master_fd_set);

    int max_fd = socket_fd_;
    std::vector<int> client_fds;
    struct timeval timeout;

    std::string my_host_port = config_.host + ":" + std::to_string(config_.portno);

    while (RUN_KV_PRIMARY_THREAD.load())
    {
        try
        {
            // sleep(1);
            // fprintf(stderr, "KVPrimaryThread: Waiting for incoming connections...\n");

            // Get the primary node
            primary_node_ = get_primary_node_();

            // If im the primary, I need to connect to all the internal ports
            // of the nodes in my replication group (including myself)
            bool found_primary_node = (primary_node_.host != "") && (primary_node_.port != 0);
            bool am_i_primary = primary_node_.port == config_.portno;
            if (found_primary_node && am_i_primary)
            {
                // Check that im connnected to all the internal ports of the nodes in my replication group
                // If not, connect to them.
                std::map<int, std::vector<KVServer>> map = COORDINATOR_SERVICE->get_kv_servers_map();
                auto it = map.find(config_.rg_id);
                if (it != map.end())
                {
                    for (const auto &server : it->second)
                    {
                        std::string s = server.host + ":" + std::to_string(server.port);

                        // Skip myself
                        if (s == my_host_port)
                            continue;

                        // Check if im already connected to the server
                        bool is_in_map = port_to_private_fd_map_.find(s) != port_to_private_fd_map_.end();
                        // Check if the socket is still open
                        if (is_in_map)
                        {
                            int private_fd = port_to_private_fd_map_[s];

                            char buffer;

                            // Set the socket to non-blocking mode
                            int flags = fcntl(private_fd, F_GETFL, 0);
                            flags |= O_NONBLOCK;
                            fcntl(private_fd, F_SETFL, flags);

                            // Peek at the socket to check if it's still open
                            int result = recv(private_fd, &buffer, 1, MSG_PEEK);

                            // Set the socket back to blocking mode
                            flags &= ~O_NONBLOCK;
                            fcntl(private_fd, F_SETFL, flags);

                            if (result == 1)
                                continue; // Could peek at data, so socket is still open
                            if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                                continue; // Couldn't peek, but socket is still open

                            // Socket is closed, close and remove from the map
                            FD_CLR(private_fd, &master_fd_set);
                            close(private_fd); 
                            port_to_private_fd_map_.erase(s);  
                        }

                        // Connect to the internal port of the server
                        int client_fd;
                        try
                        {
                            client_fd = connect_to_server_(server);
                        }
                        catch (const std::exception &e)
                        {
                            fprintf(stderr, "KVPrimaryThread: Exception: %s\n", e.what());
                            continue;
                        }

                        if (client_fd >= 0)
                        {
                            client_fds.push_back(client_fd);
                            FD_SET(client_fd, &master_fd_set);

                            // Store the mapping of public host and port to private fd
                            port_to_private_fd_map_[s] = client_fd;
                        }
                    }
                }
            }
            // else 
            // {
            //     // Secondaries should not have connections open to the interal ports of the other nodes
            //     for (auto it = port_to_private_fd_map_.begin(); it != port_to_private_fd_map_.end();)
            //     {
            //         // Close socket and remove from the map
            //         FD_CLR(it->second, &master_fd_set);
            //         close(it->second);
            //         client_fds.erase(std::remove(client_fds.begin(), client_fds.end(), it->second), client_fds.end());
            //         it = port_to_private_fd_map_.erase(it);
            //     }
            // }

            // TODO: If your not the primary, make sure to close the connections to the replicas
            // When and if you happen to become the primary again, you can reconnect to them.
            read_fd_set = master_fd_set;

            // Timeout: 1 second
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            int activity = select(max_fd + 1, &read_fd_set, nullptr, nullptr, &timeout);
            if (activity < 0)
            {
                throw std::runtime_error("KVPrimaryThread: Select Error " + std::string(strerror(errno)) + "\n");
            }
            else if (activity == 0)
            {
                // Timeout occurred, no incoming connection, loop again
                if (DEBUG) fprintf(stderr, "KVPrimaryThread: No activity on sockets, continuing...\n");
                continue;
            }

            // Check for new connection on the listening socket
            if (FD_ISSET(socket_fd_, &read_fd_set))
            {
                int client_fd = accept(socket_fd_, nullptr, nullptr);

                if (DEBUG)
                    fprintf(stderr, "KVPrimaryThread: New connection on listening socket %d: %d\n", socket_fd_, client_fd);
                
                if (client_fd < 0)
                {
                    fprintf(stderr, "KVPrimaryThread: Failed to accept new connection (%s)\n", strerror(errno));
                    continue;
                }

                if (client_fd >= FD_SETSIZE)
                {
                    fprintf(stderr, "KVPrimaryThread: Too many clients connected. Max clients: %d\n", FD_SETSIZE);
                    close(client_fd);
                    continue;
                }

                client_fds.push_back(client_fd);
                FD_SET(client_fd, &master_fd_set);
                if (client_fd > max_fd)
                    max_fd = client_fd;

                if (DEBUG)
                    fprintf(stderr, "KVPrimaryThread: New connection accepted: %d\n", client_fd);

                continue;
            }

            for (auto it = client_fds.begin(); it != client_fds.end();)
            {
                int client_fd = *it;
                bool remove_client = false;

                if (FD_ISSET(client_fd, &read_fd_set))
                {
                    if (DEBUG)
                        fprintf(stderr, "KVPrimaryThread: Stuff on client %d\n", *it);
                    try
                    {
                        DispatcherResponse response = {DispatcherStatusCode::READING_DATA, ""};
                        while (response.first == DispatcherStatusCode::READING_DATA)
                        {
                            fprintf(stderr, "KVPrimaryThread: Reading data from client %d\n", client_fd);
                            std::string complete_message = read_data_(client_fd);

                            if (DEBUG) truncated_print("KVPrimaryThread: Received message from client", complete_message, client_fd);

                            response = command_dispatcher_.dispatch(complete_message);
                        }

                        int respond_to_fd = client_fd;

                        if (response.first == DispatcherStatusCode::TO_PT)
                        {
                            // We need to find the corresponding private fd based on the public port of the replica node
                            // Get the host and port from the message. Note that any DispatcherStatusCode::TO_PT response, it must have the first argument to be the host and port of the request initiator
                            string message = response.second;
                            size_t space_pos = message.find(' ');
                            size_t second_space_pos = message.find(' ', space_pos + 1);
                            std::string host_port = message.substr(space_pos + 1, second_space_pos - space_pos - 1);

                            // If its me, then I need to dispatch the message directly instead of resending it to myself
                            if (host_port == my_host_port)
                            {
                                command_dispatcher_.dispatch(message);
                                ++it;
                                continue;
                            }

                            // TODO: CHECK IF HOST_PORT IS IN THE MAP
                            if (port_to_private_fd_map_.find(host_port) == port_to_private_fd_map_.end())
                            {
                                fprintf(stderr, "KVPrimaryThread: Failed to find private fd for host and port %s\n", host_port.c_str());
                                ++it;
                                continue;
                            }

                            // Use the host and port to find the corresponding private fd
                            respond_to_fd = port_to_private_fd_map_[host_port];
                        }

                        if (response.second != "")
                        {
                            SocketWriter writer(respond_to_fd);
                            writer.write_message(response.second);
                            if (DEBUG) truncated_print("KVPrimaryThread S:", response.second, client_fd);
                        }

                        if (response.first == DispatcherStatusCode::QUIT)
                        {
                            remove_client = true;
                        }
                    }
                    catch (const std::exception &e)
                    {
                        if (DEBUG)
                            fprintf(stderr, "KVPrimaryThread: Exception from client %d: %s\n", client_fd, e.what());
                        
                        remove_client = true;
                    }
                }

                if (remove_client)
                {

                    if (DEBUG)
                        fprintf(stderr, "KVPrimaryThread closing socket: %d\n", client_fd);

                    close(client_fd);
                    FD_CLR(client_fd, &master_fd_set);
                    it = client_fds.erase(it); // Erase and advance

                    for (auto &map_entry : port_to_private_fd_map_)
                    {
                        if (map_entry.second == client_fd)
                        {
                            port_to_private_fd_map_.erase(map_entry.first); // Mark as closed
                            break;
                        }
                    }
                }
                else
                {
                    ++it;
                }
            }
        }
        catch (const std::exception &e)
        {
            if (DEBUG)
                fprintf(stderr, "KVPrimaryThread: Exception: %s", e.what());

            sleep(1);
        }
    }

    // Only reached if we are shutting down the whole server
    if (DEBUG)
        fprintf(stderr, "--> KVPrimaryThread: Exiting...\n");

    // TODO: Perform a cleanup
    for (auto client_fd : client_fds)
    {
        if (DEBUG) fprintf(stderr, "Closed socket %d", client_fd);
        close(client_fd);
    }

    // This is so the main thread can wait until it is actually done cleaning up
    RUN_KV_PRIMARY_THREAD.store(true);

    return;
}

KVServer KVPrimaryThread::get_primary_node_()
{
    // Check if the current node is the primary node based on *COORDINATOR_SERVICE
    std::map<int, std::vector<KVServer>> map = COORDINATOR_SERVICE->get_kv_servers_map();
    auto it = map.find(config_.rg_id);
    if (it != map.end())
    {
        for (const auto &server : it->second)
        {
            if (server.is_primary)
            {
                return server;
            }
        }
    }
    if (DEBUG)
        fprintf(stderr, "KVPrimaryThread: No primary node found in the replication group\n");
    return KVServer();
}

std::string KVPrimaryThread::read_data_(const int sockfd)
{
    std::string response;
    char buffer[1024];
    const std::string termination = "\r\n";
    size_t term_pos = std::string::npos;

    while (true)
    {
        // Peek at the data without removing it from the kernel buffer
        int peek_bytes_received = recv(sockfd, buffer, sizeof(buffer), MSG_PEEK);
        if (peek_bytes_received < 0)
            throw std::runtime_error("IStorageService: Failed reading bytes from socket " + std::to_string(sockfd) + "\n");

        if (peek_bytes_received == 0)
            throw std::runtime_error("KVPrimaryThread: Connection closed on socket " + std::to_string(sockfd) + "\n");

        std::string chunk(buffer, peek_bytes_received);

        // Check if termination exists in current chunk
        term_pos = chunk.find(termination);
        if (term_pos != std::string::npos)
        {
            // Calculate how many bytes to actually read (termination + its length)
            size_t bytes_to_read = term_pos + termination.length();

            size_t bytes_received = 0;
            while (bytes_received < bytes_to_read)
            {
                // Read the remaining bytes
                size_t read = recv(sockfd, buffer, bytes_to_read - bytes_received, 0);
                if (bytes_received < 0)
                    throw std::runtime_error("KVPrimaryThread: Failed reading bytes from socket " + std::to_string(sockfd) + "\n");

                response.append(buffer, read);
                bytes_received += read;
            }
            break;
        }
        else
        {
            // Read all the peeked data since no termination found
            size_t bytes_received = 0;
            while (bytes_received < peek_bytes_received)
            {
                // Read the remaining bytes
                size_t read = recv(sockfd, buffer, peek_bytes_received - bytes_received, 0);
                if (bytes_received < 0)
                    throw std::runtime_error("KVPrimaryThread: Failed reading bytes from socket " + std::to_string(sockfd) + "\n");

                response.append(buffer, read);
                bytes_received += read;
            }
        }
    }
    return response.substr(0, response.size() - termination.length());
}

void KVPrimaryThread::bind_and_listen_()
{
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1)
    {
        fprintf(stderr, "Failed to create socket\n");
        return;
    }

    fprintf(stderr, "KVPrimaryThread: Created socket %d\n", serverSocket);

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(config_.portno + 1); // The internal port number is the port number + 1

    int optval = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (::bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
    {
        fprintf(stderr, "KVPrimaryThread: Failed to bind the socket\n");
        close(serverSocket);
        return;
    }

    if (listen(serverSocket, 5) == -1)
    {
        fprintf(stderr, "KVPrimaryThread: Failed to listen\n");
        close(serverSocket);
        return;
    }

    socket_fd_ = serverSocket;
}

int KVPrimaryThread::connect_to_server_(const KVServer &server)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        throw std::runtime_error("Failed to create socket for server " + server.host + ":" + std::to_string(server.port));
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server.port_gc);
    inet_pton(AF_INET, server.host.c_str(), &server_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(sockfd);
        throw std::runtime_error("Failed to connect to server " + server.host + ":" + std::to_string(server.port));
    }

    return sockfd;
}