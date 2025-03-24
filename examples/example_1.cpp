#include <cstdlib>
#include <print>

#include "tpl.hpp"
#include "tpl/signal_tree/tree.hpp"

using namespace tpl;

int main() {
    constexpr auto N = 4ul;
    auto tree = SignalTree<N>{};
    /*std::println("HERE: {} => {} | {}", level.levels, level.total_bits, level.size);*/
    /*std::print("Extents: ");*/
    /*for (auto el: level.extents) {*/
    /*    std::print("{}, ", el);*/
    /*}*/
    /*std::println("");*/
    /**/
    /*std::print("Strides: ");*/
    /*for (auto el: level.strides) {*/
    /*    std::print("{}, ", el);*/
    /*}*/
    /*std::println("");*/

    tree.set(0);
    tree.set(2);
    tree.set(1);
    tree.debug_print();

    {
        auto [idx, zero] = tree.select();
        std::println("Selcted: {} => {}", idx.index, zero);
    }
    {
        auto [idx, zero] = tree.select();
        std::println("Selcted: {} => {}", idx.index, zero);
    }
    {
        auto [idx, zero] = tree.select();
        std::println("Selcted: {} => {}", idx.index, zero);
    }

    tree.debug_print();

    std::println("IsEmpty: {}", tree.empty());
    

    /*Schedular s(tpl::hardware_max_parallism);*/
    /*auto t0 = s.add_task([]{});*/
    //               r
    //               4
    //             /   \
    //            a0    a1
    //            2     2
    //           /  \  /  \
    //          b0  b1 b2  b3
    //          1   1  1   1
}
