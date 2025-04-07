#ifdef NDEBUG
    #undef NDEBUG
#endif

#include <cstdlib>
#include <ctime>
#include <print>

#include "tpl.hpp"

using namespace tpl;

struct Node: HazardPointerObjBase<Node> {
    int a;
};

int main() {
    /*auto list = HeadonlyBlockSizedList<int, 4>{};*/
    /*for (auto i = 1; i < 11; ++i) list.push(i);*/
    /*list.consume([](auto item) {*/
    /*    std::println("Item: {}", item);*/
    /*});*/
    auto tmp = new Node{ .a = 4 };
    std::atomic<Node*> node{ tmp };
    {
        auto h = make_hazard_pointer();
        auto p = h.protect(node);
        assert(p != nullptr);
        assert(p->a == 4);
        assert(hazard_pointer_default_domain().is_hazard(p));

        auto h1 = make_hazard_pointer();
        auto p1 = h1.protect(node);
        assert(p1 != nullptr);
        assert(p1->a == 4);
        assert(hazard_pointer_default_domain().is_hazard(p1));

        auto h2 = make_hazard_pointer();
        auto p2 = h2.protect(node);
        assert(p2 != nullptr);
        assert(p2->a == 4);
        assert(hazard_pointer_default_domain().is_hazard(p2));

        h.reset_protection();
        assert(hazard_pointer_default_domain().is_hazard(p));
        h1.reset_protection();
        assert(hazard_pointer_default_domain().is_hazard(p1));
        h2.reset_protection();
        assert(!hazard_pointer_default_domain().is_hazard(p2));
    }

    {
        Node* t = node.exchange(nullptr);
        assert(!hazard_pointer_default_domain().is_hazard(t));
        t->retire();
    }
}
