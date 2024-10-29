#pragma once
#include "cppld_api_types.hpp"
#include "lifetime.hpp"
#include <functional>
#include <future>
#include <span>
#include <thread>
namespace cppld {

// Because sometimes you want both
// designed after std::for_each
template <typename Input, typename Function>
constexpr auto for_each_indexed(Input& inputRange, Function f) -> Function {
    for (size_t i{0}; auto& e : inputRange) {
        f(e, i++);
    }
    return f;
};

// Because sometimes you want things to be a bit more in parallel
template <typename Input, typename Function>
auto parallel_for_each_indexed(Input& inputRange, Function f) -> Function {
    auto numThreads = std::thread::hardware_concurrency();
    std::vector<std::future<void>> futures;

    // This should be enough
    futures.reserve(inputRange.size());

    auto elementsPerThread = inputRange.size() / numThreads;
    // So few elements that it is not worth running
    if (elementsPerThread == 0) {
        for (size_t i{0}; auto& e : inputRange) {
            futures.push_back(std::async(std::launch::async, f,
                                         std::reference_wrapper(e), i++));
        }
    } else {
        auto apply = [](auto begin, auto end, Function pf, size_t startIndex) {
            size_t i{startIndex};
            for (; begin < end; ++begin) {
                pf(*begin, i++);
            }
        };
        // The first slices get a fixed size
        for (size_t i = 0; i < (numThreads - 1); ++i) {
            auto startID = static_cast<std::ptrdiff_t>(i * elementsPerThread);
            auto endID = static_cast<std::ptrdiff_t>((i + 1) * elementsPerThread);
            // std::async launches a thread but the implementation can also choose to use a threadpool
            // So manually launching a thread could be less efficient
            futures.push_back(std::async(std::launch::async, apply,
                                         inputRange.begin() + startID, inputRange.begin() + endID, f, startID));
        }
        // The last one maybe gets a bit more
        auto lastBitStart = static_cast<std::ptrdiff_t>((numThreads - 1) * elementsPerThread);
        futures.push_back(std::async(std::launch::async, apply,
                                     inputRange.begin() + lastBitStart, inputRange.end(), f, lastBitStart));
    }
    // runs the destructor of the future which awaits the results
    futures.clear();
    return f;
}

// Because the I only want to supply numElements once
template <typename T>
constexpr auto view_as_span(void* ptr, std::size_t numElements) -> readonly_span<T> {
    return readonly_span<T>{estd::start_lifetime_as_array<T>(ptr, numElements), numElements};
}

// Because some things need to be aligned correctly
template <std::integral I, std::integral A>
constexpr I alignup(I address, A alignment) {
    alignment = std::max(alignment, A{1});
    auto mod = address % alignment;
    return mod ? (address + (alignment - mod)) : address;
}
} // namespace cppld