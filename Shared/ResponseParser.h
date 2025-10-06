#include <string>
#include <vector>
#include <set>

class ResponseParser
{

public:
    ResponseParser() = default;
    ~ResponseParser() = default;

    static std::string parse_get(const std::string &response);
    static bool parse_cput(const std::string &response);
    static std::set<std::string> parse_list_rows(const std::string &response);
    static std::set<std::string> parse_list_columns(const std::string &response);
    static std::size_t parse_bytes(const std::string &response);

private:
    // Removes a prefix from the response
    static std::string remove_prefix_(const std::string &input, const std::string prefix);
    // Splits the response by a given delimiter
    static std::vector<std::string> split_by_delimiter_(const std::string &input, const std::string delimiter);
};