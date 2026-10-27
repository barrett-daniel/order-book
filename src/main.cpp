#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <iostream>
#include <list>
#include <memory>
#include <sstream>
#include <vector>

#include "cli.hpp"
#include "order_book.hpp"
#include "order_factory.hpp"

void run_cli();
void run_server();

int main(int argc, char** argv) {

    if (argc != 2) {
        std::cerr << "invalid program arguments, specify either: -cli or -server" << std::endl;
    }

    if (strcmp(argv[1], "-cli") == 0) {
        CLI cli;
        cli.run();
    }

    if (strcmp(argv[1], "-server") == 0) run_server();

    return 0;
}

void run_server() {

}
