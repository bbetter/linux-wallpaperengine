#pragma once

#include "include/cef_render_process_handler.h"

namespace WallpaperEngine::WebBrowser::CEF {
/**
 * Renderer-process handler that injects the Wallpaper Engine JavaScript API stubs
 * into every web wallpaper context before the page scripts run.
 */
class WallpaperEngineRenderHandler : public CefRenderProcessHandler {
public:
    WallpaperEngineRenderHandler () = default;

    /**
     * Called in the renderer process when a new V8 context (page frame) is created.
     * Injects window.wallpaperRegisterAudioListener and related API stubs so that
     * web wallpapers that call these functions do not throw TypeError.
     */
    void OnContextCreated (CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefV8Context> context) override;

private:
    IMPLEMENT_REFCOUNTING (WallpaperEngineRenderHandler);
    DISALLOW_COPY_AND_ASSIGN (WallpaperEngineRenderHandler);
};
} // namespace WallpaperEngine::WebBrowser::CEF
