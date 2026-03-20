#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Data/Model/Object.h"

namespace WallpaperEngine::Render::Objects {

class CText : public CObject {
public:
    CText (Wallpapers::CScene& scene, const Data::Model::Text& text);
    ~CText () override;

    void render () override;

private:
    void renderTextToTexture ();
    void setupGeometry ();
    void setupShader ();

    [[nodiscard]] std::string resolveText () const;
    void updateTextureContent ();

    const Data::Model::Text& m_text;

    std::string m_lastRenderedText;

    GLuint m_texture = 0;
    GLuint m_vao = 0;
    GLuint m_positionBuffer = 0;
    GLuint m_texcoordBuffer = 0;
    GLuint m_shader = 0;
    GLint m_uniformTexture = -1;
    GLint m_attribPosition = -1;
    GLint m_attribTexcoord = -1;
    glm::mat4 m_mvp {};

    int m_texWidth = 0;
    int m_texHeight = 0;
};

} // namespace WallpaperEngine::Render::Objects
