#include "mail-service.h"
#include "common.h"
#include "cookie-handler.h"
#include "drive-handlers.h"
#include "utils.h"

#include <iostream>
#include <string>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <ctime>

MailService::MailService(const MailConfig& config):config(config) {

}

MailService::~MailService() {
    // destructor code
    if (pop3Socket >= 0) close(pop3Socket);
    if (smtpSocket >= 0) close(smtpSocket);
}

bool MailService::connectToPop3Server()
{
    // Need to log in with USER and PASS commands.
    pop3Socket = socket(AF_INET, SOCK_STREAM, 0);
    // std::cout << "[DEBUG] Creating socket for POP3 server" << pop3Socket << std::endl;
    if (pop3Socket < 0)
    {
        perror("Socket creation failed");
        return false;
    }
    sockaddr_in pop3Addr;
    memset(&pop3Addr, 0, sizeof(pop3Addr));
    pop3Addr.sin_family = AF_INET;
    pop3Addr.sin_port = htons(config.pop3Port);
    pop3Addr.sin_addr.s_addr = inet_addr(config.pop3Server.c_str());

    std::cout << "[DEBUG] Connecting to POP3 server: " << config.pop3Server << ":" << config.pop3Port << std::endl;

    if (connect(pop3Socket, (struct sockaddr *)&pop3Addr, sizeof(pop3Addr)) < 0)
    {
        perror("Connection to POP3 server failed");
        close(pop3Socket);
        return false;
    }

    // Logic to connect to the POP3 server.
    std::cout << "Connected to POP3 server: " << config.pop3Server << ":" << config.pop3Port << std::endl;

    // Send USER and PASS commands to authenticate.
    std::string response;

    if (!expectOkResponse(pop3Socket, "WELCOME", response))
    {
        std::cerr << "Failed to receive response from POP3 server." << std::endl;
        close(pop3Socket);
        return false;
    }

    std::cout << "[S]: " << response << std::endl;

    // Use cookie handler to get username, and then get pw from the storage service.

    // std::string username = getCookie(,"username");

    std::string userCommand = "USER " + config.username + "\r\n";
    send(pop3Socket, userCommand.c_str(), userCommand.size(), 0);
    std::cout << "[C]: " << userCommand << std::endl;
    if (!expectOkResponse(pop3Socket, userCommand, response))
    {
        std::cerr << "Failed to send USER command." << std::endl;
        close(pop3Socket);
        return false;
    }
    std::cout << config.username << " HAS LOGGED IN!" << std::endl;

    std::string passCommand = "PASS " + config.password + "\r\n";
    send(pop3Socket, passCommand.c_str(), passCommand.size(), 0);
    std::cout << "[C]: " << passCommand << std::endl;
    if (!expectOkResponse(pop3Socket, passCommand, response))
    {
        std::cerr << "Failed to send PASS command." << std::endl;
        close(pop3Socket);
        return false;
    }
    std::cout << "[DEBUG] PASS command sent successfully" << std::endl;
    return true;
}

bool MailService::connectToSmtpServer() {
    // Logic to connect to the SMTP server.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    smtpSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (smtpSocket < 0)
    {
        perror("Socket creation failed");
        return false;
    }
    sockaddr_in smtpAddr;
    memset(&smtpAddr, 0, sizeof(smtpAddr));
    smtpAddr.sin_family = AF_INET;
    smtpAddr.sin_port = htons(config.smtpPort);
    smtpAddr.sin_addr.s_addr = inet_addr(config.smtpServer.c_str());

    if (connect(smtpSocket, (struct sockaddr *)&smtpAddr, sizeof(smtpAddr)) < 0)
    {
        perror("Connection to SMTP server failed");
        close(smtpSocket);
        smtpSocket = -1;
        return false;
    }

    std::string ehloCommand = "HELO penncloud.upenn.edu\r\n";
    send(smtpSocket, ehloCommand.c_str(), ehloCommand.size(), 0);
    std::string response;
    if (!expectOkResponse(smtpSocket, "HELO", response))
    {
        std::cerr << "Failed to send HELO command." << std::endl;
        close(smtpSocket);
        smtpSocket = -1;
        return false;
    }
    // std::cout << "Connected to SMTP server: " << config.smtpServer << ":" << config.smtpPort << std::endl;
    return true;
}

bool MailService::connectToServers()
{
    // Connect to the POP3 server.
    std::cout << "[DEBUG] Connecting to POP3 server..." << std::endl;
    if (!connectToPop3Server())
    {
        std::cerr << "Failed to connect to POP3 server." << std::endl;
        return false;
    }
    // Connect to the SMTP server.
    std::cout << "[DEBUG] Connecting to SMTP server..." << std::endl;
    if (!connectToSmtpServer())
    {
        std::cerr << "Failed to connect to SMTP server." << std::endl;
        return false;
    }
    return true;
}

