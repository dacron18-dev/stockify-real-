// Generic building blocks: templates, concepts, operator overloading, multiple/virtual inheritance.
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// ---------- Concepts (C++20) + function template ----------
template <typename T> concept Numeric = std::is_arithmetic_v<T>;
template <Numeric T> double average(const std::vector<T>& v) {
    if (v.empty()) return 0;
    double sum = 0;
    for (T x : v) sum += static_cast<double>(x);           // type casting
    return sum / static_cast<double>(v.size());
}

// ---------- Class template with 2 type parameters (generic TTL cache) ----------
template <typename K, typename V>
class TtlCache {
    using Clock = std::chrono::steady_clock;
    std::map<K, std::pair<Clock::time_point, V>> data_;
    std::chrono::seconds ttl_;
    mutable std::mutex m_;                                  // thread safety
public:
    explicit TtlCache(std::chrono::seconds ttl) : ttl_(ttl) {}
    std::optional<V> get(const K& k) const {
        std::lock_guard<std::mutex> g(m_);
        auto it = data_.find(k);
        if (it == data_.end() || Clock::now() - it->second.first > ttl_) return std::nullopt;
        return it->second.second;
    }
    void put(const K& k, V v) {
        std::lock_guard<std::mutex> g(m_);
        data_[k] = {Clock::now(), std::move(v)};
    }
};

// ---------- Non-type template parameter: fixed-size ring buffer ----------
template <typename T, std::size_t N>
class RingBuffer {
    std::array<T, N> buf_{};
    std::size_t head_ = 0, size_ = 0;
    mutable std::mutex m_;
public:
    void push(T v) {
        std::lock_guard<std::mutex> g(m_);
        buf_[head_] = std::move(v);
        head_ = (head_ + 1) % N;
        if (size_ < N) ++size_;
    }
    std::vector<T> items() const {                          // oldest first
        std::lock_guard<std::mutex> g(m_);
        std::vector<T> out;
        for (std::size_t i = 0; i < size_; ++i) out.push_back(buf_[(head_ + N - size_ + i) % N]);
        return out;
    }
};

// ---------- Variadic template (fold expression) ----------
template <typename... Args>
std::string join(const char* sep, const Args&... args) {
    std::ostringstream os;
    bool first = true;
    ((os << (first ? "" : sep) << args, first = false), ...);
    return os.str();
}

// ---------- Template specialization + partial specialization ----------
template <typename T> struct Formatter {
    static std::string fmt(const T& v) { std::ostringstream o; o << v; return o.str(); }
};
template <> struct Formatter<double> {                      // full specialization
    static std::string fmt(const double& v) { std::ostringstream o; o << std::fixed << std::setprecision(2) << v; return o.str(); }
};
template <typename T> struct Formatter<std::optional<T>> {  // partial specialization
    static std::string fmt(const std::optional<T>& v) { return v ? Formatter<T>::fmt(*v) : "-"; }
};

// ---------- Operator overloading: Money value type ----------
class Money {
    double v_;
public:
    explicit Money(double v = 0.0) noexcept : v_(v) {}      // constructor with default argument
    double value() const noexcept { return v_; }
    Money operator+(const Money& o) const noexcept { return Money(v_ + o.v_); }   // binary
    Money operator-(const Money& o) const noexcept { return Money(v_ - o.v_); }
    Money operator*(double k) const noexcept { return Money(v_ * k); }
    Money operator-() const noexcept { return Money(-v_); }                        // unary
    Money& operator+=(const Money& o) noexcept { v_ += o.v_; return *this; }
    Money& operator++() noexcept { v_ += 1; return *this; }                        // prefix ++
    Money operator++(int) noexcept { Money old(*this); v_ += 1; return old; }      // postfix ++
    bool operator<(const Money& o) const noexcept { return v_ < o.v_; }
    bool operator==(const Money& o) const noexcept { return v_ == o.v_; }
    explicit operator double() const noexcept { return v_; }                       // conversion operator
    friend std::ostream& operator<<(std::ostream& os, const Money& m) {            // friend function
        return os << '$' << std::fixed << std::setprecision(2) << m.v_;
    }
    friend std::istream& operator>>(std::istream& is, Money& m) { return is >> m.v_; }
};

// ---------- Multiple + virtual (diamond) inheritance ----------
class Timestamped {                                         // shared virtual base: one timestamp per object
    std::time_t at_ = std::time(nullptr);
public:
    virtual ~Timestamped() = default;
    std::time_t at() const noexcept { return at_; }
};
class Loggable : public virtual Timestamped {
public:
    virtual std::string toLogLine() const = 0;
};
class Serializable : public virtual Timestamped {
public:
    virtual std::string toCsv() const = 0;
};
