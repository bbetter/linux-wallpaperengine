#include "ObjectScriptContext.h"

#include <algorithm>
#include <sstream>

#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Objects/CImage.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

using namespace WallpaperEngine::Scripting;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Render::Objects;
using namespace WallpaperEngine::Render::Wallpapers;
using JSON = WallpaperEngine::Data::JSON::JSON;

ObjectScriptContext::ObjectScriptContext (
    CImage& owner,
    const std::string& scriptSource,
    const std::map<std::string, DynamicValue*>& scriptProps
) :
    m_owner (owner),
    m_scriptSource (scriptSource),
    m_scriptProps (scriptProps) {
    this->m_runtime = JS_NewRuntime ();
    if (!this->m_runtime) {
        sLog.error ("ObjectScriptContext: Failed to create JS runtime");
        return;
    }
    this->m_ctx = JS_NewContext (this->m_runtime);
    if (!this->m_ctx) {
        sLog.error ("ObjectScriptContext: Failed to create JS context");
        JS_FreeRuntime (this->m_runtime);
        this->m_runtime = nullptr;
        return;
    }
    JS_SetContextOpaque (this->m_ctx, this);
}

ObjectScriptContext::~ObjectScriptContext () {
    if (this->m_ctx) {
        JS_FreeContext (this->m_ctx);
        this->m_ctx = nullptr;
    }
    if (this->m_runtime) {
        JS_FreeRuntime (this->m_runtime);
        this->m_runtime = nullptr;
    }
}

// ---- helpers ----

std::string ObjectScriptContext::stripScript (const std::string& source) const {
    std::string body = source;
    auto eraseAll = [&] (const std::string& token) {
        size_t pos;
        while ((pos = body.find (token)) != std::string::npos)
            body.erase (pos, token.size ());
    };
    eraseAll ("'use strict';");
    eraseAll ("\"use strict\";");

    // Remove import statements
    size_t pos;
    while ((pos = body.find ("import ")) != std::string::npos) {
        size_t end = body.find (';', pos);
        if (end != std::string::npos)
            body.erase (pos, end - pos + 1);
        else {
            end = body.find ('\n', pos);
            body.erase (pos, end != std::string::npos ? end - pos : std::string::npos);
        }
    }

    // Strip export keywords
    while ((pos = body.find ("export ")) != std::string::npos)
        body.erase (pos, 7);

    return body;
}

std::string ObjectScriptContext::extractWorkshopId () const {
    static const char* keys[] = { "__workshopId = '", "__workshopId='", "__workshopId = \"", "__workshopId=\"" };
    for (const char* key : keys) {
        size_t pos = this->m_scriptSource.find (key);
        if (pos == std::string::npos)
            continue;
        pos += std::strlen (key);
        size_t end = this->m_scriptSource.find_first_of ("'\"", pos);
        if (end == std::string::npos)
            continue;
        return this->m_scriptSource.substr (pos, end - pos);
    }
    return "";
}

std::string ObjectScriptContext::resolveModelPath (const std::string& path) const {
    const std::string workshopId = this->extractWorkshopId ();
    if (workshopId.empty ())
        return path;
    // "models/bar.json" → "models/workshop/2092495494/bar.json"
    size_t slash = path.find ('/');
    if (slash == std::string::npos)
        return path;
    return path.substr (0, slash + 1) + "workshop/" + workshopId + "/" + path.substr (slash + 1);
}

