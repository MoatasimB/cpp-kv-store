#include "KVStore.hpp"
#include "Server.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    try {
        const int port = argc > 1 ? std::stoi(argv[1]) : 6380;
        const std::string snapshot_path = argc > 2 ? argv[2] : "snapshot.db";
        KVStore store(snapshot_path);
        Server server(store, port);
        server.run();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
