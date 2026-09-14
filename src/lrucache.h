#ifndef _KCACHE_LRUCACHE_H_
#define _KCACHE_LRUCACHE_H_

#include <vector>
#include <string>
#include <optional>


struct ByteView
{
    ByteView(const std::string &str);

    int64_t len() const { return m_data.size(); }

    std::string toString() const { return std::string(m_data.begin(), m_data.end()); }


    std::vector<char> m_data;
};

using ByteViewOptional = std::optional<ByteView>;


struct Entry
{
    Entry(std::string k, const ByteView &v) : m_key(std::move(k)), m_value(v) {}

    bool operator==(const Entry &entry) const { return m_key == entry.m_key && m_value.toString() == entry.m_value.toString(); }


    std::string m_key;

    ByteView m_value;
};


#endif
