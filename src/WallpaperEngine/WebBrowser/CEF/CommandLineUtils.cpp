#include "CommandLineUtils.h"

#include <cstdlib>
#include <filesystem>

namespace WallpaperEngine::WebBrowser::CEF {
namespace {
void appendCommonDisableFeatures (CefRefPtr<CefCommandLine> command_line) {
    command_line->AppendSwitchWithValue (
	"disable-features",
	// UseOzonePlatform must NOT be disabled on Wayland — it's required for CEF to connect
	// to the Wayland display.  Vulkan is disabled per-flag below instead.
	"IsolateOrigins,HardwareMediaKeyHandling,"
	"WebContentsOcclusion,RendererCodeIntegrityEnabled,site-per-process"
    );
}

std::filesystem::path g_resourceDir;
std::filesystem::path g_localesDir;
bool g_hasPaths = false;

void ensureResourcePathsFromEnv () {
    if (g_hasPaths) {
	return;
    }

    if (const char* envResource = std::getenv ("WALLPAPER_RESOURCE_DIR")) {
	g_resourceDir = envResource;
	if (const char* envLocales = std::getenv ("WALLPAPER_LOCALES_DIR")) {
	    g_localesDir = envLocales;
	}
	g_hasPaths = true;
    }
}

void appendLinuxSwitches (CefRefPtr<CefCommandLine> command_line) {
    command_line->AppendSwitch ("no-sandbox");
    command_line->AppendSwitch ("disable-setuid-sandbox");

    const char* x11Display = std::getenv ("DISPLAY");
    const char* waylandDisplay = std::getenv ("WAYLAND_DISPLAY");

    if (waylandDisplay && waylandDisplay[0] != '\0' && !(x11Display && x11Display[0] != '\0')) {
	// Pure Wayland session: tell CEF to use the Wayland ozone backend
	command_line->AppendSwitchWithValue ("ozone-platform", "wayland");
    } else if (x11Display && x11Display[0] != '\0') {
	// X11 or XWayland session
	command_line->AppendSwitchWithValue ("ozone-platform", "x11");
    }

    ensureResourcePathsFromEnv ();
    if (g_hasPaths) {
	command_line->AppendSwitchWithValue ("resources-dir-path", g_resourceDir.string ());
	command_line->AppendSwitchWithValue ("locales-dir-path", g_localesDir.string ());
    }
}
} // namespace

void configureLinuxCommandLine (CefRefPtr<CefCommandLine> command_line) {
    appendCommonDisableFeatures (command_line);
#if defined(__linux__)
    appendLinuxSwitches (command_line);
#endif
}

void setResourcePaths (std::filesystem::path resourceDir, std::filesystem::path localesDir) {
    g_resourceDir = std::move (resourceDir);
    g_localesDir = std::move (localesDir);
    g_hasPaths = true;
}
} // namespace WallpaperEngine::WebBrowser::CEF
