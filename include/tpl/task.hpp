#ifndef AMT_TPL_TASK_HPP
#define AMT_TPL_TASK_HPP

#include <functional>
#include "thread.hpp"

namespace tpl {

    struct Task {
        using priority_t = ThisThread::Priority;
        #if defined(__cpp_lib_move_only_function)
        using fn_t = std::move_only_function<void()>;
        #else
        using fn_t = std::function<void()>;
        #endif

        priority_t priority{ priority_t::normal };
        
    };

} // namespace tpl

#endif // AMT_TPL_TASK_HPP
