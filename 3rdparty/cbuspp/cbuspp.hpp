#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <flat_map>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <variant>
#include <vector>

namespace cbuspp {

using steady_clock_t = std::chrono::steady_clock;
using duration_t = steady_clock_t::duration;
using time_point_t = steady_clock_t::time_point;

template <typename T>
class value;

template <typename T>
class shared_value;

template <typename>
struct is_value : std::false_type { };

template <typename T>
struct is_value<value<T>> : std::true_type { };

template <typename T>
inline constexpr bool is_value_v = is_value<std::remove_cvref_t<T>>::value;

template <typename>
struct is_shared_value : std::false_type { };

template <typename T>
struct is_shared_value<shared_value<T>> : std::true_type { };

template <typename T>
inline constexpr bool is_shared_value_v = is_shared_value<std::remove_cvref_t<T>>::value;

template <typename T>
concept value_t = is_value_v<T>;

template <typename T>
concept shared_value_t = is_shared_value_v<T>;

struct context;

enum class str_match : std::uint8_t {
    exact,
    prefix,
    suffix,
    contains
};

enum class bits_match : std::uint8_t {
    exact,
    all,
    any,
    none
};

template <std::size_t N>
struct fixed_string {
    char data[N + 1] { };

    constexpr fixed_string(const char (&str)[N + 1])
    {
        std::copy_n(str, N + 1, data);
    }

    constexpr operator std::string_view() const
    {
        return std::string_view(data, N);
    }

    [[nodiscard]] static constexpr std::size_t size() noexcept
    {
        return N;
    }

    constexpr auto operator<=>(const fixed_string&) const = default;
};

template <std::size_t N>
fixed_string(const char (&)[N]) -> fixed_string<N - 1>;

template <fixed_string Tag, typename Value = void>
struct event_tag {
    static constexpr auto tag = Tag;

    using value_t = std::conditional_t<std::is_void_v<Value> || std::is_same_v<std::remove_cv_t<Value>, std::monostate>,
        std::monostate, Value>;
};

template <typename T>
concept event_tag_t = std::is_empty_v<T> && requires {
    typename T::value_t;
    { T::tag } -> std::convertible_to<fixed_string<T::tag.size()>>;
};

using subscription_id_t = std::uint64_t;

#if __cpp_lib_move_only_function >= 202110L
using task_t = std::move_only_function<void(const void*, const context&) const>;
using filter_t = std::move_only_function<bool(const context&) const>;
#else
using task_t = std::function<void(const void*, const context&)>;
using filter_t = std::function<bool(const context&)>;
#endif

template <typename E>
concept executor_t = requires(E& e) {
    {
        e.post([]() { })
    } -> std::same_as<void>;
};

template <typename E>
struct is_executor : std::bool_constant<executor_t<E>> { };

template <typename T>
    requires(std::is_void_v<T>
        || std::is_same_v<std::remove_cv_t<T>, std::monostate>)
class value<T> {
public:
    using value_type = std::monostate;

    constexpr value() = default;
    constexpr value(const value&) = default;
    constexpr value(value&&) noexcept = default;
    constexpr value& operator=(const value&) = default;
    constexpr value& operator=(value&&) noexcept = default;
    constexpr ~value() = default;

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return true; }

    template <typename Self>
    [[nodiscard]] std::monostate operator*(this Self&& self) noexcept
    {
        return { };
    }

    template <typename F>
    [[nodiscard]] constexpr auto transform(this auto&& self, F&& f)
    {
        using R = std::remove_cvref_t<std::invoke_result_t<F>>;

        if constexpr (std::is_void_v<R>) {
            std::invoke(std::forward<F>(f));
            return value<void> { };
        } else {
            return value<R>(std::invoke(std::forward<F>(f)));
        }
    }

    template <typename F>
    [[nodiscard]] constexpr decltype(auto) and_then(this auto&& self, F&& f)
    {
        return std::invoke(std::forward<F>(f));
    }

    template <typename F>
    constexpr decltype(auto) inspect(this auto&& self, F&& f)
    {
        std::invoke(std::forward<F>(f));
        return std::forward<decltype(self)>(self);
    }

    constexpr void swap(value&) noexcept { }

private:
    [[no_unique_address]] std::monostate data_;
};

template <typename T>
    requires(std::is_object_v<T>
        && !std::is_same_v<std::remove_cv_t<T>, std::monostate>)
class value<T> {
public:
    using value_type = T;

    constexpr value()
        requires std::default_initializable<T>
    = default;

    constexpr explicit value(const T& v) noexcept(std::is_nothrow_copy_constructible_v<T>)
        : data_ { v }
    {
    }

