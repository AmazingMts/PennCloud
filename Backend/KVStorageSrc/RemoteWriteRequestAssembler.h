#include "KVServerCommand.h"
#include <string>
#include <vector>
#include "IStorageService.h"

typedef std::vector<std::byte> tablet_value;

class RemoteWriteRequestAssembler
{

public:
    RemoteWriteRequestAssembler() = default;
    ~RemoteWriteRequestAssembler() = default;

    static std::string assemble_put(const std::string message_id, const std::string &row_key, const std::string &column_key, const tablet_value &value);
    static std::string assemble_cput(const std::string message_id, const std::string &row_key, const std::string &column_key, const tablet_value &cvalue, const tablet_value &value);
    static std::string assemble_move(const std::string message_id, const std::string &row_key, const std::string &column_key, const std::string &new_column_key);
    static std::string assemble_del(const std::string message_id, const std::string &row_key, const std::string &column_key);
    static std::string assemble_create_row(const std::string message_id, const std::string &row_key);
    static std::string assemble_remote_write_result(const std::string initiator_address, const BroadcastResult &result);
    static std::string create_message_id();

private:
    // Convert tablet_value to string (same as to_string in IStorageService)
    static std::string to_string_(const tablet_value &data);
};