# ADR 0001: C++20, Qt 6 Widgets and CMake

**Status:** Accepted

**Context:** The client must feel native on Windows, work well with screen readers, and later port to Linux and macOS. The owner asked for C++ and Qt.

**Options:** Qt Widgets; Qt Quick (QML); native Win32 or WinUI.

**Decision:** C++20, Qt 6 Widgets, and CMake with presets. The minimum Qt version in the code is 6.4, which is what Linux CI uses. Windows release builds use Qt 6.8 LTS or later, because live screen reader announcements (`QAccessibleAnnouncementEvent`) need 6.8.

**Why:** Widgets map to native accessibility roles and have the longest record with screen readers. Qt Quick accessibility needs more manual work for each control. Win32 would block porting.

**Consequences:** The UI looks native rather than highly custom; theming uses styles and palettes. Code compiles with warnings as errors on GCC, Clang and MSVC. APIs deprecated before Qt 6.4 are disabled.

**Validation:** CI builds on Windows (MSVC) and Linux (GCC, Clang). NVDA test script on each release candidate.
