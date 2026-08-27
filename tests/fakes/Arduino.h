#pragma once

#include <algorithm>

class FakeSerialType {
 public:
  template <typename... Args>
  void printf(const char*, Args...) {}

  template <typename T>
  void println(const T&) {}

  void println() {}
};

extern FakeSerialType Serial;

using std::min;