    constexpr explicit value(T&& v) noexcept(std::is_nothrow_move_constructible_v<T>)
        : data_ { std::move(v) }
    {
    }

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    constexpr explicit value(std::in_place_t, Args&&... args) noexcept(
        std::is_nothrow_constructible_v<T, Args...>)
        : data_(std::forward<Args>(args)...)
    {
    }

    constexpr value(const value&) = default;
    constexpr value(value&&) noexcept = default;
    constexpr value& operator=(const value&) = default;
    constexpr value& operator=(value&&) noexcept = default;
    constexpr ~value() = default;

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return true; }

    template <typename Self>
    [[nodiscard]] decltype(auto) operator*(this Self&& self) noexcept
    {
        // 括号不可省:decltype(auto) 对未加括号的成员访问会取成员的声明类型,
        // 导致按值返回 —— 每次解包都拷贝整个 T,且不可拷贝类型直接编译失败
        return (std::forward<Self>(self).data_);
    }

    template <typename Self>
    [[nodiscard]] auto operator->(this Self&& self) noexcept
    {
        return std::addressof(self.data_);
    }

    template <typename F>
    [[nodiscard]] constexpr auto transform(this auto&& self, F&& f)
    {
        using R = std::remove_cvref_t<std::invoke_result_t<F, decltype(*std::forward<decltype(self)>(self))>>;

        if constexpr (std::is_void_v<R>) {
            std::invoke(std::forward<F>(f), *std::forward<decltype(self)>(self));
            return value<void> { };
        } else {
            return value<R>(std::invoke(std::forward<F>(f), *std::forward<decltype(self)>(self)));
        }
    }

    template <typename F>
    [[nodiscard]] constexpr decltype(auto) and_then(this auto&& self, F&& f)
    {
        return std::invoke(std::forward<F>(f), *std::forward<decltype(self)>(self));
    }

    template <typename F>
    constexpr decltype(auto) inspect(this auto&& self, F&& f)
    {
        std::invoke(std::forward<F>(f), self.data_);
        return std::forward<decltype(self)>(self);
    }

    constexpr void swap(value& other) noexcept(
        std::is_nothrow_swappable_v<T>)
    {
        using std::swap;
        swap(data_, other.data_);
    }

private:
    T data_ { };
};

template <typename T>
class value<T&> {
public:
    using value_type = T;

    constexpr explicit value(T& v) noexcept
        : ptr_(std::addressof(v))
    {
    }

    constexpr explicit value(std::reference_wrapper<T> ref) noexcept
        : ptr_(std::addressof(ref.get()))
    {
    }

    constexpr value(const value&) = default;
    constexpr value(value&&) noexcept = default;
    constexpr value& operator=(const value&) = default;
    constexpr value& operator=(value&&) noexcept = default;
    constexpr ~value() = default;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return ptr_ != nullptr;
    }

    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept
    {
        return *self.ptr_;
    }

    template <typename Self>
    [[nodiscard]] constexpr auto operator->(this Self&& self) noexcept
    {
        return self.ptr_;
    }

    template <typename F>
    [[nodiscard]] constexpr auto transform(this auto&& self, F&& f)
    {
        using R = std::remove_cvref_t<std::invoke_result_t<F, decltype(*std::forward<decltype(self)>(self))>>;
        if constexpr (std::is_void_v<R>) {
            std::invoke(std::forward<F>(f), *std::forward<decltype(self)>(self));
            return value<void> { };
        } else {
            return value<R>(std::invoke(std::forward<F>(f), *std::forward<decltype(self)>(self)));
        }
    }

    template <typename F>
    [[nodiscard]] constexpr decltype(auto) and_then(this auto&& self, F&& f)
    {
        return std::invoke(std::forward<F>(f), *std::forward<decltype(self)>(self));
    }

    template <typename F>
    constexpr decltype(auto) inspect(this auto&& self, F&& f)
    {
        std::invoke(std::forward<F>(f), *std::forward<decltype(self)>(self));
        return std::forward<decltype(self)>(self);
    }

    constexpr void swap(value& other) noexcept
    {
        using std::swap;
        swap(ptr_, other.ptr_);
    }

private:
    T* ptr_ { nullptr };
};

template <typename T>
    requires(std::is_void_v<T> || std::is_same_v<std::remove_cv_t<T>, std::monostate>)
class shared_value<T> : public value<T> {
public:
    using value<T>::value;

    [[nodiscard]] constexpr bool unique() const noexcept { return true; }
    [[nodiscard]] constexpr std::size_t use_count() const noexcept { return 0; }
};

template <typename T>
    requires(std::copy_constructible<T>
        && !std::is_same_v<std::remove_cv_t<T>, std::monostate>)
class shared_value<T> {
public:
    using value_type = T;

    constexpr shared_value() noexcept = default;

