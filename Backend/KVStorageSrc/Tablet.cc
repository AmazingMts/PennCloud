#include <vector>
#include <memory>
#include <map>
#include <set>
#include <string>
#include <fstream>
#include <numeric>
#include "Tablet.h"

void Tablet::set_value(tablet_value &target, tablet_value &value)
{
    // Resize target to size of value
    target.resize(value.size());
    // Actually deallocate memory if target is larger than value
    target.shrink_to_fit();
    // No copy needed if value is empty
    if (value.size() == 0)
        return;
    // Move value into target, value is destroyed
    target = std::move(value);
    // Clear value
    value.clear();
}

TabletStatus Tablet::create_row(const std::string &row_key)
{
    // Noop if row exists
    if (has_row(row_key)) return TabletStatus::ROW_KEY_ERR;

    // Else, add row
    data.emplace(std::piecewise_construct,
                 std::forward_as_tuple(row_key),
                 std::forward_as_tuple());
    row_mutexes.try_emplace(row_key);

    return TabletStatus::OK;
}

size_t Tablet::size() const
{
    // Calculate size by iterating over rows
    return std::accumulate(data.begin(), data.end(), 0,
        [](const size_t& size, const auto& row_kv) { return size + row_kv.second.size; });
}

void Tablet::clear()
{
    // Clear all data
    data.clear();
    row_mutexes.clear();
}

TabletStatus Tablet::create_column(const std::string &row_key, const std::string &column_key)
{
    // Throw error if row does not exist
    if (!has_row(row_key))
        return TabletStatus::ROW_KEY_ERR;

    // Add column, noop if it already exists
    data[row_key].data.try_emplace(column_key);

    return TabletStatus::OK;
}

TabletStatus Tablet::write(const std::string &row_key, const std::string &column_key, tablet_value &value)
{
    // Create column if it does not exist
    if (create_column(row_key, column_key) != TabletStatus::OK)
        return TabletStatus::ROW_KEY_ERR;

    // Get current value size incase of overwrite
    size_t old_value_size{data[row_key].data[column_key].size()};

    // Insert value in column and keep track of size
    data[row_key].size += value.size() - old_value_size;
    set_value(data[row_key].data[column_key], value);

    return TabletStatus::OK;
}

TabletStatus Tablet::compare(const std::string &row_key, const std::string &column_key,
    const tablet_value &value, bool &result)
{
    if (!has_row(row_key)) return TabletStatus::ROW_KEY_ERR;
    if (!has_column(row_key, column_key)) return TabletStatus::COLUMN_KEY_ERR;

    result = (data[row_key].data[column_key] == value);

    return TabletStatus::OK;
}

TabletStatus Tablet::move(const std::string &row_key, const std::string &column_key, const std::string &new_column_key)
{
    if (!has_row(row_key))
        return TabletStatus::ROW_KEY_ERR;
    if (!has_column(row_key, column_key))
        return TabletStatus::COLUMN_KEY_ERR;

    // Create column if it does not exist
    // (existance of row has already been checked)
    create_column(row_key, new_column_key);

    // Move column to new column key
    set_value(data[row_key].data[new_column_key], data[row_key].data[column_key]);
    data[row_key].data.erase(column_key);

    return TabletStatus::OK;
}

TabletStatus Tablet::conditional_write(const std::string &row_key, const std::string &column_key,
                                       const tablet_value &cvalue, tablet_value &value, bool &result)
{
    if (!has_row(row_key))
        return TabletStatus::ROW_KEY_ERR;
    if (!has_column(row_key, column_key))
        return TabletStatus::COLUMN_KEY_ERR;

    if (data[row_key].data[column_key] == cvalue)
    {
        result = true;
        data[row_key].size += value.size() - cvalue.size();
        set_value(data[row_key].data[column_key], value);
    }
    else
        result = false;

    return TabletStatus::OK;
}

TabletStatus Tablet::remove(const std::string &row_key, const std::string &column_key, size_t &value_size)
{
    if (!has_row(row_key))
        return TabletStatus::ROW_KEY_ERR;
    if (!has_column(row_key, column_key))
        return TabletStatus::COLUMN_KEY_ERR;

    // Remove column from row
    value_size = data[row_key].data[column_key].size();
    data[row_key].size -= value_size;
    data[row_key].data.erase(column_key);

    return TabletStatus::OK;
}

