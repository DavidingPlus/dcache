#include "lrucache.h"


ByteView ::ByteView(const std::string &str)
{
    m_data.resize(str.size());
    std::copy(str.begin(), str.end(), m_data.begin());
}
