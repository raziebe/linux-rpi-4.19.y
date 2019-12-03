#include <iostream>
#include "dump.h"

int main(int argc, char *argv[])
{
    dump parser;
    try
    {
        parser.parse("./dump.out");
        parser.dumpOut("./dump.out.second");
    } catch (const std::exception &e)
    {
        std::cout << e.what();
    }
    return 0;
}