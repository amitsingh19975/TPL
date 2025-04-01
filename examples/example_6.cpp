#include <cstdlib>
#include <ctime>
#include <print>

#include "tpl.hpp"

using namespace tpl;

int main() {
    Scheduler s;
    bounded_channel_t<std::size_t, 64> ch;
    auto ts = s
        | [&ch] {
            for (auto i = 0ul; i < 100; ++i) {
                auto res = ch.send(i);
                if (!res) {
                    if (res.error() == ChannelError::closed) return;
                }
                ThisThread::sleep_for(std::chrono::milliseconds(rand() % 1000));
            }
            ch.close();
        }
        | [&ch] {
            for (auto i = 0ul; i < 100; ++i) {
                auto res = ch.send(i + 100);
                if (!res) {
                    if (res.error() == ChannelError::closed) return;
                }
                ThisThread::sleep_for(std::chrono::milliseconds(rand() % 100));
            }
        }
        | [&ch](TaskToken& t) {
            if (ch.is_closed() && ch.empty()) return;
            auto val = ch.receive();
            if (!val) {
                return;
            }
            std::println("Item: {}", *val);
            t.schedule();
        };
    ts.run();
}
