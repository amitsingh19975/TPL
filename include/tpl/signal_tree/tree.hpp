#ifndef AMT_TPL_SIGNAL_TREE_TREE_HPP
#define AMT_TPL_SIGNAL_TREE_TREE_HPP

#include "signal_index.hpp"
#include "level.hpp"
#include "../maths.hpp"

// For the orginal work, you can visit the link below.
// https://github.com/buildingcpp/work_contract
namespace tpl {
    template <std::size_t N>
        requires (maths::is_non_zero_power_of_two(N))
    struct SignalTree {
        using base_type = internal::LevelContainer<N>;

        static constexpr auto capacity = N;

        auto empty() const noexcept { return m_levels.empty(); };

        TPL_ATOMIC_FUNC_ATTR auto set(
            SignalIndex signal_index
        ) noexcept -> std::pair<bool /*WasEmpty*/, bool /*Successful*/> {
            return m_levels.set(signal_index);
        }

        TPL_ATOMIC_FUNC_ATTR auto set(
            std::size_t signal_index
        ) noexcept -> std::pair<bool /*WasEmpty*/, bool /*Successful*/> {
            return m_levels.set(signal_index);
        }

        TPL_ATOMIC_FUNC_ATTR auto select() noexcept -> std::pair<SignalIndex, bool /*IsZero*/>  {
            return m_levels.select();
        }

        void debug_print(bool bin = false) const {
            m_levels.debug_print(bin);
        }

        constexpr auto const& data() const noexcept {
            return m_levels.data();
        }

        template <std::size_t L>
        constexpr auto get_level() const noexcept {
            return m_levels.template get_nodes<L>();
        }
    private:
        base_type m_levels;
    };
} // namespace tpl

#endif // AMT_TPL_SIGNAL_TREE_TREE_HPP