    constexpr explicit shared_value(const T& v)
        : ptr_(std::make_shared<T>(v))
    {
    }

    constexpr explicit shared_value(T&& v)
        : ptr_(std::make_shared<T>(std::move(v)))
    {
    }

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    constexpr explicit shared_value(std::in_place_t, Args&&... args)
        : ptr_(std::make_shared<T>(std::forward<Args>(args)...))
    {
    }

    constexpr shared_value(const shared_value&) = default;
    constexpr shared_value(shared_value&&) noexcept = default;
    constexpr shared_value& operator=(const shared_value&) = default;
    constexpr shared_value& operator=(shared_value&&) noexcept = default;
    constexpr ~shared_value() = default;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return ptr_ != nullptr;
    }

    [[nodiscard]] const T& read() const noexcept
    {
        assert(ptr_ != nullptr);
        return *ptr_;
    }

    [[nodiscard]] T& write() noexcept
    {
        detach();
        assert(ptr_ != nullptr);
        return *ptr_;
    }

    template <typename Self>
    [[nodiscard]] constexpr auto operator*(this Self&& self) noexcept
        -> std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const T&, T&>
    {
        if constexpr (!std::is_const_v<std::remove_reference_t<Self>>) {
            self.detach();
        }
        assert(self.ptr_ != nullptr);
        return *self.ptr_;
    }

    template <typename Self>
    [[nodiscard]] constexpr auto operator->(this Self&& self) noexcept
        -> std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const T*, T*>
    {
        if constexpr (!std::is_const_v<std::remove_reference_t<Self>>) {
            self.detach();
        }
        assert(self.ptr_ != nullptr);
        return self.ptr_.get();
    }

    [[nodiscard]]
    bool unique() const noexcept
    {
        return ptr_.use_count() <= 1;
    }

    [[nodiscard]]
    std::size_t use_count() const noexcept
    {
        return ptr_.use_count();
    }

    template <typename F>
    [[nodiscard]] constexpr auto transform(this auto&& self, F&& f)
    {
        if constexpr (!std::is_const_v<std::remove_reference_t<decltype(self)>>) {
            self.detach();
        }

        assert(self.ptr_ != nullptr);

        using R = std::remove_cvref_t<std::invoke_result_t<F, decltype(*std::forward<decltype(self)>(self))>>;

        if constexpr (std::is_void_v<R>) {
            std::invoke(std::forward<F>(f), *std::forward<decltype(self)>(self));
            return shared_value<void> { };
        } else {
            return shared_value<R>(std::invoke(std::forward<F>(f), *std::forward<decltype(self)>(self)));
        }
    }

    template <typename F>
    [[nodiscard]] constexpr decltype(auto) and_then(this auto&& self, F&& f)
    {
        if constexpr (!std::is_const_v<std::remove_reference_t<decltype(self)>>) {
            self.detach();
        }

        assert(self.ptr_ != nullptr);

        return std::invoke(std::forward<F>(f), *std::forward<decltype(self)>(self));
    }

    template <typename F>
    [[nodiscard]] constexpr decltype(auto) inspect(this auto&& self, F&& f)
    {
        if constexpr (!std::is_const_v<std::remove_reference_t<decltype(self)>>) {
            self.detach();
        }

        assert(self.ptr_ != nullptr);

        std::invoke(std::forward<F>(f), *std::forward<decltype(self)>(self));
        return std::forward<decltype(self)>(self);
    }

    template <typename F>
    constexpr decltype(auto) modify(this auto&& self, F&& f)
    {
        if constexpr (!std::is_const_v<std::remove_reference_t<decltype(self)>>) {
            self.detach();
        }

        assert(self.ptr_ != nullptr);

        return std::invoke(std::forward<F>(f), *self.ptr_);
    }

    constexpr void swap(shared_value& other) noexcept
    {
        using std::swap;
        swap(ptr_, other.ptr_);
    }

private:
    void detach()
    {
        if (ptr_.use_count() > 1) {
            ptr_ = std::make_shared<T>(*ptr_);
        }
    }

    std::shared_ptr<T> ptr_ { nullptr };
};

struct context {
    std::optional<std::int32_t> priority;
    std::optional<duration_t> delay_for;
    std::optional<time_point_t> execute_at;

    std::optional<std::uint64_t> id;
    std::optional<std::uint64_t> causation_id;

    std::optional<std::uint64_t> tags;

    std::optional<std::string> trace_id;
    std::optional<std::uint64_t> span_id;
    std::optional<std::uint64_t> parent_span_id;
};

template <typename P, typename Context>
concept predicate_of = requires(const P& p, const Context& ctx) {
    { p(ctx) } -> std::convertible_to<bool>;
};

