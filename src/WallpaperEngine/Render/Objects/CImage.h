#pragma once

#include <optional>

#include "CRenderable.h"
#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Render/Objects/Effects/CPass.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

#include "WallpaperEngine/Render/Shaders/Shader.h"

#include "../TextureProvider.h"

#include <glm/vec3.hpp>

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;

namespace WallpaperEngine::Render::Objects::Effects {
class CMaterial;
class CPass;
} // namespace WallpaperEngine::Render::Objects::Effects

namespace WallpaperEngine::Scripting {
class ObjectScriptContext;
}

namespace WallpaperEngine::Render::Objects {
class CEffect;

class CImage final : public CRenderable {
    friend CObject;

public:
    CImage (Wallpapers::CScene& scene, const Image& image);
    ~CImage () override;

    void setup () override;
    void render () override;

    [[nodiscard]] const Image& getImage () const;
    [[nodiscard]] const std::vector<CEffect*>& getEffects () const;
    [[nodiscard]] const Effects::CMaterial* getMaterial () const;
    [[nodiscard]] glm::vec2 getSize () const;

    [[nodiscard]] GLuint getSceneSpacePosition () const;
    [[nodiscard]] GLuint getCopySpacePosition () const;
    [[nodiscard]] GLuint getPassSpacePosition () const;
    [[nodiscard]] GLuint getTexCoordCopy () const;
    [[nodiscard]] GLuint getTexCoordPass () const;

    [[nodiscard]] const float& getBrightness () const override;

    // Script-driven property overrides (set by ObjectScriptContext each frame)
    void setScriptOrigin (const glm::vec3& v);
    void setScriptScale (const glm::vec3& v);
    void setScriptAngles (const glm::vec3& degrees);
    void setScriptParallaxDepth (const glm::vec2& v);
    void setScriptAlignment (const std::string& s);

    [[nodiscard]] glm::vec3 getEffectiveOrigin () const;
    [[nodiscard]] glm::vec3 getEffectiveScale () const;

    // Called by CScene::renderFrame before the render loop
    void runObjectScriptUpdate ();
    [[nodiscard]] const float& getUserAlpha () const override;
    [[nodiscard]] const float& getAlpha () const override;
    [[nodiscard]] const glm::vec3& getColor () const override;
    [[nodiscard]] const glm::vec4& getColor4 () const override;
    [[nodiscard]] const glm::vec3& getCompositeColor () const override;

    /**
     * Performs a ping-pong on the available framebuffers to be able to continue rendering things to them
     *
     * @param drawTo The framebuffer to use
     * @param asInput The last texture used as output (if needed)
     */
    void pinpongFramebuffer (std::shared_ptr<const CFBO>* drawTo, std::shared_ptr<const TextureProvider>* asInput);

protected:
    void setupPasses ();

    void updateScreenSpacePosition ();
    void recomputePositionAndUploadVBO ();

private:
    GLuint m_sceneSpacePosition;
    GLuint m_copySpacePosition;
    GLuint m_passSpacePosition;
    GLuint m_texcoordCopy;
    GLuint m_texcoordPass;

    glm::mat4 m_modelViewProjectionScreen = {};
    glm::mat4 m_modelViewProjectionPass = {};
    glm::mat4 m_modelViewProjectionCopy = {};
    glm::mat4 m_modelViewProjectionScreenInverse = {};
    glm::mat4 m_modelViewProjectionPassInverse = {};
    glm::mat4 m_modelViewProjectionCopyInverse = {};

    glm::mat4 m_modelMatrix = {};
    glm::mat4 m_viewProjectionMatrix = {};

    std::shared_ptr<const CFBO> m_mainFBO = nullptr;
    std::shared_ptr<const CFBO> m_subFBO = nullptr;
    std::shared_ptr<const CFBO> m_currentMainFBO = nullptr;
    std::shared_ptr<const CFBO> m_currentSubFBO = nullptr;

    const Image& m_image;

    std::vector<CEffect*> m_effects = {};
    Effects::CMaterial* m_material = nullptr;
    Effects::CMaterial* m_colorBlendMaterial = nullptr;
    std::vector<Effects::CPass*> m_passes = {};
    std::vector<MaterialPassUniquePtr> m_virtualPassess = {};

    glm::vec4 m_pos = {};
    glm::vec3 m_sceneCenter = {};

    bool m_initialized = false;

    // Puppet mesh (from MDL file)
    struct PuppetMesh {
        GLuint vbo = GL_NONE;
        GLuint ibo = GL_NONE;
        GLsizei numIndices = 0;
    };
    std::unique_ptr<PuppetMesh> m_puppetMesh;
    void parsePuppetMesh (const glm::vec2& size);

    // Script-driven positional overrides (set per-frame by ObjectScriptContext)
    std::optional<glm::vec3> m_overrideOrigin;
    std::optional<glm::vec3> m_overrideScale;
    std::optional<glm::vec3> m_overrideAngles;         // stored in radians
    std::optional<glm::vec2> m_overrideParallaxDepth;
    std::optional<std::string> m_overrideAlignment;
    bool m_positionDirty = false;

    // Persistent object script context (created when visible has a script with createLayer/thisScene)
    std::unique_ptr<WallpaperEngine::Scripting::ObjectScriptContext> m_objectScriptCtx;

    struct {
	struct {
	    MaterialUniquePtr material;
	    ImageEffectPassOverrideUniquePtr override;
	} colorBlending;
    } m_materials;
};
} // namespace WallpaperEngine::Render::Objects
