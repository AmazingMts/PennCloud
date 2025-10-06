#include "RemoteWriteRequestAssembler.h"
#include <unistd.h>
#include <random>
#include <sstream>
#include "Globals.h"

std::string RemoteWriteRequestAssembler::assemble_put(const std::string message_id, const std::string &row_key, const std::string &column_key, const tablet_value &value)
{
    std::string response{"?" + command_to_string(KVServerCommand::PUT) + " " + row_key + " " + column_key + " " + std::to_string(value.size()) + " " + HOST + ":" + to_string(PORT_NO) + " " + message_id + "\r\n"};
    response.reserve(response.size() + value.size());
    response.append(reinterpret_cast<const char *>(value.data()), value.size());
    return response;
    // return "?" + command_to_string(KVServerCommand::PUT) + " " + row_key + " " + column_key + " " + std::to_string(to_string_(value).size()) + " " + HOST + ":" + to_string(PORT_NO) + " " + message_id + "\r\n" + to_string_(value);
}

std::string RemoteWriteRequestAssembler::assemble_cput(const std::string message_id, const std::string &row_key, const std::string &column_key, const tablet_value &cvalue,
                                                       const tablet_value &value)
{
    std::string response{"?" + command_to_string(KVServerCommand::CPUT) + " " + row_key + " " + column_key + " " + std::to_string(to_string_(cvalue).size()) + " " + std::to_string(to_string_(value).size()) + " " + HOST + ":" + to_string(PORT_NO) + " " + message_id + "\r\n"};
    response.reserve(response.size() + cvalue.size() + 2 + value.size());
    response.append(reinterpret_cast<const char *>(cvalue.data()), cvalue.size());
    response.append("\r\n");
    response.append(reinterpret_cast<const char *>(value.data()), value.size());
    return response;
    // return "?" + command_to_string(KVServerCommand::CPUT) + " " + row_key + " " + column_key + " " + std::to_string(to_string_(cvalue).size()) + " " + std::to_string(to_string_(value).size()) + " " + HOST + ":" + to_string(PORT_NO) + " " + message_id + "\r\n" + to_string_(cvalue) + "\r\n" + to_string_(value);
}

std::string RemoteWriteRequestAssembler::assemble_move(const std::string message_id, const std::string &row_key, const std::string &column_key, const std::string &new_column_key)
{
    return "?" + command_to_string(KVServerCommand::MOVE) + " " + row_key + " " + column_key + " " + new_column_key + " " + HOST + ":" + to_string(PORT_NO) + " " + message_id;
}

std::string RemoteWriteRequestAssembler::assemble_del(const std::string message_id, const std::string &row_key, const std::string &column_key)
{
    return "?" + command_to_string(KVServerCommand::DEL) + " " + row_key + " " + column_key + " " + HOST + ":" + to_string(PORT_NO) + " " + message_id;
}

std::string RemoteWriteRequestAssembler::assemble_create_row(const std::string message_id, const std::string &row_key)
{
    return "?" + command_to_string(KVServerCommand::CROW) + " " + row_key + " " + HOST + ":" + to_string(PORT_NO) + " " + message_id;
}

std::string RemoteWriteRequestAssembler::assemble_remote_write_result(const std::string initiator_address, const BroadcastResult &result)
{
    // Only primary node will send this command out (i.e., the '#').
    return "#" + command_to_string(KVServerCommand::RW_RESULT) + " " + initiator_address + " " + result.message_id + " " + std::to_string(result.success);
}

std::string RemoteWriteRequestAssembler::to_string_(const tablet_value &data)
{
    return std::string{reinterpret_cast<const char *>(data.data()), data.size()};
}

std::string RemoteWriteRequestAssembler::create_message_id()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    int i;
    ss << std::hex;
    for (i = 0; i < 8; i++)
    {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 4; i++)
    {
        ss << dis(gen);
    }
    ss << "-4";
    for (i = 0; i < 3; i++)
    {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen);
    for (i = 0; i < 3; i++)
    {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 12; i++)
    {
        ss << dis(gen);
    };
    return ss.str();
}