template <typename T, typename Compare>
constexpr bool match(
    const std::optional<T>& pub,
    const std::optional<T>& sub,
    Compare cmp) noexcept
{
    if (!sub.has_value())
        return true;
    if (!pub.has_value())
        return false;
    return cmp(*pub, *sub);
}

struct always_pred {
    template <typename Context>
    [[nodiscard]] static constexpr bool operator()(const Context&) noexcept
    {
        return true;
    }
};

struct never_pred {
    template <typename Context>
    [[nodiscard]] static constexpr bool operator()(const Context&) noexcept
    {
        return false;
    }
};

template <typename Context, auto MemberPtr, typename Compare = std::equal_to<>>
struct field_pred {
    using value_t = std::remove_cvref_t<
        decltype(*std::invoke(MemberPtr, std::declval<const Context&>()))>;

    std::optional<value_t> expected;
    [[no_unique_address]] Compare cmp { };

    [[nodiscard]] constexpr bool operator()(const Context& ctx) const noexcept
    {
        return match(
            std::invoke(MemberPtr, ctx),
            expected,
            cmp);
    }
};

template <typename Context, auto MemberPtr, str_match Mode = str_match::exact>
struct str_pred {
    std::optional<std::string> expected;

    [[nodiscard]] constexpr bool operator()(const Context& ctx) const noexcept
    {
        const auto& pub = std::invoke(MemberPtr, ctx);
        if (!expected.has_value())
            return true;
        if (!pub.has_value())
            return false;
        if constexpr (Mode == str_match::exact)
            return *pub == *expected;
        else if constexpr (Mode == str_match::prefix)
            return pub->starts_with(*expected);
        else if constexpr (Mode == str_match::contains)
            return pub->find(*expected) != std::string::npos;
        else
            return pub->ends_with(*expected);
    }
};

template <typename Context, auto MemberPtr, bits_match Mode = bits_match::exact>
struct bits_pred {
    std::optional<std::uint64_t> mask;

    [[nodiscard]] constexpr bool operator()(const Context& ctx) const noexcept
    {
        const auto& pub = std::invoke(MemberPtr, ctx);
        if (!mask.has_value())
            return true;
        if (!pub.has_value())
            return false;
        if constexpr (Mode == bits_match::exact)
            return *pub == *mask;
        else if constexpr (Mode == bits_match::all)
            return (*pub & *mask) == *mask;
        else if constexpr (Mode == bits_match::any)
            return (*pub & *mask) != 0;
        else
            return (*pub & *mask) == 0;
    }
};

template <typename L, typename R>
struct and_pred {
    [[no_unique_address]] L left;
    [[no_unique_address]] R right;

    template <typename Context>
        requires predicate_of<L, Context> && predicate_of<R, Context>
    [[nodiscard]] constexpr bool operator()(const Context& ctx) const
        noexcept(noexcept(left(ctx)) && noexcept(right(ctx)))
    {
        return left(ctx) && right(ctx);
    }
};

template <typename L, typename R>
struct or_pred {
    [[no_unique_address]] L left;
    [[no_unique_address]] R right;

    template <typename Context>
        requires predicate_of<L, Context> && predicate_of<R, Context>
    [[nodiscard]] constexpr bool operator()(const Context& ctx) const
        noexcept(noexcept(left(ctx)) && noexcept(right(ctx)))
    {
        return left(ctx) || right(ctx);
    }
};

template <typename L, typename R>
struct xor_pred {
    [[no_unique_address]] L left;
    [[no_unique_address]] R right;

    template <typename Context>
        requires predicate_of<L, Context> && predicate_of<R, Context>
    [[nodiscard]] constexpr bool operator()(const Context& ctx) const
        noexcept(noexcept(left(ctx)) && noexcept(right(ctx)))
    {
        return left(ctx) != right(ctx);
    }
};

template <typename P>
struct not_pred {
    [[no_unique_address]] P inner;

    template <typename Context>
        requires predicate_of<P, Context>
    [[nodiscard]] constexpr bool operator()(const Context& ctx) const
        noexcept(noexcept(inner(ctx)))
    {
        return !inner(ctx);
    }
};

template <typename L, typename R>
    requires predicate_of<std::decay_t<L>, context> && predicate_of<std::decay_t<R>, context>
constexpr auto operator&&(L&& left, R&& right)
{
    return and_pred<std::decay_t<L>, std::decay_t<R>> { std::forward<L>(left), std::forward<R>(right) };
}

template <typename L, typename R>
    requires predicate_of<std::decay_t<L>, context> && predicate_of<std::decay_t<R>, context>
constexpr auto operator||(L&& left, R&& right)
{
    return or_pred<std::decay_t<L>, std::decay_t<R>> { std::forward<L>(left), std::forward<R>(right) };
}

