#include "BrowserApp.h"
#include "WallpaperEngine/Logging/Log.h"
#include <cstdlib>

using namespace WallpaperEngine::WebBrowser::CEF;

BrowserApp::BrowserApp (WallpaperEngine::Application::WallpaperApplication& application) :
    SubprocessApp (application) { }

CefRefPtr<CefBrowserProcessHandler> BrowserApp::GetBrowserProcessHandler () { return this; }

void BrowserApp::OnContextInitialized () {
    // register all the needed schemes, "wp" + the background id is going to be our scheme
    for (const auto& [workshopId, factory] : this->getHandlerFactories ()) {
	CefRegisterSchemeHandlerFactory (
	    WPSchemeHandlerFactory::generateSchemeName (workshopId), static_cast<const char*> (nullptr), factory
	);
    }
}

void BrowserApp::OnBeforeCommandLineProcessing (const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
    command_line->AppendSwitchWithValue (
	"disable-features",
	"IsolateOrigins,HardwareMediaKeyHandling,WebContentsOcclusion,RendererCodeIntegrityEnabled,site-per-process"
    );
    command_line->AppendSwitch ("disable-gpu-shader-disk-cache");
    command_line->AppendSwitch ("disable-site-isolation-trials");
    command_line->AppendSwitch ("disable-web-security");
    command_line->AppendSwitchWithValue ("remote-allow-origins", "*");
    command_line->AppendSwitchWithValue ("autoplay-policy", "no-user-gesture-required");
    command_line->AppendSwitch ("disable-background-timer-throttling");
    command_line->AppendSwitch ("disable-backgrounding-occluded-windows");
    command_line->AppendSwitch ("disable-background-media-suspend");
    command_line->AppendSwitch ("disable-renderer-backgrounding");
    command_line->AppendSwitch ("disable-test-root-certs");
    command_line->AppendSwitch ("disable-bundled-ppapi-flash");
    command_line->AppendSwitch ("disable-breakpad");
    command_line->AppendSwitch ("disable-field-trial-config");
    command_line->AppendSwitch ("no-experiments");
#if defined(__linux__)
    command_line->AppendSwitch ("no-sandbox");
    command_line->AppendSwitch ("disable-setuid-sandbox");

    const char* sessionType = std::getenv ("XDG_SESSION_TYPE");
    const char* waylandDisplay = std::getenv ("WAYLAND_DISPLAY");
    if ((sessionType && std::string (sessionType) == "wayland") || (waylandDisplay && waylandDisplay[0] != '\0')) {
	// Prefer native Wayland/Ozone path when running inside Wayland sessions.
	// This avoids hard dependency on GLX/X11 in compositors like Hyprland and KDE Wayland.
	command_line->AppendSwitchWithValue ("ozone-platform-hint", "wayland");
	command_line->AppendSwitchWithValue ("ozone-platform", "wayland");
	command_line->AppendSwitchWithValue ("enable-features", "UseOzonePlatform");
    }
#endif
    // TODO: ACTIVATE THIS IF WE EVER SUPPORT MACOS OFFICIALLY
    /*
if (process_type.empty()) {
#if defined(OS_MACOSX)
  // Disable the macOS keychain prompt. Cookies will not be encrypted.
  command_line->AppendSwitch("use-mock-keychain");
#endif
}*/
}

void BrowserApp::OnBeforeChildProcessLaunch (CefRefPtr<CefCommandLine> command_line) {
    // Keep CEF child process argv minimal. Forwarding wallpaper/runtime args into
    // utility/zygote processes can make child startup run main-path initialization.
    (void)command_line;
}
