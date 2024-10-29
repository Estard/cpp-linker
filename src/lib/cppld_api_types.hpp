#pragma once
#include <span>

namespace cppld {

/**
 * @brief Indicates whether an operation completed successfully
 * Codes other than "ok" indicate the type of error that occured.
 * 
 */
enum class StatusCode {
    ok = 0,
    not_ok,
    bad_input_file,
    symbol_redefined,
    symbol_undefined,
    system_failure
};

/**
 * @brief For parameters that are expected to be written to but not read from
 * 
 */
template <typename T>
using out = T&;

/**
 * @brief For paramters that have relevant inital state and will be modifed by the function
 * 
 */
template <typename T>
using inout = T&;

/**
 * @brief For paramters that are passed in as read only
 * 
 */
template <typename T>
using in = T const&;

/**
 * @brief std::span is basically just a pointer and a size, but the elements are modifiable
 * This readonly_span is the same but prohibits modification which is in line with the in parameters
 * 
 * @tparam T 
 */
template <typename T>
using readonly_span = std::span<const T>;

} // namespace cppld