template <typename P>
    requires predicate_of<std::decay_t<P>, context>
constexpr auto operator!(P&& pred)
{
    return not_pred<std::decay_t<P>> { std::forward<P>(pred) };
}

template <executor_t Executor>
class scheduler {
public:
#if __cpp_lib_move_only_function >= 202110L
    using scheduled_func_t = std::move_only_function<void() const>;
#else
    using scheduled_func_t = std::function<void()>;
#endif

    struct scheduled_task {
        time_point_t execute_at;
        scheduled_func_t func;

        [[nodiscard]] constexpr bool operator>(const scheduled_task& other) const noexcept
        {
            return execute_at > other.execute_at;
        }
    };

    explicit scheduler(Executor& executor) noexcept
        : executor_ { executor }
    {
    }

    ~scheduler()
    {
        if (scheduler_thread_.joinable()) {
            scheduler_thread_.request_stop();
            cv_.notify_all();
            scheduler_thread_.join();
        }
    }

    void schedule_after(duration_t delay, scheduled_func_t task)
    {
        schedule_at(steady_clock_t::now() + delay, std::move(task));
    }

    void schedule_at(time_point_t time, scheduled_func_t task)
    {
        bool should_notify = false;
        {
            std::lock_guard lock(mutex_);

            if (!scheduler_thread_.joinable()) {
                scheduler_thread_ = std::jthread([this](std::stop_token st) {
                    dispatch_loop(st);
                });
            }

            should_notify = tasks_.empty() || time < tasks_.front().execute_at;

            tasks_.push_back(scheduled_task {
                .execute_at = time,
                .func = std::move(task) });
            std::ranges::push_heap(tasks_, std::greater<> { });
        }
        if (should_notify)
            cv_.notify_one();
    }

private:
    void dispatch_loop(std::stop_token st)
    {
        std::unique_lock lock(mutex_);

        while (!st.stop_requested()) {
            if (tasks_.empty()) {
                cv_.wait(lock, st, [this] { return !tasks_.empty(); });
                if (st.stop_requested())
                    break;
            }

            auto now = steady_clock_t::now();
            const auto& top = tasks_.front();
            auto exec_time = top.execute_at;

            if (now >= exec_time) {
                std::ranges::pop_heap(tasks_, std::greater<> { });
                auto node = std::move(tasks_.back());
                tasks_.pop_back();

                lock.unlock();
                executor_.post(std::move(node.func));
                lock.lock();
            } else {
                cv_.wait_until(lock, st, exec_time, [this, exec_time] {
                    return tasks_.empty() || tasks_.front().execute_at < exec_time;
                });
            }
        }
    }

    Executor& executor_;

    std::vector<scheduled_task> tasks_;
    std::mutex mutex_;
    std::condition_variable_any cv_;

    std::jthread scheduler_thread_;
};

template <executor_t Executor>
class bus {
public:
    explicit bus(Executor& executor) noexcept
        : executor_ { executor }
        , scheduler_ { executor }
    {
    }

    template <event_tag_t Tag>
    class router;

    template <event_tag_t Tag>
    class filter;

    struct subscriber {
        subscription_id_t id;
        std::shared_ptr<task_t> callback;
        filter_t filter;
    };

    template <event_tag_t Tag>
        requires(std::is_void_v<typename Tag::value_t>
            || std::is_same_v<typename Tag::value_t, std::monostate>
            || std::is_same_v<typename Tag::value_t, value<void>>)
    router<Tag> post() noexcept
    {
        return router<Tag>(*this, typename Tag::value_t { });
    }

    template <event_tag_t Tag>
        requires((is_value_v<typename Tag::value_t> || is_shared_value_v<typename Tag::value_t>)
            && !std::is_void_v<typename Tag::value_t>
            && !std::is_same_v<typename Tag::value_t, std::monostate>)
    router<Tag> post(const typename Tag::value_t& value) noexcept
    {
        return router<Tag>(*this, typename Tag::value_t { value });
    }

    template <event_tag_t Tag>
        requires((is_value_v<typename Tag::value_t> || is_shared_value_v<typename Tag::value_t>)
            && !std::is_void_v<typename Tag::value_t>
            && !std::is_same_v<typename Tag::value_t, std::monostate>)
    router<Tag> post(typename Tag::value_t&& value) noexcept
    {
        return router<Tag>(*this, std::move(value));
    }

    template <event_tag_t Tag>
    filter<Tag> on() noexcept
    {
        return filter<Tag>(*this);
    }

    template <event_tag_t Tag>
    subscription_id_t bind(subscriber sub) noexcept
    {
        auto tag = std::type_index(typeid(Tag));
        auto id = next_subscription_id_.fetch_add(1, std::memory_order_relaxed);

        sub.id = id;
        subscription_index_.insert_or_assign(id, tag);
        subscribers_[tag].push_back(std::move(sub));
        return id;
    }

