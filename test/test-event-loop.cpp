// Copyright (C) 2020  Matthew "strager" Glazar
// See end of file for extended copyright information.

#if defined(__EMSCRIPTEN__)
// No LSP on the web.
#else

#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <quick-lint-js/container/heap-function.h>
#include <quick-lint-js/io/event-loop.h>
#include <quick-lint-js/io/pipe.h>
#include <quick-lint-js/port/char8.h>
#include <quick-lint-js/port/thread.h>
#include <quick-lint-js/spy-lsp-message-parser.h>
#include <thread>

using namespace std::literals::chrono_literals;

namespace quick_lint_js {
namespace {
void write_full_message(Platform_File_Ref, String8_View);

struct Spy_Event_Loop : public Event_Loop<Spy_Event_Loop> {
  explicit Spy_Event_Loop(Platform_File_Ref pipe) : pipe_(pipe) {}

  std::optional<Platform_File_Ref> get_readable_pipe() const {
    return this->pipe_;
  }

  void append(String8_View data) {
    std::unique_lock lock(this->mutex_);
    this->read_data_.append(data);
    this->new_data_.notify_all();

    if (this->append_callback_) {
      this->append_callback_(data);
    }
  }

  template <class Func>
  void set_append_callback(Func on_append) {
    this->append_callback_ = std::move(on_append);
  }

  void unset_readable_pipe() { this->pipe_ = std::nullopt; }

  String8 get_read_data() {
    std::unique_lock lock(this->mutex_);
    return this->read_data_;
  }

  template <class Func>
  void wait_until_data(Func&& predicate) {
    std::unique_lock lock(this->mutex_);
    this->new_data_.wait(lock, [this, &predicate]() -> bool {
      return predicate(this->read_data_);
    });
  }

#if QLJS_HAVE_KQUEUE || QLJS_HAVE_POLL
  std::optional<POSIX_FD_File_Ref> get_pipe_write_fd() const {
    return this->pipe_write_fd_;
  }

  template <class... Args>
  void on_pipe_write_event(Args&&... args) {
    this->pipe_write_event_callback_(std::forward<Args>(args)...);
  }

  template <class Func>
  void set_pipe_write(POSIX_FD_File_Ref fd, Func on_event) {
    this->pipe_write_fd_ = fd;
    this->pipe_write_event_callback_ = std::move(on_event);
  }
#endif

#if QLJS_HAVE_KQUEUE
  void on_fs_changed_kevent(const struct ::kevent&) {}
  void on_fs_changed_kevents() {}
#endif

#if QLJS_HAVE_INOTIFY
  std::optional<POSIX_FD_File_Ref> get_inotify_fd() const {
    return std::nullopt;
  }

  void on_fs_changed_event(const ::pollfd&) {}
#endif

#if defined(_WIN32)
  void on_fs_changed_event(::OVERLAPPED*,
                           [[maybe_unused]] ::DWORD number_of_bytes_transferred,
                           [[maybe_unused]] ::DWORD error) {}
#endif

 private:
  std::optional<Platform_File_Ref> pipe_;

  Mutex mutex_;
  Condition_Variable new_data_;

  // Protected by mutex_:
  String8 read_data_;

#if QLJS_HAVE_KQUEUE || QLJS_HAVE_POLL
  std::optional<POSIX_FD_File_Ref> pipe_write_fd_;
#endif

  Heap_Function<void(String8_View)> append_callback_;
#if QLJS_HAVE_KQUEUE
  Heap_Function<void(const struct ::kevent&)> pipe_write_event_callback_;
#elif QLJS_HAVE_POLL
  Heap_Function<void(const ::pollfd&)> pipe_write_event_callback_;
#endif
};

class Test_Event_Loop : public ::testing::Test {
 public:
  Pipe_FDs pipe = make_pipe_for_event_loop();
  Spy_Event_Loop loop{this->pipe.reader.ref()};