JSValue ObjectScriptContext::dynamicValueToJS (const DynamicValue& value) const {
    JSContext* ctx = this->m_ctx;
    switch (value.getType ()) {
        case DynamicValue::Float:
            return JS_NewFloat64 (ctx, value.getFloat ());
        case DynamicValue::Int:
            return JS_NewInt32 (ctx, value.getInt ());
        case DynamicValue::Boolean:
            return JS_NewBool (ctx, value.getBool ());
        case DynamicValue::String:
            return JS_NewString (ctx, value.getString ().c_str ());
        case DynamicValue::Vec2: {
            JSValue o = JS_NewObject (ctx);
            JS_SetPropertyStr (ctx, o, "x", JS_NewFloat64 (ctx, value.getVec2 ().x));
            JS_SetPropertyStr (ctx, o, "y", JS_NewFloat64 (ctx, value.getVec2 ().y));
            return o;
        }
        case DynamicValue::Vec3: {
            JSValue o = JS_NewObject (ctx);
            JS_SetPropertyStr (ctx, o, "x", JS_NewFloat64 (ctx, value.getVec3 ().x));
            JS_SetPropertyStr (ctx, o, "y", JS_NewFloat64 (ctx, value.getVec3 ().y));
            JS_SetPropertyStr (ctx, o, "z", JS_NewFloat64 (ctx, value.getVec3 ().z));
            return o;
        }
        default:
            return JS_UNDEFINED;
    }
}

double ObjectScriptContext::readJSDouble (JSValue obj, const char* prop) const {
    JSValue v = JS_GetPropertyStr (this->m_ctx, obj, prop);
    double d = 0.0;
    if (!JS_IsException (v) && !JS_IsUndefined (v))
        JS_ToFloat64 (this->m_ctx, &d, v);
    JS_FreeValue (this->m_ctx, v);
    return d;
}

std::string ObjectScriptContext::readJSString (JSValue obj, const char* prop) const {
    JSValue v = JS_GetPropertyStr (this->m_ctx, obj, prop);
    std::string result;
    if (!JS_IsException (v) && !JS_IsUndefined (v) && JS_IsString (v)) {
        const char* s = JS_ToCString (this->m_ctx, v);
        if (s) {
            result = s;
            JS_FreeCString (this->m_ctx, s);
        }
    }
    JS_FreeValue (this->m_ctx, v);
    return result;
}

void ObjectScriptContext::readBarState (BarState& state, JSValue barObj) const {
    state.origin.x = static_cast<float> (readJSDouble (barObj, "__ox"));
    state.origin.y = static_cast<float> (readJSDouble (barObj, "__oy"));
    state.origin.z = static_cast<float> (readJSDouble (barObj, "__oz"));
    state.scale.x = static_cast<float> (readJSDouble (barObj, "__sx"));
    state.scale.y = static_cast<float> (readJSDouble (barObj, "__sy"));
    state.scale.z = static_cast<float> (readJSDouble (barObj, "__sz"));
    state.angles.x = static_cast<float> (readJSDouble (barObj, "__ax"));
    state.angles.y = static_cast<float> (readJSDouble (barObj, "__ay"));
    state.angles.z = static_cast<float> (readJSDouble (barObj, "__az"));
    const std::string align = readJSString (barObj, "__align");
    if (!align.empty ())
        state.alignment = align;
    state.parallaxDepth.x = static_cast<float> (readJSDouble (barObj, "__pdx"));
    state.parallaxDepth.y = static_cast<float> (readJSDouble (barObj, "__pdy"));
}

void ObjectScriptContext::applyBarState (BarState& state) const {
    if (!state.image)
        return;
    state.image->setScriptOrigin (state.origin);
    state.image->setScriptScale (state.scale);
    state.image->setScriptAngles (state.angles);
    state.image->setScriptParallaxDepth (state.parallaxDepth);
    state.image->setScriptAlignment (state.alignment);
}

// ---- C callbacks ----

