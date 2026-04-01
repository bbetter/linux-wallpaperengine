#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

#include "CRenderable.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Model/Types.h"
#include "WallpaperEngine/Render/CFBO.h"
#include "WallpaperEngine/Render/Objects/Effects/CPass.h"

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render::Objects {

/**
 * Private base class initialized before CRenderable so its material outlives the base.
 */
struct CTextMaterialHolder {
    explicit CTextMaterialHolder (Wallpapers::CScene& scene);
    Data::Model::MaterialUniquePtr m_textPassthroughMaterial;
};

class CText : private CTextMaterialHolder, public CRenderable {
public:
    CText (Wallpapers::CScene& scene, const Data::Model::Text& text);
    ~CText () override;

    void setup () override;
    void render () override;

    [[nodiscard]] glm::vec2 getSize () const;

    // CRenderable pure virtuals
    [[nodiscard]] const float& getBrightness () const override;
    [[nodiscard]] const float& getUserAlpha () const override;
    [[nodiscard]] const float& getAlpha () const override;
    [[nodiscard]] const glm::vec3& getColor () const override;
    [[nodiscard]] const glm::vec4& getColor4 () const override;
    [[nodiscard]] const glm::vec3& getCompositeColor () const override;

private:
    void renderTextToTexture ();
    void setupGeometry ();
    void setupShader ();
    void setupPasses ();
    void updateScreenSpacePosition ();
    void pinpongFramebuffer (std::shared_ptr<const CFBO>* drawTo, std::shared_ptr<const TextureProvider>* asInput);

    [[nodiscard]] std::string resolveText () const;
    void updateTextureContent ();

    const Data::Model::Text& m_text;

    std::string m_lastRenderedText;

    // Raw FreeType texture
    GLuint m_textureID = 0;
    int m_texWidth = 0;
    int m_texHeight = 0;

    // GL buffers for all effect passes (pass-space and scene-space)
    GLuint m_sceneSpacePosition = GL_NONE;
    GLuint m_passSpacePosition = GL_NONE;
    GLuint m_texcoordPass = GL_NONE;

    // MVP matrices for effect passes
    glm::mat4 m_modelViewProjectionScreen = {};
    glm::mat4 m_modelViewProjectionPass = glm::mat4 (1.0f);
    glm::mat4 m_modelViewProjectionPassInverse = {};
    glm::mat4 m_modelViewProjectionScreenInverse = {};
    glm::mat4 m_modelMatrix = {};
    glm::mat4 m_viewProjectionMatrix = {};
    glm::vec3 m_sceneCenter = {};

    // Ping-pong FBOs used when effects are active
    std::shared_ptr<const CFBO> m_mainFBO;
    std::shared_ptr<const CFBO> m_subFBO;
    std::shared_ptr<const CFBO> m_currentMainFBO;
    std::shared_ptr<const CFBO> m_currentSubFBO;

    // Effect passes
    std::vector<Effects::CPass*> m_passes;
    std::vector<Data::Model::MaterialPassUniquePtr> m_virtualPasses;

    // Cached color/alpha values for CRenderable pure virtuals
    float m_brightness = 1.0f;
    glm::vec3 m_colorVec3 = {1.0f, 1.0f, 1.0f};
    glm::vec4 m_colorVec4 = {1.0f, 1.0f, 1.0f, 1.0f};

    // Direct-render path (used when no effects are present)
    GLuint m_vao = 0;
    GLuint m_positionBuffer = 0;
    GLuint m_texcoordBuffer = 0;
    GLuint m_shader = 0;
    GLint m_uniformTexture = -1;
    GLint m_attribPosition = -1;
    GLint m_attribTexcoord = -1;
    glm::mat4 m_mvp {};

    bool m_initialized = false;
};

} // namespace WallpaperEngine::Render::Objects
