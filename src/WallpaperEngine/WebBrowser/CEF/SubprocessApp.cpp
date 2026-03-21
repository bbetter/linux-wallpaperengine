#include "CommandLineUtils.h"
#include "SubprocessApp.h"
#include "WPSchemeHandlerFactory.h"
#include "WallpaperEngineRenderHandler.h"
#include "WallpaperEngine/Data/Model/Project.h"

using namespace WallpaperEngine::WebBrowser::CEF;

SubprocessApp::SubprocessApp () = default;

SubprocessApp::SubprocessApp (WallpaperEngine::Application::WallpaperApplication& application) :
    SubprocessApp () {
    for (const auto& info : application.getBackgrounds () | std::views::values) {
	this->m_handlerFactories[info->workshopId] = new WPSchemeHandlerFactory (*info);
    }
}

void SubprocessApp::OnRegisterCustomSchemes (CefRawPtr<CefSchemeRegistrar> registrar) {
    registrar->AddCustomScheme (
	WPSchemeHandlerFactory::generateSchemeName (),
	CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE | CEF_SCHEME_OPTION_FETCH_ENABLED
    );
}

void SubprocessApp::OnBeforeCommandLineProcessing (const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
    (void)process_type;
    configureLinuxCommandLine (command_line);
}

const std::map<std::string, WPSchemeHandlerFactory*>& SubprocessApp::getHandlerFactories () const {
    return this->m_handlerFactories;
}

CefRefPtr<CefRenderProcessHandler> SubprocessApp::GetRenderProcessHandler () {
    return new WallpaperEngineRenderHandler ();
}