JSValue ObjectScriptContext::js_createLayer (JSContext* ctx, JSValueConst /*thisVal*/, int argc,
                                             JSValueConst* argv) {
    auto* self = static_cast<ObjectScriptContext*> (JS_GetContextOpaque (ctx));
    if (!self || argc < 1)
        return JS_UNDEFINED;

    const char* pathStr = JS_ToCString (ctx, argv[0]);
    if (!pathStr)
        return JS_UNDEFINED;
    std::string path (pathStr);
    JS_FreeCString (ctx, pathStr);

    const std::string resolvedPath = self->resolveModelPath (path);

    CImage* newBar = self->m_owner.getScene ().createScriptLayer (resolvedPath);
    if (!newBar)
        return JS_UNDEFINED;

    // Get the new bar's initial state from its image data
    const auto& img = newBar->getImage ();
    const glm::vec3 origin = img.origin->value->getVec3 ();
    const glm::vec3 scale = img.scale->value->getVec3 ();
    const glm::vec3 angles = img.angles->value->getVec3 ();
    const std::string alignment = img.alignment;
    const glm::vec2 pd = img.parallaxDepth->value->getVec2 ();

    // Create JS layer object with storage fields
    JSValue barObj = JS_NewObject (ctx);
    JS_SetPropertyStr (ctx, barObj, "__ox", JS_NewFloat64 (ctx, origin.x));
    JS_SetPropertyStr (ctx, barObj, "__oy", JS_NewFloat64 (ctx, origin.y));
    JS_SetPropertyStr (ctx, barObj, "__oz", JS_NewFloat64 (ctx, origin.z));
    JS_SetPropertyStr (ctx, barObj, "__sx", JS_NewFloat64 (ctx, scale.x));
    JS_SetPropertyStr (ctx, barObj, "__sy", JS_NewFloat64 (ctx, scale.y));
    JS_SetPropertyStr (ctx, barObj, "__sz", JS_NewFloat64 (ctx, scale.z));
    JS_SetPropertyStr (ctx, barObj, "__ax", JS_NewFloat64 (ctx, angles.x));
    JS_SetPropertyStr (ctx, barObj, "__ay", JS_NewFloat64 (ctx, angles.y));
    JS_SetPropertyStr (ctx, barObj, "__az", JS_NewFloat64 (ctx, angles.z));
    JS_SetPropertyStr (ctx, barObj, "__align", JS_NewString (ctx, alignment.c_str ()));
    JS_SetPropertyStr (ctx, barObj, "__pdx", JS_NewFloat64 (ctx, pd.x));
    JS_SetPropertyStr (ctx, barObj, "__pdy", JS_NewFloat64 (ctx, pd.y));

    // Attach getter/setter properties via the JS __attachLayerProps helper
    JSValue g = JS_GetGlobalObject (ctx);
    JSValue attachFn = JS_GetPropertyStr (ctx, g, "__attachLayerProps");
    JS_FreeValue (ctx, g);

    if (JS_IsFunction (ctx, attachFn)) {
        JSValue result = JS_Call (ctx, attachFn, JS_UNDEFINED, 1, &barObj);
        if (JS_IsException (result)) {
            JSValue exc = JS_GetException (ctx);
            const char* s = JS_ToCString (ctx, exc);
            if (s) { sLog.error ("ObjectScriptContext [__attachLayerProps]: ", s); JS_FreeCString (ctx, s); }
            JS_FreeValue (ctx, exc);
        }
        JS_FreeValue (ctx, result);
    }
    JS_FreeValue (ctx, attachFn);

    self->m_bars.push_back (BarState {
        .image = newBar,
        .origin = origin,
        .scale = scale,
        .angles = angles,
        .parallaxDepth = pd,
        .alignment = alignment,
    });

    return barObj;
}

JSValue ObjectScriptContext::js_sortLayer (JSContext* ctx, JSValueConst /*thisVal*/, int argc,
                                           JSValueConst* argv) {
    auto* self = static_cast<ObjectScriptContext*> (JS_GetContextOpaque (ctx));
    if (!self || argc < 2 || self->m_bars.empty ())
        return JS_UNDEFINED;

    int32_t index = 0;
    JS_ToInt32 (ctx, &index, argv[1]);

    // sortLayer is always called right after createLayer, so the target is m_bars.back()
    const BarState& last = self->m_bars.back ();
    if (last.image) {
        CObject* obj = self->m_owner.getScene ().getMutableObject (last.image->getId ());
        if (obj)
            self->m_owner.getScene ().sortScriptLayer (obj, index);
    }

    return JS_UNDEFINED;
}

