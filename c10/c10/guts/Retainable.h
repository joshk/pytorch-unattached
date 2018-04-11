#pragma once

#include <atomic>
#include <stdexcept>

namespace c10 { namespace guts {

// Base for intrusive refcounting
class RetainableImpl {
  std::atomic<int> refcount_;

  template <typename T, typename TImpl, typename NullType>
  friend class Retainable;

public:
  virtual ~RetainableImpl() {}

protected:
  RetainableImpl() : refcount_(1) {}
};

// Impl cannot be a dependent type, because this template is being used as a CRTP, and at the
// time of template instantiation we don't know anything about T yet.
template <typename T, typename TImpl, typename NullType>
class Retainable {
  TImpl *pImpl;

  void retain() {
    if (pImpl == NullType::singleton()) return;
    ++pImpl->refcount_;
  }

  void release() {
    if (pImpl == NullType::singleton()) return;
    if (--pImpl->refcount_ == 0) {
      delete pImpl;
    }
  }

protected:

  Retainable() : pImpl(NullType::singleton()) {}

  // NB: invariant: if self == nullptr, then nullptr == NullType::singleton()
  Retainable(TImpl *self) : pImpl(self) {}

  Retainable(const T &rhs)
      : pImpl(rhs.pImpl) {
    if (pImpl != NullType::singleton()) {
      retain();
    }
  }

  Retainable(T &&rhs) noexcept
      : pImpl(rhs.pImpl) {
    rhs.pImpl = NullType::singleton();
  }

  // NB: Non-virtual!!!
  ~Retainable() {
    if (pImpl != NullType::singleton()) {
      release();
    }
  }

  inline TImpl *get() const {
    return pImpl;
  }

  TImpl *detach() {
    TImpl *ret = pImpl;
    pImpl = NullType::singleton();
    return ret;
  }

public:
  // Copy assignment
  T &operator=(T &&rhs) &noexcept {
    // smessmer to @ezyang: I'd explicitly set rhs to undefined for better debugability.
    // ezyang to @smessmer: That's a bunch of extra refcount bumps though, isn't it?
    rhs.swap(*this);
    return *this;
  }

  T &operator=(T const &rhs) &{
    //TensorBase ctor retains original rhs.pImpl
    //then rhs.pImpl is swapped with this->pImpl
    //finally TensorBase dtor releases rhs.pImpl, which was originally this->pImpl
    T(rhs).swap(*this);
    return *this;
  }

  void reset() {
    T().swap(*this);
  }

  void swap(T &rhs) noexcept {
    TImpl *tmp = pImpl;
    pImpl = rhs.pImpl;
    rhs.pImpl = tmp;
  }

  // We do a lot of null-pointer checks in our code, good to have this be cheap.
  bool defined() const {
    return pImpl != NullType::singleton();
  }
};

}} // namespace c10::guts