    subscription_id_t unsubscribe(subscription_id_t id) noexcept
    {
        auto idx_it = subscription_index_.find(id);
        if (idx_it == subscription_index_.end())
            return subscription_id_t { 0 };

        auto tag = idx_it->second;
        subscription_index_.erase(idx_it);

        auto& vec = subscribers_[tag];
        auto it = std::find_if(vec.begin(), vec.end(), [id](const subscriber& s) {
            return s.id == id;
        });
        if (it == vec.end())
            return subscription_id_t { 0 };

        vec.erase(it);
        if (vec.empty())
            subscribers_.erase(tag);

        return id;
    }

    template <event_tag_t Tag>
    std::size_t subscriber_count() const noexcept
    {
        auto it = subscribers_.find(std::type_index(typeid(Tag)));
        return it != subscribers_.end() ? it->second.size() : 0;
    }

    template <event_tag_t Tag>
    bool has_subscribers() const noexcept
    {
        return subscriber_count<Tag>() > 0;
    }

    template <event_tag_t Tag>
    void do_sync(const Tag::value_t& value, const context& ctx) noexcept
    {
        auto it = subscribers_.find(std::type_index(typeid(Tag)));
        if (it == subscribers_.end())
            return;

        for (const auto& sub : it->second) {
            if (sub.filter(ctx)) {
                (*sub.callback)(&value, ctx);
            }
        }
    }

    template <event_tag_t Tag>
    void do_async(const Tag::value_t& value, const context& ctx) noexcept
    {
        auto it = subscribers_.find(std::type_index(typeid(Tag)));
        if (it == subscribers_.end())
            return;

        for (const auto& sub : it->second) {
            if (sub.filter(ctx)) {
                auto cb = sub.callback;

                if (ctx.delay_for) {
                    scheduler_.schedule_after(*ctx.delay_for, [cb, value, ctx] {
                        (*cb)(&value, ctx);
                    });
                } else if (ctx.execute_at) {
                    scheduler_.schedule_at(*ctx.execute_at, [cb, value, ctx] {
                        (*cb)(&value, ctx);
                    });
                } else {
                    executor_.post([cb, value, ctx] {
                        (*cb)(&value, ctx);
                    });
                }
            }
        }
    }

private:
    Executor& executor_;
    scheduler<Executor> scheduler_;

    std::atomic<subscription_id_t> next_subscription_id_ { 1 };

    std::flat_map<std::type_index, std::vector<subscriber>> subscribers_;
    std::flat_map<subscription_id_t, std::type_index> subscription_index_;

public:
    template <event_tag_t Tag>
    class router {
    public:
        constexpr ~router() = default;

        router(const router&) = delete;
        router(router&&) = delete;

        router& operator=(const router&) = delete;
        router& operator=(router&&) = delete;

        constexpr router& with_priority(std::int32_t p) noexcept
        {
            ctx_.priority = p;
            return *this;
        }

        constexpr router& delay_for(duration_t d) noexcept
        {
            ctx_.delay_for = d;
            return *this;
        }

        constexpr router& execute_at(time_point_t t) noexcept
        {
            ctx_.execute_at = t;
            return *this;
        }

        constexpr router& with_id(std::uint64_t id) noexcept
        {
            ctx_.id = id;
            return *this;
        }

        constexpr router& caused_by(std::uint64_t causation_id) noexcept
        {
            ctx_.causation_id = causation_id;
            return *this;
        }

        constexpr router& with_tags(std::uint64_t tags) noexcept
        {
            ctx_.tags = tags;
            return *this;
        }

        constexpr router& with_trace_id(std::string trace_id) noexcept
        {
            ctx_.trace_id = std::move(trace_id);
            return *this;
        }

        constexpr router& with_span_id(std::uint64_t span_id) noexcept
        {
            ctx_.span_id = span_id;
            return *this;
        }

        constexpr router& with_parent_span_id(std::uint64_t parent_span_id) noexcept
        {
            ctx_.parent_span_id = parent_span_id;
            return *this;
        }

        constexpr router& with_context(context ctx) noexcept
        {
            ctx_ = std::move(ctx);
            return *this;
        }

        constexpr void sync() noexcept
            requires std::is_same_v<typename Tag::value_t, std::monostate>
            || is_value_v<typename Tag::value_t>
        {
            bus_.template do_sync<Tag>(value_, ctx_);
        }

        constexpr void async() noexcept
            requires std::is_same_v<typename Tag::value_t, std::monostate>
            || is_shared_value_v<typename Tag::value_t>
        {
            bus_.template do_async<Tag>(value_, ctx_);
        }

