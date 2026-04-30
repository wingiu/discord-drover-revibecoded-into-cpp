#pragma once

#include "common.h"

struct SocketManagerItem {
    SOCKET sock = INVALID_SOCKET;
    bool is_tcp = false;
    bool is_udp = false;
    bool has_sent = false;
    bool fake_http_proxy_flag = false;
    ULONGLONG created_at = 0;
};

class SocketManager {
public:
    void add(SOCKET sock, int sock_type, int sock_protocol);
    bool is_first_send(SOCKET sock, SocketManagerItem& item);
    void set_fake_http_proxy_flag(SOCKET sock);
    bool reset_fake_http_proxy_flag(SOCKET sock);

private:
    int find_index_by_sock(SOCKET sock) const;
    void collect_garbage();

    std::mutex mutex_;
    std::vector<SocketManagerItem> items_;
};
