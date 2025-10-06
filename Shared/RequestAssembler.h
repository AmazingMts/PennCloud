#include "KVServerCommand.h"
#include <string>
#include <vector>
#include <variant>
#include <stdexcept>

typedef std::vector<std::byte> tablet_value;

class RequestAssembler
{

public:
    RequestAssembler() = default;
    ~RequestAssembler() = default;

    static std::string assemble_get(const std::string &row_key, const std::string &column_key);
    static std::string assemble_put(const std::string &row_key, const std::string &column_key, const tablet_value &value);
    static std::string assemble_cput(const std::string &row_key, const std::string &column_key, const tablet_value &cvalue,
                                     const tablet_value &value);
    static std::string assemble_move(const std::string &row_key, const std::string &column_key, const std::string &new_column_key);
    static std::string assemble_del(const std::string &row_key, const std::string &column_key);
    static std::string assemble_list_rows();
    static std::string assemble_list_columns(const std::string &row_key);
    static std::string assemble_create_row(const std::string &row_key);
    static std::string assemble_shut_down();
    static std::string assemble_bring_up();
};