    private:
        bus& bus_;
        context ctx_ { };
        typename Tag::value_t value_;

        friend class bus;

        explicit router(bus& b, typename Tag::value_t value) noexcept
            : bus_ { b }
            , value_ { std::move(value) }
        {
        }
    };

    template <event_tag_t Tag>
    class filter {
    public:
        constexpr ~filter() = default;

        filter(const filter&) = delete;
        filter(filter&&) = delete;
        filter& operator=(const filter&) = delete;
        filter& operator=(filter&&) = delete;

        constexpr filter& min_priority(std::uint32_t p) noexcept
        {
            attach_pred(field_pred<context, &context::priority, std::greater_equal<>> { .expected = p });
            return *this;
        }

        constexpr filter& require_id(std::uint64_t id) noexcept
        {
            attach_pred(field_pred<context, &context::id> { .expected = id });
            return *this;
        }

        constexpr filter& require_causation_id(std::uint64_t causation_id) noexcept
        {
            attach_pred(field_pred<context, &context::causation_id> { .expected = causation_id });
            return *this;
        }

        constexpr filter& and_tags(std::uint64_t tags) noexcept
        {
            attach_pred(bits_pred<context, &context::tags, bits_match::all> { .mask = tags });
            return *this;
        }

        constexpr filter& or_tags(std::uint64_t tags) noexcept
        {
            attach_pred(bits_pred<context, &context::tags, bits_match::any> { .mask = tags });
            return *this;
        }

        constexpr filter& not_tags(std::uint64_t tags) noexcept
        {
            attach_pred(bits_pred<context, &context::tags, bits_match::none> { .mask = tags });
            return *this;
        }

        constexpr filter& require_trace_id(std::string trace_id) noexcept
        {
            attach_pred(field_pred<context, &context::trace_id> { .expected = std::move(trace_id) });
            return *this;
        }

        constexpr filter& require_span_id(std::uint64_t span_id) noexcept
        {
            attach_pred(field_pred<context, &context::span_id> { .expected = span_id });
            return *this;
        }

        constexpr filter& require_parent_span_id(std::uint64_t parent_span_id) noexcept
        {
            attach_pred(field_pred<context, &context::parent_span_id> { .expected = parent_span_id });
            return *this;
        }

        template <predicate_of<context> P>
        constexpr filter& where(P&& pred) noexcept
        {
            attach_pred(std::forward<P>(pred));
            return *this;
        }

        template <typename F>
            requires(!std::is_same_v<typename Tag::value_t, std::monostate>
                && std::invocable<F, const typename Tag::value_t&, const context&>)
        subscription_id_t call(F&& f) noexcept
        {
            auto cb = task_t([func = std::forward<F>(f)](const void* v, const context& ctx) {
                func(*static_cast<const typename Tag::value_t*>(v), ctx);
            });

            subscriber sub {
                .callback = std::make_shared<task_t>(std::move(cb)),
                .filter = std::move(pred_),
            };

            return bus_.template bind<Tag>(std::move(sub));
        }

        template <typename F>
            requires(!std::is_same_v<typename Tag::value_t, std::monostate>
                && std::invocable<F, const typename Tag::value_t&>
                && !std::invocable<F, const typename Tag::value_t&, const context&>
                && !std::invocable<F, const context&>)
        subscription_id_t call(F&& f) noexcept
        {
            auto cb = task_t([func = std::forward<F>(f)](const void* v, const context&) {
                func(*static_cast<const typename Tag::value_t*>(v));
            });

            subscriber sub {
                .callback = std::make_shared<task_t>(std::move(cb)),
                .filter = std::move(pred_),
            };

            return bus_.template bind<Tag>(std::move(sub));
        }

        template <typename F>
            requires std::invocable<F, const context&>
        subscription_id_t call(F&& f) noexcept
        {
            auto cb = task_t([func = std::forward<F>(f)](const void*, const context& ctx) {
                func(ctx);
            });

            subscriber sub {
                .callback = std::make_shared<task_t>(std::move(cb)),
                .filter = std::move(pred_),
            };

            return bus_.template bind<Tag>(std::move(sub));
        }

        template <typename F>
            requires(std::invocable<F> && !std::invocable<F, const context&>)
        subscription_id_t call(F&& f) noexcept
        {
            auto cb = task_t([func = std::forward<F>(f)](const void*, const context&) {
                func();
            });

            subscriber sub {
                .callback = std::make_shared<task_t>(std::move(cb)),
                .filter = std::move(pred_),
            };

            return bus_.template bind<Tag>(std::move(sub));
        }

        template <typename T>
        subscription_id_t call(T& obj, void (T::*method)()) noexcept
        {
            return call([&obj, method](const context&) {
                (obj.*method)();
            });
        }

