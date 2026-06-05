#pragma once

#include <string>

namespace tasks
{
// Build the Obsidian Local REST API URL for a vault note:
//   <base>/vault/<percent-encoded path>
// A trailing '/' on base and a leading '/' on note_path are normalised away so
// exactly one separator sits between the segments. The path is percent-encoded
// but '/' is preserved so nested notes (Inbox/Tasks.md) keep their structure.
std::string build_vault_url(const std::string &base, const std::string &note_path);
} // namespace tasks
