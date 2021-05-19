// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_TEXT_INPUT_MANAGER_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_TEXT_INPUT_MANAGER_H_

#include <Windows.h>
#include <Windowsx.h>

#include <optional>
#include <string>

#include "flutter/shell/platform/common/geometry.h"

namespace flutter {

// Management interface for IME-based text input on Windows.
//
// When inputting text in CJK languages, text is entered via a multi-step
// process, where direct keyboard input is buffered into a composing string,
// which is then converted into the desired characters by selecting from a
// candidates list and committing the change to the string.
//
// This implementation wraps the Win32 IMM32 APIs and provides a mechanism for
// creating and positioning the IME window, a system caret, and the candidates
// list as well as for accessing composing and results string contents.
class TextInputManagerWin32 {
 public:
  TextInputManagerWin32() noexcept = default;
  ~TextInputManagerWin32() = default;

  TextInputManagerWin32(const TextInputManagerWin32&) = delete;
  TextInputManagerWin32& operator=(const TextInputManagerWin32&) = delete;

  // Sets the window handle with which the IME is associated.
  void SetWindowHandle(HWND window_handle);

  // Creates a new IME window and system caret.
  //
  // This method should be invoked in response to the WM_IME_SETCONTEXT and
  // WM_IME_STARTCOMPOSITION events.
  void CreateImeWindow();

  // Destroys the current IME window and system caret.
  //
  // This method should be invoked in response to the WM_IME_ENDCOMPOSITION
  // event.
  void DestroyImeWindow();

  // Updates the current IME window and system caret position.
  //
  // This method should be invoked when handling user input via
  // WM_IME_COMPOSITION events.
  void UpdateImeWindow();

  // Updates the current IME window and system caret position.
  //
  // This method should be invoked when handling cursor position/size updates.
  void UpdateCaretRect(const Rect& rect);

  // Returns the cursor position relative to the start of the composing range.
  long GetComposingCursorPosition() const;

  // Returns the contents of the composing string.
  //
  // This may be called in response to WM_IME_COMPOSITION events where the
  // GCS_COMPSTR flag is set in the lparam. In some IMEs, this string may also
  // be set in events where the GCS_RESULTSTR flag is set. This contains the
  // in-progress composing string.
  std::optional<std::u16string> GetComposingString() const;

  // Returns the contents of the result string.
  //
  // This may be called in response to WM_IME_COMPOSITION events where the
  // GCS_RESULTSTR flag is set in the lparam. This contains the final string to
  // be committed in the composing region when composition is ended.
  std::optional<std::u16string> GetResultString() const;

 private:
  // Returns either the composing string or result string based on the value of
  // the |type| parameter.
  std::optional<std::u16string> GetString(int type) const;

  // Moves the IME composing and candidates windows to the current caret
  // position.
  void MoveImeWindow(HIMC imm_context);

  // The window with which the IME windows are associated.
  HWND window_handle_ = nullptr;

  // True if IME-based composing is active.
  bool ime_active_ = false;

  // The system caret rect.
  Rect caret_rect_ = {{0, 0}, {0, 0}};
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_TEXT_INPUT_MANAGER_H_
