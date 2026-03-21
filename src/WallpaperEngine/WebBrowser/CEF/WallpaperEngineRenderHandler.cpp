#include "WallpaperEngineRenderHandler.h"

#include "include/cef_v8.h"

namespace WallpaperEngine::WebBrowser::CEF {

// JavaScript stubs for the Wallpaper Engine web API.
// Injected before any page script runs so that web wallpapers that call
// window.wallpaperRegisterAudioListener (and similar functions) do not throw
// "is not a function" TypeErrors which can destabilise the CEF renderer process.
static const char* WALLPAPER_ENGINE_JS_API = R"js(
(function () {
    if (typeof window.wallpaperRegisterAudioListener === 'undefined') {
        // Called by web wallpapers to receive per-frame FFT audio data.
        // The engine currently passes audio data from PulseAudio via IPC;
        // until that path is wired up, register the callback so it can be
        // invoked later without the page throwing a TypeError on init.
        window._wallpaperAudioCallbacks = window._wallpaperAudioCallbacks || [];
        window.wallpaperRegisterAudioListener = function (callback) {
            if (typeof callback === 'function') {
                window._wallpaperAudioCallbacks.push(callback);
            }
        };
    }
    if (typeof window.wallpaperRegisterSettingChangeListener === 'undefined') {
        window.wallpaperRegisterSettingChangeListener = function (name, callback) {};
    }
    if (typeof window.wallpaperPropertyListener === 'undefined') {
        window.wallpaperPropertyListener = {};
    }
    if (typeof window.wallpaperToggleAudio === 'undefined') {
        window.wallpaperToggleAudio = function (enabled) {};
    }
    if (typeof window.wallpaperGetVolume === 'undefined') {
        window.wallpaperGetVolume = function () { return 1.0; };
    }
    if (typeof window.wallpaperPlayMediaFile === 'undefined') {
        window.wallpaperPlayMediaFile = function (filePath) {};
    }
})();
)js";

void WallpaperEngineRenderHandler::OnContextCreated (
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context
) {
    // Only inject into main frames; skip iframes to avoid double-injection.
    if (!frame->IsMain ()) {
        return;
    }

    CefRefPtr<CefV8Value> retval;
    CefRefPtr<CefV8Exception> exception;

    context->Eval (WALLPAPER_ENGINE_JS_API, frame->GetURL (), 1, retval, exception);
}

} // namespace WallpaperEngine::WebBrowser::CEF
