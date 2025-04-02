#ifndef AMT_TPL_THREAD_HPP
#define AMT_TPL_THREAD_HPP

#include "hw_config.hpp"
#include <cstdint>
#include <vector>
#include <cstring>
#include <limits>
#include <optional>
#include <thread>
#include <chrono>

#if __has_include(<unistd.h>)
    #include <unistd.h>
#endif

#if defined(_POSIX_VERSION)
    #include <pthread.h>
    #include <sched.h>
    #include <sys/resource.h>
#elif !defined(_WIN32)
#error "Unknown platform"
#endif

#ifdef __linux__
    #include <sys/syscall.h>
    #include <sys/sysinfo.h>
#endif

#if __has_include(<sys/cpuset.h>)
    #include <sys/param.h>
    #include <sys/cpuset.h>
    #if !defined(TPL_USE_BSD_API)
        #define TPL_USE_BSD_API
    #endif
#endif

namespace tpl {

    struct Process {
    #if defined(_WIN32)
        enum class Priority {
            idle = IDLE_PRIORITY_CLASS,
            below_normal = BELOW_NORMAL_PRIORITY_CLASS,
            normal = NORMAL_PRIORITY_CLASS,
            above_normal = ABOVE_NORMAL_PRIORITY_CLASS,
            high = HIGH_PRIORITY_CLASS,
            realtime = REALTIME_PRIORITY_CLASS
        };
    #else
        enum class Priority {
            idle = PRIO_MAX - 2,
            below_normal = PRIO_MAX / 2,
            normal = 0,
            above_normal = PRIO_MIN / 3,
            high = (PRIO_MIN * 2) / 3,
            realtime = PRIO_MIN
        };
    #endif

        [[nodiscard]] inline static auto get_affinity() -> std::optional<std::vector<bool>> {
            #if defined(__APPLE__)
            return {};
            #elif defined(_WIN32)
            DWORD_PTR process_mask = 0;
            DWORD_PTR system_mask = 0;
            if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) == 0) {
                return {};
            }
            auto num_cpus = static_cast<std::size_t>(std::bit_width(system_mask));
            std::vector<bool> affinity(num_cpus);
            for (std::size_t i = 0; i < num_cpus; ++i) {
                affinity[i] = ((process_mask & (1ULL << i)) != 0ULL);
            }
            return affinity;
            #elif defined(__linux__)
            cpu_set_t cpu_set;
            CPU_ZERO(&cpu_set);
            if (sched_getaffinity(getpid(), sizeof(cpu_set_t), &cpu_set) != 0) {
                return {};
            }
            auto num_cpus = get_nprocs();
            if (num_cpus < 1) return {};
            std::vector<bool> affinity(static_cast<std::size_t>(num_cpus));
            for (std::size_t i = 0; i < affinity.size(); ++i) {
                affinity[i] = CPU_ISSET(i, &cpu_set);
            }
            #elif defined(TPL_USE_BSD_API)
            cpuset_t cpu_set;
            CPU_ZERO(&cpu_set);

