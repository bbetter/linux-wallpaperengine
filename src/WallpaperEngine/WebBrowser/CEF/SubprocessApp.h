#pragma once

#include "WPSchemeHandlerFactory.h"
#include "WallpaperEngine/WebBrowser/WebBrowserContext.h"
#include "include/cef_app.h"
#include "include/cef_render_process_handler.h"

namespace WallpaperEngine::Application {
class WallpaperApplication;
}

namespace WallpaperEngine::WebBrowser::CEF {
class SubprocessApp : public CefApp {
public:
    SubprocessApp ();
    explicit SubprocessApp (WallpaperEngine::Application::WallpaperApplication& application);

    void OnBeforeCommandLineProcessing (const CefString& process_type, CefRefPtr<CefCommandLine> command_line)
	override;

    void OnRegisterCustomSchemes (CefRawPtr<CefSchemeRegistrar> registrar) override;

    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler () override;

protected:
    const std::map<std::string, WPSchemeHandlerFactory*>& getHandlerFactories () const;

private:
    std::map<std::string, WPSchemeHandlerFactory*> m_handlerFactories = {};
    IMPLEMENT_REFCOUNTING (SubprocessApp);
    DISALLOW_COPY_AND_ASSIGN (SubprocessApp);
};
}
