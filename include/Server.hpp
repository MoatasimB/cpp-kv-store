#pragma once

#include "KVStore.hpp"

class Server {
public:
    explicit Server(KVStore& store, int port = 6380);
    void run();

private:
    KVStore& store_;
    int port_;
};