bool MailService::readEmail(int emailId, std::string &response, std::unordered_map<std::string, std::unordered_map<std::string, std::string> > &emailList)
{
    // Logic to read an email from the POP3 server.
    std::string request = "RETR " + std::to_string(emailId) + "\r\n";
    send(pop3Socket, request.c_str(), request.size(), 0);
    // std::cout << "Reading email with ID: " << emailId << std::endl;
    if (!expectOkResponse(pop3Socket, request, response))
    {
        std::cerr << "Failed to send RETR command." << std::endl;
        return false;
    }
    // Parse the raw response to extract from, to, subject, and body.
    std::istringstream responseStream(response);
    std::string line;
    auto& emailDetails = emailList[std::to_string(emailId)];
    bool inBody = false;

    while (std::getline(responseStream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line == ".") {
            break; // End of message, don't include
        }

        // Unstuff lines starting with ".."
        if (line.rfind("..", 0) == 0) {
            line.erase(0, 1);  // RFC 5321 dot-stuffing
        }

        if (!inBody) {
            if (line.empty()) {
                inBody = true;
                continue;
            }
            size_t colonPos = line.find(":");
            if (colonPos != std::string::npos) {
                std::string key = line.substr(0, colonPos);
                std::string value = line.substr(colonPos + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                if (!value.empty() && value.front() == '<' && value.back() == '>') {
                    value = value.substr(1, value.size() - 2);
                }
                emailDetails[key] = value;
                std::cout << "[DEBUG] " << key << ": " << value << std::endl;
            }
        } else {
            emailDetails["Body"] += line + "\n";
        }
    }
    // send(pop3Socket, "QUIT\r\n", 6, 0);
    // expectOkResponse(pop3Socket, "QUIT", response);
    // close(pop3Socket);
    // pop3Socket = -1;

    // Trim the body to remove trailing newlines and CRLF.
    std::cout << "[DEBUG] Email body before trimming: " << emailDetails["Body"] << " size: "<< emailDetails["Body"].size() << std::endl;
    if (emailDetails["Body"].size() >= 5 && emailDetails["Body"].compare(emailDetails["Body"].size() - 5, 5, "\r\n.\r\n") == 0)
    {
        emailDetails["Body"].erase(emailDetails["Body"].size() - 5);
    }
    return true;
}

bool MailService::fetchEmails(std::string &response,std::unordered_map<std::string, std::unordered_map<std::string, std::string> > &emailList, int page, int limit)
{
    // Logic to fetch emails from the POP3 server.
    int start = (page - 1) * limit + 1;
    int end = page * limit;

    std::string request = "UIDL\r\n";
    send(pop3Socket, request.c_str(), request.size(), 0);
    std::cout << "[DEBUG] Fetching emails from POP3 server..." << std::endl;

    if (!expectOkResponse(pop3Socket, request, response))
    {
        std::cerr << "Failed to send UIDL command." << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Response: " << response << std::endl;

    // Only want to look at emails from start to end.
    // Need to parse the response to get the email list.

    // Need to parse the response to RETR for every email uidl.
    std::istringstream responseStream(response);
    std::string line;

    std::vector <std::string> emailIds;

        while (getline(responseStream, line)) {
            std::cout << "[DEBUG] Processing line: " << line << std::endl;
            if (!line.empty() && line.back() == '\r') {
                line.pop_back(); // Trim CR
            }

            // std::cout << "Processing line: " << line << std::endl;

            if (line == "+OK" || line == "-ERR" || line.empty() || line == ".") {
                continue;
            }
            std::string emailBody;
            // Extract the email ID from the line.
            std::string emailId = line.substr(0, line.find(" "));
            // std::unordered_map<std::string, std::unordered_map<std::string, std::string> > emailDetails;
            std::cout << "Header information for Email ID: " << emailId << std::endl;
            if (!readEmail(std::stoi(emailId), emailBody, emailList)) {
                std::cerr << "Failed to read email with ID: " << line << std::endl;
                continue;
            }

            // std::cout << "Email body: " << emailBody << std::endl;
            // Need to parse the email body to get the uid, subject, from, to. and store in the emailList.
        }

    send(pop3Socket, "QUIT\r\n", 6, 0);
    expectOkResponse(pop3Socket, "QUIT", response);
    close(pop3Socket);
    pop3Socket = -1;
    return true;
}

bool MailService::receiveResponse(int socket, std::string &response)
{
    char buffer[1024];
    response.clear();

    while (true) {
        ssize_t bytesRead = recv(socket, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead < 0)
        {
            perror("Receive failed");
            return false;
        }
        buffer[bytesRead] = '\0';
        response += buffer;

        if (response.size() >= 2 &&
            response.substr(response.size() - 2) == "\r\n") {
            return true;
        }
    }
}

bool MailService::sendEmail(const std::string &body)
{


    auto params = parseFormEncoded(body);
    std::string action = params["actionType"];
    std::cout << "[DEBUG] Action: " << action << std::endl;

    std::string sender = config.username + "@penncloud.upenn.edu";
    std::string recp = params["to"];
    std::string sub = params["subject"];
    std::cout << "[DEBUG sending email] Subject: " << sub << std::endl;
    if (sub.empty()) {
        sub = "(No Subject)";
    }
    std::string body_ = params["body"];
    std::string time = formatCurrentDateTime();
    std::string response;

    std::string MAIL = "MAIL FROM:<" + sender + ">\r\n";
    send(smtpSocket, MAIL.c_str(), MAIL.size(), 0);
    if (!expectOkResponse(smtpSocket, MAIL, response)) {
        std::cerr << "Failed to send MAIL command.\n";
        return false;
    }

    std::string RECP = "RCPT TO:<" + recp + ">\r\n";
    send(smtpSocket, RECP.c_str(), RECP.size(), 0);
    if (!expectOkResponse(smtpSocket, RECP, response)) {
        std::cerr << "Failed to send RCPT command.\n";
        return false;
    }

    std::cout << "Sending to " << recp << std::endl;

    std::string DATA = "DATA\r\n";
    send(smtpSocket, DATA.c_str(), DATA.size(), 0);
    if (!expectOkResponse(smtpSocket, DATA, response)) {
        std::cerr << "Failed to send DATA command.\n";
        return false;
    }

    std::string DATA_BODY =
        "From: <" + sender + ">\r\n"
        "To: <" + recp + ">\r\n"
        "Date: " + time + "\r\n"
        "Subject: " + sub + "\r\n\r\n" +
        body_ + "\r\n.\r\n";

    std::cout << "Sending email body:\n" << DATA_BODY << std::endl;

    send(smtpSocket, DATA_BODY.c_str(), DATA_BODY.size(), 0);
    if (!expectOkResponse(smtpSocket, DATA_BODY, response)) {
        std::cerr << "Failed to send email body.\n";
        return false;
    }

    std::cout << "Email sent successfully.\n";
    send(smtpSocket, "QUIT\r\n", 6, 0);
    if (!expectOkResponse(smtpSocket, "QUIT\r\n", response)) {
        std::cerr << "Failed to send QUIT command.\n";
        return false;
    }
    std::cout << "Disconnected from SMTP server.\n";
    close(smtpSocket);
    smtpSocket = -1; // Reset the socket to indicate it's closed.
    return true;
}

bool MailService::deleteEmail(int emailId)
{
    /* This is only marked for deletion. It is only deleted after quit command is received.*/
    std::string request = "DELE " + std::to_string(emailId) + "\r\n";
    std::string response;
    send(pop3Socket, request.c_str(), request.size(), 0);
    std::cout << "[DEBUG] Deleting email with ID: " << emailId << std::endl;

    if (!expectOkResponse(pop3Socket, request, response))
    {
        std::cerr << "Failed to send DELE command." << std::endl;
        return false;
    }

    /* Send QUIT command to finalize the transaction. */
    std::string quitCommand = "QUIT\r\n";
    send(pop3Socket, quitCommand.c_str(), quitCommand.size(), 0);
    std::cout << "[DEBUG] Sending QUIT command..." << std::endl;
    if (!expectOkResponse(pop3Socket, quitCommand, response))
    {
        std::cerr << "Failed to send QUIT command." << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Email deleted successfully." << std::endl;
    return true;
}

bool MailService::expectOkResponse(int socket, const std::string &command, std::string &response)
{
    // std::string response;
    response.clear();
    if (!receiveResponse(socket, response))
    {
        std::cerr << "Failed to receive response for command: " << command << std::endl;
        return false;
    }
    std::cout << "[S] Response: " << response << std::endl;
    if (response.rfind("-ERR", 0) == 0)
    {
        std::cerr << "Error in response: " << response << std::endl;
        return false;
    }
    return true;
}

bool MailService::sendCommand(int socket, const std::string &command)
{
    return (send(socket, command.c_str(), command.size(), 0) >= 0);

}

std::string MailService::urlDecode(const std::string &str) {
    std::string result;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%') {
            if (i + 2 < str.size()) {
                std::string hex = str.substr(i + 1, 2);
                char decodedChar = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
                result += decodedChar;
                i += 2;
            } else {
                // Malformed % — just append it as-is
                result += '%';
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

std::string MailService::formatCurrentDateTime() {
    std::time_t now = std::time(nullptr);
    std::tm tm_local = *std::localtime(&now); // Use gmtime(&now) if you want UTC

    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S %z", &tm_local);
    return std::string(buffer);
}