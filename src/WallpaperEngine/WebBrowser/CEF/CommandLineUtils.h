#pragma once

#include "include/cef_command_line.h"

#include <filesystem>

namespace WallpaperEngine::WebBrowser::CEF {
void configureLinuxCommandLine (CefRefPtr<CefCommandLine> command_line);
void setResourcePaths (std::filesystem::path resourceDir, std::filesystem::path localesDir);
}
