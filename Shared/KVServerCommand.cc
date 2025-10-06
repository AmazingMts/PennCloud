#include "KVServerCommand.h"

std::string command_to_string(KVServerCommand cmd)
{
    switch (cmd)
    {
    case KVServerCommand::SYNCV:
        return "SYNCV";
    case KVServerCommand::SYNCF:
        return "SYNCF";
    case KVServerCommand::RW_RESULT:
        return "RW_RESULT";
    case KVServerCommand::SHUT_DOWN:
        return "SHUT_DOWN";
    case KVServerCommand::BRING_UP:
        return "BRING_UP";
    case KVServerCommand::CPUT:
        return "CPUT";
    case KVServerCommand::PUT:
        return "PUT";
    case KVServerCommand::GET:
        return "GET";
    case KVServerCommand::DEL:
        return "DEL";
    case KVServerCommand::LISTR:
        return "LISTR";
    case KVServerCommand::LISTC:
        return "LISTC";
    case KVServerCommand::LISTT:
        return "LISTT";
    case KVServerCommand::ERR:
        return "ERR";
    case KVServerCommand::MOVE:
        return "MOVE";
    case KVServerCommand::QUIT:
        return "QUIT";
    case KVServerCommand::CROW:
        return "CROW";
    default:
        return "UNKNOWN";
    }
}