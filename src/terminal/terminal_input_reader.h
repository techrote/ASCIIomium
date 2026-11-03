#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>

#include "terminal/terminal_console_api.h"

namespace asciiomium::terminal {

struct InputDrainResult {
  bool joined = true;
  DWORD error = ERROR_SUCCESS;
};

// The sole console byte consumer. A fixed mailbox applies backpressure instead
// of dropping or growing input. No console-event API or UTF conversion occurs
// here: ReadFile + CP_UTF8 remains the exact VT transport used by the parser.
class TerminalInputReader {
 public:
  static constexpr std::size_t kCapacity = 4096;
  static constexpr int kFatalDrainExitCode = 12;

  TerminalInputReader(HANDLE input, const TerminalConsoleApi& api);
  ~TerminalInputReader();
  TerminalInputReader(const TerminalInputReader&) = delete;
  TerminalInputReader& operator=(const TerminalInputReader&) = delete;

  // Start and StopAndJoin are called by the session's normal ownership path.
  // RequestStop/finished are also safe from the console control callback.
  void Start();
  void RequestStop() noexcept;
  [[nodiscard]] bool finished() const noexcept;
  [[nodiscard]] std::string TakeAvailable(std::size_t max_bytes);
  [[nodiscard]] InputDrainResult StopAndJoin(
      std::chrono::milliseconds timeout = std::chrono::seconds(2)) noexcept;

  [[noreturn]] static void FatalDrainFailure(DWORD error) noexcept;

 private:
  struct State;
  static void Run(const std::shared_ptr<State>& state) noexcept;
  std::shared_ptr<State> state_;
  std::thread worker_;
};

}  // namespace asciiomium::terminal
