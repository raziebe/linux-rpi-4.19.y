//
// Created by erez on 18/11/2019.
//

#ifndef MICROLIME_LIMEHEADER_H
#define MICROLIME_LIMEHEADER_H

struct LimeHeader
{
    unsigned int magic{};
    unsigned int version{};
    unsigned long long s_addr{};
    unsigned long long e_addr{};
    unsigned char reserved[8]{};

    unsigned long long getNumberPages() const
    {
        return (e_addr - s_addr + 1) / PAGE_SIZE;
    }
} __attribute__ ((__packed__));


#endif //MICROLIME_LIMEHEADER_H
