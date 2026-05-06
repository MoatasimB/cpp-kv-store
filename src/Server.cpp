#include "Server.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
std::string trim(std::string text) {
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    std::size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) {
        ++start;
    }
    return text.substr(start);
}

std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return text;
}

std::vector<std::string> split(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> parts;
    std::string token;
    while (iss >> token) {
        parts.push_back(token);
    }
    return parts;
}

bool parse_int(const std::string& text, int& value) {
    try {
        std::size_t consumed = 0;
        value = std::stoi(text, &consumed);
        return consumed == text.size();
    } catch (const std::exception&) {
        return false;
    }
}

std::optional<std::pair<std::string, std::string>> parse_set_args(const std::string& line) {
    std::istringstream iss(line);
    std::string command;
    std::string key;
    if (!(iss >> command >> key)) {
        return std::nullopt;
    }

    std::string value;
    std::getline(iss, value);
    value = trim(value);
    if (value.empty()) {
        return std::nullopt;
    }
    return std::make_pair(key, value);
}

bool send_all(int fd, const std::string& data) {
    const char* cursor = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        const ssize_t sent = send(fd, cursor, remaining, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (sent == 0) {
            return false;
        }
        cursor += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

void client_session(int client_fd, KVStore& store) {
    char buffer[1024];
    const char* greeting = "Welcome to KV store. Commands: SET GET DEL EXPIRE SAVE QUIT\n";
    send_all(client_fd, greeting);

    std::string pending;
    while (true) {
        std::memset(buffer, 0, sizeof(buffer));
        ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;

        pending.append(buffer, static_cast<std::size_t>(bytes));
        std::size_t newline = std::string::npos;
        while ((newline = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            const auto response = Server::handle_command(store, line);
            if (!send_all(client_fd, response)) {
                close(client_fd);
                return;
            }
            if (upper(trim(line)) == "QUIT") {
                close(client_fd);
                return;
            }
        }
    }
    close(client_fd);
}
}  // namespace

Server::Server(KVStore& store, int port) : store_(store), port_(port) {}

std::string Server::handle_command(KVStore& store, const std::string& line) {
    const auto cleaned = trim(line);
    const auto parts = split(cleaned);
    if (parts.empty()) return "ERR empty command\n";

    const std::string cmd = upper(parts[0]);
    if (cmd == "SET") {
        auto args = parse_set_args(cleaned);
        if (!args.has_value()) return "ERR usage: SET key value\n";
        store.set(args->first, args->second);
        return "OK\n";
    }
    if (cmd == "GET") {
        if (parts.size() != 2) return "ERR usage: GET key\n";
        auto value = store.get(parts[1]);
        return value ? *value + "\n" : "(nil)\n";
    }
    if (cmd == "DEL") {
        if (parts.size() != 2) return "ERR usage: DEL key\n";
        return store.del(parts[1]) ? "1\n" : "0\n";
    }
    if (cmd == "EXPIRE") {
        if (parts.size() != 3) return "ERR usage: EXPIRE key seconds\n";
        int seconds = 0;
        if (!parse_int(parts[2], seconds)) return "ERR seconds must be an integer\n";
        return store.expire(parts[1], seconds) ? "1\n" : "0\n";
    }
    if (cmd == "TTL") {
        if (parts.size() != 2) return "ERR usage: TTL key\n";
        auto ttl = store.ttl(parts[1]);
        return ttl.has_value() ? std::to_string(*ttl) + "\n" : "-2\n";
    }
    if (cmd == "SAVE") {
        store.save_snapshot();
        return "OK\n";
    }
    if (cmd == "COMPACT") {
        store.compact();
        return "OK\n";
    }
    if (cmd == "STATS") {
        return "keys=" + std::to_string(store.size()) + "\n";
    }
    if (cmd == "PING") {
        return "PONG\n";
    }
    if (cmd == "QUIT") {
        return "BYE\n";
    }
    return "ERR unknown command\n";
}

void Server::run() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        close(server_fd);
        throw std::runtime_error("Failed to bind socket");
    }
    if (listen(server_fd, 16) < 0) {
        close(server_fd);
        throw std::runtime_error("Failed to listen");
    }

    std::cout << "KV store listening on port " << port_ << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            continue;
        }
        std::thread(client_session, client_fd, std::ref(store_)).detach();
    }
}
