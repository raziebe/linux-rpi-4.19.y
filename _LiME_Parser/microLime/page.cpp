//
// Created by yuval on 12/11/2019.
//

#include <cstring>
#include "page.h"
#include "constants.h"

page::page(unsigned long address, const char *content)
        : m_address(address), m_content()
{
    std::memcpy(m_content, content, PAGE_SIZE);
}

bool page::operator<(const page &p2) const
{
    return m_address < p2.m_address;
}

const unsigned char *page::getContent() const
{
    return m_content;
}

unsigned int page::getAddress() const
{
    return static_cast<unsigned int>(m_address);
}

bool page::operator==(const page &rhs) const
{
    return (std::memcmp(m_content, rhs.m_content, PAGE_SIZE) == 0) && m_address == rhs.m_address;
}

bool page::operator!=(const page &rhs) const
{
    return !(rhs == *this);
}
