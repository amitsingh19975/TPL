#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <print>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <vector>

#include "tpl.hpp"

using namespace tpl;

static constexpr auto PORT = "3000";

void* get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &reinterpret_cast<sockaddr_in*>(sa)->sin_addr;
    }

    return &reinterpret_cast<sockaddr_in6*>(sa)->sin6_addr;
}

struct Connection {
    int fd;
    sockaddr_storage addr{};
};

struct Message {
    Connection c;
    std::string message;
};

int main() {
    int sockfd;
    addrinfo *servinfo{nullptr};

    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int yes = 1;

    if (
        auto rv = getaddrinfo(nullptr, PORT, &hints, &servinfo);
        rv != 0
    ) {
        std::println(stderr, "[Server]: Error while getting address info: {}", gai_strerror(rv));
        exit(1);
    }

    auto p = servinfo;
    for (; p != nullptr; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("[Server]: Socket error: ");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("[Server]: Set socket opt error: ");
            exit(2);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("[Server]: Binding error: ");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo);

    if (p == nullptr)  {
        fprintf(stderr, "[Server]: failed to bind\n");
        exit(3);
    }

    // MAX 10 connection
    if (listen(sockfd, 10) == -1) {
        perror("listen");
        exit(4);
    }

    std::println("[Server]: waiting for connections...\n");

    Scheduler s;
    bounded_channel_t<Message*, 64> ch;


    // this is not thread-safe
    auto conns = std::vector<Connection>{};
    conns.reserve(10);

    auto read_task = [&ch, &conns](TaskToken& t, std::size_t id) {
        if (ch.is_closed()) return;
        if (conns.size() <= id) {
            ThisThread::sleep_for(std::chrono::milliseconds(10));
            t.schedule();
            return;
        }
        auto c = conns[id];
        char buff[256] = {};
        auto n = read(c.fd, buff, sizeof(buff) - 1);
        if (n != 0) {
            buff[n] = 0;
            auto tmp = new Message{
                .c = c,
                .message = std::string(buff)
            };
            std::println("[Server]: Client({}) sent '{}'", id, tmp->message);
            auto res = ch.send(tmp);
            if (!res) {
                if (res.error() == ChannelError::closed) return;
            }
            t.schedule();
        } else {
            // connection might be closed
        }
    };

    auto write_task = [&ch, &conns](TaskToken& t) {
        if (ch.is_closed() && ch.empty()) return;
        auto tmp = ch.receive();
        if (!tmp) {
            return;
        }
        auto msg = tmp.value();

        auto size = conns.size();
        for (auto i = 0ul; i < size; ++i) {
            auto nc = conns[i];
            write(nc.fd, msg->message.data(), msg->message.size());
        }

        delete msg;

        t.schedule();
    };

    auto ts = s
        | [&ch, &conns, sockfd](TaskToken& t) {
            if (ch.is_closed()) return;
            if (conns.size() == 10) return;
            auto c = Connection();
            socklen_t sin_size = sizeof(c.addr);
            auto new_fd = accept(sockfd, reinterpret_cast<struct sockaddr *>(&c.addr), &sin_size);
            c.fd = new_fd;
            if (new_fd == -1) {
                perror("[Server]: Accept: ");
                t.schedule();
                return;
            }
            char buff[INET6_ADDRSTRLEN];
            inet_ntop(c.addr.ss_family, get_in_addr(reinterpret_cast<struct sockaddr *>(&c.addr)), buff, sizeof(buff));
            std::println("[Server]: Connected to '{}'", buff);
            conns.push_back(c);
            t.schedule();
        } | TaskGroup { // Task per connection
            [&read_task](auto& t) { return read_task(t, 0); },
            [&read_task](auto& t) { return read_task(t, 1); },
            [&read_task](auto& t) { return read_task(t, 2); },
            [&read_task](auto& t) { return read_task(t, 3); },
            [&read_task](auto& t) { return read_task(t, 4); },
            [&read_task](auto& t) { return read_task(t, 5); },
            [&read_task](auto& t) { return read_task(t, 6); },
            [&read_task](auto& t) { return read_task(t, 7); },
            [&read_task](auto& t) { return read_task(t, 8); },
            [&read_task](auto& t) { return read_task(t, 9); },
            [&read_task](auto& t) { return read_task(t, 10); },
        } | write_task;
    ts.run();

    for (auto c: conns) {
        close(c.fd);
    }

    close(sockfd);
}
