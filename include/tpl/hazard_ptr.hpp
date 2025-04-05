#ifndef AMT_TPL_HAZARD_PTR_HPP
#define AMT_TPL_HAZARD_PTR_HPP

#include "list.hpp"
#include <atomic>
#include <concepts>
#include <functional>
#include <memory>
#include <memory_resource>
#include <utility>

namespace tpl {
    struct HazardPointer;

    struct HazardPointerDomain {
        static constexpr auto default_max_reclaimed_nodes = 1000;
        HazardPointerDomain(std::size_t max_reclaimed_nodes = default_max_reclaimed_nodes) noexcept
            : m_max_reclaimed_nodes(max_reclaimed_nodes)
        {}
        explicit HazardPointerDomain(
            std::pmr::polymorphic_allocator<std::byte> poly_alloc,
            std::size_t max_reclaimed_nodes = default_max_reclaimed_nodes
        ) noexcept
            : m_alloc(std::move(poly_alloc))
            , m_max_reclaimed_nodes(max_reclaimed_nodes)
        {}
        HazardPointerDomain(HazardPointerDomain const&) = delete;
        HazardPointerDomain& operator=(HazardPointerDomain const&) = delete;
        ~HazardPointerDomain() {
            cleanup();
        }

        template <typename T>
        constexpr auto is_hazard(T const* ptr) const noexcept -> bool {
            if (ptr == nullptr) return false;
            auto index = m_resources.index_of(ptr);
            return !index.empty();
        }

        auto cleanup() -> bool {
            return m_reclaimed.consume([](ReclaimedWrapper&& w) {
                w.destory();
            });
        }
    private:
        struct ReclaimedWrapper {
            using deleter_t = std::function<void(void*)>;
            void* value{nullptr};
            deleter_t deleter{nullptr};

            ReclaimedWrapper() noexcept = default;
            ReclaimedWrapper(void* ptr, deleter_t fn) noexcept
                : value(ptr)
                , deleter(std::move(fn))
            {}

            ReclaimedWrapper(ReclaimedWrapper const&) = delete;
            ReclaimedWrapper(ReclaimedWrapper && other) noexcept
                : value(std::exchange(other.value, nullptr))
                , deleter(std::move(other.deleter))
            {}

            ReclaimedWrapper& operator=(ReclaimedWrapper const&) = delete;
            ReclaimedWrapper& operator=(ReclaimedWrapper && other) noexcept {
                if (this == &other) [[unlikely]] return *this;
                value = std::exchange(other.value, nullptr);
                deleter = std::move(other.deleter);
                return *this;
            }

            auto destory() -> void {
                auto del = std::exchange(deleter, nullptr);
                if (del) {
                    del(value);
                }
                value = nullptr;
            }
        };
        using list_t = HeadonlyBlockSizedList<void const*>;
        using reclaimed_list_t = HeadonlyBlockSizedList<ReclaimedWrapper>;

        friend struct HazardPointer;

        template <typename>
        friend struct HazardPointerObjBase;

        auto get_resource() -> list_t::Index {
            return m_resources.insert_or_push(nullptr);
        }

        template <typename D>
        auto release_resource(std::byte* ptr, D deleter) -> void {
            if (is_hazard(ptr)) {
                return;
            }

            m_reclaimed.push(ReclaimedWrapper(ptr, std::move(deleter)));
            while (true) {
                auto old = m_current_reclaimed_size.load(std::memory_order_relaxed);
                if (m_current_reclaimed_size.compare_exchange_strong(
                    old,
                    old >= m_max_reclaimed_nodes ? 0 : old + 1
                )) {
                    cleanup();
                    m_current_reclaimed_size.store(m_reclaimed.size());
                    break;
                }
            }
        }
    private:
        std::pmr::polymorphic_allocator<std::byte> m_alloc{};
        list_t m_resources{m_alloc};
        reclaimed_list_t m_reclaimed{m_alloc};
        mutable std::atomic<std::size_t> m_current_id_gen{};
        std::atomic<std::size_t> m_current_reclaimed_size{};
        std::size_t m_max_reclaimed_nodes{default_max_reclaimed_nodes};
    };

    static inline HazardPointerDomain& hazard_pointer_default_domain() {
        static HazardPointerDomain domain(std::pmr::get_default_resource());
        return domain;
    }

    template <typename T>
    struct HazardPointerObjBase {
        template <typename D = std::default_delete<T>>
        void retire(
            D d = D(),
            HazardPointerDomain& domain = hazard_pointer_default_domain()
        ) noexcept {
            domain.release_resource(reinterpret_cast<std::byte*>(this), [d = std::move(d)](void* ptr) {
                if (!ptr) return;
                std::invoke(d, reinterpret_cast<T*>(ptr));
            });
        }
        HazardPointerObjBase() = default;
        HazardPointerObjBase(HazardPointerObjBase const&) = default;
        HazardPointerObjBase(HazardPointerObjBase&&) = default;
        HazardPointerObjBase& operator=(HazardPointerObjBase const&) = default;
        HazardPointerObjBase& operator=(HazardPointerObjBase&&) = default;
        ~HazardPointerObjBase() = default;
    };

    struct HazardPointer {
        constexpr HazardPointer() noexcept
            : HazardPointer(hazard_pointer_default_domain())
        {}
        constexpr HazardPointer(HazardPointerDomain& domain) noexcept
            : m_domain(&domain)
            , m_index(m_domain->get_resource())
        {}
        constexpr HazardPointer(HazardPointer&&) noexcept = default;
        constexpr HazardPointer& operator=(HazardPointer&&) noexcept = default;
        ~HazardPointer() noexcept {
            reset_protection();
        }

        constexpr auto empty() const noexcept -> bool {
            return m_index.empty();
        }

        template<typename T>
            requires (std::derived_from<T, HazardPointerObjBase<T>>)
        auto protect(std::atomic<T*> const& src) noexcept -> T* {
            auto item = src.load(std::memory_order_acquire);
            while(true) {
                auto val = reinterpret_cast<T const**>(m_index.as_ptr());
                assert(val != nullptr);
                *val = item;
                item = src.load(std::memory_order_acquire);
                if (*val == item) {
                    return item;
                }
            }
        }

        template<typename T>
            requires (std::derived_from<T, HazardPointerObjBase<T>>)
        auto try_protect(T*& ptr, std::atomic<T*> const& src) noexcept -> bool {
            auto item = src.load(std::memory_order_acquire);
            auto val = reinterpret_cast<T const**>(m_index.as_ptr());
            assert(val != nullptr);
            *val = item;
            if (*val != src.load(std::memory_order_acquire)) {
                *val = nullptr;
                return false;
            }
            ptr = item;
            return true;
        }

        template<typename T>
            requires (std::derived_from<T, HazardPointerObjBase<T>>)
        auto reset_protection(T const* ptr) noexcept -> void {
            assert(!empty());
            m_index.mark_delete(ptr);
        }

        void reset_protection(nullptr_t = nullptr) noexcept {
            assert(!empty());
            m_index.mark_delete(nullptr);
        }

        void swap(HazardPointer& other) noexcept {
            std::swap(m_index, other.m_index);
            std::swap(m_domain, other.m_domain);
        }
    private:
        HazardPointerDomain* m_domain;
        HazardPointerDomain::list_t::Index m_index{};
    };

    static inline HazardPointer make_hazard_pointer(HazardPointerDomain& domain) noexcept {
        return HazardPointer(domain);
    }

    static inline HazardPointer make_hazard_pointer() noexcept {
        return HazardPointer();
    }
} // namespace tpl

#endif // AMT_TPL_HAZARD_PTR_HPP
