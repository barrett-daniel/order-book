#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>

#include "cli.hpp"
#include "server.hpp"

int main(int argc, char** argv) {

    if (argc != 2) {
        std::cerr << "invalid program arguments, specify either: -cli or -server" << std::endl;
    }

    if (strcmp(argv[1], "-cli") == 0) {
        CLI cli;
        cli.run();
    }

    if (strcmp(argv[1], "-server") == 0) {
        Server server;
    }

    return 0;
}
