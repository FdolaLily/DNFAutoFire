#pragma once
// System task dialogs for the double-click update: the question shown when a
// different build of an installed copy is opened, and the progress / result of
// update::replace() while the service and client are swapped.
#include "app_update.h"

namespace dafclient::update {

enum class Choice { Replace, Skip, Cancel };
// "Update the installed program?" for the copy found by findInstalled().
Choice confirm(const Target& target, const Version& self, Offer offer);
// Replaces target with source behind a progress dialog (the caller is elevated and
// does not hold the single-instance lock). Returns the process exit code.
int runWithProgress(const std::wstring& source, const std::wstring& target);
// A plain message box in the same style (icon: TD_INFORMATION_ICON, TD_WARNING_ICON, ...).
void notice(const wchar_t* icon, const std::wstring& instruction, const std::wstring& content);

} // namespace dafclient::update
