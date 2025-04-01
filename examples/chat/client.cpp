#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <print>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>

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

    if (
        auto rv = getaddrinfo(nullptr, PORT, &hints, &servinfo);
        rv != 0
    ) {
        std::println(stderr, "[Client]: Error while getting address info: {}", gai_strerror(rv));
        exit(1);
    }

    auto p = servinfo;
    for (; p != nullptr; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("[Client]: Socket error: ");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("[Client]: connect");
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo);

    if (p == nullptr)  {
        fprintf(stderr, "[Client]: failed to connect\n");
        exit(3);
    }

    Scheduler s;

    auto res = s
        | [&sockfd](TaskToken& t) {
            std::string line;
            std::getline(std::cin, line);
            auto n = write(sockfd, line.data(), line.size());
            if (n == 0) return; // connection is closed
            t.schedule();
        }
        | [&sockfd](TaskToken& t) {
            char buff[256] = {0};
            auto n = read(sockfd, buff, sizeof(buff) - 1);
            if (n == 0) return;
            buff[n] = 0;
            std::println("Message: {}", buff);
            t.schedule();
        }
        ;

    res.run();

    close(sockfd);
}
