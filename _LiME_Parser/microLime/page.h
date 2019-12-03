//
// Created by yuval on 12/11/2019.
//

#ifndef MICROLIME_PAGE_H
#define MICROLIME_PAGE_H


#include "constants.h"

class page
{
private:
    unsigned char m_content[4096];
    unsigned long m_address;

public:
    page(unsigned long address, const char content[PAGE_SIZE]);

    const unsigned char *getContent() const;

    unsigned int getAddress() const;

    bool operator<(const page &p2) const;

    bool operator==(const page &rhs) const;

    bool operator!=(const page &rhs) const;

};


#endif //MICROLIME_PAGE_H