void Tablet::split(Tablet &tablet)
{
    tablet.clear();

    const size_t total_size{this->size()};

    std::set<std::string> row_keys;
    this->list_rows(row_keys);

    size_t new_size{0};
    auto it{row_keys.begin()};
    for (; it != row_keys.end(); ++it)
    {
        if (new_size + this->data[*it].size > total_size / 2)
            break;

        // keep row in this tablet
        new_size += this->data[*it].size;
    }

    tablet.first_row_key = *it;
    for (; it != row_keys.end(); ++it)
    {
        // move row and mutex to new tablet
        tablet.data.insert(this->data.extract(*it));
        tablet.row_mutexes.insert(this->row_mutexes.extract(*it));
    }
}

TabletStatus Tablet::read(const std::string &row_key, const std::string &column_key, tablet_value &value) const
{
    if (!has_row(row_key))
        return TabletStatus::ROW_KEY_ERR;
    if (!has_column(row_key, column_key))
        return TabletStatus::COLUMN_KEY_ERR;

    value = data.at(row_key).data.at(column_key);

    return TabletStatus::OK;
}

TabletStatus Tablet::list_columns(const std::string &row_key, std::set<std::string> &column_keys) const
{
    if (!has_row(row_key))
        return TabletStatus::ROW_KEY_ERR;

    for (auto const &column_kv : data.at(row_key).data)
        column_keys.insert(column_kv.first);

    return TabletStatus::OK;
}

void Tablet::list_rows(std::set<std::string> &row_keys) const
{
    for (auto const &row_kv : data)
        row_keys.insert(row_kv.first);
}

void Tablet::save_to_file(const std::string &filename) const
{
    // save binary data to filename.bin.tablet
    std::ofstream ofs_bin{filename + ".bin.tblt", std::ios::binary};
    // save index data to filename.idx.tablet
    std::ofstream ofs_idx{filename + ".idx.tblt"};

    size_t pos{0};
    for (auto const &row_kv : data)
    {
        if (row_kv.second.size == 0)
        {
            ofs_idx << row_kv.first << ",\n";
        }
        for (auto const &column_kv : row_kv.second.data)
        {
            // Write row key, column key and data size to index file
            ofs_idx << row_kv.first << "," << column_kv.first << "," << column_kv.second.size() << "\n";
            // Write binary data to binary file
            ofs_bin.write(reinterpret_cast<const char *>(column_kv.second.data()), column_kv.second.size());
        }
    }

    // Close files
    ofs_bin.close();
    ofs_idx.close();
}

void Tablet::read_from_file(const std::string &filename)
{
    // Clear tablet
    clear();

    // save binary data to filename.bin.tablet
    std::ifstream ifs_bin{filename + ".bin.tblt", std::ios::binary};
    // save index data to filename.idx.tablet
    std::ifstream ifs_idx{filename + ".idx.tblt"};

    size_t size;
    std::string line, row_key, column_key;
    while (std::getline(ifs_idx, line))
    {
        // Extract row key
        size_t sep1 = line.find(",") + 1;
        row_key = line.substr(0, sep1 - 1);
        // Extract column key
        size_t sep2 = line.find(",", sep1);

        // Add empty row
        if (sep2 == std::string::npos && data.count(row_key) == 0)
        {
            data.emplace(std::piecewise_construct,
                         std::forward_as_tuple(row_key),
                         std::forward_as_tuple());
            continue;
        }

        column_key = line.substr(sep1, sep2 - sep1);
        // Extract position
        size = std::stoul(line.substr(sep2 + 1));

        // Ensure that row exists
        if (data.count(row_key) == 0)
            data.emplace(std::piecewise_construct,
                         std::forward_as_tuple(row_key),
                         std::forward_as_tuple());

        // Create column
        data[row_key].data.try_emplace(column_key);

        // Read binary data into tablet
        data[row_key].size += size;
        data[row_key].data.at(column_key).resize(size);
        ifs_bin.read(reinterpret_cast<char *>(data[row_key].data.at(column_key).data()), size);
    }

    first_row_key = data.begin()->first;

    // Close files
    ifs_bin.close();
    ifs_idx.close();
}