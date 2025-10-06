#include <set>
#include <iostream>
#include <vector>
#include <string>
#include <ctime>
#include <unistd.h>
#include <signal.h>
#include <resolv.h>
#include <netdb.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include "SocketWriter.h"
#include "IStorageService.h"
#include "SmtpCommandDispatcher.h"
#include "Globals.h"
// #include "../../Shared/SharedStructures.h"
#include "relay.h"

void* relay_main(void *args)
{
    RemoteEmail *email = static_cast<RemoteEmail *>(args);

    fprintf(stdout, "Reached relay_main\n");
    
    std::map<std::string, std::vector<sockaddr_in>> mail_servers;
    for (const auto &recipient : email->recipients)
    {
        fprintf(stdout, "Recipient: %s@%s\n", recipient.user.c_str(), recipient.domain.c_str());
        RelayStatus status;
        if (mail_servers.find(recipient.domain) == mail_servers.end())
        {
            mail_servers[recipient.domain] = std::vector<sockaddr_in>();
            status = resolve_mailserver_ip(recipient.domain, mail_servers[recipient.domain]);
            if (status != RelayStatus::OK)
            {
                fprintf(stderr, "Error resolving mail server for %s@%s\n", recipient.user.c_str(), recipient.domain.c_str());
                error_mail(status, email->user_from.user, email->mail_content, recipient);
                continue;
            }
            fprintf(stdout, "Resolved mail server for %s@%s\n", recipient.user.c_str(), recipient.domain.c_str());
        }

        for (const auto &mail_server: mail_servers[recipient.domain])
        {
            fprintf(stdout, "Trying to send mail to %s@%s\n", recipient.user.c_str(), recipient.domain.c_str());
            status = send_remote_mail(email->user_from, recipient, mail_server, email->mail_content);
            if (status != RelayStatus::OK)
            {
                fprintf(stderr, "Error sending mail to %s@%s\n", recipient.user.c_str(), recipient.domain.c_str());
                error_mail(status, email->user_from.user, email->mail_content, recipient);
                continue;
            }  
            else
            {
                fprintf(stdout, "Mail sent to %s@%s\n", recipient.user.c_str(), recipient.domain.c_str());
                break;
            }
        }
    }

    delete email;

    return nullptr;
}

RelayStatus resolve_mailserver_ip(const std::string &domain, std::vector<sockaddr_in> &addresses)
{
    struct __res_state state_storage; 
    res_state resolve_state = &state_storage;
    res_ninit(resolve_state);
    unsigned char response[NS_PACKETSZ];
    
    // Make maximum 3 attempts to resolve the MX records
    int response_len;
    int attempts{0};
    while (true)
    {
        response_len = res_nquery(resolve_state, domain.data(), ns_c_in, ns_t_mx, response, sizeof(response));
        if (response_len > 0) break;

        // Error handling
        switch (h_errno)
        {
        case NO_ADDRESS:
            res_nclose(resolve_state);
            return RelayStatus::MX_RECORD_NOT_FOUND;
        case HOST_NOT_FOUND:
            res_nclose(resolve_state);
            return RelayStatus::DOMAIN_NOT_FOUND;
        case TRY_AGAIN: // Wait shortly and continue with loop
            if (attempts++ < 3)
            {
                sleep(100);
                break;
            }
            else
            {
                res_nclose(resolve_state);
                return RelayStatus::DNS_CONNECTION_FAILED;
            }
        default:
            res_nclose(resolve_state);
            return RelayStatus::DNS_CONNECTION_FAILED;
        }
    }
    res_nclose(resolve_state);

    // Initialize the DNS parser
    ns_msg handle;
    if (ns_initparse(response, response_len, &handle) < 0)
        return RelayStatus::DNS_CONNECTION_FAILED;

    // Parse the DNS response and save priority and host name in an (ordered) set
    ns_rr rr;
    std::set<std::pair<int, char[NS_MAXDNAME]>> servers;
    int answer_count = ns_msg_count(handle, ns_s_an);
    for (int i{0}; i < answer_count; ++i)
    {
        std::pair<int, char[NS_MAXDNAME]> tmp_server;
        if (ns_parserr(&handle, ns_s_an, i, &rr))
            continue;

        const unsigned char *rdata = ns_rr_rdata(rr);
        // The first two bytes of the rdata are the priority
        tmp_server.first = ns_get16(rdata);

        // The rest of the rdata is the host name
        if (ns_name_uncompress(ns_msg_base(handle), ns_msg_end(handle), rdata + 2,
                               tmp_server.second, NS_MAXDNAME) < 0)
            continue;

        servers.insert(tmp_server);
    }

    if (servers.empty()) return RelayStatus::DNS_CONNECTION_FAILED;

    // Resolve host names to IP addresses
    addrinfo hints{};
    hints.ai_family = AF_INET;          // IPv4
    hints.ai_socktype = SOCK_STREAM;    // Stream socket
    struct addrinfo *result;
    for (const auto &server : servers)
    {
        if (getaddrinfo(server.second, NULL, &hints, &result))
            continue;

        sockaddr_in addr = *reinterpret_cast<sockaddr_in *>(result->ai_addr);
        addr.sin_port = htons(25); // SMTP port
        addr.sin_family = AF_INET;
        addresses.push_back(addr);

        freeaddrinfo(result);
    }
    if (addresses.empty()) return RelayStatus::DNS_CONNECTION_FAILED;

    return RelayStatus::OK;
}