            if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, getpid(), sizeof(cpu_set), &cpu_set) != 0) {
                return {};
            }

            int num_cpus = 0;
            size_t len = sizeof(num_cpus);
            if (sysctlbyname("hw.ncpu", &num_cpus, &len, nullptr, 0) != 0 || num_cpus < 1) {
                return {};
            }

            std::vector<bool> affinity(static_cast<std::size_t>(num_cpus));
            for (std::size_t i = 0; i < affinity.size(); ++i) {
                affinity[i] = CPU_ISSET(i, &cpu_set);
            }

            return affinity;
            #endif
            return {};
        }

        inline static auto set_affinity(
            [[maybe_unused]] std::vector<bool> const& affinity
        ) -> bool {
            #if defined(_WIN32)
            DWORD_PTR process_mask = 0;
            auto size = std::min<std::size_t>(affinity.size(), sizeof(DWORD_PTR) * 8);
            for (auto i = 0ul; i < size; ++i) {
                process_mask |= (affinity[i] ? (1ULL << i) : 0ULL);
            }
            return SetProcessAffinityMask(GetCurrentProcess(), process_mask) != 0;
            #elif defined(__linux__)
            cpu_set_t cpu_set;
            CPU_ZERO(&cpu_set);
            auto size = std::min<std::size_t>(affinity.size(), CPU_SETSIZE);
            for (std::size_t i = 0; i < size; ++i) {
                if (affinity[i]) CPU_SET(i, &cpu_set);
            }
            return sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpu_set) == 0;
            #elif defined(TPL_USE_BSD_API)
            cpuset_t cpu_set;
            CPU_ZERO(&cpu_set);

            auto size = std::min<std::size_t>(affinity.size(), CPU_SETSIZE);
            for (std::size_t i = 0; i < size; ++i) {
                if (affinity[i]) CPU_SET(i, &cpu_set);
            }

            return cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, getpid(), sizeof(cpu_set), &cpu_set) == 0;
            #endif
            return false;
        }

        [[nodiscard]] inline static auto get_priority() -> std::optional<Priority> {
            #if defined(_WIN32)
            DWORD const priority = GetPriorityClass(GetCurrentProcess());
            if (priority == 0) return {};
            return static_cast<Priority>(priority);
            #elif defined(_POSIX_VERSION)
            auto p = getpriority(PRIO_PROCESS, static_cast<id_t>(getpid()));
            switch (static_cast<Priority>(p)) {
                case Priority::idle:
                case Priority::below_normal:
                case Priority::normal:
                case Priority::above_normal:
                case Priority::high:
                case Priority::realtime: return static_cast<Priority>(p);
                default: return {};
            }
            #endif
            return {};
        }

        inline static auto set_priority(Priority p) -> bool {
            #if defined(_WIN32)
            return SetPriorityClass(GetCurrentProcess(), static_cast<DWORD>(p)) != 0;
            #elif defined(_POSIX_VERSION)
            return setpriority(PRIO_PROCESS, static_cast<id_t>(getpid()), static_cast<int>(p)) == 0;
            #endif
            return {};
        }

        #if defined(_WIN32)
        using pid_t = DWORD;
        #elif defined(_POSIX_VERSION)
        using pid_t = pid_t;
        #endif
        static auto get_id() noexcept -> pid_t {
        #if defined(_WIN32)
            return GetProcessId(GetCurrentProcess());
        #elif defined(_POSIX_VERSION)
            return getpid();
        #endif
        }
    };

    struct ThisThread {
    #if defined(_WIN32)
        enum class Priority {
            idle = THREAD_PRIORITY_IDLE,
            lowest = THREAD_PRIORITY_LOWEST,
            below_normal = THREAD_PRIORITY_BELOW_NORMAL,
            normal = THREAD_PRIORITY_NORMAL,
            above_normal = THREAD_PRIORITY_ABOVE_NORMAL,
            highest = THREAD_PRIORITY_HIGHEST,
            realtime = THREAD_PRIORITY_TIME_CRITICAL
        };
    #else
        enum class Priority {
            idle,
            lowest,
            below_normal,
            normal,
            above_normal,
            highest,
            realtime
        };
    #endif
        [[nodiscard]] static auto get_affinity() -> std::optional<std::vector<bool>> {
            #if defined(_WIN32)
            DWORD_PTR process_mask = 0;
            DWORD_PTR system_mask = 0;
            if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) == 0) {
                return {};
            }
            const DWORD_PTR previous_mask = SetThreadAffinityMask(GetCurrentThread(), process_mask);
            if (previous_mask == 0) return {};
            SetThreadAffinityMask(GetCurrentThread(), previous_mask);
            auto num_cpus = static_cast<std::size_t>(std::bit_width(system_mask));
            std::vector<bool> affinity(num_cpus);
            for (std::size_t i = 0; i < num_cpus; ++i) {
                affinity[i] = ((previous_mask & (1ULL << i)) != 0ULL);
            }
            return affinity;
            #elif defined(__linux__)
            cpu_set_t cpu_set;
            CPU_ZERO(&cpu_set);
            if (pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpu_set) != 0) {
                return {};
            }
            const int num_cpus = get_nprocs();
            if (num_cpus < 1) return {};
            std::vector<bool> affinity(static_cast<std::size_t>(num_cpus));
            for (std::size_t i = 0; i < affinity.size(); ++i) {
                affinity[i] = CPU_ISSET(i, &cpu_set);
            }
            return affinity;
            #elif defined(TPL_USE_BSD_API)
            cpuset_t cpuset;
            CPU_ZERO(&cpuset);
            pthread_t thread = pthread_self();
            if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, thread, sizeof(cpuset), &cpuset) != 0) {
                return {};
            }

            const int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
            if (num_cpus < 1) return {};

            std::vector<bool> affinity(static_cast<std::size_t>(num_cpus));
            for (std::size_t i = 0; i < affinity.size(); ++i) {
                affinity[i] = CPU_ISSET(i, &cpuset);
            }

            return affinity;
            #endif
            return {};
        }

        static auto set_affinity(
            [[maybe_unused]] std::vector<bool> const& affinity
        ) -> bool {
            #if defined(_WIN32)
            DWORD_PTR thread_mask = 0;
            auto size = std::min<std::size_t>(affinity.size(), sizeof(DWORD_PTR) * 8);
            for (auto i = 0ul; i < size; ++i) {
                thread_mask |= (affinity[i] ? (1ULL << i) : 0ULL);
            }
            return SetThreadAffinityMask(GetCurrentThread(), thread_mask) != 0;
            #elif defined(__linux__)
            cpu_set_t cpu_set;
            CPU_ZERO(&cpu_set);
            auto size = std::min<std::size_t>(affinity.size(), CPU_SETSIZE);
            for (std::size_t i = 0; i < size; ++i) {
                if (affinity[i]) CPU_SET(i, &cpu_set);
            }
            return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpu_set) == 0;
            #elif defined(TPL_USE_BSD_API)
            cpuset_t cpuset;
            CPU_ZERO(&cpuset);

            auto size = std::min<std::size_t>(affinity.size(), CPU_SETSIZE);
            for (std::size_t i = 0; i < size; ++i) {
                if (affinity[i]) CPU_SET(i, &cpuset);
            }

            pthread_t thread = pthread_self();
            return cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, thread, sizeof(cpuset), &cpuset) == 0;
            #endif
            return false;
        }

        [[nodiscard]] static auto get_name() -> std::optional<std::string> {
            #if defined(_WIN32)
            PWSTR data = nullptr;
            HRESULT const hr = GetThreadDescription(GetCurrentThread(), &data);
            if (FAILED(hr)) return {};
            if (data == nullptr) return {};
            auto const size = WideCharToMultiByte(CP_UTF8, 0, data, -1, nullptr, 0, nullptr, nullptr);
            if (size < 2) {
                LocalFree(data);
                return std::nullopt;
            }
            std::string name(static_cast<std::size_t>(size) - 1, 0);
            auto const result = WideCharToMultiByte(CP_UTF8, 0, data, -1, name.data(), size, nullptr, nullptr);
            LocalFree(data);
            if (result == 0) return {};
            return name;
            #elif defined(_POSIX_VERSION)
                #ifdef __linux__
                static constexpr std::size_t buffer_size = 16;
                #else
                static constexpr std::size_t buffer_size = 64;
                #endif
                char name[buffer_size] = {};
                if (pthread_getname_np(pthread_self(), name, buffer_size) != 0) return {};
                auto tmp = std::string(name);
                if (tmp.empty()) return {};
                return tmp;
            #endif
            return {};
        }

        static auto set_name([[maybe_unused]] std::string_view name) -> bool {
            #if defined(_WIN32)
            auto const size = MultiByteToWideChar(CP_UTF8, 0, name.data(), -1, nullptr, 0);
            if (size == 0) return false;
            std::wstring wide(static_cast<std::size_t>(size), 0);
            if (MultiByteToWideChar(CP_UTF8, 0, name.data(), -1, wide.data(), size) == 0) {
                return false;
            }
            HRESULT const hr = SetThreadDescription(GetCurrentThread(), wide.data());
            return SUCCEEDED(hr);
            #elif defined(__APPLE__)
            return pthread_setname_np(name.data()) == 0;
            #else
            return pthread_setname_np(pthread_self(), name.data()) == 0;
            #endif
            return false;
        }

        [[nodiscard]] static auto get_priority() -> std::optional<Priority> {
            #if defined(_WIN32)
            auto priority = GetThreadPriority(GetCurrentThread());
            if (priority == THREAD_PRIORITY_ERROR_RETURN) return {};
            return static_cast<Priority>(priority);
            #elif defined(__linux__)
            int policy = 0;
            struct sched_param param = {};
            if (pthread_getschedparam(pthread_self(), &policy, &param) != 0) return {};
            if (policy == SCHED_FIFO && param.sched_priority == sched_get_priority_max(SCHED_FIFO)) {
                return Priority::realtime;
            }
            if (policy == SCHED_RR && param.sched_priority == sched_get_priority_min(SCHED_RR) + (sched_get_priority_max(SCHED_RR) - sched_get_priority_min(SCHED_RR)) / 2) {
                return Priority::highest;
            }
            if (policy == SCHED_IDLE) return Priority::idle;
            if (policy == SCHED_OTHER) {
                auto p = getpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)));
                switch(p) {
                case PRIO_MIN + 2:
                    return Priority::above_normal;
                case 0:
                    return Priority::normal;
                case (PRIO_MAX / 2) + (PRIO_MAX % 2):
                    return Priority::below_normal;
                case PRIO_MAX - 3:
                    return Priority::lowest;
                default: return {};
                }
            }
            #elif defined(__APPLE__)
            int policy = 0;
            struct sched_param param = {};
            if (pthread_getschedparam(pthread_self(), &policy, &param) != 0) return {};
            if (policy == SCHED_FIFO && param.sched_priority == sched_get_priority_max(SCHED_FIFO)) {
                return Priority::realtime;
            }

            if (policy == SCHED_RR && param.sched_priority == sched_get_priority_min(SCHED_RR) + (sched_get_priority_max(SCHED_RR) - sched_get_priority_min(SCHED_RR)) / 2) {
                return Priority::highest;
            }

            if (policy == SCHED_OTHER) {
                auto min_p = sched_get_priority_min(SCHED_OTHER);
                auto max_p = sched_get_priority_max(SCHED_OTHER);
                auto diff = max_p - min_p;
                if (param.sched_priority == max_p) return Priority::above_normal;
                if (param.sched_priority == min_p + diff / 2) return Priority::normal;
                if (param.sched_priority == min_p + (diff * 2) / 3) return Priority::below_normal;
                if (param.sched_priority == min_p + diff / 3) return Priority::lowest;
                if (param.sched_priority == min_p) return Priority::idle;
            }

            #elif defined(TPL_USE_BSD_API)
            int policy = 0;
            struct sched_param param = {};

            if (pthread_getschedparam(pthread_self(), &policy, &param) != 0) {
                return {}; // Failed to get scheduling parameters
            }

            if (policy == SCHED_FIFO && param.sched_priority == sched_get_priority_max(SCHED_FIFO)) {
                return Priority::realtime;
            }

            if (policy == SCHED_RR &&
                param.sched_priority == sched_get_priority_min(SCHED_RR) + 
                (sched_get_priority_max(SCHED_RR) - sched_get_priority_min(SCHED_RR)) / 2) {
                return Priority::highest;
            }