JSValue ObjectScriptContext::js_getLayerIndex (JSContext* ctx, JSValueConst /*thisVal*/, int /*argc*/,
                                               JSValueConst* /*argv*/) {
    auto* self = static_cast<ObjectScriptContext*> (JS_GetContextOpaque (ctx));
    if (!self)
        return JS_NewInt32 (ctx, 0);

    const int idx = self->m_owner.getScene ().getLayerIndex (self->m_owner.getId ());
    return JS_NewInt32 (ctx, idx);
}

// ---- init / update ----

static void logJSException (JSContext* ctx, const char* where) {
    JSValue exc = JS_GetException (ctx);
    if (!JS_IsNull (exc) && !JS_IsUndefined (exc)) {
        const char* s = JS_ToCString (ctx, exc);
        if (s) {
            sLog.error ("ObjectScriptContext [", where, "]: ", s);
            JS_FreeCString (ctx, s);
        }
    }
    JS_FreeValue (ctx, exc);
}

void ObjectScriptContext::init () {
    if (!this->m_ctx || this->m_initialized)
        return;
    this->m_initialized = true;

    JSContext* ctx = this->m_ctx;

    // ---- Register C callbacks on global object ----
    JSValue g = JS_GetGlobalObject (ctx);
    JS_SetPropertyStr (ctx, g, "__createLayer", JS_NewCFunction (ctx, js_createLayer, "__createLayer", 1));
    JS_SetPropertyStr (ctx, g, "__sortLayer", JS_NewCFunction (ctx, js_sortLayer, "__sortLayer", 2));
    JS_SetPropertyStr (ctx, g, "__getLayerIndex", JS_NewCFunction (ctx, js_getLayerIndex, "__getLayerIndex", 1));

    // Set scriptProps object for createScriptProperties()
    JSValue propsObj = JS_NewObject (ctx);
    for (const auto& [k, v] : this->m_scriptProps) {
        if (v)
            JS_SetPropertyStr (ctx, propsObj, k.c_str (), this->dynamicValueToJS (*v));
    }
    JS_SetPropertyStr (ctx, g, "__initProps", propsObj);
    JS_FreeValue (ctx, g);

    // thisLayer initial state from owner image data
    const auto& img = this->m_owner.getImage ();
    const glm::vec3 ownerOrigin = img.origin->value->getVec3 ();
    const glm::vec3 ownerScale = img.scale->value->getVec3 ();
    const glm::vec3 ownerAngles = img.angles->value->getVec3 ();
    const std::string ownerAlign = img.alignment;
    const glm::vec2 ownerPd = img.parallaxDepth->value->getVec2 ();

    // Add owner as bar[0]
    this->m_bars.push_back (BarState {
        .image = &this->m_owner,
        .origin = ownerOrigin,
        .scale = ownerScale,
        .angles = ownerAngles,
        .parallaxDepth = ownerPd,
        .alignment = ownerAlign,
    });

    const float cw = static_cast<float> (this->m_owner.getScene ().getWidth ());
    const float ch = static_cast<float> (this->m_owner.getScene ().getHeight ());

    // ---- Evaluate preamble (helpers, stubs, thisLayer, thisScene) ----
    std::ostringstream pre;
    pre << "var __props = globalThis.__initProps;\n"
        << "function createScriptProperties(){\n"
        << "  var b={\n"
        << "    addSlider:function(o){if(!(o.name in __props))__props[o.name]=o.value;return b;},\n"
        << "    addCheckbox:function(o){if(!(o.name in __props))__props[o.name]=o.value;return b;},\n"
        << "    addCombo:function(o){\n"
        << "      if(!(o.name in __props)){\n"
        << "        var dv=o.value!==undefined?o.value:(o.options&&o.options.length?o.options[0].value:undefined);\n"
        << "        __props[o.name]=dv;\n"
        << "      }\n"
        << "      return b;\n"
        << "    },\n"
        << "    addColor:function(o){if(!(o.name in __props))__props[o.name]=o.value;return b;},\n"
        << "    addText:function(o){if(!(o.name in __props))__props[o.name]=o.value;return b;},\n"
        << "    finish:function(){return __props;}\n"
        << "  };\n"
        << "  return b;\n"
        << "}\n"

        // Vec constructors
        << "var Vec3=function(x,y,z){\n"
        << "  if(y===undefined&&z===undefined){this.x=x||0;this.y=x||0;this.z=x||0;}\n"
        << "  else{this.x=x||0;this.y=y||0;this.z=z||0;}\n"
        << "};\n"
        << "var Vec2=function(x,y){\n"
        << "  if(y===undefined){this.x=x||0;this.y=x||0;}\n"
        << "  else{this.x=x||0;this.y=y||0;}\n"
        << "};\n"
        << "var Vec4=function(x,y,z,w){this.x=x||0;this.y=y||0;this.z=z||0;this.w=w||0;};\n"

        // makeVec3 helper with .copy()
        << "function makeVec3(x,y,z){\n"
        << "  return{x:x||0,y:y||0,z:z||0,copy:function(){return makeVec3(this.x,this.y,this.z);}};\n"
        << "}\n"

        // Layer object property attachment (called by C++ too via __attachLayerProps(obj))
        << "function __attachLayerProps(obj){\n"
        << "  Object.defineProperty(obj,'origin',{\n"
        << "    get:function(){return makeVec3(obj.__ox,obj.__oy,obj.__oz);},\n"
        << "    set:function(v){obj.__ox=(v&&v.x)||0;obj.__oy=(v&&v.y)||0;obj.__oz=(v&&v.z)||0;},\n"
        << "    enumerable:true,configurable:true\n"
        << "  });\n"
        << "  Object.defineProperty(obj,'scale',{\n"
        << "    get:function(){return makeVec3(obj.__sx,obj.__sy,obj.__sz);},\n"
        << "    set:function(v){obj.__sx=(v&&v.x)||0;obj.__sy=(v&&v.y)||0;obj.__sz=(v&&v.z)||0;},\n"
        << "    enumerable:true,configurable:true\n"
        << "  });\n"
        << "  Object.defineProperty(obj,'angles',{\n"
        << "    get:function(){return makeVec3(obj.__ax,obj.__ay,obj.__az);},\n"
        << "    set:function(v){obj.__ax=(v&&v.x)||0;obj.__ay=(v&&v.y)||0;obj.__az=(v&&v.z)||0;},\n"
        << "    enumerable:true,configurable:true\n"
        << "  });\n"
        << "  Object.defineProperty(obj,'alignment',{\n"
        << "    get:function(){return obj.__align;},\n"
        << "    set:function(v){obj.__align=v;},\n"
        << "    enumerable:true,configurable:true\n"
        << "  });\n"
        << "  Object.defineProperty(obj,'parallaxDepth',{\n"
        << "    get:function(){return{x:obj.__pdx,y:obj.__pdy};},\n"
        << "    set:function(v){obj.__pdx=(v&&v.x)||0;obj.__pdy=(v&&v.y)||0;},\n"
        << "    enumerable:true,configurable:true\n"
        << "  });\n"
        << "}\n"

        // WPE API stubs
        << "var __audioZero=(function(){var a=[];for(var i=0;i<64;i++)a.push(0);return{average:a,peaks:a};})();\n"
        << "var engine={\n"
        << "  canvasSize:{x:" << cw << ",y:" << ch << "},frametime:0.016,timeOfDay:0.5,\n"
        << "  AUDIO_RESOLUTION_16:16,AUDIO_RESOLUTION_32:32,AUDIO_RESOLUTION_64:64,\n"
        << "  registerAudioBuffers:function(){return __audioZero;},\n"
        << "  registerUpdateEvent:function(){},registerInitEvent:function(){},\n"
        << "  registerDestroyEvent:function(){},on:function(){},\n"
        << "  setTimeout:function(){return 0;},clearTimeout:function(){},\n"
        << "  setInterval:function(){return 0;},clearInterval:function(){}\n"
        << "};\n"
        << "var WEMath={\n"
        << "  smoothStep:function(e0,e1,x){var t=Math.max(0,Math.min(1,(x-e0)/(e1-e0)));return t*t*(3-2*t);},\n"
        << "  clamp:function(x,lo,hi){return Math.max(lo,Math.min(hi,x));},\n"
        << "  mix:function(a,b,t){return a*(1-t)+b*t;},\n"
        << "  fract:function(x){return x-Math.floor(x);},\n"
        << "  mod:function(x,y){return x-y*Math.floor(x/y);}\n"
        << "};\n"
        << "var console={log:function(){},warn:function(){},error:function(){},debug:function(){}};\n"

        // thisLayer
        << "var __tl={__ox:" << ownerOrigin.x << ",__oy:" << ownerOrigin.y << ",__oz:" << ownerOrigin.z
        << ",__sx:" << ownerScale.x << ",__sy:" << ownerScale.y << ",__sz:" << ownerScale.z
        << ",__ax:" << ownerAngles.x << ",__ay:" << ownerAngles.y << ",__az:" << ownerAngles.z
        << ",__align:'" << ownerAlign << "',__pdx:" << ownerPd.x << ",__pdy:" << ownerPd.y << "};\n"
        << "__attachLayerProps(__tl);\n"
        << "var thisLayer=__tl;\n"

        // thisScene
        << "var thisScene={\n"
        << "  createLayer:__createLayer,\n"
        << "  sortLayer:__sortLayer,\n"
        << "  getLayerIndex:__getLayerIndex\n"
        << "};\n";

    const std::string preStr = pre.str ();
    JSValue preResult = JS_Eval (ctx, preStr.c_str (), preStr.size (), "<preamble>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException (preResult))
        logJSException (ctx, "preamble");
    JS_FreeValue (ctx, preResult);

    // ---- Evaluate user script body ----
    const std::string body = this->stripScript (this->m_scriptSource);
    JSValue bodyResult = JS_Eval (ctx, body.c_str (), body.size (), "<script>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException (bodyResult))
        logJSException (ctx, "script body");
    JS_FreeValue (ctx, bodyResult);

    // ---- Call JS init() ----
    g = JS_GetGlobalObject (ctx);
    JSValue initFn = JS_GetPropertyStr (ctx, g, "init");
    JS_FreeValue (ctx, g);

    if (JS_IsFunction (ctx, initFn)) {
        JSValue r = JS_Call (ctx, initFn, JS_UNDEFINED, 0, nullptr);
        if (JS_IsException (r))
            logJSException (ctx, "init");
        JS_FreeValue (ctx, r);
    }
    JS_FreeValue (ctx, initFn);
}

void ObjectScriptContext::update () {
    if (!this->m_ctx || !this->m_initialized)
        return;

    JSContext* ctx = this->m_ctx;

    // Call JS update()
    JSValue g = JS_GetGlobalObject (ctx);
    JSValue updateFn = JS_GetPropertyStr (ctx, g, "update");
    JS_FreeValue (ctx, g);

    if (JS_IsFunction (ctx, updateFn)) {
        JSValue r = JS_Call (ctx, updateFn, JS_UNDEFINED, 0, nullptr);
        if (JS_IsException (r))
            logJSException (ctx, "update");
        JS_FreeValue (ctx, r);
    }
    JS_FreeValue (ctx, updateFn);

    // Read back bar states from JS `bars` array
    g = JS_GetGlobalObject (ctx);
    JSValue barsArr = JS_GetPropertyStr (ctx, g, "bars");
    JS_FreeValue (ctx, g);

    if (JS_IsArray (barsArr)) {
        int64_t len = 0;
        JS_GetLength (ctx, barsArr, &len);
        const int count = std::min (static_cast<int> (len), static_cast<int> (this->m_bars.size ()));
        for (int i = 0; i < count; i++) {
            JSValue barObj = JS_GetPropertyUint32 (ctx, barsArr, static_cast<uint32_t> (i));
            readBarState (this->m_bars[i], barObj);
            JS_FreeValue (ctx, barObj);
        }
    }
    JS_FreeValue (ctx, barsArr);

    // Apply bar states to CImages
    for (auto& state : this->m_bars) {
        applyBarState (state);
    }
}
