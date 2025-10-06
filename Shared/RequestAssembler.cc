#include "RequestAssembler.h"
#include "IStorageService.h"

std::string RequestAssembler::assemble_get(const std::string &row_key, const std::string &column_key)
{
    return command_to_string(KVServerCommand::GET) + " " + row_key + " " + column_key + "\r\n";
}

std::string RequestAssembler::assemble_put(const std::string &row_key, const std::string &column_key, const tablet_value &value)
{
    std::string response{command_to_string(KVServerCommand::PUT) + " " + row_key + " " + column_key + " " + std::to_string(value.size()) + "\r\n"};
    response.reserve(response.size() + value.size() + 2);
    response.append(reinterpret_cast<const char *>(value.data()), value.size());
    response.append("\r\n");
    return response;
    // return command_to_string(KVServerCommand::PUT) + " " + row_key + " " + column_key + " " + std::to_string(value.size()) + "\r\n" + IStorageService::to_string(value) + "\r\n";
}

std::string RequestAssembler::assemble_cput(const std::string &row_key, const std::string &column_key, const tablet_value &cvalue,
                                            const tablet_value &value)
{
    std::string response{command_to_string(KVServerCommand::CPUT) + " " + row_key + " " + column_key + " " + std::to_string(cvalue.size()) + " " + std::to_string(value.size())+ "\r\n"};
    response.reserve(response.size() + cvalue.size() + value.size() + 4);
    response.append(reinterpret_cast<const char *>(cvalue.data()), cvalue.size());
    response.append("\r\n");
    response.append(reinterpret_cast<const char *>(value.data()), value.size());
    response.append("\r\n");
    return response;
    // return command_to_string(KVServerCommand::CPUT) + " " + row_key + " " + column_key + " " + std::to_string(cvalue.size()) + " " + std::to_string(value.size()) + "\r\n" + IStorageService::to_string(cvalue) + "\r\n" + IStorageService::to_string(value) + "\r\n";
}

std::string RequestAssembler::assemble_move(const std::string &row_key, const std::string &column_key, const std::string &new_column_key)
{
    return command_to_string(KVServerCommand::MOVE) + " " + row_key + " " + column_key + " " + new_column_key + "\r\n";
}

std::string RequestAssembler::assemble_del(const std::string &row_key, const std::string &column_key)
{
    return command_to_string(KVServerCommand::DEL) + " " + row_key + " " + column_key + "\r\n";
}

std::string RequestAssembler::assemble_list_rows()
{
    return command_to_string(KVServerCommand::LISTR) + "\r\n";
}

std::string RequestAssembler::assemble_list_columns(const std::string &row_key)
{
    return command_to_string(KVServerCommand::LISTC) + " " + row_key + "\r\n";
}

std::string RequestAssembler::assemble_create_row(const std::string &row_key)
{
    return command_to_string(KVServerCommand::CROW) + " " + row_key + "\r\n";
}

std::string RequestAssembler::assemble_shut_down()
{
    return command_to_string(KVServerCommand::SHUT_DOWN) + "\r\n";
}

std::string RequestAssembler::assemble_bring_up()
{
    return command_to_string(KVServerCommand::BRING_UP) + "\r\n";
}