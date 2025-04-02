#ifndef AMT_TPL_EXPR_HPP
#define AMT_TPL_EXPR_HPP

#include "scheduler.hpp"
#include "tpl/task.hpp"
#include <concepts>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tpl {

    struct SchedulerException: std::runtime_error {
        SchedulerError e;
        SchedulerException(SchedulerError e)
            : std::runtime_error(std::format("Schedule Error: {}", to_string(e)))
        {}
    };

    namespace internal {
        struct SchedulerExpr;

        template <typename T>
        static constexpr auto is_scheduler_v = 
            std::same_as<std::decay_t<T>, SchedulerExpr>
            || std::same_as<T, Scheduler*>
        ;

        template <typename T>
        static constexpr auto is_dependency_v = std::same_as<std::decay_t<T>, Scheduler::DependencyTracker>;

        template <typename C>
        struct Expr;

        template <typename L, typename R, typename Op>
        struct BinaryExpr;

        template <typename E, typename Op>
        struct UnaryExpr;

        template <typename T>
        struct is_expr: std::false_type {};

        template <typename E>
        struct is_expr<Expr<E>>: std::true_type {};

        template <typename L, typename R, typename O>
        struct is_expr<BinaryExpr<L, R, O>>: std::true_type {};

        template <typename E, typename O>
        struct is_expr<UnaryExpr<E, O>>: std::true_type {};

        template <typename T>
        static constexpr auto is_expr_v = is_expr<std::decay_t<T>>::value;

        template <typename T>
        struct has_scheduler_helper: std::false_type{};

        template <>
        struct has_scheduler_helper<SchedulerExpr>: std::true_type{};

        template <>
        struct has_scheduler_helper<Scheduler>: std::true_type{};

        template <typename E>
        struct has_scheduler_helper<Expr<E>>: has_scheduler_helper<E> {};

        template <typename E, typename O>
        struct has_scheduler_helper<UnaryExpr<E, O>>: has_scheduler_helper<E> {};

        template <typename L, typename R, typename O>
        struct has_scheduler_helper<BinaryExpr<L, R, O>>
            : std::bool_constant<
                has_scheduler_helper<L>::value ||
                has_scheduler_helper<R>::value
            >
        {};


        template <std::size_t N>
        struct TaskGroupResult {
            std::array<Scheduler::DependencyTracker, N> data;

            // t -> data
            auto deps_on(Scheduler::DependencyTracker t) {
                for (auto i = 0ul; i < data.size(); ++i) {
                    auto res = data[i].deps_on(t);
                    if (!res) throw SchedulerException(res.error());
                }
            }

            template <std::size_t M>
            auto deps_on(TaskGroupResult<M> other) {
                // this.deps_on(other)
                for (auto o: other) {
                    for (auto i = 0ul; i < data.size(); ++i) {
                        auto res = data[i].deps_on(o);
                        if (!res) throw SchedulerException(res.error());
                    }
                }
            }

            // data -> t
            auto required_by(Scheduler::DependencyTracker t) {
                for (auto i = 0ul; i < data.size(); ++i) {
                    auto res = t.deps_on(data[i]);
                    if (!res) throw SchedulerException(res.error());
                }
            }

            template <std::size_t M>
            auto required_by(TaskGroupResult<M> other) {
                // other.deps_on(this)
                for (auto o: other) {
                    for (auto i = 0ul; i < data.size(); ++i) {
                        auto res = o.deps_on(data[i]);
                        if (!res) throw SchedulerException(res.error());
                    }
                }
            }

            auto set_error_handler(ErrorHandler handler) {
                for (auto i = 0ul; i < data.size() - 1; ++i) {
                    data[i].set_error_handler(handler);
                }
                data.back().set_error_handler(std::move(handler));
            }
        };

        template <typename T>
        struct is_task_group_result: std::false_type{};

        template <std::size_t N>
        struct is_task_group_result<TaskGroupResult<N>>: std::true_type{};

        template <typename T>
        static constexpr auto is_task_group_result_v = is_task_group_result<std::decay_t<T>>::value;

        template <typename T>
        concept has_scheduler = has_scheduler_helper<std::decay_t<T>>::value;
        template <typename C>
        struct Expr {
            using child_t = C;

            constexpr Expr() noexcept = default;
            constexpr Expr(Expr const&) noexcept = delete;
            constexpr Expr(Expr &&) noexcept = default;
            constexpr Expr& operator=(Expr const&) noexcept = delete;
            constexpr Expr& operator=(Expr &&) noexcept = default;
            constexpr ~Expr() noexcept = default;

            constexpr auto self() noexcept -> child_t& {
                return *static_cast<child_t*>(this);
            }

            constexpr auto self() const noexcept -> child_t const& {
                return *static_cast<child_t const*>(this);
            }

            auto operator()(Scheduler& s) -> void {
                self()(s);
            }

            auto operator()() -> Scheduler* {
                auto s = get_scheduler();
                assert(s != nullptr);
                this->operator()(*s);
                return s;
            }

            constexpr auto get_scheduler() noexcept -> Scheduler* requires (has_scheduler<C>) {
                return self().get_scheduler();
            }

            auto run(Scheduler& s) -> void {
                this->operator()(s);
                auto res = s.run();
                if (!res) throw SchedulerException(res.error());
            }

            auto run() -> void requires (has_scheduler<C>) {
                auto s = this->operator()();
                auto res = s->run();
                if (!res) throw SchedulerException(res.error());
            }
        };

        struct SchedulerExpr: Expr<SchedulerExpr> {
            Scheduler* s;

            constexpr auto get_scheduler() noexcept -> Scheduler* {
                return s; 
            }
        };

    } // namespace internal

    template <typename... Ts>
        requires ((sizeof...(Ts) > 0) && ((std::invocable<Ts> || std::invocable<Ts, TaskToken&>) && ...))
    struct TaskGroup: internal::Expr<TaskGroup<Ts...>> {
        static constexpr auto N = sizeof...(Ts);
        std::tuple<Ts...> group;
        constexpr TaskGroup(Ts&&...ts) noexcept
            : group(std::forward<Ts>(ts)...)
        {}

        constexpr TaskGroup(TaskGroup const&) noexcept = delete;
        constexpr TaskGroup(TaskGroup &&) noexcept = default;
        constexpr TaskGroup& operator=(TaskGroup const&) noexcept = delete;
        constexpr TaskGroup& operator=(TaskGroup &&) noexcept = default;
        constexpr ~TaskGroup() noexcept = default;

        constexpr auto operator()(Scheduler& s) const noexcept -> internal::TaskGroupResult<N> {
            return { .data = [&, this]<std::size_t... Is>(std::index_sequence<Is...>)
                {
                    std::array res {
                        (s.add_task(std::move(std::get<Is>(group))))...
                    };
                    return res;
                }(std::make_index_sequence<sizeof...(Ts)>{})
            };
        }
    };

    namespace internal {
        template <typename T>
        struct is_task_group: std::false_type{};

        template <typename... Ts>
        struct is_task_group<TaskGroup<Ts...>>: std::true_type{};

        template <typename... Ts>
        struct is_expr<TaskGroup<Ts...>>: std::true_type {};

        template <typename Fn>
        static constexpr auto is_task_fn = !is_expr<Fn>::value && (std::invocable<Fn> || std::invocable<Fn, TaskToken&>) && !std::same_as<std::decay_t<Fn>, SchedulerExpr>;

        template <std::size_t I>
        struct BinaryOp: std::integral_constant<std::size_t, I> {};

        static constexpr auto binary_parallel = BinaryOp<0>{}; // '|'
        static constexpr auto binary_sink = BinaryOp<1>{}; // '>'
        static constexpr auto binary_error = BinaryOp<2>{}; // '>'

        template <typename L, typename R, typename Op>
        struct BinaryExpr: Expr<BinaryExpr<L, R, Op>> {
            using lhs_t = L;
            using rhs_t = R;

            constexpr BinaryExpr(L&& l, R&& r, Op) noexcept
                : lhs(std::forward<L>(l))
                , rhs(std::forward<R>(r))
            {}

            constexpr auto get_scheduler() noexcept -> Scheduler* requires (has_scheduler<L> || has_scheduler<R>) {
                if constexpr (has_scheduler<L>) return lhs.get_scheduler();
                else return rhs.get_scheduler();
            }

            auto operator()(Scheduler& s) requires (binary_parallel.value == Op::value) {
                return eval(s, std::move(lhs), std::move(rhs), binary_parallel);
            }

            auto operator()(Scheduler& s) requires (binary_sink.value == Op::value) {
                static_assert(
                    !(is_scheduler_v<L> || is_scheduler_v<R>)
                );
                return eval(s, std::move(lhs), std::move(rhs), binary_sink);
            }

            auto operator()(Scheduler& s) requires (binary_error.value == Op::value) {
                static_assert(
                    !(is_scheduler_v<L> || is_scheduler_v<R>)
                );
                return eval(s, std::move(lhs), std::move(rhs), binary_error);
            }

            lhs_t lhs;
            rhs_t rhs;

        private:
            template <typename TL, typename TR>
                requires (is_expr_v<TL> && is_expr_v<TR>)
            auto eval(Scheduler& s, TL&& l, TR&& r, auto op) {
                return eval(s, l(s), r(s), op);
            }

            template <typename TL, typename TR>
                requires (is_expr_v<TL> && is_task_fn<TR>)
            auto eval(Scheduler& s, TL&& l, TR&& r, auto op) {
                return eval(s, l(s), s.add_task(std::forward<TR>(r)), op);
            }

            template <typename TL, typename TR>
                requires (is_dependency_v<TL> && is_task_fn<TR>)
            auto eval(Scheduler& s, TL&& l, TR&& r, auto op) {
                return eval(s, l, s.add_task(std::forward<TR>(r)), op);
            }

            template <typename TL, typename TR>
                requires (is_task_group_result_v<TL> && is_task_fn<TR>)
            auto eval(Scheduler& s, TL&& l, TR&& r, auto op) {
                return eval(s, std::forward<TL>(l), s.add_task(std::forward<TR>(r)), op);
            }

            template <typename TL, typename TR>
                requires (is_task_fn<TL> && is_dependency_v<TR>)
            auto eval(Scheduler& s, TL&& l, TR&& r, auto op) {
                return eval(s, s.add_task(l), r, op);
            }

            template <typename TL, typename TR>
                requires (is_task_fn<TL> && is_expr_v<TR>)
            auto eval(Scheduler& s, TL&& l, TR&& r, auto op) {
                return eval(s, s.add_task(std::forward<TL>(l)), r(s), op);
            }

            template <typename TL, typename TR>
                requires (is_task_fn<TL> && is_task_group_result_v<TR>)
            auto eval(Scheduler& s, TL&& l, TR&& r, auto op) {
                return eval(s, s.add_task(std::forward<TL>(l)), std::forward<TR>(r), op);
            }

            template <typename TL, typename TR, typename O>
                requires (is_dependency_v<TL> && is_dependency_v<TR>)
            auto eval(Scheduler&, TL&& l, TR&& r, O) {
                if constexpr (O::value == binary_sink.value) {
                    auto res = l.deps_on(r);
                    if (!res) throw SchedulerException(res.error());
                }
                return r;
            }

            template <typename TL, typename TR>
                requires (is_dependency_v<TL> && is_task_group_result_v<TR>)
            auto eval(Scheduler&, TL&&, TR&& r, decltype(binary_parallel)) {
                return r;
            }

            template <typename TL, typename TR>
                requires (is_task_group_result_v<TL> && is_dependency_v<TR>)
            auto eval(Scheduler&, TL&&, TR&& r, decltype(binary_parallel)) {
                return r;
            }

            template <typename TL, typename TR>
                requires (is_dependency_v<TL> && is_task_group_result_v<TR>)
            auto eval(Scheduler&, TL&& l, TR&& r, decltype(binary_sink)) {
                r.deps_on(l);
                return r;
            }

            template <typename TL, typename TR>
                requires (is_task_group_result_v<TL> && is_dependency_v<TR>)
            auto eval(Scheduler&, TL&& l, TR&& r, decltype(binary_sink)) {
                l.required_by(r);
                return r;
            }

            template <typename TL, typename TR>
                requires (std::same_as<std::decay_t<TL>, SchedulerExpr>)
            auto eval(Scheduler& s, TL&&, TR&& r, auto) {
                if constexpr (is_expr_v<TR>) return r(s);
                else if constexpr (is_task_fn<TR>) {
                    return s.add_task(std::forward<TR>(r));
                }
                else return r;
            }

            template <typename TL, typename TR>
                requires (std::same_as<std::decay_t<TR>, SchedulerExpr>)
            auto eval(Scheduler& s, TL&& l, TR&&, auto) {
                if constexpr (is_expr_v<TL>) return l(s);
                else if constexpr (is_task_fn<TL>) {
                    return s.add_task(std::forward<TL>(l));
                }
                else return l;
            }

            // Error
            template <typename TL, typename TR>
                requires (is_dependency_v<TL> && std::same_as<std::decay_t<TR>, ErrorHandler>)
            auto eval(Scheduler&, TL&& l, TR&& r, decltype(binary_error)) {
                l.set_error_handler(std::forward<TR>(r));
                return l;
            }

            template <typename TL, typename TR>
                requires (is_task_fn<TL> && std::same_as<std::decay_t<TR>, ErrorHandler>)
            auto eval(Scheduler& s, TL&& l, TR&& r, decltype(binary_error)) {
                return s.add_task(std::forward<TL>(l), std::forward<TR>(r));
            }

            template <typename TL, typename TR>
                requires (is_expr_v<TL> && std::same_as<std::decay_t<TR>, ErrorHandler>)
            auto eval(Scheduler& s, TL&& l, TR&& r, decltype(binary_error) op) {
                return eval(s, l(s), std::forward<TR>(r), op);
            }

            template <typename TL, typename TR>
                requires (is_task_group_result_v<TL> && std::same_as<std::decay_t<TR>, ErrorHandler>)
            auto eval(Scheduler&, TL&& l, TR&& r, decltype(binary_error)) {
                l.set_error_handler(std::forward<TR>(r));
                return l;
            }
        };

        template <typename E, typename Op>
        struct UnaryExpr: Expr<UnaryExpr<E, Op>> {
            using expr_t = E;
            using op_t = Op;

            constexpr UnaryExpr(E&& e, Op o) noexcept
                : expr(std::forward<E>(e))
                , op(o)
            {}

            constexpr auto get_scheduler() noexcept -> Scheduler* requires (has_scheduler<E>) {
                return expr.get_scheduler();
            }

            expr_t expr;
            op_t op;
        };

        template <typename L, typename R, typename O>
        BinaryExpr(L&&, R&&, O) -> BinaryExpr<L, R, O>;

        template <typename E, typename O>
        UnaryExpr(E&&, O) -> UnaryExpr<E, O>;
    } // namespace internal

} // namespace tpl

