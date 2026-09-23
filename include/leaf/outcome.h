#pragma once

#include <leaf/error.h>

#include <variant>

namespace leaf {

template <class T>
class Outcome final {
 public:
  static Outcome success(T value) noexcept { return Outcome(std::move(value)); }

  static Outcome failure(Error error) noexcept { return Outcome(std::move(error)); }

  bool hasValue() const noexcept { return state_.index() == 0; }

  const T* value() const noexcept {
    return hasValue() ? &std::get<0>(state_) : nullptr;
  }

  T* value() noexcept { return hasValue() ? &std::get<0>(state_) : nullptr; }

  const Error* error() const noexcept {
    return hasValue() ? nullptr : &std::get<1>(state_);
  }

 private:
  explicit Outcome(T value) noexcept : state_(std::move(value)) {}
  explicit Outcome(Error error) noexcept : state_(std::move(error)) {}

  std::variant<T, Error> state_;
};

}  // namespace leaf