RelayStatus send_remote_mail(const EmailAddress &user_from, const EmailAddress &recipient,
                             const sockaddr_in remote_server, const std::string &mail_content)
{
    // Open stream socket
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0)
    {
        fprintf(stderr, "Error creating socket\n");
        return RelayStatus::MAIL_CONNECTION_FAILED;
    }

    // Connect to remote mail server
    if (connect(sfd, reinterpret_cast<const sockaddr *>(&remote_server), sizeof(remote_server)) < 0)
    {
        fprintf(stderr, "Error connecting to remote server\n");
        close(sfd);
        return RelayStatus::MAIL_CONNECTION_FAILED;
    }

    // Instatiate SocketWriter
    SocketWriter writer(sfd);
    std::string response;

    // Write HELO message
    std::string message{"HELO " + user_from.domain + "\r\n"};
    ssize_t size_written = writer.write_message(message);
    if (size_written < 0)
    {
        fprintf(stderr, "Error writing HELO message to socket\n");
        close(sfd);
        return RelayStatus::MAIL_CONNECTION_FAILED;
    }

    // Wait for server response
    char buffer[1024];
    while (response.find("\r\n250") == std::string::npos)
    {
        ssize_t bytes_received = recv(sfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received < 0)
        {
            fprintf(stderr, "Error receiving response from server\n");
            close(sfd);
            return RelayStatus::MAIL_CONNECTION_FAILED;
        }
        response += std::string(buffer, bytes_received);
    }
    response.clear();

    // Send all other commands in one go
    message = "MAIL FROM:<" + user_from.user + "@" + user_from.domain +
              ">\r\nRCPT TO:<" + recipient.user + "@" + recipient.domain +
              ">\r\nDATA\r\n" + mail_content + "\r\n.\r\nQUIT\r\n";

    size_written = writer.write_message(message);
    if (size_written < 0)
    {
        fprintf(stderr, "Error writing MAIL message to socket\n");
        close(sfd);
        return RelayStatus::MAIL_CONNECTION_FAILED;
    }

    close(sfd);
    return RelayStatus::OK;
}

void error_mail(const RelayStatus status, const std::string &user, const std::string &mail_content, const EmailAddress &recipient)
{
    std::string error_message;
    switch (status)
    {
        case RelayStatus::DOMAIN_NOT_FOUND:
            error_message = "Provided domain could not be found";
            break;
        case RelayStatus::MX_RECORD_NOT_FOUND:
            error_message = "MX record not be found for provided domain";
            break;
        case RelayStatus::DNS_CONNECTION_FAILED:
            error_message = "Internal connection error during DNS resolution";
            break;
        case RelayStatus::MAIL_CONNECTION_FAILED:
            error_message = "Internal connection error during mail server connection";
            break;
        default:
            error_message = "Unknown error";
            break;
    }

    std::time_t time = std::time(nullptr);
    std::tm *localtime = std::localtime(&time);
    IStorageService storage(COORDINATOR_SERVICE->get_kv_servers_map());
    std::string failure_email
    {
        "From: <EMAIL-RELAY@penncloud.upenn.edu>\r\n"
        "To: <" + user + "@penncloud.upenn.edu>\r\n"
        "Subject: Mail delivery failed\r\n"
        "Date: " + asctime(localtime) +
        "\r\n\r\n"
        "The following error occurred while trying to deliver your email to <" + recipient.user + "@" + recipient.domain + ">:\r\n" +
        error_message + "\r\n\r\n---------- Forwarded message ----------\r\n" + mail_content
    };

    storage.put(user_to_row(user) + "-MAILBOX", uuid::generate_uuid_v4(), IStorageService::from_string(failure_email));
}

