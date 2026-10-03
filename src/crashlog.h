#pragma once
// Crash reporting and crash-loop recovery.
//
// A crash while applying a personalization option used to leave no trace at all, so the app now
// installs an unhandled-exception and terminate handler that writes everything useful to
// %APPDATA%\Voxual\crash-<time>.log, keeps a breadcrumb trail of what the app was doing, and
// leaves a session marker so the next start can offer to reset the appearance.
#include <string>

namespace crashlog {

void install();                 // install the handlers (call as early as possible)
void beginSession();            // drop a session marker; remembers whether the last run ended cleanly
void endSession();              // clean exit: remove the marker
bool previousSessionCrashed();
std::string lastCrashLogPath();  // newest crash log, if any

void breadcrumb(const std::string& text);   // "what the app was doing", kept in a small ring buffer

}  // namespace crashlog
