#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
int connect_to_server(const std::string& host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error("socket failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        throw std::runtime_error("invalid host");
    }
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        throw std::runtime_error("connect failed");
    }
    return fd;
}

void send_all(int fd, const std::string& data) {
    const char* cursor = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        ssize_t sent = send(fd, cursor, remaining, 0);
        if (sent <= 0) {
            throw std::runtime_error("send failed");
        }
        cursor += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
}

std::string read_line(int fd) {
    std::string line;
    char c = '\0';
    while (recv(fd, &c, 1, 0) == 1) {
        if (c == '\n') {
            return line;
        }
        line.push_back(c);
    }
    throw std::runtime_error("recv failed");
}

long long percentile(std::vector<long long>& values, double pct) {
    if (values.empty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>((pct / 100.0) * static_cast<double>(values.size() - 1));
    return values[index];
}
}  // namespace

int main(int argc, char** argv) {
    const int clients = argc > 1 ? std::stoi(argv[1]) : 8;
    const int ops_per_client = argc > 2 ? std::stoi(argv[2]) : 1000;
    const std::string host = argc > 3 ? argv[3] : "127.0.0.1";
    const int port = argc > 4 ? std::stoi(argv[4]) : 6380;

    std::mutex mutex;
    std::vector<long long> latencies_us;
    latencies_us.reserve(static_cast<std::size_t>(clients * ops_per_client));

    const auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    for (int client = 0; client < clients; ++client) {
        threads.emplace_back([&, client] {
            int fd = connect_to_server(host, port);
            read_line(fd);
            std::vector<long long> local;
            local.reserve(static_cast<std::size_t>(ops_per_client));

            for (int i = 0; i < ops_per_client; ++i) {
                const auto op_start = std::chrono::steady_clock::now();
                const std::string key = "bench:" + std::to_string(client) + ":" + std::to_string(i);
                send_all(fd, "SET " + key + " value-" + std::to_string(i) + "\n");
                read_line(fd);
                send_all(fd, "GET " + key + "\n");
                read_line(fd);
                const auto op_end = std::chrono::steady_clock::now();
                local.push_back(std::chrono::duration_cast<std::chrono::microseconds>(op_end - op_start).count());
            }
            send_all(fd, "QUIT\n");
            close(fd);

            std::lock_guard lock(mutex);
            latencies_us.insert(latencies_us.end(), local.begin(), local.end());
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    const auto operations = static_cast<long long>(clients) * ops_per_client * 2;

    std::cout << "clients=" << clients << "\n";
    std::cout << "request_pairs=" << clients * ops_per_client << "\n";
    std::cout << "ops_per_second=" << static_cast<long long>(operations / seconds) << "\n";
    std::cout << "p50_pair_us=" << percentile(latencies_us, 50) << "\n";
    std::cout << "p95_pair_us=" << percentile(latencies_us, 95) << "\n";
    std::cout << "p99_pair_us=" << percentile(latencies_us, 99) << "\n";
    return 0;
}
