#ifndef AMT_TPL_HW_CONFIG_HPP
#define AMT_TPL_HW_CONFIG_HPP

#include <cassert>
#include <cstdint>
#include <string_view>

#if __has_include(<windows.h>) && defined(_WIN32)
    #define TPL_USE_WINDOWS_API
#elif __has_include(<sys/sysctl.h>)
    #define TPL_USE_BSD_API
#elif __has_include(<sys/sysinfo.h>)
    #define TPL_USE_LINUX_API
#endif

#if defined(TPL_USE_WINDOWS_API)
#include <bit>
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#    define WIN32_IS_MEAN_WAS_LOCALLY_DEFINED
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#    define NOMINMAX_WAS_LOCALLY_DEFINED
#  endif
#
#  include <windows.h>
#
#  ifdef WIN32_IS_MEAN_WAS_LOCALLY_DEFINED
#    undef WIN32_IS_MEAN_WAS_LOCALLY_DEFINED
#    undef WIN32_LEAN_AND_MEAN
#  endif
#  ifdef NOMINMAX_WAS_LOCALLY_DEFINED
#    undef NOMINMAX_WAS_LOCALLY_DEFINED
#    undef NOMINMAX
#  endif
#elif defined(TPL_USE_BSD_API)
    #include <sys/sysctl.h>
#elif defined(TPL_USE_LINUX_API)
    #include <sys/sysinfo.h>
#endif


namespace tpl {

    struct HardwareConfig {
        std::uint32_t active_cpus;
        std::uint32_t logical_cpus;
        std::uint32_t physical_cpus;
    };

    namespace internal {
        enum class HardwareConfigKind {
            active,
            logical,
            physical
        };

        [[nodiscard]] [[using gnu: always_inline]] static inline auto get_hardware_cpu_info_helper(
            [[maybe_unused]] HardwareConfigKind kind
        ) noexcept -> std::uint32_t {
            std::uint32_t val{1};
            #ifdef TPL_USE_LINUX_API
                switch (kind) {
                    case HardwareConfigKind::logical:
                    case HardwareConfigKind::physical:
                        return static_cast<std::uint32_t>(sysconf(_SC_NPROCESSORS_CONF));
                    case HardwareConfigKind::active: {
                        #ifdef __USE_GNU
                            cpu_set_t cpuset;
                            if (pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0) {
                                return static_cast<std::uint32_t>(CPU_COUNT(&cpuset));
                            }
                        #endif
                        return static_cast<std::uint32_t>(sysconf(_SC_NPROCESSORS_ONLN));
                    }
                }
            #elif defined(TPL_USE_BSD_API)
                std::string_view name;
                #if defined(__APPLE__)
                switch (kind) {
                  case HardwareConfigKind::active:
                        name = "hw.logicalcpu_max"; break;
                  case HardwareConfigKind::logical:
                        name = "hw.physicalcpu_max"; break;
                  case HardwareConfigKind::physical:
                        name = "hw.activecpu"; break;
                }
                #elif defined(__FreeBSD__)
                    name = "kern.smp.cpus";
                #endif
                if (name.empty()) return val;
                std::size_t valsz = sizeof(val);
                [[maybe_unused]] auto r = sysctlbyname(name.data(), &val, &valsz, NULL, 0);
                assert(r == 0);
            #elif defined(TPL_USE_WINDOWS_API)
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION slpiInfo = NULL;
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION slpiCurrent = NULL;
            DWORD dwProcessorLogicalCount = 0;
            DWORD dwProcessorPhysicalCount = 0;
            DWORD dwSize = 0;
            while (true) {
                DWORD dwResult;
                if (GetLogicalProcessorInformation(slpiInfo, &dwSize)) break;
                dwResult = GetLastError();
                if (slpiInfo) free(slpiInfo);

                if (dwResult == ERROR_INSUFFICIENT_BUFFER) {
                    slpiInfo = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(dwSize);
                    assert(slpiInfo);
                }
                else {
                    slpiInfo = NULL;
                    dwSize = 0;
                    break;
                }
            }

            for (slpiCurrent = slpiInfo;
                dwSize >= sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
                slpiCurrent++, dwSize -= sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION)) {
                switch (slpiCurrent->Relationship) {
                case RelationProcessorCore:
                    ++dwProcessorPhysicalCount;
                    dwProcessorLogicalCount += static_cast<DWORD>(std::popcount(slpiCurrent->ProcessorMask));
                    break;
#if _WIN32_WINNT >= _WIN32_WINNT_WIN10
                case RelationProcessorDie:
#endif
                case RelationProcessorPackage:
                case RelationNumaNode:
#if _WIN32_WINNT >= _WIN32_WINNT_WIN10
                case RelationNumaNodeEx:
#endif
                case RelationCache:
                case RelationGroup:
#if NTDDI_VERSION >= NTDDI_WIN10_CO
                case RelationProcessorModule:
#endif
                case RelationAll:
                    break;
                }
            }
            free(slpiInfo);

            switch (kind) {
            case HardwareConfigKind::active:
            case HardwareConfigKind::logical:
                return static_cast<std::uint32_t>(dwProcessorLogicalCount);
            case HardwareConfigKind::physical:
                return static_cast<std::uint32_t>(dwProcessorPhysicalCount);
            }
            #endif
            return val;
        }
    } // namespace internal

    [[nodiscard]] [[using gnu: always_inline]] static inline auto get_hardware_cpu_info() noexcept -> HardwareConfig {
        return {
            .active_cpus = internal::get_hardware_cpu_info_helper(internal::HardwareConfigKind::active),
            .logical_cpus = internal::get_hardware_cpu_info_helper(internal::HardwareConfigKind::logical),
            .physical_cpus = internal::get_hardware_cpu_info_helper(internal::HardwareConfigKind::physical)
        };
    }

    inline static auto hardware_cpu_info = get_hardware_cpu_info();

} // namespace tpl

#undef TPL_USE_BSD_API
#undef TPL_USE_LINUX_API
#undef TPL_USE_WINDOWS_API

#endif // AMT_TPL_HW_CONFIG_HPP
