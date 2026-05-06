#pragma once

#include "KVStore.hpp"

#include <string>

class Server {
public:
    explicit Server(KVStore& store, int port = 6380);
    static std::string handle_command(KVStore& store, const std::string& line);
    void run();

private:
    KVStore& store_;
    int port_;
};
