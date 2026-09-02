module;

#include <defs.hpp>

DISABLE_WARNING
#include <godot_cpp/classes/stream_peer_tcp.hpp>
ENABLE_WARNING

module game.network;

namespace craftbuild {
    void SendQueue::store(Message const& message) {
        std::lock_guard lock(msg_mutex);
        msg.push_back(message);
    }

    void SendQueue::send(StreamPeerTCP& peer) {
        if (peer.get_status() != StreamPeerTCP::STATUS_CONNECTED) return;

        std::vector<Message> send_msg;
        {
            std::lock_guard lock(msg_mutex);
            msg.swap(send_msg);
        }

        for (auto const& message : send_msg) {
            std::string arg = message.content.std_str() + '\2';

            arg.reserve(message.arguments.size());
            for (auto const& E : message.arguments) arg += E + '\1';
            arg += '\0';

            godot::PackedByteArray data;
            data.resize(static_cast<int64_t>(arg.size()));
            memcpy(data.ptrw(), arg.data(), arg.size());

            if (Error err = peer.put_data(data); err != godot::OK) {
                log<LogType::ERROR>("Failed to send message: "f << message.content);
                break;
            }
        }
    }

    ReceiveState ReceiveQueue::receive(StreamPeerTCP& peer, List<char>& out_data) {
        for (usize i : range<usize>(len(buffer))) {
            if (buffer.data()[i] == '\0') {
                out_data.resize(i);
                memcpy(out_data.data(), buffer.data(), i);

                usize remaining = len(buffer) - (i + 1);
                if (remaining > 0) {
                    memmove(buffer.data(), buffer.data() + i + 1, remaining);
                    buffer.resize(remaining);
                }
                else buffer.clear();
                return ReceiveState::COMPLETE;
            }
        }

        peer.poll();
        StreamPeerTCP::Status status = peer.get_status();
        if (status != StreamPeerTCP::STATUS_CONNECTED) return ReceiveState::ERROR;

        auto available = peer.get_available_bytes();
        if (available <= 0) return ReceiveState::WAITING;

        auto res = peer.get_partial_data(available);
        auto err = static_cast<godot::Error>((int)res[0]);
        if (err != godot::OK) return ReceiveState::ERROR;

        PackedByteArray chunk = res[1];
        auto read_bytes = chunk.size();

        if (read_bytes <= 0) return ReceiveState::WAITING;

        auto old_len = len(buffer);
        buffer.resize(old_len + read_bytes);
        memcpy(buffer.data() + old_len, chunk.ptr(), read_bytes);

        for (usize i : range<usize>(old_len, len(buffer))) {
            if (buffer.data()[i] == '\0') {
                out_data.resize(i);
                memcpy(out_data.data(), buffer.data(), i);

                size_t remaining = len(buffer) - (i + 1);
                if (remaining > 0) {
                    memmove(buffer.data(), buffer.data() + i + 1, remaining);
                    buffer.resize(remaining);
                }
                else buffer.clear();
                return ReceiveState::COMPLETE;
            }
        }
        return ReceiveState::WAITING;
    }

    Message ReceiveQueue::parse(List<char> buffer) {
        std::string str(buffer.data(), len(buffer));

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