#ifndef MAIL_SERVICE_H
#define MAIL_SERVICE_H

#include <iostream>
#include <string>
#include <unordered_map>
// mail-service handles connection to POP3/SMTP servers.
struct MailConfig
{
    std::string pop3Server = "127.0.0.1";
    int pop3Port = 900;
    std::string smtpServer = "127.0.0.1";
    int smtpPort = 800;
    std::string username;
    std::string password;
};

class MailService
{
    public:
        MailService(const MailConfig& config);
        ~MailService();

        bool connectToServers();

        // Send an email.
        bool sendEmail(const std::string &body);

        // Fetch emails from the POP3 server.
        bool fetchEmails(std::string &response, std::unordered_map<std::string, std::unordered_map<std::string, std::string> > &emailList, int page, int limit);

        // Read an email.
        bool readEmail(int emailId, std::string &response, std::unordered_map<std::string, std::unordered_map<std::string, std::string> > &emailList);

        bool deleteEmail(int emailId);

        // Disconnect from the POP3 server.
        void disconnectPop3();

        // Disconnect from the SMTP server.
        void disconnectSmtp();

        // Receiving response from the server.
        bool receiveResponse(int socket, std::string &response);

        // Getters and Setters
        int getPop3Socket() {
            return pop3Socket;
        }

    private:
        MailConfig config;
        // std::string pop3Server = "127.0.0.1";
        // int pop3Port = 8001;
        // std::string smtpServer = "127.0.0.1";
        // int smtpPort = 8002;
        int pop3Socket = -1;
        int smtpSocket = -1;

        // Connect to the POP3 server.
        bool connectToPop3Server();

        // Connect to the SMTP server.
        bool connectToSmtpServer();

        // Add any other necessary member variables here.
        bool expectOkResponse(int socket, const std::string &command, std::string &response);
        bool sendCommand(int socket, const std::string &command);

        // std::unordered_map<std::string, std::string> parseFormEncoded(const std::string& body);
        std::string urlDecode(const std::string &str);
        std::string formatCurrentDateTime();
};

#endif // MAIL_SERVICE_H