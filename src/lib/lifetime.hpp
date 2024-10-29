#pragma once
#include <bit>
#include <cassert>
#include <new>
#include <type_traits>

/*
    This file is a substitution for missing C++23 features that would make code more correct with regards to object lifetimes
    You could consider this experimental additions to the std
*/
namespace estd {

//https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2674r0.pdf
template <typename T>
struct is_implicit_lifetime : std::disjunction<
                                  std::is_scalar<T>,
                                  std::is_array<T>,
                                  std::is_aggregate<T>,
                                  std::conjunction<
                                      std::is_trivially_destructible<T>,
                                      std::disjunction<
                                          std::is_trivially_default_constructible<T>,
                                          std::is_trivially_copy_constructible<T>,
                                          std::is_trivially_move_constructible<T>>>> {};

// C++-23 adds this function. For reference see "Taking a Byte Out of C++ - Avoiding Punning by Starting Lifetimes" -> https://youtu.be/pbkQG09grFw?t=1335
// This uses the ability to implicitly start lifetimes to start the lifetime of whatever we later have in our memory mapped file
// This code should generate no overhead compared to a regular reinterpret_cast
template <typename T>
T* start_lifetime_as(void* ptr) noexcept {
    static_assert(is_implicit_lifetime<T>{});
    assert(std::bit_cast<uintptr_t>(ptr) % alignof(T) == 0);
    const auto bytes = new (ptr) std::byte[sizeof(T)];
    auto tPtr = reinterpret_cast<T*>(bytes);
    (void) *tPtr;
    return tPtr;
}

template <typename T>
T* start_lifetime_as_array(void* ptr, std::size_t n) noexcept {
    static_assert(is_implicit_lifetime<T>{});
    assert(std::bit_cast<uintptr_t>(ptr) % alignof(T) == 0);
    assert(n > 0);
    const auto bytes = new (ptr) std::byte[sizeof(T) * n];
    auto tPtr = reinterpret_cast<T*>(bytes);
    (void) *tPtr;
    return tPtr;
}
} // namespace estd