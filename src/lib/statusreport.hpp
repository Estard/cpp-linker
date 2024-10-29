#pragma once
#include "cppld_api_types.hpp"
#include <iostream>
#include <mutex>
namespace cppld {

template <typename... TArgs>
auto report(StatusCode status, TArgs&&... to_print) -> StatusCode {
    auto statusToErrorString = [](StatusCode s) {
        switch (s) {
            case StatusCode::ok: return "Not an error. Just wanted to let you know that wverything is going well so far";
            case StatusCode::not_ok: return "Something went wrong and it's probably your fault";
            case StatusCode::bad_input_file: return "There is something wrong with an input file you provided";
            case StatusCode::symbol_redefined: return "Global symbol redefined";
            case StatusCode::symbol_undefined: return "Reference to undefined symbol";
            case StatusCode::system_failure: return "Operating System refuses to cooperate";
            default: return "Task failed successfully";
        }
    };

    static std::mutex ioMutex{};
    std::unique_lock ioLock{ioMutex};

    std::cerr << "[Error] " << statusToErrorString(status) << ": ";
    (std::cerr << ... << to_print) << '\n';
    return status;
}

} // namespace cppld