#pragma once

#include <map>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Data/Model/Types.h"

extern "C" {
#include "quickjs.h"
}

namespace WallpaperEngine::Render::Objects {
class CImage;
}
namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Scripting {
using namespace WallpaperEngine::Data::Model;

/**
 * Persistent JS execution context for a CImage whose 'visible' value is a
 * ScriptedDynamicValue that uses thisScene.createLayer / thisScene.getLayerIndex /
 * thisScene.sortLayer (i.e. an "object script").
 *
 * init() is called once after construction to run the JS init() function;
 * update() is called each frame to run JS update() and push the resulting
 * positions back into the C++ CImage objects.
 */
class ObjectScriptContext {
public:
    ObjectScriptContext (
        WallpaperEngine::Render::Objects::CImage& owner,
        const std::string& scriptSource,
        const std::map<std::string, DynamicValue*>& scriptProps
    );
    ~ObjectScriptContext ();

    ObjectScriptContext (const ObjectScriptContext&) = delete;
    ObjectScriptContext& operator= (const ObjectScriptContext&) = delete;

    /** Run once after construction: evaluates the script and calls JS init(). */
    void init ();

    /** Run each frame: calls JS update() and applies resulting bar positions. */
    void update ();

private:
    struct BarState {
        WallpaperEngine::Render::Objects::CImage* image; // nullptr = owner
        glm::vec3 origin;
        glm::vec3 scale;
        glm::vec3 angles;     // degrees (JS convention)
        glm::vec2 parallaxDepth;
        std::string alignment;
    };

    // ---- helpers ----
    std::string stripScript (const std::string& source) const;
    std::string extractWorkshopId () const;
    std::string resolveModelPath (const std::string& path) const;

    JSValue dynamicValueToJS (const DynamicValue& value) const;

    void readBarState (BarState& state, JSValue barObj) const;
    void applyBarState (BarState& state) const;

    double readJSDouble (JSValue obj, const char* prop) const;
    std::string readJSString (JSValue obj, const char* prop) const;

    // ---- C callbacks (thisScene methods) ----
    static JSValue js_createLayer (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    static JSValue js_sortLayer (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    static JSValue js_getLayerIndex (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);

    // ---- state ----
    WallpaperEngine::Render::Objects::CImage& m_owner;
    std::string m_scriptSource;
    std::map<std::string, DynamicValue*> m_scriptProps;

    JSRuntime* m_runtime = nullptr;
    JSContext* m_ctx = nullptr;

    std::vector<BarState> m_bars;
    std::vector<ObjectUniquePtr> m_scriptCreatedObjectData;
    bool m_initialized = false;
};

} // namespace WallpaperEngine::Scripting
