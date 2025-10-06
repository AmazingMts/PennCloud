#ifndef FRONTEND_SERVER_H
#define FRONTEND_SERVER_H

/*
Design of a frontend server:
1. A frontend server is a server that takes in requests from clients and forwards them to the backend servers.
2. When a frontend server is first spun up, it will register itself with the load balancer.
3. Main functionalities:
    a. Email requests:
        - View all emails
        - View email by ID
        - Send email
        - Delete email
        - Forward email
    b. Drive requests:
        - upload files
        - download files
    c. User account:
        - Create user account
        - Log in
        - Log out?
        - Authnticate
    d. Admin console:
        - Display bigtable data
        - Display all nodes
        - Enable and Disable
4. Serve static HTML files to browser.
5. Routes:
    - /email
    - /drive
    - /user
    - /admin
*/

#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unordered_map>
#include <pthread.h>
#include <mutex>
#include <deque>
#include <vector>

#include "common.h"
#include "mail-service.h"
#include "email-handlers.h"

class FrontendServer
{
friend void* workerLoop(void* arg);

public:
    FrontendServer(
        const std::string &ip,
        int port
        // const std::string &lbIp,
        // int lbPort,
        // int numThreads,
        // bool debug = false,
        // CoordinatorService *coordinatorService = nullptr
    );
    ~FrontendServer();
    void run();
    void stop();

    static std::string readFile(std::string &filepath);

    static void sendResponse(std::string response, clientContext *client, std::string statusCode = "200 OK");

    static void sendHeadResponse(clientContext *client, ssize_t contentLength, std::string statusCode = "200 OK");

    // Function to fetch data from the backend server.
    void routeToBackend(
        std::string method,
        std::string path,
        std::string body,
        clientContext *client,
        const std::string &backendAddr, std::string &response);


private:
    std::string serverIp;
    int serverPort;
    int coordinatorSocket;
    sockaddr_in coordinatorAddr;

    std::unordered_map<pthread_t, clientContext*> clientMap;

    // task queue
    std::deque<clientContext*> taskQueue;

    // thread pool
    std::vector<pthread_t> threadPool;
    pthread_mutex_t taskQueueMutex;
    pthread_cond_t taskQueueCond;

    // void *workerLoop(void *arg);

    void deregisterWithLoadBalancer();
    void serveStaticFiles();
    static void routeRequests(std::string method, std::string route, std::string body, std::unordered_map<std::string, std::string> header, clientContext *client);
    // static void handleEmailRequests(clientContext *client, std::string method, std::string path);
    // static void handleDriveRequests(clientContext *client, std::string method, std::string path, std::string body, std::string username);
    static void handleAdminRequests(clientContext *client, std::string method, std::string path);
    static void handleLBControlMessages();


    void handleClient(clientContext* client);

    // static void *handleClient(void *clientSocket);

    /* Functions to send data back to backend. */
    // Function to query coordinator to locate the correct backend server.
    std::string queryCoordinator(std::string key);
    void formatRequest(std::string method, std::string path, std::string body, std::string &request);

    void queryBackend(std::string request, std::string &response);

    static void sendRedirectResponse(clientContext *client, const std::string &location, const std::string &htmlResponse);

    // startCoordinatorListener();
};

#endif