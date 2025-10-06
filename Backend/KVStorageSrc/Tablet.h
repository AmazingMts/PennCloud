#pragma once

#include <vector>
#include <memory>
#include <map>
#include <set>
#include <string>
#include <fstream>
#include <iostream>
#include <shared_mutex>
#include <cstddef>
#include "Globals.h"

typedef std::vector<std::byte> tablet_value;
typedef std::map<std::string, tablet_value> tablet_row;

struct TabletRow
{
    tablet_row data;
    size_t size{0};
};

typedef std::map<std::string, TabletRow> tablet_data;

enum class TabletStatus
{
    // No error
    OK,
    // Row key not found
    ROW_KEY_ERR,
    // Column key not found
    COLUMN_KEY_ERR,
    // Cannot access tablet because a tablet is being split
    SPLITTING_ERR,
    // Cannot write to tablet because it cannot be loaded
    CACHING_ERR,
    // Value size is too large
    VALUE_SIZE_ERR,
    // Row size is too large
    ROW_SIZE_ERR,
};

class Tablet
{
private:
    tablet_data data;
    std::string first_row_key;

    // Helper function to set value in tablet, `value` will be consumed
    void set_value(tablet_value &target, tablet_value &value);

    // Creates column, noop if it already exists
    TabletStatus create_column(const std::string &row_key, const std::string &column_key);

public:
    // Lock for the entire tablet
    std::shared_mutex tablet_mutex;
    // Lock for each row
    std::map<std::string, std::shared_mutex> row_mutexes;

    // Creates row, noop if it already exists
    TabletStatus create_row(const std::string &row_key);

    // Returns size of tablet
    size_t size() const;

    // Clears all data
    void clear();

    // Writes value to a given row and column key
    // If column does not exist, it will be created
    // Existing value will be overwritten
    // Input value will be cleared
    TabletStatus write(const std::string &row_key, const std::string &column_key, tablet_value &value);

    // Gets value from a given row and column key
    TabletStatus read(const std::string &row_key, const std::string &column_key, tablet_value &value) const;

    // Moves value from one column to another
    // If new column does not exist, it will be created
    // Existing value in the new column will be overwritten
    TabletStatus move(const std::string &row_key, const std::string &column_key, const std::string &new_column_key);

    // Writes value in a given row and column if the current value is equal to cvalue
    // Result is true if the value was written, false otherwise
    // Input value will be cleared
    TabletStatus conditional_write(const std::string &row_key, const std::string &column_key,
        const tablet_value &cvalue, tablet_value &value, bool &result);

    // Compares value in a given row and column with cvalue
    TabletStatus compare(const std::string &row_key, const std::string &column_key,
        const tablet_value &value, bool &result);

    // Removes value for a given row and column key
    TabletStatus remove(const std::string &row_key, const std::string &column_key, size_t &value_size);

    void split(Tablet &tablet);

    std::string get_first_row_key() const
    {
        return first_row_key;
    }

    friend class TabletArray;

    // Checks if row exists
    inline bool has_row(const std::string &row_key) const
    {
        return data.count(row_key);
    }

    // Checks if column exists
    inline bool has_column(const std::string &row_key, const std::string &column_key) const
    {
        return has_row(row_key) && data.at(row_key).data.count(column_key);
    }

    // Lists all columns for a given row key
    TabletStatus list_columns(const std::string &row_key, std::set<std::string> &column_keys) const;

    // Lists all rows
    void list_rows(std::set<std::string> &row_keys) const;

    // Saves tablet to file
    void save_to_file(const std::string &filename) const;

    // Reads data from file
    void read_from_file(const std::string &filename);

    // Converts tablet_value to string, data will be copied
    static inline std::string to_string(const tablet_value &data, size_t start=0, size_t len=std::string::npos)
    {
        return std::string{reinterpret_cast<const char *>(data.data() + start), std::min(data.size() - start, len)};
    }

    // Converts string to tablet_value, data will be copied
    static inline tablet_value from_string(const std::string &data, size_t start=0, size_t len=std::string::npos)
    {
        return tablet_value(reinterpret_cast<const std::byte *>(data.c_str() + start),
            reinterpret_cast<const std::byte *>(data.c_str() + std::min(data.size() - start, len)));
    }
};