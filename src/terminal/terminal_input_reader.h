#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>

#include "terminal/terminal_console_api.h"

namespace asciiomium::terminal {

struct InputDrainResult {
  // Also true when thread creation never succeeded and there is none to join.
  bool joined = true;
  bool handle_closed = true;
  DWORD error = ERROR_SUCCESS;

  [[nodiscard]] bool succeeded() const noexcept {
    return joined && handle_closed;
  }
};

// The sole console character consumer. ReadConsoleW avoids lossy conversion in
// older console hosts; bounded incremental UTF-16 conversion preserves the
// parser's UTF-8/VT byte transport. A fixed mailbox applies backpressure.
class TerminalInputReader {
 public:
  static constexpr std::size_t kCapacity = 4096;
  static constexpr std::size_t kReadUnits = 1024;
  static constexpr int kFatalDrainExitCode = 12;

  TerminalInputReader(HANDLE input, const TerminalConsoleApi& api);
  ~TerminalInputReader();
  TerminalInputReader(const TerminalInputReader&) = delete;
  TerminalInputReader& operator=(const TerminalInputReader&) = delete;

  // Start and StopAndJoin are called by the session's normal ownership path.
  // RequestStop/finished/input_released are also safe from a control callback.
  void Start();
  void RequestStop() noexcept;
  [[nodiscard]] bool finished() const noexcept;
  // A returned read alone does not dispose of a console-host waiter. Input
  // restoration is safe only after the joined reader's own handle is closed.
  [[nodiscard]] bool input_released() const noexcept;
  [[nodiscard]] std::string TakeAvailable(std::size_t max_bytes);
  [[nodiscard]] InputDrainResult StopAndJoin(
      std::chrono::milliseconds timeout = std::chrono::seconds(2)) noexcept;

  [[noreturn]] static void FatalDrainFailure(InputDrainResult result) noexcept;

 private:
  struct State;
  static void Run(const std::shared_ptr<State>& state) noexcept;
  [[nodiscard]] InputDrainResult CloseInput() noexcept;
  std::shared_ptr<State> state_;
  std::thread worker_;
};

}  // namespace asciiomium::terminal