template <typename Fn>
    requires (tpl::internal::is_task_fn<Fn>)
static inline constexpr auto operator|(tpl::Scheduler& s, Fn&& fn) noexcept {
    return tpl::internal::BinaryExpr(
        tpl::internal::SchedulerExpr{ .s = &s },
        std::forward<Fn>(fn),
        tpl::internal::binary_parallel
    );
}

template <typename T>
    requires (tpl::internal::is_expr_v<T>)
static inline constexpr auto operator|(tpl::Scheduler& s, T&& e) noexcept {
    return tpl::internal::BinaryExpr(
        tpl::internal::SchedulerExpr{ .s = &s },
        std::forward<T>(e),
        tpl::internal::binary_parallel
    );
}

template <typename L, typename R>
    requires (tpl::internal::is_task_fn<L> && tpl::internal::is_expr_v<R>)
static inline constexpr auto operator|(L&& l, R&& r) noexcept {
    return tpl::internal::BinaryExpr(
        std::forward<L>(l),
        std::forward<R>(r),
        tpl::internal::binary_parallel
    );
}

template <typename L, typename R>
    requires (tpl::internal::is_expr_v<L> && tpl::internal::is_task_fn<R>)
static inline constexpr auto operator|(L&& l, R&& r) noexcept {
    return tpl::internal::BinaryExpr(
        std::forward<L>(l),
        std::forward<R>(r),
        tpl::internal::binary_parallel
    );
}

