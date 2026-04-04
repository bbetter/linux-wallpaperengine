#pragma once

#include "WallpaperEngine/Render/Camera.h"

#include "WallpaperEngine/Render/CWallpaper.h"

namespace WallpaperEngine::Render {
class Camera;
class CObject;
}

namespace WallpaperEngine::Render::Objects {
class CImage;
}

namespace WallpaperEngine::Render::Wallpapers {
using namespace WallpaperEngine::Data::Model;

class CScene final : public CWallpaper {
public:
    CScene (
	const Wallpaper& wallpaper, RenderContext& context, AudioContext& audioContext,
	const WallpaperState::TextureUVsScaling& scalingMode, const uint32_t& clampMode
    );

    ~CScene () override;

    [[nodiscard]] Camera& getCamera () const;

    [[nodiscard]] const Scene& getScene () const;

    [[nodiscard]] int getWidth () const override;
    [[nodiscard]] int getHeight () const override;

    const glm::vec2* getMousePosition () const;
    const glm::vec2* getMousePositionLast () const;
    const glm::vec2* getParallaxDisplacement () const;

    [[nodiscard]] const std::vector<CObject*>& getObjectsByRenderOrder () const;
    [[nodiscard]] const CObject* getObject (int id) const;
    [[nodiscard]] CObject* getMutableObject (int id);

    /**
     * Returns the index of the given object id in the render order, or 0 if not found.
     */
    [[nodiscard]] int getLayerIndex (int id) const;

    /**
     * Creates a new image layer from a model path (for use by object scripts).
     * The created object is added to the scene but NOT to the render order —
     * call sortScriptLayer() to position it.
     */
    Objects::CImage* createScriptLayer (const std::string& modelPath);

    /**
     * Inserts obj into m_objectsByRenderOrder at position index (clamped).
     * Removes obj from its current position first if already present.
     */
    void sortScriptLayer (CObject* obj, int index);

protected:
    void renderFrame (const glm::ivec4& viewport) override;
    void updateMouse (const glm::ivec4& viewport);

    friend class CWallpaper;

private:
    Render::CObject* createObject (const Object& object);
    void addObjectToRenderOrder (const Object& object);

    std::unique_ptr<Camera> m_camera;
    ObjectUniquePtr m_bloomObjectData;
    CObject* m_bloomObject = nullptr;
    std::map<int, CObject*> m_objects = {};
    std::vector<CObject*> m_objectsByRenderOrder = {};
    glm::vec2 m_mousePosition = {};
    glm::vec2 m_mousePositionLast = {};
    glm::vec2 m_parallaxDisplacement = {};
    std::shared_ptr<const CFBO> _rt_4FrameBuffer = nullptr;
    std::shared_ptr<const CFBO> _rt_8FrameBuffer = nullptr;
    std::shared_ptr<const CFBO> _rt_Bloom = nullptr;
    std::shared_ptr<const CFBO> _rt_shadowAtlas = nullptr;

    // Script-created objects (data owned here, render objects in m_objects)
    std::vector<ObjectUniquePtr> m_scriptCreatedObjectData;
    int m_nextScriptLayerId = -10001;
};
} // namespace WallpaperEngine::Render::Wallpapers
