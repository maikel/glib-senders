#ifndef GLIB_SENDERS_SEQUENCE_HPP
#define GLIB_SENDERS_SEQUENCE_HPP

#include "glib-senders/atomic_intrusive_queue.hpp"

#include <atomic>
#include <concepts>
#include <optional>
#include <ranges>
#include <stdexec/execution.hpp>
#include <tuple>
#include <variant>

namespace gsenders {
namespace sequence_ {
using env_t = stdexec::__make_env_t<
    stdexec::__empty_env,
    stdexec::__with<stdexec::get_stop_token_t, stdexec::in_place_stop_token>>;

enum class operation_status { start, running, done };

template <class... Senders> struct completion_signatures;

template <class Sender, class... Senders>
struct completion_signatures<Sender, Senders...> {
  using AddSigl = stdexec::__minvoke<
      stdexec::__mconcat<stdexec::__q<stdexec::completion_signatures>>,
      stdexec::completion_signatures_of_t<Senders>...>;

  using type =
      stdexec::make_completion_signatures<Sender, stdexec::no_env, AddSigl>;
};

template <class... Senders>
using completion_signatures_t =
    typename completion_signatures<Senders...>::type;

template <class Sig> struct signature_to_tuple;

template <class Ret, class... Args> struct signature_to_tuple<Ret(Args...)> {
  using type = std::tuple<Ret, Args...>;
};

template <class Sig>
using signature_to_tuple_t = typename signature_to_tuple<Sig>::type;

template <class... Senders>
using result_type_t =
    stdexec::__mapply<stdexec::__transform<stdexec::__q<signature_to_tuple_t>,
                                           stdexec::__mbind_front_q<
                                               std::variant, std::monostate>>,
                      completion_signatures_t<Senders...>>;

template <class... Senders> struct stream;
template <class... Senders> class sender;
template <class... Senders> class cleanup_sender;

template <class... Senders> class receiver {
public:
  receiver(std::size_t rank, stream<Senders...>* stream) noexcept
      : rank_(rank), stream_(stream) {}

private:
  std::size_t rank_;
  stream<Senders...>* stream_;

  template <class CPO, class... Args> void notify(CPO, Args&&... args) noexcept;

  auto get_env() const noexcept -> env_t {
    using with_token = stdexec::__with<stdexec::get_stop_token_t,
                                       stdexec::in_place_stop_token>;
    return env_t{with_token{stream_->global_stop_source_.get_token()}};
  }

  template <stdexec::__one_of<stdexec::set_value_t, stdexec::set_stopped_t,
                              stdexec::set_error_t>
                CPO,
            class... Args>
  friend void tag_invoke(CPO cpo, receiver&& self, Args&&... args) noexcept {
    self.notify(cpo, (Args &&) args...);
  }

  friend auto tag_invoke(stdexec::get_env_t, const receiver& self) noexcept
      -> env_t {
    return self.get_env();
  }
};

template <class... Senders>
class stream : public std::ranges::view_interface<stream<Senders...>> {
public:
  static constexpr std::size_t N = sizeof...(Senders);

  stream(Senders&&... senders)
      : stream(std::make_index_sequence<N>{}, ((Senders &&) senders)...) {}

  ~stream() {
    if (count_done_.load() != N) {
      stdexec::sync_wait(cleanup());
    }
  }

  auto next() noexcept -> sender<Senders...>;

  struct sentinel {};

  struct iterator {
    stream* parent_;
    sender<Senders...> operator*() const noexcept { return parent_->next(); }

    iterator& operator++() noexcept { return *this; }
    iterator operator++(int) noexcept { return *this; }

    auto operator<=>(const iterator&) const = default;

    friend bool operator==(sentinel, iterator it) noexcept { return it.done(); }

    friend bool operator==(iterator it, sentinel end) noexcept {
      return end == it;
    }

    bool done() const noexcept { return parent_->count_done_ == N; }
  };

  auto cleanup() noexcept -> cleanup_sender<Senders...>;

  auto begin() noexcept -> iterator { return iterator{this}; }
  auto end() const noexcept -> sentinel { return sentinel{}; };

private:
  template <std::size_t... Is>
  stream(std::index_sequence<Is...>, Senders&&... senders)
      : operation_states_{stdexec::__conv{[&, this] {
          return stdexec::connect((Senders &&) senders,
                                  receiver<Senders...>{Is, this});
        }}...} {}