        template <typename T>
        subscription_id_t call(T& obj, void (T::*method)() const) noexcept
        {
            return call([&obj, method](const context&) {
                (obj.*method)();
            });
        }

        template <typename T>
        subscription_id_t call(T* obj, void (T::*method)()) noexcept
        {
            return call([obj, method](const context&) {
                (obj->*method)();
            });
        }

        template <typename T>
        subscription_id_t call(T* obj, void (T::*method)() const) noexcept
        {
            return call([obj, method](const context&) {
                (obj->*method)();
            });
        }

        // — member function: (const value_t&, const context&) —
        template <typename T>
            requires(!std::is_same_v<typename Tag::value_t, std::monostate>)
        subscription_id_t call(T& obj, void (T::*method)(const typename Tag::value_t&, const context&)) noexcept
        {
            return call([&obj, method](const typename Tag::value_t& v, const context& ctx) {
                (obj.*method)(v, ctx);
            });
        }

        template <typename T>
            requires(!std::is_same_v<typename Tag::value_t, std::monostate>)
        subscription_id_t call(T& obj, void (T::*method)(const typename Tag::value_t&, const context&) const) noexcept
        {
            return call([&obj, method](const typename Tag::value_t& v, const context& ctx) {
                (obj.*method)(v, ctx);
            });
        }

        template <typename T>
            requires(!std::is_same_v<typename Tag::value_t, std::monostate>)
        subscription_id_t call(T* obj, void (T::*method)(const typename Tag::value_t&, const context&)) noexcept
        {
            return call([obj, method](const typename Tag::value_t& v, const context& ctx) {
                (obj->*method)(v, ctx);
            });
        }

        template <typename T>
            requires(!std::is_same_v<typename Tag::value_t, std::monostate>)
        subscription_id_t call(T* obj, void (T::*method)(const typename Tag::value_t&, const context&) const) noexcept
        {
            return call([obj, method](const typename Tag::value_t& v, const context& ctx) {
                (obj->*method)(v, ctx);
            });
        }

        // — member function: (const value_t&) —
        template <typename T>
            requires(!std::is_same_v<typename Tag::value_t, std::monostate>)
        subscription_id_t call(T& obj, void (T::*method)(const typename Tag::value_t&)) noexcept
        {
            return call([&obj, method](const typename Tag::value_t& v) {
                (obj.*method)(v);
            });
        }

        template <typename T>
            requires(!std::is_same_v<typename Tag::value_t, std::monostate>)
        subscription_id_t call(T& obj, void (T::*method)(const typename Tag::value_t&) const) noexcept
        {
            return call([&obj, method](const typename Tag::value_t& v) {
                (obj.*method)(v);
            });
        }

        template <typename T>
            requires(!std::is_same_v<typename Tag::value_t, std::monostate>)
        subscription_id_t call(T* obj, void (T::*method)(const typename Tag::value_t&)) noexcept
        {
            return call([obj, method](const typename Tag::value_t& v) {
                (obj->*method)(v);
            });
        }

        template <typename T>
            requires(!std::is_same_v<typename Tag::value_t, std::monostate>)
        subscription_id_t call(T* obj, void (T::*method)(const typename Tag::value_t&) const) noexcept
        {
            return call([obj, method](const typename Tag::value_t& v) {
                (obj->*method)(v);
            });
        }

        // — member function: (const context&) —
        template <typename T>
        subscription_id_t call(T& obj, void (T::*method)(const context&)) noexcept
        {
            return call([&obj, method](const context& ctx) {
                (obj.*method)(ctx);
            });
        }

        template <typename T>
        subscription_id_t call(T& obj, void (T::*method)(const context&) const) noexcept
        {
            return call([&obj, method](const context& ctx) {
                (obj.*method)(ctx);
            });
        }

        template <typename T>
        subscription_id_t call(T* obj, void (T::*method)(const context&)) noexcept
        {
            return call([obj, method](const context& ctx) {
                (obj->*method)(ctx);
            });
        }

        template <typename T>
        subscription_id_t call(T* obj, void (T::*method)(const context&) const) noexcept
        {
            return call([obj, method](const context& ctx) {
                (obj->*method)(ctx);
            });
        }

    private:
        bus& bus_;
        filter_t pred_ = always_pred { };

        template <predicate_of<context> C>
        void attach_pred(C&& c) noexcept
        {
            auto previous = std::move(pred_);
            pred_ = [prev = std::move(previous), next = std::forward<C>(c)](const context& ctx) -> bool {
                return prev(ctx) && next(ctx);
            };
        }

        friend class bus;

        explicit filter(bus& b) noexcept
            : bus_ { b }
        {
        }
    };
};

} // namespace cbuspp