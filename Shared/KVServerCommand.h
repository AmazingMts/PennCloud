#pragma once
#include <string>

enum class KVServerCommand
{
    TEST_FORWARDER, // Test forwarder
    SYNCV,          // Retrieve version numbers for synchronization
    SYNCF,          // Retrieve files for synchronization
    RW_RESULT,      // Result of a remote write operation
    SHUT_DOWN,      // Shut down the server
    BRING_UP,       // Bring server back up
    CROW,           // Create row
    CPUT,           // Conditional put
    PUT,            // Save value
    GET,            // Retrieve value
    DEL,            // Delete value
    LISTR,          // List rows
    LISTC,          // List columns
    LISTT,          // List tablets
    ERR,            // Error
    MOVE,           // Move column
    QUIT            // Quit command
};

std::string command_to_string(KVServerCommand cmd);