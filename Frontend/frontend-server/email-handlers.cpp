#include "email-handlers.h"
#include "frontend-server.h"
#include "mail-service.h"
#include "common.h"
#include "../../Shared/IStorageService.h"
#include "drive-handlers.h"
#include "utils.h"

#include <iostream>
#include <string>
#include <sstream>

void handleEmailRequests(clientContext *client, std::string method, std::string path, std::string body, IStorageService *storage, std::string username, std::string password)
{
    MailConfig config = {
        .pop3Server = "127.0.0.1",
        .pop3Port = 900,
        .smtpServer = "127.0.0.1",
        .smtpPort = 800,
        .username = username,
        .password = password,
    };

    /* Requests are stateless, so initiate a mailservice per request.*/
    MailService mailService(config);
    if (!mailService.connectToServers()) {
        std::cerr << "Failed to connect to mail servers\n";
        FrontendServer::sendResponse("500 Internal Server Error", client, "500 Internal Server Error");
        return;
    }

    bool isGet = (method == "GET");
    bool isHead = (method == "HEAD");

    // Handle email requests.
    if (isGet || isHead)
    {
        if (path.rfind("/email/inbox", 0) == 0)
        {
            // Step 2: Fetch email list from POP3
            std::string rawResponse;
            // int page = std::stoi(getQueryParam(path, "page"));
            // int limit = std::stoi(getQueryParam(path, "limit"));

            std::unordered_map<std::string, std::unordered_map<std::string, std::string>> emailList;
            if (!mailService.fetchEmails(rawResponse, emailList, 1, 20))
            {
                if (isGet) {
                    std::cerr << "Failed to send UIDL request to POP3\n";
                    FrontendServer::sendResponse("500 Internal Server Error", client, "500 Internal Server Error");
                    return;
                } else if (isHead) {
                    FrontendServer::sendHeadResponse(client, 0, "500 Internal Server Error");
                }
            }

            // // Step 3: Parse the response to get email list
            // std::cout << "Getting inbox.html\n" << std::endl;
            std::cout << "[STEP3] Fetched email by email ids" << std::endl;
            std::string filePath = "static/email/inbox.html";
            std::string htmlResp = FrontendServer::readFile(filePath);
            std::string emailListHtml;

            std::istringstream responseStream(rawResponse);
            std::string line;

            std::cout << "[DEBUG email-handler] Checking Email List\n" << std::endl;
            for (const auto& email : emailList) {
                std::cout << "[DEBUG email-handler] Email ID: " << email.first << std::endl;
                for (const auto& header : email.second) {
                    std::cout << "[DEBUG email-handler] " << header.first << ": " << header.second << std::endl;
                }
            }
            for (const auto& [index, fields] : emailList) {
                // std::cout << "Serving inbox view: " << std::endl;
                std::string from = fields.at("From");
                // fields.count("From") ? fields.at("From") : "Unknown";
                std::cout << "[DEBUG] From: " << from << std::endl;
                std::string subject = fields.at("Subject");
                // fields.count("Subject") ? fields.at("Subject") : "(No Subject)";
                std::cout << "[DEBUG] Subject: " << subject << std::endl;
                std::string date = fields.count("Date") ? fields.at("Date") : "";

                emailListHtml += "<tr onclick=\"window.location='/email/view?id=" + index + "'\">\n";
                emailListHtml += "<td>" + from + "</td><td>" + subject + "</td><td>" + date + "</td>\n";
                emailListHtml += "</tr>\n";
            }
            // Serve the inbox.html file.
            std::cout << "[DEBUG] Replaced placeholders with details" << std::endl;
            int pos = htmlResp.find("{{$Emails}}");
            // Replace the placeholder {{$Emails}} with the actual email list.
            if (pos != std::string::npos) {
                htmlResp.replace(pos, 11, emailListHtml);
            }
            else
            {
                std::cerr << "Error: Placeholder {{$Emails}} not found in inbox.html\n";
            }

            int pos2 = htmlResp.find("/email/send");
            if (pos2 != std::string::npos) {
                htmlResp.replace(pos2, 11, "/email/send?user=" + config.username);
            }
            else
            {
                std::cerr << "Error: Placeholder /email/send not found in inbox.html\n";
            }

            int pos3 = htmlResp.find("{{username}}");
            if (pos3 != std::string::npos) {
                // htmlResp.replace(pos3, 12, config.username);
                std::string initial = config.username.empty() ? "?" : std::string(1, config.username[0]);
                htmlResp = replacePlaceholder(htmlResp, "{{username}}", initial);
                // htmlResp = replacePlaceholder(htmlResp, "{{username}}", username);
            }
            else
            {
                std::cerr << "Error: Placeholder {{username}} not found in inbox.html\n";
            }
            if (isGet) {
                FrontendServer::sendResponse(htmlResp, client);
            } else if (isHead) {
                FrontendServer::sendHeadResponse(client, htmlResp.size(), "200 OK");
            }

        }
        else if (path.rfind("/email/send", 0) == 0)
        {
        std::string action = getQueryParam(path, "actionType");
        std::string to = urlDecode(getQueryParam(path, "to"));
        std::string subject = urlDecode((getQueryParam(path, "subject")));
        std::string body = urlDecode(getQueryParam(path, "original"));
        std::string timestamp = urlDecode(getQueryParam(path, "time"));

        if (action == "send") {
            std::cout << "[DEBUG] Composing new email" << std::endl;
            // Leave all fields empty
        }
        else if (action == "reply") {
            std::cout << "[DEBUG] Replying to email" << std::endl;
            // to = urlDecode(getQueryParam(path, "to"));
            subject = "Re: " + subject;
            body = "\n\nOn " + timestamp + " <" + to + "> wrote:\n" + body;
        }
        else if (action == "forward") {
            to = urlDecode(getQueryParam(path, "to"));
            std::cout << "[DEBUG] Forwarding email" << std::endl;
            // subject = "Fwd: " + subject;
            body = "\n\n--- Forwarded message ---\n From: " + urlDecode(getQueryParam(path, "from")) + "\n" +
                   "To: " + to + "\n" +
                   "Date: " + timestamp + "\n" +
                   "Subject: " + subject + "\n\n" +
                   body + "\n\n--- End of forwarded message ---\n\n";
        }

        std::cout << "[DEBUG] Serving email_compose.html\n";
        std::string filePath = "static/email/email_compose.html";
        std::string htmlResp = FrontendServer::readFile(filePath);

        htmlResp = replacePlaceholder(htmlResp, "{{to}}", to);
        htmlResp = replacePlaceholder(htmlResp, "{{subject}}", subject);
        htmlResp = replacePlaceholder(htmlResp, "{{body}}", body);

        if (isGet) {
            FrontendServer::sendResponse(htmlResp, client);
        } else if (isHead) {
            FrontendServer::sendHeadResponse(client, htmlResp.size(), "200 OK");
        }
        }
        else if (path.rfind("/email/view?", 0) == 0)
        {
            std::string emailId = path.substr(path.find("=") + 1);
            std::cout << "Serving emailId.html for email ID: " << emailId << "\n";
            std::string filePath = "static/email/email_view.html";
            std::string rawResponse;
            std::unordered_map<std::string, std::unordered_map<std::string, std::string> > emailList;
            if (!mailService.readEmail(std::stoi(emailId), rawResponse, emailList)) {
                if (isGet) {
                    std::cerr << "Failed to send RETR request to POP3\n";
                    FrontendServer::sendResponse("500 Internal Server Error", client, "500 Internal Server Error");
                } else if (isHead) {
                    FrontendServer::sendHeadResponse(client, 0, "500 Internal Server Error");
                }
                return;

            }

            std::string htmlResp = FrontendServer::readFile(filePath);
            std::string &body = emailList[emailId]["Body"];

            if (body.size() >= 5 && body.compare(body.size() - 5, 5, "\r\n.\r\n") == 0)
            {
                body.erase(body.size() - 5);
            }
            std::cout << "[DEBUG]Email body: " << body << std::endl;
            // std::cout << "[DEBUG]Email body: " << body << std::endl;
            const std::string& subject = emailList[emailId]["Subject"];
            // std::cout << "[DEBUG]Email subject: " << subject << std::endl;
            const std::string& from = emailList[emailId]["From"];
            // std::cout << "[DEBUG]Email from: " << from << std::endl;
            const std::string& to = emailList[emailId]["To"];
            // std::cout << "[DEBUG]Email to: " << to << std::endl;
            const std::string& date = emailList[emailId]["Date"];
            // std::cout << "[DEBUG]Email date: " << date << std::endl;

            size_t pos;

            if ((pos = htmlResp.find("{{body}}")) != std::string::npos) {
                // htmlResp.replace(pos, std::string("{{body}}").length(), body);
                htmlResp = replacePlaceholder(htmlResp, "{{body}}", body);
            }
            if ((pos = htmlResp.find("{{subject}}")) != std::string::npos) {
                // htmlResp.replace(pos, std::string("{{subject}}").length(), subject);
                htmlResp = replacePlaceholder(htmlResp, "{{subject}}", subject);
            }
            if ((pos = htmlResp.find("{{sender}}")) != std::string::npos) {
                // htmlResp.replace(pos, std::string("{{sender}}").length(), from);
                htmlResp = replacePlaceholder(htmlResp, "{{sender}}", from);
            }
            if ((pos = htmlResp.find("{{recipient}}")) != std::string::npos) {
                // htmlResp.replace(pos, std::string("{{recipient}}").length(), to);
                htmlResp = replacePlaceholder(htmlResp, "{{recipient}}", to);
            }
            if ((pos = htmlResp.find("{{timestamp}}")) != std::string::npos) {
                // htmlResp.replace(pos, std::string("{{timestamp}}").length(), date);
                htmlResp = replacePlaceholder(htmlResp, "{{timestamp}}", date);
            }

            htmlResp = replacePlaceholder(htmlResp, "/email/delete", "/email/delete?id=" + emailId);

            if (isGet) {
                FrontendServer::sendResponse(htmlResp, client);
            } else if (isHead) {
                FrontendServer::sendHeadResponse(client, htmlResp.size(), "200 OK");
            }
        }
    }
    else if (method == "POST")
    {
        if (path == "/email/send")
        {
        auto params = parseFormEncoded(body);
        std::string action = params["actionType"];
        std::string to = params["to"];
        std::string subject = params["subject"];
        std::string messageBody = params["body"];
        std::string timestamp = params["time"];
        std::string original = params["original"];
        std::string from = params["from"]; // used in forward

        if (action == "reply" || action == "forward") {
            // Re-render compose page with filled fields
            std::string filePath = "static/email/email_compose.html";
            std::string htmlResp = FrontendServer::readFile(filePath);

            if (action == "reply") {
                subject = "Re: " + subject;
                messageBody = "\n\nOn " + timestamp + " <" + to + "> wrote:\n" + original;
            } else if (action == "forward") {
                subject = "Fwd: " + subject;
                messageBody = "\n\n--- Forwarded message ---\nFrom: " + from +
                              "\nTo: " + to +
                              "\nDate: " + timestamp +
                              "\nSubject: " + subject +
                              "\n\n" + original +
                              "\n--- End of forwarded message ---\n";
            }

            htmlResp = replacePlaceholder(htmlResp, "{{actionType}}", "send");
            htmlResp = replacePlaceholder(htmlResp, "{{to}}", action == "reply" ? to : "");
            htmlResp = replacePlaceholder(htmlResp, "{{subject}}", subject);
            htmlResp = replacePlaceholder(htmlResp, "{{body}}", messageBody);

            FrontendServer::sendResponse(htmlResp, client);
            return;
        }

        // actionType == "send": send the email
        if (!mailService.sendEmail(body)) {
            std::cerr << "Failed to send email\n";
            FrontendServer::sendResponse("500 Internal Server Error", client, "500 Internal Server Error");
            return;
        }
        std::string path = "static/email/email_sent.html";
        std::string sentPage = FrontendServer::readFile(path);
        FrontendServer::sendResponse(sentPage, client);

        }
        else if (path.rfind("/email/delete", 0) == 0)
        {
            std::cout << "[DEBUG] Deleting email" << std::endl;
            // Need to parse the body to get the email details.
            // Fetch username from the cookie.
            int emailId = std::stoi(getQueryParam(path, "id"));
            if (!mailService.deleteEmail(emailId)) {
                std::cerr << "Failed to delete email\n";
                FrontendServer::sendResponse("500 Internal Server Error", client, "500 Internal Server Error");
                return;
            } else  {
                std::string path = "static/email/email_deleted.html";
                std::string htmlResp = FrontendServer::readFile(path);
                FrontendServer::sendResponse(htmlResp, client);
            }
        }
        else if (path.rfind("/email/logout", 0) == 0)
        {
            std::cout << "[DEBUG] Logging out" << std::endl;
            // Revokes the session by setting the cookie to an empty value.
            std::string cookie = "Set-Cookie: sessionid=; expires=Thu, 01 Jan 1970 00:00:00 GMT; path=/; HttpOnly\r\n";
            std::string response = "HTTP/1.1 302 Found\r\n"
                                   "Location: /user/login\r\n"
                                   + cookie +
                                   "Content-Length: 0\r\n"
                                   "Connection: keep-alive\r\n"
                                   "\r\n";
            FrontendServer::sendResponse(response, client);
        }
    }
    return;
}

std::string getQueryParam(const std::string &url, const std::string &param)
{
    size_t keyPos = url.find(param + "=");
    if (keyPos == std::string::npos) return "";

    size_t valueStart = keyPos + param.length() + 1;
    size_t valueEnd = url.find("&", valueStart);
    if (valueEnd == std::string::npos) valueEnd = url.length();

    return url.substr(valueStart, valueEnd - valueStart);
}

std::string replacePlaceholder(const std::string& html, const std::string& placeholder, const std::string& value) {
    std::string result = html;
    size_t pos = 0;
    while ((pos = result.find(placeholder, pos)) != std::string::npos) {
        result.replace(pos, placeholder.length(), value);
        pos += value.length();  // Move past the inserted value
    }
    return result;
}