template <typename L, typename R>
    requires (tpl::internal::is_expr_v<L> && tpl::internal::is_expr_v<R>)
static inline constexpr auto operator|(L&& l, R&& r) noexcept {
    return tpl::internal::BinaryExpr(
        std::forward<L>(l),
        std::forward<R>(r),
        tpl::internal::binary_parallel
    );
}

// Error handler
template <typename L, typename R>
    requires (tpl::internal::is_task_fn<L> && std::same_as<tpl::ErrorHandler, std::decay_t<R>>)
static inline constexpr auto operator+(L&& l, R&& r) noexcept {
    return tpl::internal::BinaryExpr(
        std::forward<L>(l),
        std::forward<R>(r),
        tpl::internal::binary_error
    );
}

template <typename L, typename R>
    requires (tpl::internal::is_expr_v<L> && std::same_as<tpl::ErrorHandler, std::decay_t<R>>)
static inline constexpr auto operator+(L&& l, R&& r) noexcept {
    return tpl::internal::BinaryExpr(
        std::forward<L>(l),
        std::forward<R>(r),
        tpl::internal::binary_error
    );
}

// Expr > Group
template <typename L, typename R, typename O, typename T>
    requires (tpl::internal::is_task_group<std::decay_t<T>>::value)
static inline constexpr auto operator>(tpl::internal::BinaryExpr<L, R, O> expr, T&& g) noexcept {
    return tpl::internal::BinaryExpr(
        expr,
        std::forward<T>(g),
        tpl::internal::binary_sink
    );
}

template <typename L, typename R>
    requires (tpl::internal::is_expr_v<L> && tpl::internal::is_expr_v<R>)
static inline constexpr auto operator>(L&& l, R&& r) noexcept {
    return tpl::internal::BinaryExpr(
        std::forward<L>(l),
        std::forward<R>(r),
        tpl::internal::binary_sink
    );
}

template <typename L, typename R>
    requires (tpl::internal::is_task_fn<L> && tpl::internal::is_expr_v<R>)
static inline constexpr auto operator>(L&& l, R&& r) noexcept {
    return tpl::internal::BinaryExpr(
        std::forward<L>(l),
        std::forward<R>(r),
        tpl::internal::binary_sink
    );
}

template <typename L, typename R>
    requires (tpl::internal::is_expr_v<L> && tpl::internal::is_task_fn<R>)
static inline constexpr auto operator>(L&& l, R&& r) noexcept {
    return tpl::internal::BinaryExpr(
        std::forward<L>(l),
        std::forward<R>(r),
        tpl::internal::binary_sink
    );
}

#endif // AMT_TPL_EXPR_HPP
