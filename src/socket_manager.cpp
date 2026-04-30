#include "socket_manager.h"

int SocketManager::find_index_by_sock(SOCKET sock) const
{
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].sock == sock) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void SocketManager::collect_garbage()
{
    const ULONGLONG threshold = GetTickCount64() - 30000;
    items_.erase(std::remove_if(items_.begin(),
                                items_.end(),
                                [threshold](const SocketManagerItem& item) {
                                    return item.created_at < threshold;
                                }),
                 items_.end());
}

void SocketManager::add(SOCKET sock, int sock_type, int sock_protocol)
{
    SocketManagerItem item;
    item.sock = sock;
    item.is_tcp = sock_type == SOCK_STREAM && (sock_protocol == IPPROTO_TCP || sock_protocol == 0);
    item.is_udp = sock_type == SOCK_DGRAM && (sock_protocol == IPPROTO_UDP || sock_protocol == 0);
    item.created_at = GetTickCount64();

    std::lock_guard lock(mutex_);
    collect_garbage();
    const int index = find_index_by_sock(sock);
    if (index >= 0) {
        items_[static_cast<size_t>(index)] = item;
    } else {
        items_.push_back(item);
    }
}

bool SocketManager::is_first_send(SOCKET sock, SocketManagerItem& item)
{
    std::lock_guard lock(mutex_);
    const int index = find_index_by_sock(sock);
    if (index < 0 || items_[static_cast<size_t>(index)].has_sent) {
        return false;
    }
    items_[static_cast<size_t>(index)].has_sent = true;
    item = items_[static_cast<size_t>(index)];
    return true;
}

void SocketManager::set_fake_http_proxy_flag(SOCKET sock)
{
    std::lock_guard lock(mutex_);
    const int index = find_index_by_sock(sock);
    if (index >= 0) {
        items_[static_cast<size_t>(index)].fake_http_proxy_flag = true;
    }
}

bool SocketManager::reset_fake_http_proxy_flag(SOCKET sock)
{
    std::lock_guard lock(mutex_);
    const int index = find_index_by_sock(sock);
    if (index < 0 || !items_[static_cast<size_t>(index)].fake_http_proxy_flag) {
        return false;
    }
    items_[static_cast<size_t>(index)].fake_http_proxy_flag = false;
    return true;
}