 private:
  static Pipe_FDs make_pipe_for_event_loop() {
    Pipe_FDs pipe = make_pipe();
#if QLJS_EVENT_LOOP_READ_PIPE_NON_BLOCKING
    pipe.reader.set_pipe_non_blocking();
#endif
    return pipe;
  }
};

TEST_F(Test_Event_Loop, stops_on_pipe_read_eof) {
  this->pipe.writer.close();

  this->loop.run();
  // run() should terminate.
}

TEST_F(Test_Event_Loop, reads_data_in_pipe_buffer) {
  write_full_message(this->pipe.writer.ref(), u8"Hi"_sv);
  this->pipe.writer.close();

  this->loop.run();

  EXPECT_EQ(this->loop.get_read_data(), u8"Hi");
}

TEST_F(Test_Event_Loop, reads_many_messages) {
  std::thread writer_thread([this]() {
    write_full_message(this->pipe.writer.ref(), u8"first"_sv);
    this->loop.wait_until_data(
        [](const String8& data) -> bool { return data == u8"first"; });

    write_full_message(this->pipe.writer.ref(), u8"SECOND"_sv);
    this->loop.wait_until_data(
        [](const String8& data) -> bool { return data == u8"firstSECOND"; });

    this->pipe.writer.close();
  });

  this->loop.run();

  writer_thread.join();
  EXPECT_EQ(this->loop.get_read_data(), u8"firstSECOND");
}

TEST_F(Test_Event_Loop, stops_if_no_reader) {
  write_full_message(this->pipe.writer.ref(), u8"Hi"_sv);
  this->loop.unset_readable_pipe();

  this->loop.run();
  // run() should terminate.
}

TEST_F(Test_Event_Loop, stops_if_reader_is_unset_after_receiving_data) {
  write_full_message(this->pipe.writer.ref(), u8"Hi"_sv);
  this->loop.set_append_callback([&](String8_View data) -> void {
    EXPECT_EQ(data, u8"Hi"_sv);
    this->loop.unset_readable_pipe();
  });

  this->loop.run();
  // run() should terminate.
}

#if QLJS_HAVE_KQUEUE || QLJS_HAVE_POLL
TEST_F(Test_Event_Loop, signals_writable_pipe) {
  bool called = false;
  this->loop.set_pipe_write(this->pipe.writer.ref(),
                            [this, &called](const auto& event) {
                              called = true;
#if QLJS_HAVE_KQUEUE
                              EXPECT_EQ(event.ident, this->pipe.writer.get());
                              EXPECT_EQ(event.filter, EVFILT_WRITE);
#elif QLJS_HAVE_POLL
                              EXPECT_EQ(event.fd, this->pipe.writer.get());
                              EXPECT_TRUE(event.revents & POLLOUT);
#endif
                              // Stop event_loop::run.
                              this->pipe.writer.close();
                            });

  this->loop.run();
  EXPECT_TRUE(called);
}

TEST_F(Test_Event_Loop, does_not_write_to_unwritable_pipe) {
  // Make a pipe such that POLLOUT will not be signalled.
  Pipe_FDs full_pipe = make_pipe();
  full_pipe.writer.set_pipe_non_blocking();
  write_full_message(full_pipe.writer.ref(),
                     String8(full_pipe.writer.get_pipe_buffer_size(), 'x'));

  this->loop.set_pipe_write(full_pipe.writer.ref(), [](const auto&) {
    ADD_FAILURE() << "on_pipe_write_event should not be called";
  });

  std::thread writer_thread([this]() {
    std::this_thread::sleep_for(10ms);
    // Interrupt event_loop::run on the main thread.
    this->pipe.writer.close();
  });
  this->loop.run();

  writer_thread.join();
}
#endif

void write_full_message(Platform_File_Ref file, String8_View message) {
  auto write_result = file.write_full(message.data(), message.size());
  EXPECT_TRUE(write_result.ok()) << write_result.error_to_string();
}
}
}

#endif

// quick-lint-js finds bugs in JavaScript programs.
// Copyright (C) 2020  Matthew "strager" Glazar
//
// This file is part of quick-lint-js.
//
// quick-lint-js is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// quick-lint-js is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with quick-lint-js.  If not, see <https://www.gnu.org/licenses/>.
