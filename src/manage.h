// catberry install / uninstall / versions: getting the catberry tools from
// the GitHub Releases and removing them again.
#pragma once

#include <string>

// "<tool> versions:" and the published versions, newest first, marking the
// installed one. Exit code.
int listVersions(const std::string &tool);
// Installs `version` ("latest" or a number) of `tool` (ac, mtools, roofce,
// catberry). Exit code.
int installTool(const std::string &tool, const std::string &version, bool reinstall);
// Stops and removes every copy of `tool`, then offers to delete its
// settings and files. Exit code.
int uninstallTool(const std::string &tool);
// Windows: deletes the catberry.old.exe a self-update left behind.
// Nothing on Linux. Called at every start.
void removeOldLauncher();
