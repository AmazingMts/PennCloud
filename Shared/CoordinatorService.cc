#include "CoordinatorService.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sstream>
#include <cstring>
#include <regex>
#include <csignal>
#include "SimpleSocketReader.h"

CoordinatorService::CoordinatorService(std::string coordinator_host, int coordinator_port, bool debug)
    : coordinator_host_(coordinator_host), coordinator_port_(coordinator_port), debug_(debug)
{
    if (debug_)
        printf("CoordinatorService initialized with host: %s, port: %d, debug: %s\n",
               coordinator_host_.c_str(), coordinator_port_, debug_ ? "true" : "false");
};

CoordinatorService::~CoordinatorService()
{
    // Destructor implementation
}

void CoordinatorService::start(std::atomic<bool> &run)
{
    while (run.load())
    {
        // Simulate delay to test wait_until_ready
        // sleep(10);

        get_kv_servers_();

        // Print the result for demonstration
        if (debug_)
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

        // Notify that the CoordinatorService is ready (i.e., running)
        {
            std::shared_lock<std::shared_mutex> map_lock(kv_map_mutex_);
            if (!kv_servers_map_.empty())
            {
                std::unique_lock<std::mutex> lock(coord_service_ready_mutex_);
                ready_cv_.notify_all();
            }
        }

        sleep(2);
    }

    // Only reached if we are shutting down the whole server
    if (debug_)
        fprintf(stderr, "--> Coordinator Service: Exiting...\n");

    // TODO: Perform a cleanup

    // This is so the main thread can wait until it is actually done cleaning up
    run.store(true);

    return;
}

std::map<int, std::vector<KVServer>> CoordinatorService::get_kv_servers_map()
{
    std::shared_lock<std::shared_mutex> lock(kv_map_mutex_);
    return kv_servers_map_;
}

void CoordinatorService::wait_until_ready()
{
    std::unique_lock<std::mutex> lock(coord_service_ready_mutex_);
    ready_cv_.wait(lock, [this]
                   {
        std::shared_lock<std::shared_mutex> map_lock(kv_map_mutex_);
        return !kv_servers_map_.empty(); });
}

void CoordinatorService::get_kv_servers_()
{
    // TODO: Try catch instead of failing
    // Make TCP connection to the Coordinator server
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {

        fprintf(stderr, "Error creating socket for Coordinator Server\n");
        return;
    }
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(coordinator_port_);
    inet_pton(AF_INET, coordinator_host_.c_str(), &serv_addr.sin_addr);
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        fprintf(stderr, "Error connecting to Coordinator Server\n");
        close(sockfd);
        return;
    }

    // Read the initial response from the Coordinator server
    // std::string initial_response = read_response_(sockfd);
    std::string initial_response;
    try
    {
        initial_response = SimpleSocketReader::read(sockfd);
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Coordinator Service: %s\n", e.what());
        close(sockfd);
        return;
    }

    if (debug_)
    {
        fprintf(stderr, "Received initial response: %s\n", initial_response.c_str());
    }

    // Send a request to the Coordinator server
    std::string request = "KVLIST\r\n";
    send(sockfd, request.c_str(), request.size(), 0);
    // std::string kvlist_response = read_response_(sockfd);
    std::string kvlist_response;
    try
    {
        kvlist_response = SimpleSocketReader::read(sockfd);
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Coordinator Service: %s\n", e.what());
        close(sockfd);
        return;
    }

    // Parse the response to extract KVServer information and fill in the map
    std::map<int, std::vector<KVServer>> kv_servers_map;
    std::istringstream iss(kvlist_response);
    std::string line;
    while (std::getline(iss, line))
    {
        // Example: "1:(localhost:8080,true)(localhost:8081,true)(localhost:8082,true)\n2:(localhost:8084,true)(localhost:8085,true)(localhost:8086,true)\n\r\n"
        std::regex group_rgx(R"((\d+:)|(\([^)]+\)))");
        std::smatch group_match;
        if (std::regex_search(line, group_match, group_rgx))
        {
            int group_id = std::stoi(group_match[1].str());
            std::vector<KVServer> servers;
            std::regex server_rgx(R"(\(([^:]+):(\d+),(true|false),(true|false)\))");
            std::sregex_iterator it(line.begin(), line.end(), server_rgx);
            std::sregex_iterator end;
            while (it != end)
            {
                KVServer server;
                server.host = (*it)[1].str();
                server.port = std::stoi((*it)[2].str());
                server.is_primary = ((*it)[3].str() == "true");
                server.is_alive = ((*it)[4].str() == "true");
                server.port_gc = server.port + 1;
                servers.push_back(server);
                ++it;
            }

            kv_servers_map[group_id] = servers;
        }

        // Set the map to the class member variable
        {
            std::unique_lock<std::shared_mutex> lock(kv_map_mutex_);
            kv_servers_map_ = kv_servers_map;
        }
    }

    // Call QUIT to close the connection
    std::string quit_request = "QUIT\r\n";
    send(sockfd, quit_request.c_str(), quit_request.size(), 0);
    // std::string quit_response = read_response_(sockfd);
    std::string quit_response;
    try
    {
        quit_response = SimpleSocketReader::read(sockfd);
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Coordinator Service: %s\n", e.what());
        close(sockfd);
        return;
    }
    
    if (debug_)
    {
        fprintf(stderr, "Received QUIT response: %s\n", quit_response.c_str());
    }

    close(sockfd);
}

// std::string CoordinatorService::read_response_(int sockfd)
// {
//     char buffer[1024];
//     std::string response;

//     while (true)
//     {
//         int bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
//         if (bytes_received < 0)
//         {
//             fprintf(stderr, "Error receiving data from Coordinator Server\n");
//             break;
//         }

//         buffer[bytes_received] = '\0'; // Null-terminate the received data
//         response += buffer;

//         // Check if the response ends with "\r\n" (indicating the end of the message)
//         if (response.size() >= 2 && response.substr(response.size() - 2) == "\r\n")
//         {
//             break;
//         }
//     }

//     return response;
// }