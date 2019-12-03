//
// Created by yuval on 12/11/2019.
//

#ifndef MICROLIME_DUMP_H
#define MICROLIME_DUMP_H

#include "page.h"
#include "LimeHeader.h"
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <iterator>

class dump
{
private:
    std::vector<LimeHeader> m_lime_headers;
    std::vector<page> m_pages;

public:
    explicit dump(unsigned long expected_num_pages = 242688);

    void parse(const std::string &path);

    void dumpOut(const std::string &path);
};


#endif //MICROLIME_DUMP_H
