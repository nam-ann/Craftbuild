module;

#include <defs.hpp>

NO_WARNING
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#undef ERROR
DO_WARNING

module game.network;

namespace craftbuild {
    void SendQueue::store(Message const& message) {
        std::lock_guard lock(msg_mutex);
        msg.push_back(message);
    }

    void SendQueue::send(SOCKET socket) {
        std::vector<Message> send_msg;

        {
            std::lock_guard lock(msg_mutex);
            msg.swap(send_msg);
        }

        for (auto const& message : send_msg) {
            std::string arg = message.content.std_str() + '\2';
            for (auto const& E : message.arguments) arg += E + '\1';
            arg += '\0';

            int32 total_payload_sent = 0;
            while (total_payload_sent < arg.size()) {
                int32 sent = ::send(socket, arg.data() + total_payload_sent, (int32)(arg.size() - total_payload_sent), 0);
                if (sent == SOCKET_ERROR) {
                    if (WSAGetLastError() == WSAEWOULDBLOCK) {
                        std::this_thread::sleep_for(1ms);
                        continue;
                    }
                    log<LogType::ERROR>(format{} << "Failed to send message: " << message.content);
                    break;
                }
                total_payload_sent += sent;
            }
        }
    }

    ReceiveState ReceiveQueue::receive(SOCKET socket, List<char>& out_data) {
        for (usize i : range<usize>(len(buffer))) {
            if (buffer.c_ptr()[i] == '\0') {
                out_data.resize(i);
                memcpy(out_data.c_ptr(), buffer.c_ptr(), i);

                size_t remaining = len(buffer) - (i + 1);
                if (remaining > 0) {
                    memmove(buffer.c_ptr(), buffer.c_ptr() + i + 1, remaining);
                    buffer.resize(remaining);
                }
                else buffer.clear();
                return ReceiveState::COMPLETE;
            }
        }

        size_t old_len = len(buffer);
        buffer.resize(old_len + 1024);

        int32 r = recv(socket, buffer.c_ptr() + old_len, 1024, 0);

        if (r > 0) {
            buffer.resize(old_len + r);

            for (usize i : range<usize>(old_len, len(buffer))) {
                if (buffer.c_ptr()[i] == '\0') {
                    out_data.resize(i);
                    memcpy(out_data.c_ptr(), buffer.c_ptr(), i);

                    size_t remaining = len(buffer) - (i + 1);
                    if (remaining > 0) {
                        memmove(buffer.c_ptr(), buffer.c_ptr() + i + 1, remaining);
                        buffer.resize(remaining);
                    }
                    else buffer.clear();
                    return ReceiveState::COMPLETE;
                }
            }
            return ReceiveState::WAITING;
        }
        else if (r == 0) return ReceiveState::ERROR;
        else {
            buffer.resize(old_len);
            int32 err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) return ReceiveState::WAITING;
            return ReceiveState::ERROR;
        }
    }

    Message ReceiveQueue::parse(List<char> buffer) {
        std::string str(buffer.c_ptr(), len(buffer));

        usize pos = str.find('\2');
        if (pos == std::string::npos) return { Str(str), {} };

        Message message;
        message.content = Str(str.substr(0, pos));

        std::string args_str = str.substr(pos + 1);
        usize start = 0;

        while (true) {
            usize end = args_str.find('\1', start);
            if (end == std::string::npos) {
                std::string last_arg = args_str.substr(start);
                if (not last_arg.empty()) message.arguments.push_back(last_arg);
                break;
            }
            message.arguments.push_back(args_str.substr(start, end - start));
            start = end + 1;
        }

        return message;
    }
}