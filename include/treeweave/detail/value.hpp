#ifndef TREEWEAVE_DETAIL_VALUE_HPP
#define TREEWEAVE_DETAIL_VALUE_HPP

#include <array>
#include <cstddef>
#include <type_traits>

#include <poet/poet.hpp>

namespace treeweave::detail {

/// Scalar/array uniform wrapper. Behaves as a scalar when `N == 1` and as a
/// `std::array<T, N>` otherwise, exposing the same arithmetic operators in
/// both forms so the rest of treeweave can be written dim-agnostic. The four
/// elementwise binary operators all funnel through `apply` / `apply_scalar`,
/// which lets the optimiser see one inlined loop per op rather than four
/// duplicated bodies.
template <typename T, std::size_t N>
class Value {
    using storage_t = std::conditional_t<N == 1, T, std::array<T, N>>;
    storage_t data_{};

    template <class Op>
    [[nodiscard]] constexpr auto apply(const Value &rhs, Op op) const -> Value {
        if constexpr (N == 1) {
            return Value(static_cast<T>(op(data_, rhs.data_)));
        } else {
            std::array<T, N> r{};
            for (std::size_t i = 0; i < N; ++i)
                r[i] = op(data_[i], rhs.data_[i]);
            return Value(r);
        }
    }

    template <class Op>
    [[nodiscard]] constexpr auto apply_scalar(const T &rhs, Op op) const -> Value {
        if constexpr (N == 1) {
            return Value(static_cast<T>(op(data_, rhs)));
        } else {
            std::array<T, N> r{};
            for (std::size_t i = 0; i < N; ++i)
                r[i] = op(data_[i], rhs);
            return Value(r);
        }
    }

  public:
    // enable_if (not a requires-clause): apple-clang chokes on `requires(M != 1)`
    // here ("substitution into constraint expression resulted in a non-constant
    // expression") because at N==1 std::array<T,N> collides with the array<T,1>
    // overload. SFINAE via enable_if removes the overload cleanly on every
    // compiler, so suppress modernize-use-constraints for just these two ctors.
    // NOLINTBEGIN(modernize-use-constraints)
    template <std::size_t M = N, typename = std::enable_if_t<M == 1>>
    constexpr Value(const T &val) : data_(val) {}
    constexpr Value(const std::array<T, 1> &arr) : data_(arr[0]) {}

    template <std::size_t M = N, typename = std::enable_if_t<M != 1>>
    constexpr Value(const std::array<T, N> &arr) : data_(arr) {}
    // NOLINTEND(modernize-use-constraints)
    Value()                                      = default;
    Value(const Value &)                         = default;
    Value(Value &&) noexcept                     = default;
    auto operator=(const Value &) -> Value &     = default;
    auto operator=(Value &&) noexcept -> Value & = default;
    ~Value()                                     = default;

    Value(const T *arr) {
        if constexpr (N == 1) {
            data_ = arr[0];
        } else {
            poet::static_for<N>([&](auto I) -> void {
                constexpr std::size_t i = I;
                data_[i]                = arr[i];
            });
        }
    }

    constexpr auto operator+(const Value &rhs) const -> Value {
        return apply(rhs, [](T a, T b) -> T { return a + b; });
    }
    constexpr auto operator-(const Value &rhs) const -> Value {
        return apply(rhs, [](T a, T b) -> T { return a - b; });
    }
    constexpr auto operator*(const Value &rhs) const -> Value {
        return apply(rhs, [](T a, T b) -> T { return a * b; });
    }
    constexpr auto operator/(const Value &rhs) const -> Value {
        return apply(rhs, [](T a, T b) -> T { return a / b; });
    }

    constexpr auto operator+(const T &rhs) const -> Value {
        return apply_scalar(rhs, [](T a, T b) -> T { return a + b; });
    }
    constexpr auto operator-(const T &rhs) const -> Value {
        return apply_scalar(rhs, [](T a, T b) -> T { return a - b; });
    }
    constexpr auto operator*(const T &rhs) const -> Value {
        return apply_scalar(rhs, [](T a, T b) -> T { return a * b; });
    }
    constexpr auto operator/(const T &rhs) const -> Value {
        return apply_scalar(rhs, [](T a, T b) -> T { return a / b; });
    }

    constexpr auto operator[](std::size_t idx) -> T & {
        if constexpr (N == 1) {
            static_cast<void>(idx);
            return data_;
        } else {
            return data_[idx];
        }
    }

    [[nodiscard]] constexpr auto operator[](std::size_t idx) const -> const T & {
        if constexpr (N == 1) {
            static_cast<void>(idx);
            return data_;
        } else {
            return data_[idx];
        }
    }

    constexpr auto begin() -> T * {
        if constexpr (N == 1)
            return &data_;
        else
            return data_.data();
    }
    constexpr auto end() -> T * {
        if constexpr (N == 1)
            return &data_ + 1;
        else
            return data_.data() + N;
    }
    [[nodiscard]] constexpr auto begin() const -> const T * {
        if constexpr (N == 1)
            return &data_;
        else
            return data_.data();
    }
    [[nodiscard]] constexpr auto end() const -> const T * {
        if constexpr (N == 1)
            return &data_ + 1;
        else
            return data_.data() + N;
    }

    [[nodiscard]] constexpr auto prod() const -> T {
        if constexpr (N == 1) {
            return data_;
        } else {
            T result = T{1};
            for (const auto &val : data_)
                result *= val;
            return result;
        }
    }

    constexpr operator T() const {
        static_assert(N == 1, "Can only cast to scalar if N == 1");
        return data_;
    }

    constexpr operator std::array<T, N>() const {
        if constexpr (N == 1)
            return std::array<T, 1>{data_};
        else
            return data_;
    }

    /// Always-array view: useful for passing the underlying coordinates to
    /// generic vector-of-double sinks (e.g. exception ctors) without
    /// branching on `N` at the call site.
    [[nodiscard]] constexpr auto as_array() const -> std::array<T, N> {
        if constexpr (N == 1) {
            return std::array<T, N>{data_};
        } else {
            return data_;
        }
    }
};

template <typename T>
Value(const T &) -> Value<T, 1>;

template <typename T, std::size_t N>
Value(const std::array<T, N> &) -> Value<T, N>;

} // namespace treeweave::detail

#endif // TREEWEAVE_DETAIL_VALUE_HPP
