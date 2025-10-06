//#include "SmtpCommandDispatcher.h"

enum class RelayStatus
{
    OK,
    DOMAIN_NOT_FOUND,
    MX_RECORD_NOT_FOUND,
    DNS_CONNECTION_FAILED,
    MAIL_CONNECTION_FAILED
};

struct RemoteEmail
{
    EmailAddress user_from; // User sending the email
    std::vector<EmailAddress> recipients; // User receiving the email
    std::string mail_content; // Content of the email
};

void* relay_main(void *args);
RelayStatus resolve_mailserver_ip(const std::string &domain, std::vector<sockaddr_in> &addresses);
RelayStatus send_remote_mail(const EmailAddress &user_from, const EmailAddress &recipient,
                              const sockaddr_in remote_server, const std::string &mail_content);
void error_mail(const RelayStatus status, const std::string &user, const std::string &mail_content,
                const EmailAddress &recipient);