#ifdef SCHED_IDLE  // Only defined in some BSD versions
            if (policy == SCHED_IDLE) {
                return Priority::idle;
            }
#endif

            if (policy == SCHED_OTHER) {
                pid_t tid = static_cast<pid_t>(syscall(SYS_gettid));
                int p = getpriority(PRIO_PROCESS, tid);

                switch (p) {
                    case PRIO_MIN + 2:
                        return Priority::above_normal;
                    case 0:
                        return Priority::normal;
                    case (PRIO_MAX / 2) + (PRIO_MAX % 2):
                        return Priority::below_normal;
                    case PRIO_MAX - 3:
                        return Priority::lowest;
                    default:
                        return {}; // Unknown priority level
                }
            }
            #endif
            return {};
        }

        [[nodiscard]] static auto set_priority(Priority p) -> bool {
            if (s_priority == p) return true;
            s_priority = p;

            #if defined(_WIN32)
            return SetThreadPriority(GetCurrentThread(), static_cast<int>(p)) != 0;
            #elif defined(__linux__)
            int policy = 0;
            struct sched_param param = {};
            std::optional<int> nice_val = std::nullopt;
            switch (p) {
                case Priority::idle: {
                    policy = SCHED_IDLE;
                    param.sched_priority = 0;
                } break;
                case Priority::lowest: {
                    policy = SCHED_OTHER;
                    param.sched_priority = 0;
                    nice_val = PRIO_MAX - 3;
                } break;
                case Priority::below_normal: {
                    policy = SCHED_OTHER;
                    param.sched_priority = 0;
                    nice_val = (PRIO_MAX / 2) + (PRIO_MAX % 2);
                } break;
                case Priority::normal: {
                    policy = SCHED_OTHER;
                    param.sched_priority = 0;
                    nice_val = 0;
                } break;
                case Priority::above_normal: {
                    policy = SCHED_OTHER;
                    param.sched_priority = 0;
                    nice_val = PRIO_MIN + 2;
                } break;
                case Priority::highest: {
                    policy = SCHED_RR;
                    param.sched_priority = sched_get_priority_min(SCHED_RR) + (sched_get_priority_max(SCHED_RR) - sched_get_priority_min(SCHED_RR)) / 2;
                } break;
                case Priority::realtime: {
                    policy = SCHED_FIFO;
                    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
                } break;
            }
            bool success = (pthread_setschedparam(pthread_self(), policy, &param) == 0);
            if (nice_val.has_value()) {
                success = success && (setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), nice_val.value()) == 0);
            }
            return success;
            #elif defined(__APPLE__)
            int policy = 0;
            struct sched_param param = {};
            switch (p) {
                case Priority::idle: {
                    policy = SCHED_OTHER;
                    param.sched_priority = sched_get_priority_min(SCHED_OTHER);
                } break;
                case Priority::lowest: {
                    policy = SCHED_OTHER;
                    param.sched_priority = sched_get_priority_min(SCHED_OTHER) + (sched_get_priority_max(SCHED_OTHER) - sched_get_priority_min(SCHED_OTHER)) / 3;
                } break;
                case Priority::below_normal: {
                    policy = SCHED_OTHER;
                    param.sched_priority = sched_get_priority_min(SCHED_OTHER) + (sched_get_priority_max(SCHED_OTHER) - sched_get_priority_min(SCHED_OTHER)) * 2 / 3;
                } break;
                case Priority::normal: {
                    policy = SCHED_OTHER;
                    param.sched_priority = sched_get_priority_min(SCHED_OTHER) + (sched_get_priority_max(SCHED_OTHER) - sched_get_priority_min(SCHED_OTHER)) / 2;
                } break;
                case Priority::above_normal: {
                    policy = SCHED_OTHER;
                    param.sched_priority = sched_get_priority_max(SCHED_OTHER);
                } break;
                case Priority::highest: {
                    policy = SCHED_RR;
                    param.sched_priority = sched_get_priority_min(SCHED_RR) + (sched_get_priority_max(SCHED_RR) - sched_get_priority_min(SCHED_RR)) / 2;
                } break;
                case Priority::realtime: {
                    policy = SCHED_FIFO;
                    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
                } break;
            }
            return pthread_setschedparam(pthread_self(), policy, &param) == 0;
            #elif defined(TPL_USE_BSD_API)
            int policy = 0;
            struct sched_param param = {};
            std::optional<int> nice_val = std::nullopt;

            switch (p) {
                #ifdef SCHED_IDLE
                case Priority::idle:
                    policy = SCHED_IDLE;
                    param.sched_priority = 0;
                    break;
                #endif
                case Priority::lowest:
                    policy = SCHED_OTHER;
                    param.sched_priority = 0;
                    nice_val = PRIO_MAX - 3;
                    break;
                case Priority::below_normal:
                    policy = SCHED_OTHER;
                    param.sched_priority = 0;
                    nice_val = (PRIO_MAX / 2) + (PRIO_MAX % 2);
                    break;
                case Priority::normal:
                    policy = SCHED_OTHER;
                    param.sched_priority = 0;
                    nice_val = 0;
                    break;
                case Priority::above_normal:
                    policy = SCHED_OTHER;
                    param.sched_priority = 0;
                    nice_val = PRIO_MIN + 2;
                    break;
                case Priority::highest:
                    policy = SCHED_RR;
                    param.sched_priority = sched_get_priority_min(SCHED_RR) + 
                                           (sched_get_priority_max(SCHED_RR) - sched_get_priority_min(SCHED_RR)) / 2;
                    break;
                case Priority::realtime:
                    policy = SCHED_FIFO;
                    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
                    break;
            }

            bool success = (pthread_setschedparam(pthread_self(), policy, &param) == 0);

            if (nice_val.has_value()) {
                pid_t tid = static_cast<pid_t>(syscall(SYS_gettid));
                success = success && (setpriority(PRIO_PROCESS, tid, nice_val.value()) == 0);
            }

            return success;
            #endif
            return false;
        }

        static auto get_id() noexcept -> decltype(auto) {
            return std::this_thread::get_id();
        }

        #if defined(_WIN32)
        using tid_t = DWORD;
        #elif defined(__APPLE__)
        using tid_t = std::uint64_t;
        #else
        using tid_t = tid_t;
        #endif
        static auto get_native_id() noexcept -> tid_t {
        #if defined(_WIN32)
            return GetCurrentThreadId();
        #elif defined(__APPLE__)
            std::uint64_t id;
            if (pthread_threadid_np(pthread_self(), &id) != 0) return std::uint64_t{};
            return id;
        #else
            return static_cast<tid_t>(syscall(SYS_gettid));
        #endif
        }

        static auto yield() noexcept -> void {
            return std::this_thread::yield();
        }

        static auto sleep_for(const std::chrono::nanoseconds& ns) noexcept -> void {
            return std::this_thread::sleep_for(ns);
        }

        template <typename C, typename D>
        static auto sleep_until(const std::chrono::time_point<C, D>& t) noexcept -> void {
            return std::this_thread::sleep_until(t);
        }

        static auto is_main() noexcept -> bool {
        #if defined(_WIN32)
            return get_native_id() == s_main_thread_id;
        #elif defined(__APPLE__)
            return pthread_main_np() != 0;
        #else
            return get_native_id() == static_cast<pid_t>(syscall(SYS_gettid));
        #endif
        }

        static auto stack_size() noexcept -> std::size_t {
        #if defined(_WIN32)
            ULONG_PTR low, high;
            GetCurrentThreadStackLimits(&low, &high);
            return static_cast<size_t>(high - low);
        #elif defined(__APPLE__)
            return pthread_get_stacksize_np(pthread_self());
        #else
            pthread_attr_t attr;
            if (pthread_attr_np(pthread_self(), &attr) != 0) {
                return {};
            }
            std::size_t size{};
            auto res = pthread_attr_getstacksize(&attr, &size);
            pthread_attr_destroy(&attr);
            if (res != 0) return {};
            return size;
        #endif
        }

        static auto pool_id() noexcept -> std::size_t {
            return s_pool_id;
        }
    private:
        friend struct WorkerPool;
    private:
        static thread_local std::size_t s_pool_id;
        static thread_local Priority s_priority;
    #if defined(_WIN32)
        static tid_t s_main_thread_id;
    #endif
    };

    #if defined(_WIN32)
    inline ThisThread::tid_t ThisThread::s_main_thread_id = get_native_id();
    #endif
    inline thread_local ThisThread::Priority ThisThread::s_priority = ThisThread::get_priority().value_or(Priority::normal);
    inline thread_local std::size_t ThisThread::s_pool_id = std::numeric_limits<std::size_t>::max();

    inline static std::size_t hardware_max_parallism() noexcept {
        return std::max(hardware_cpu_info.active_cpus, hardware_cpu_info.active_cpus / hardware_cpu_info.logical_cpus / hardware_cpu_info.physical_cpus);
    }
} // namespace tpl

#if defined(TPL_USE_BSD_API)
    #undef TPL_USE_BSD_API
#endif

#endif // AMT_TPL_THREAD_HPP
