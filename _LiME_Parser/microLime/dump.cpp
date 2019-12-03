//
// Created by yuval on 12/11/2019.
//

#include <algorithm>
#include "dump.h"
#include "LimeHeader.h"

using namespace std;


dump::dump(unsigned long expected_num_pages)
{
    m_pages.reserve(expected_num_pages);
}

void dump::parse(const string &path)
{
    std::ifstream file(path, fstream::binary | fstream::in);
    if (file.rdstate() == ios::failbit)
        throw std::runtime_error("file not exist");

    LimeHeader lime_header{};

    unsigned long address{};
    char memTemp[PAGE_SIZE];

    if (file.is_open())
        while (file)
        {
            /* read lime header */
            if (!file.read(reinterpret_cast<char *>(&lime_header), sizeof(LimeHeader)))
                break;

            if (lime_header.magic != LIME_MAGIC)
                break;

            m_lime_headers.push_back(lime_header); // Should copy

            auto num_pages_header = lime_header.getNumberPages(); // Calculation seems to be correct
            for (unsigned long long i = 0; i < num_pages_header; ++i)
            {
                /* read address & memory content */
                if (!file.read(reinterpret_cast<char *>(&address), sizeof(address)))
                    break;

                if (!file.read(memTemp, PAGE_SIZE))
                    break;

                /* copy page to vector */
                m_pages.emplace_back(page(address, memTemp));
            }
        }
    file.close();

    std::sort(m_pages.begin(), m_pages.end());
}

void dump::dumpOut(const std::string &path)
{
    ofstream outfile(path, ios::binary);

    auto begin = m_pages.begin();

    std::for_each(m_lime_headers.begin(), m_lime_headers.end(), [&](const LimeHeader &lime_header) {
        outfile.write(reinterpret_cast<const char *>(&lime_header), sizeof(lime_header));
        for (; begin != m_pages.end(); ++begin)
            outfile.write(reinterpret_cast<const char *>(begin->getContent()), PAGE_SIZE);
    });

    outfile.flush();
    outfile.close();
}

