#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <memory>

struct ForwardPacket {
    uint32_t index;
    int16_t module;
    uint16_t cmd;
    bool reliable;
    bool modified;
    uint32_t length;
    std::vector<uint8_t> payload;
    std::string timestamp;
};

class PacketForwarder {
public:
    static PacketForwarder& getInstance() {
        static PacketForwarder instance;
        return instance;
    }

    // 启动TCP服务器，port=8888
    bool start(int port = 8888);
    void stop();
    
    // 转发封包
    void forward(uint32_t index, int16_t module, uint16_t cmd, 
                 bool reliable, bool modified,
                 const uint8_t* data, uint32_t len);
    
    bool isRunning() const { return running; }
    int getClientCount() const;
    uint64_t getForwardedCount() const { return forwarded_count; }

private:
    PacketForwarder() = default;
    ~PacketForwarder() { stop(); }
    
    void serverThread();
    void broadcast(const std::string& data);
    
    std::atomic<bool> running{false};
    std::atomic<uint64_t> forwarded_count{0};
    std::unique_ptr<std::thread> server_thread;
    
    int server_fd{-1};
    std::vector<int> client_fds;
    mutable std::mutex clients_mtx;
    int port_{8888};
};