  template <class...> friend struct receiver;
  template <class R, class... S> friend struct operation;
  template <class R, class... S> friend struct cleanup_operation;

  std::tuple<stdexec::connect_result_t<Senders, receiver<Senders...>>...>
      operation_states_;

  std::atomic<operation_status> status_ = operation_status::start;

  std::optional<stdexec::in_place_stop_source> local_stop_source_{};
  stdexec::in_place_stop_source global_stop_source_{};

  struct result_node {
    result_type_t<Senders...> result;
    std::atomic<result_node*> next;
  };

  static auto pop_front(stream& stream) -> result_node* {
    return stream.result_queue_.pop_front();
  }

  std::array<result_node, N> result_nodes_{};
  std::atomic<std::size_t> count_done_{0};
  atomic_intrusive_queue<result_node, &result_node::next> result_queue_{};
  std::atomic<typename stream<Senders...>::result_node* (*)(stream&)>
      complete_ = nullptr;
  void* receiver_ = nullptr;
};

template <class... Senders>
template <class CPO, class... Args>
void receiver<Senders...>::notify(CPO cpo, Args&&... args) noexcept {
  try {
    stream_->result_nodes_[rank_]
        .result.template emplace<std::tuple<CPO, std::decay_t<Args>...>>(
            cpo, ((Args &&) args)...);
  } catch (...) {
    stream_->result_nodes_[rank_]
        .result
        .template emplace<std::tuple<stdexec::set_error_t, std::exception_ptr>>(
            stdexec::set_error, std::current_exception());
  }
  stream_->result_queue_.push_back(&stream_->result_nodes_[rank_]);
  int n = stream_->count_done_.fetch_add(1);
  if (n == sizeof...(Senders) - 1) {
    stream_->status_.store(operation_status::done);
  }
  typename stream<Senders...>::result_node* (*callback)(stream<Senders...>&) =
      stream_->complete_.exchange(&stream_->pop_front);
  if (callback && callback != &stream_->pop_front) {
    stream_->complete_.store(nullptr);
    callback(*stream_);
  }
}

template <class Receiver, class... Senders> struct cleanup_operation {
  stream<Senders...>* stream_;
  Receiver receiver_;

  static auto complete(stream<Senders...>& s) ->
      typename stream<Senders...>::result_node* {
    Receiver& receiver = *static_cast<Receiver*>(s.receiver_);
    std::size_t count = sizeof...(Senders);
    if (s.count_done_.compare_exchange_strong(count, 0)) {
      stdexec::set_value((Receiver &&) receiver);
      return nullptr;
    }
    s.complete_.store(&complete);
    count = sizeof...(Senders);
    if (s.count_done_.compare_exchange_strong(count, 0)) {
      stdexec::set_value((Receiver &&) receiver);
    }
    return nullptr;
  }

  void start() noexcept {
    stream_->global_stop_source_.request_stop();
    stream_->complete_.store(nullptr);
    complete(*stream_);
  }

  friend void tag_invoke(stdexec::start_t, cleanup_operation& self) noexcept {
    self.start();
  }
};

template <class... Senders> class cleanup_sender {
public:
  using completion_signatures =
      stdexec::completion_signatures<stdexec::set_value_t()>;

private:
  template <class...> friend class stream;
  stream<Senders...>* stream_{nullptr};

  explicit cleanup_sender(stream<Senders...>& stream) : stream_{&stream} {}

  template <class Receiver>
  friend auto tag_invoke(stdexec::connect_t, cleanup_sender&& self,
                         Receiver&& receiver) {
    return cleanup_operation<Receiver, Senders...>{self.stream_,
                                                   (Receiver &&) receiver};
  }

  friend auto tag_invoke(stdexec::get_env_t,
                         const cleanup_sender& self) noexcept
      -> stdexec::__empty_env {
    return {};
  }
};

template <class Receiver, class... Senders> struct operation {
  stream<Senders...>* stream_;
  Receiver receiver_;

  struct on_stop_requested {
    stdexec::in_place_stop_source& stop_source_;
    void operator()() noexcept { stop_source_.request_stop(); }
  };
  using on_stop = std::optional<typename stdexec::stop_token_of_t<
      stdexec::env_of_t<Receiver>&>::template callback_type<on_stop_requested>>;
  on_stop on_receiver_stop_{};
  using on_stream_stop = std::optional<
      stdexec::in_place_stop_token::callback_type<on_stop_requested>>;
  on_stream_stop on_stream_stop_{};

  operation(stream<Senders...>& stream, Receiver&& receiver)
      : stream_{&stream}, receiver_{(Receiver &&) receiver} {
    auto& source = stream_->local_stop_source_.emplace();
    on_receiver_stop_.emplace(
        stdexec::get_stop_token(stdexec::get_env(receiver_)),
        on_stop_requested{source});
    on_stream_stop_.emplace(stream_->global_stop_source_.get_token(),
                            on_stop_requested{source});
    stream_->receiver_ = &receiver_;
  }

  static auto complete_(Receiver&& receiver,
                        result_type_t<Senders...>&& result) noexcept -> void {
    std::visit(
        [&receiver]<class Tuple>(Tuple&& result) {
          if constexpr (std::same_as<std::decay_t<Tuple>, std::monostate>) {
            stdexec::set_stopped((Receiver &&) receiver);
          } else {
            std::apply(
                [&receiver]<class C, class... As>(C, As&&... args) noexcept {
                  try {
                    stdexec::tag_invoke(C{}, (Receiver &&) receiver,
                                        (As &&) args...);
                  } catch (...) {
                    stdexec::set_error((Receiver &&) receiver,
                                       std::current_exception());
                  }
                },
                (Tuple &&) result);
          }
        },
        (result_type_t<Senders...> &&) result);
  }

  static auto complete(stream<Senders...>& stream_) noexcept ->
      typename stream<Senders...>::result_node* {
    auto* node = stream_.result_queue_.pop_front();
    Receiver& receiver = *static_cast<Receiver*>(stream_.receiver_);
    assert(node);
    if (node) {
      complete_(std::move(receiver), std::move(node->result));
    } else {
      complete_(std::move(receiver), std::monostate{});
    }
    if (!stream_.result_queue_.empty()) {
      stream_.complete_.store(&stream_.pop_front);
    }
    return node;
  }

  void start() noexcept {
    operation_status expected = operation_status::start;
    // Check wheter we are responsible to start all operations
    stream_->status_.compare_exchange_strong(expected,
                                             operation_status::running);
    switch (expected) {
    case operation_status::start: {
      stream_->complete_.store(&complete);
      // We are responsible to start all operations
      std::apply([](auto&... o) { (stdexec::start(o), ...); },
                 stream_->operation_states_);
    } break;
    case operation_status::running: {
      // We are not responsible to start any
      // Look into the queue for the next operation
      typename stream<Senders...>::result_node* (*pop_front)(
          stream<Senders...>&) = nullptr;
      if (!stream_->complete_.compare_exchange_strong(pop_front, &complete)) {
        assert(pop_front != &complete);
        stream_->complete_.store(nullptr);
        complete(*stream_);
      }
    } break;
    case operation_status::done: {
      // We are not responsible to start all operations
      // We are responsible to complete the current operation
      complete(*stream_);
    } break;
    }
  }

  friend void tag_invoke(stdexec::start_t, operation& self) noexcept {
    self.start();
  }
};

template <class... Senders> class sender {
public:
  using completion_signatures = completion_signatures_t<Senders...>;

  sender(const sender&) = delete;
  sender& operator=(const sender&) = delete;

  sender(sender&&) = default;
  sender& operator=(sender&&) = default;

private:
  template <class...> friend class stream;
  stream<Senders...>* stream_{nullptr};

  explicit sender(stream<Senders...>& stream) : stream_{&stream} {}

  template <class Receiver>
  friend auto tag_invoke(stdexec::connect_t, sender&& self,
                         Receiver&& receiver) {
    return operation<Receiver, Senders...>(*self.stream_,
                                           (Receiver &&) receiver);
  }

  friend auto tag_invoke(stdexec::get_env_t, const sender& self) noexcept
      -> stdexec::__empty_env {
    return {};
  }
};

template <class... Senders>
auto stream<Senders...>::next() noexcept -> sender<Senders...> {
  return sender<Senders...>{*this};
}

template <class... Senders>
auto stream<Senders...>::cleanup() noexcept -> cleanup_sender<Senders...> {
  global_stop_source_.request_stop();
  return cleanup_sender<Senders...>{*this};
}

struct sequence_t {
  template <typename... Senders>
  auto operator()(Senders&&... senders) const noexcept
      -> stream<std::decay_t<Senders>...> {
    return stream(((Senders &&) senders)...);
  }
};

inline constexpr sequence_t sequence;

} // namespace sequence_

using sequence_::sequence;

} // namespace gsenders

#endif