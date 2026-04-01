#include "CText.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <map>
#include <sstream>
#include <vector>

#include "WallpaperEngine/Data/Assets/Texture.h"
#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Parsers/MaterialParser.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"
#include "WallpaperEngine/Render/FBOProvider.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Render::Objects;
using namespace WallpaperEngine::Render::Objects::Effects;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Scripting;

// ─── TextTextureProvider ──────────────────────────────────────────────────────
// Wraps a raw OpenGL texture ID as a TextureProvider so the effect pass pipeline
// can use the FreeType-rasterised text as its source texture.
namespace {
class TextTextureProvider final : public WallpaperEngine::Render::TextureProvider {
public:
    TextTextureProvider (GLuint id, uint32_t w, uint32_t h) :
        m_id (id), m_w (w), m_h (h), m_resolution {(float) w, (float) h, (float) w, (float) h} {}

    [[nodiscard]] GLuint getTextureID (uint32_t) const override { return m_id; }
    [[nodiscard]] uint32_t getTextureWidth (uint32_t) const override { return m_w; }
    [[nodiscard]] uint32_t getTextureHeight (uint32_t) const override { return m_h; }
    [[nodiscard]] uint32_t getRealWidth () const override { return m_w; }
    [[nodiscard]] uint32_t getRealHeight () const override { return m_h; }
    [[nodiscard]] WallpaperEngine::Data::Assets::TextureFormat getFormat () const override {
        return WallpaperEngine::Data::Assets::TextureFormat_ARGB8888;
    }
    [[nodiscard]] uint32_t getFlags () const override { return 0; }
    [[nodiscard]] const std::vector<WallpaperEngine::Data::Assets::FrameSharedPtr>& getFrames () const override {
        return m_frames;
    }
    [[nodiscard]] const glm::vec4* getResolution () const override { return &m_resolution; }
    [[nodiscard]] bool isAnimated () const override { return false; }
    [[nodiscard]] uint32_t getSpritesheetCols () const override { return 0; }
    [[nodiscard]] uint32_t getSpritesheetRows () const override { return 0; }
    [[nodiscard]] uint32_t getSpritesheetFrames () const override { return 0; }
    [[nodiscard]] float getSpritesheetDuration () const override { return 0.0f; }
    void incrementUsageCount () const override {}
    void decrementUsageCount () const override {}
    void update () const override {}

private:
    GLuint m_id;
    uint32_t m_w, m_h;
    glm::vec4 m_resolution;
    std::vector<WallpaperEngine::Data::Assets::FrameSharedPtr> m_frames {};
};
} // namespace

// ─── CTextMaterialHolder ──────────────────────────────────────────────────────

CTextMaterialHolder::CTextMaterialHolder (Wallpapers::CScene& scene) {
    m_textPassthroughMaterial
        = MaterialParser::load (scene.getScene ().project, "materials/util/effectpassthrough.json");
}

// ─── CText ────────────────────────────────────────────────────────────────────

CText::CText (Wallpapers::CScene& scene, const Text& text) :
    CTextMaterialHolder (scene),
    CRenderable (scene, text, *m_textPassthroughMaterial),
    m_text (text),
    m_modelViewProjectionPass (glm::mat4 (1.0f)),
    m_modelViewProjectionPassInverse (glm::inverse (glm::mat4 (1.0f))) {
    const glm::vec3 scale = text.scale->value->getVec3 ();
    sLog.debug (
        "CText '", text.name, "': size=", text.size.x, "x", text.size.y,
        " scale=", scale.x, " pointSize=", text.pointSize,
        " hasScript=", text.script.has_value (),
        " effects=", text.effects.size ()
    );

    // Render text to GL texture first so m_texWidth/m_texHeight are known.
    this->renderTextToTexture ();

    // Expose the FreeType texture through CRenderable's m_texture so CPass can
    // find the source texture via getTexture().
    this->m_texture = std::make_shared<TextTextureProvider> (m_textureID, m_texWidth, m_texHeight);

    // Build the geometry buffers used by both the direct and effect render paths.
    this->setupGeometry ();

    if (!this->m_text.effects.empty ()) {
        // Register ping-pong composite FBOs in the scene so other objects and
        // effect passes can look them up by their canonical names.
        const glm::vec2 fboSize {(float) m_texWidth, (float) m_texHeight};
        std::ostringstream nameA, nameB;
        nameA << "_rt_imageLayerComposite_" << this->m_text.id << "_a";
        nameB << "_rt_imageLayerComposite_" << this->m_text.id << "_b";

        this->m_currentMainFBO = this->m_mainFBO = scene.create (
            nameA.str (), TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1, fboSize, fboSize
        );
        this->m_currentSubFBO = this->m_subFBO = scene.create (
            nameB.str (), TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1, fboSize, fboSize
        );
    }
}

CText::~CText () {
    for (auto* pass : this->m_passes) {
        delete pass;
    }

    if (m_textureID)
        glDeleteTextures (1, &m_textureID);

    if (m_sceneSpacePosition != GL_NONE)
        glDeleteBuffers (1, &m_sceneSpacePosition);
    if (m_passSpacePosition != GL_NONE)
        glDeleteBuffers (1, &m_passSpacePosition);
    if (m_texcoordPass != GL_NONE)
        glDeleteBuffers (1, &m_texcoordPass);

    // Direct render path resources
    if (m_positionBuffer)
        glDeleteBuffers (1, &m_positionBuffer);
    if (m_texcoordBuffer)
        glDeleteBuffers (1, &m_texcoordBuffer);
    if (m_vao)
        glDeleteVertexArrays (1, &m_vao);

    if (m_shader) {
        GLuint shaders[2];
        GLsizei count = 0;
        glGetAttachedShaders (m_shader, 2, &count, shaders);
        for (GLsizei i = 0; i < count; i++)
            glDeleteShader (shaders[i]);
        glDeleteProgram (m_shader);
    }
}

void CText::setup () {
    if (this->m_initialized)
        return;

    // Let CRenderable compute m_animationTime (will be 0 for static text).
    CRenderable::setup ();

    if (this->m_text.effects.empty ()) {
        // No effects: compile the simple inline shader for the direct render path.
        this->setupShader ();
        this->m_initialized = true;
        return;
    }

    // Build CPass list from effects – mirrors CImage::setup().
    for (const auto& cur : this->m_text.effects) {
        if (!cur->visible->value->getBool ())
            continue;

        const auto fboProvider = std::make_shared<FBOProvider> (this);

        for (const auto& fbo : cur->effect->fbos) {
            fboProvider->create (*fbo, TextureFlags_ClampUVs, this->getSize ());
        }

        auto curEffect  = cur->effect->passes.begin ();
        auto endEffect  = cur->effect->passes.end ();
        auto curOverride  = cur->passOverrides.begin ();
        auto endOverride  = cur->passOverrides.end ();

        for (; curEffect != endEffect; ++curEffect) {
            if (!(*curEffect)->material.has_value ()) {
                if (!(*curEffect)->command.has_value () || !(*curEffect)->source.has_value ()
                    || !(*curEffect)->target.has_value ()) {
                    sLog.error ("CText: pass without material missing command/source/target");
                    continue;
                }
                if ((*curEffect)->command != Command_Copy) {
                    sLog.error ("CText: only copy command supported for pass without material");
                    continue;
                }

                auto virtualPass = std::make_unique<MaterialPass> (MaterialPass {
                    .blending  = BlendingMode_Normal,
                    .cullmode  = CullingMode_Disable,
                    .depthtest = DepthtestMode_Disabled,
                    .depthwrite = DepthwriteMode_Disabled,
                    .shader    = "commands/copy",
                    .textures  = { {0, *(*curEffect)->source} },
                    .combos    = {},
                    .constants = {},
                });

                const auto& config = *this->m_virtualPasses.emplace_back (std::move (virtualPass));
                this->m_passes.push_back (
                    new CPass (*this, fboProvider, config, std::nullopt, std::nullopt, (*curEffect)->target.value ())
                );
            } else {
                for (auto& pass : (*curEffect)->material.value ()->passes) {
                    const auto override = curOverride != endOverride
                        ? **curOverride
                        : std::optional<std::reference_wrapper<const ImageEffectPassOverride>> (std::nullopt);
                    const auto target = (*curEffect)->target.has_value ()
                        ? *(*curEffect)->target
                        : std::optional<std::reference_wrapper<std::string>> (std::nullopt);

                    this->m_passes.push_back (
                        new CPass (*this, fboProvider, *pass, override, (*curEffect)->binds, target)
                    );
                }

                if (curOverride != endOverride)
                    ++curOverride;
            }
        }
    }

    // Move the first pass's blending mode to the last pass (same as CImage).
    if (this->m_passes.size () > 1) {
        const auto first = this->m_passes.begin ();
        const auto last  = this->m_passes.rbegin ();
        (*last)->setBlendingMode ((*first)->getBlendingMode ());
        (*first)->setBlendingMode (BlendingMode_Normal);
    }

    this->setupPasses ();
    this->m_initialized = true;
}

void CText::setupPasses () {
    std::shared_ptr<const CFBO> drawTo    = this->m_currentMainFBO;
    std::shared_ptr<const TextureProvider> asInput = this->getTexture ();

    auto cur = this->m_passes.begin ();
    auto end = this->m_passes.end ();

    for (; cur != end; ++cur) {
        CPass* pass                           = *cur;
        std::shared_ptr<const CFBO> prevDrawTo = drawTo;

        // All intermediate passes use the full NDC quad + identity MVP.
        GLuint spacePosition             = this->m_passSpacePosition;
        const glm::mat4* projection      = &this->m_modelViewProjectionPass;
        const glm::mat4* invProjection   = &this->m_modelViewProjectionPassInverse;

        pass->setModelMatrix (&this->m_modelMatrix);
        pass->setViewProjectionMatrix (&this->m_viewProjectionMatrix);

        if (pass->getTarget ().has_value ()) {
            const std::string& target = pass->getTarget ().value ();
            drawTo = pass->getFBOProvider ()->find (target);
            if (drawTo == nullptr)
                drawTo = this->getScene ().findFBO (target);
        } else if (std::next (cur) == end && this->m_text.visible->value->getBool ()) {
            // Last visible pass: composite to the scene framebuffer.
            spacePosition  = this->m_sceneSpacePosition;
            drawTo         = this->getScene ().getFBO ();
            projection     = &this->m_modelViewProjectionScreen;
            invProjection  = &this->m_modelViewProjectionScreenInverse;
        }

        pass->setDestination (drawTo);
        pass->setInput (asInput);
        pass->setPosition (spacePosition);
        pass->setTexCoord (this->m_texcoordPass);
        pass->setModelViewProjectionMatrix (projection);
        pass->setModelViewProjectionMatrixInverse (invProjection);

        drawTo = prevDrawTo;

        if (!pass->getTarget ().has_value ())
            this->pinpongFramebuffer (&drawTo, &asInput);
    }
}

void CText::pinpongFramebuffer (
    std::shared_ptr<const CFBO>* drawTo, std::shared_ptr<const TextureProvider>* asInput
) {
    auto currentMain = this->m_currentMainFBO;
    auto currentSub  = this->m_currentSubFBO;

    if (drawTo)
        *drawTo = currentSub;
    if (asInput)
        *asInput = currentMain;

    this->m_currentMainFBO = currentSub;
    this->m_currentSubFBO  = currentMain;
}

void CText::updateScreenSpacePosition () {
    const glm::vec3 angles = this->m_text.angles->value->getVec3 ();
    glm::mat4 rotModel (1.0f);
    if (angles.x != 0.0f || angles.y != 0.0f || angles.z != 0.0f) {
        rotModel = glm::translate (rotModel, this->m_sceneCenter);
        rotModel = glm::rotate (rotModel, -angles.z, glm::vec3 (0.0f, 0.0f, 1.0f));
        rotModel = glm::rotate (rotModel, angles.y, glm::vec3 (0.0f, 1.0f, 0.0f));
        rotModel = glm::rotate (rotModel, -angles.x, glm::vec3 (1.0f, 0.0f, 0.0f));
        rotModel = glm::translate (rotModel, -this->m_sceneCenter);
    }

    glm::mat4 mvp = this->getScene ().getCamera ().getProjection ()
                    * this->getScene ().getCamera ().getLookAt ()
                    * rotModel;

    if (this->getScene ().getScene ().camera.parallax.enabled
        && !this->getScene ().getContext ().getApp ().getContext ().settings.mouse.disableparallax) {
        const double parallaxAmount = this->getScene ().getScene ().camera.parallax.amount->value->getFloat ();
        const glm::vec2 depth       = this->m_text.parallaxDepth->value->getVec2 ();
        const glm::vec2* displacement = this->getScene ().getParallaxDisplacement ();
        const float x = static_cast<float> (depth.x + parallaxAmount) * displacement->x
                        * static_cast<float> (m_texWidth);
        const float y = static_cast<float> (depth.y + parallaxAmount) * displacement->y
                        * static_cast<float> (m_texWidth);
        mvp = glm::translate (mvp, {x, y, 0.0f});
    }

    this->m_modelViewProjectionScreen        = mvp;
    this->m_modelViewProjectionScreenInverse = glm::inverse (mvp);
}

void CText::render () {
    if (!this->m_text.visible->value->getBool ())
        return;

    // Re-render FreeType texture when dynamic script output changes.
    if (this->m_text.script.has_value ()) {
        const std::string current = this->resolveText ();
        if (current != this->m_lastRenderedText) {
            this->m_lastRenderedText = current;
            this->updateTextureContent ();
        }
    }

    if (!this->m_passes.empty () && this->m_initialized) {
        // ── Effect render path ────────────────────────────────────────────────
        this->updateScreenSpacePosition ();

        glColorMask (true, true, true, true);

        auto cur = this->m_passes.begin ();
        for (const auto end = this->m_passes.end (); cur != end; ++cur) {
            if (std::next (cur) == end)
                glColorMask (true, true, true, false);
            (*cur)->render ();
        }

        glColorMask (true, true, true, true);
        return;
    }

    // ── Direct render path (no effects) ──────────────────────────────────────
    if (!this->m_initialized)
        return;

    const float alpha = this->m_text.alpha->value->getFloat ();

    glBindFramebuffer (GL_FRAMEBUFFER, this->getScene ().getWallpaperFramebuffer ());
    glViewport (0, 0, this->getScene ().getWidth (), this->getScene ().getHeight ());
    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable (GL_DEPTH_TEST);

    glUseProgram (this->m_shader);

    const GLint uniformMVP = glGetUniformLocation (this->m_shader, "u_MVP");
    glUniformMatrix4fv (uniformMVP, 1, GL_FALSE, &this->m_mvp[0][0]);

    const GLint uniformAlpha = glGetUniformLocation (this->m_shader, "u_Alpha");
    glUniform1f (uniformAlpha, alpha);

    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, this->m_textureID);
    glUniform1i (this->m_uniformTexture, 0);

    GLint prevVAO = 0;
    glGetIntegerv (GL_VERTEX_ARRAY_BINDING, &prevVAO);

    glBindVertexArray (this->m_vao);

    glEnableVertexAttribArray (this->m_attribPosition);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_positionBuffer);
    glVertexAttribPointer (this->m_attribPosition, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    glEnableVertexAttribArray (this->m_attribTexcoord);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texcoordBuffer);
    glVertexAttribPointer (this->m_attribTexcoord, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glDrawArrays (GL_TRIANGLES, 0, 6);

    glDisableVertexAttribArray (this->m_attribPosition);
    glDisableVertexAttribArray (this->m_attribTexcoord);
    glBindVertexArray (prevVAO);
    glUseProgram (0);
    glDisable (GL_BLEND);
}

// ─── CRenderable pure virtuals ────────────────────────────────────────────────

glm::vec2 CText::getSize () const {
    return {(float) m_texWidth, (float) m_texHeight};
}

const float& CText::getBrightness () const { return m_brightness; }

const float& CText::getUserAlpha () const { return this->m_text.alpha->value->getFloat (); }

const float& CText::getAlpha () const { return this->m_text.alpha->value->getFloat (); }

const glm::vec3& CText::getColor () const { return m_colorVec3; }

const glm::vec4& CText::getColor4 () const { return m_colorVec4; }

const glm::vec3& CText::getCompositeColor () const { return m_colorVec3; }

// ─── FreeType rasterisation ───────────────────────────────────────────────────

std::string CText::resolveText () const {
    if (this->m_text.script.has_value ()) {
        DynamicValue seed;
        seed.update (this->m_text.value);
        std::map<std::string, DynamicValue*> scriptPropPtrs;
        for (const auto& [name, val] : this->m_text.scriptProperties) {
            if (val)
                scriptPropPtrs[name] = val.get ();
        }
        auto result
            = ScriptEngine::instance ().evaluate (this->m_text.script.value (), scriptPropPtrs, seed);
        if (result->getType () == DynamicValue::String)
            return result->getString ();
        return result->toString ();
    }
    return this->m_text.value;
}

void CText::renderTextToTexture () {
    const glm::vec2 size = this->m_text.size;
    this->m_texWidth  = static_cast<int> (size.x > 0 ? size.x : 256);
    this->m_texHeight = static_cast<int> (size.y > 0 ? size.y : 64);

    std::vector<uint8_t> pixels (this->m_texWidth * this->m_texHeight * 4, 0);

    if (this->m_text.opaqueBackground) {
        const glm::vec3 bg = this->m_text.backgroundColor->value->getVec3 ();
        for (int i = 0; i < this->m_texWidth * this->m_texHeight; i++) {
            pixels[i * 4 + 0] = static_cast<uint8_t> (bg.r * 255);
            pixels[i * 4 + 1] = static_cast<uint8_t> (bg.g * 255);
            pixels[i * 4 + 2] = static_cast<uint8_t> (bg.b * 255);
            pixels[i * 4 + 3] = 255;
        }
    }

    std::vector<uint8_t> fontData;
    try {
        const auto stream = this->getAssetLocator ().read (this->m_text.font);
        fontData.assign (std::istreambuf_iterator<char> (*stream), std::istreambuf_iterator<char> ());
    } catch (const std::exception& e) {
        sLog.error ("CText: cannot load font '", this->m_text.font, "': ", e.what ());
        goto upload;
    }

    {
        FT_Library ftLib;
        if (FT_Init_FreeType (&ftLib) != 0) {
            sLog.error ("CText: FreeType init failed");
            goto upload;
        }

        FT_Face face;
        if (FT_New_Memory_Face (ftLib, fontData.data (), static_cast<FT_Long> (fontData.size ()), 0, &face) != 0) {
            sLog.error ("CText: failed to create FreeType face from '", this->m_text.font, "'");
            FT_Done_FreeType (ftLib);
            goto upload;
        }

        const auto pixelSize = static_cast<FT_UInt> (this->m_text.pointSize * 96.0f / 72.0f);
        FT_Set_Pixel_Sizes (face, 0, pixelSize);

        const glm::vec3 col  = this->m_text.color->value->getVec3 ();
        const uint8_t colR   = static_cast<uint8_t> (col.r * 255);
        const uint8_t colG   = static_cast<uint8_t> (col.g * 255);
        const uint8_t colB   = static_cast<uint8_t> (col.b * 255);
        const float padding  = this->m_text.padding;

        const std::string textStr = this->resolveText ();
        this->m_lastRenderedText  = textStr;
        std::vector<std::string> lines;
        {
            std::istringstream ss (textStr);
            std::string line;
            while (std::getline (ss, line))
                lines.push_back (line);
            if (lines.empty ())
                lines.push_back ("");
        }

        const float lineHeight = static_cast<float> (face->size->metrics.height) / 64.0f;

        std::vector<float> lineWidths;
        lineWidths.reserve (lines.size ());
        for (const auto& ln : lines) {
            float w = 0.0f;
            for (const char ch : ln) {
                if (FT_Load_Char (face, static_cast<FT_ULong> (ch), FT_LOAD_DEFAULT) != 0)
                    continue;
                w += static_cast<float> (face->glyph->advance.x) / 64.0f;
            }
            lineWidths.push_back (w);
        }

        const float totalTextHeight = lineHeight * static_cast<float> (lines.size ());
        const float availW          = static_cast<float> (this->m_texWidth) - 2.0f * padding;

        float baselineY;
        const float ascender = static_cast<float> (face->size->metrics.ascender) / 64.0f;
        if (this->m_text.verticalAlign == "top") {
            baselineY = padding + ascender;
        } else if (this->m_text.verticalAlign == "bottom") {
            baselineY = static_cast<float> (this->m_texHeight) - padding - totalTextHeight + ascender;
        } else {
            baselineY = (static_cast<float> (this->m_texHeight) - totalTextHeight) / 2.0f + ascender;
        }

        for (size_t li = 0; li < lines.size (); li++) {
            const std::string& ln = lines[li];
            const float lineW     = lineWidths[li];

            float penX;
            if (this->m_text.horizontalAlign == "left") {
                penX = padding;
            } else if (this->m_text.horizontalAlign == "right") {
                penX = padding + availW - lineW;
            } else {
                penX = (static_cast<float> (this->m_texWidth) - lineW) / 2.0f;
            }

            const float penY = baselineY + lineHeight * static_cast<float> (li);

            for (const char ch : ln) {
                if (FT_Load_Char (face, static_cast<FT_ULong> (ch), FT_LOAD_RENDER) != 0)
                    continue;

                FT_GlyphSlot glyph = face->glyph;
                const int bmpW     = static_cast<int> (glyph->bitmap.width);
                const int bmpH     = static_cast<int> (glyph->bitmap.rows);
                const int bearingX = glyph->bitmap_left;
                const int bearingY = glyph->bitmap_top;

                const int dstX = static_cast<int> (penX) + bearingX;
                const int dstY = static_cast<int> (penY) - bearingY;

                for (int row = 0; row < bmpH; row++) {
                    for (int col = 0; col < bmpW; col++) {
                        const int px = dstX + col;
                        const int py = dstY + row;
                        if (px < 0 || px >= this->m_texWidth || py < 0 || py >= this->m_texHeight)
                            continue;
                        const uint8_t alpha = glyph->bitmap.buffer[row * glyph->bitmap.pitch + col];
                        if (alpha == 0)
                            continue;
                        const int idx  = (py * this->m_texWidth + px) * 4;
                        const float a  = alpha / 255.0f;
                        pixels[idx + 0] = static_cast<uint8_t> (colR * a + pixels[idx + 0] * (1.0f - a));
                        pixels[idx + 1] = static_cast<uint8_t> (colG * a + pixels[idx + 1] * (1.0f - a));
                        pixels[idx + 2] = static_cast<uint8_t> (colB * a + pixels[idx + 2] * (1.0f - a));
                        pixels[idx + 3] = static_cast<uint8_t> (
                            std::min (255.0f, pixels[idx + 3] + alpha * a)
                        );
                    }
                }

                penX += static_cast<float> (glyph->advance.x) / 64.0f;
            }
        }

        FT_Done_Face (face);
        FT_Done_FreeType (ftLib);
    }

upload:
    if (this->m_textureID == 0) {
        glGenTextures (1, &this->m_textureID);
        glBindTexture (GL_TEXTURE_2D, this->m_textureID);
        glTexImage2D (
            GL_TEXTURE_2D, 0, GL_RGBA, this->m_texWidth, this->m_texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
            pixels.data ()
        );
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture (GL_TEXTURE_2D, this->m_textureID);
        glTexSubImage2D (
            GL_TEXTURE_2D, 0, 0, 0, this->m_texWidth, this->m_texHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data ()
        );
    }
    glBindTexture (GL_TEXTURE_2D, 0);
}

void CText::updateTextureContent () {
    this->renderTextToTexture ();
}

// ─── Geometry setup ───────────────────────────────────────────────────────────

void CText::setupGeometry () {
    const auto& scene = this->getScene ();
    const float sceneW = static_cast<float> (scene.getWidth ());
    const float sceneH = static_cast<float> (scene.getHeight ());

    glm::vec3 origin = this->m_text.origin->value->getVec3 ();
    const glm::vec3 scl = this->m_text.scale->value->getVec3 ();
    const glm::vec2 size = {
        static_cast<float> (this->m_texWidth) * scl.x,
        static_cast<float> (this->m_texHeight) * scl.y,
    };

    glm::vec4 pos;
    pos.x = origin.x - size.x / 2.0f;
    pos.w = origin.y + size.y / 2.0f;
    pos.z = origin.x + size.x / 2.0f;
    pos.y = origin.y - size.y / 2.0f;

    const std::string& align = this->m_text.alignment;
    if (align.find ("top") != std::string::npos) {
        pos.y += size.y / 2.0f;
        pos.w += size.y / 2.0f;
    } else if (align.find ("bottom") != std::string::npos) {
        pos.y -= size.y / 2.0f;
        pos.w -= size.y / 2.0f;
    }
    if (align.find ("left") != std::string::npos) {
        pos.x += size.x / 2.0f;
        pos.z += size.x / 2.0f;
    } else if (align.find ("right") != std::string::npos) {
        pos.x -= size.x / 2.0f;
        pos.z -= size.x / 2.0f;
    }

    pos.x -= sceneW / 2.0f;
    pos.y  = sceneH / 2.0f - pos.y;
    pos.z -= sceneW / 2.0f;
    pos.w  = sceneH / 2.0f - pos.w;

    this->m_sceneCenter = {(pos.x + pos.z) / 2.0f, (pos.y + pos.w) / 2.0f, 0.0f};

    // Build model-space matrices used by effect passes.
    this->m_modelMatrix         = glm::ortho<float> (0.0f, (float) m_texWidth, 0.0f, (float) m_texHeight);
    this->m_viewProjectionMatrix = glm::mat4 (1.0f);

    // Initial screen-space MVP (updated per-frame by updateScreenSpacePosition).
    // Rotation is NOT baked into the vertices – it lives in the MVP matrix.
    const glm::vec3 angles = this->m_text.angles->value->getVec3 ();
    glm::mat4 rotModel (1.0f);
    if (angles.x != 0.0f || angles.y != 0.0f || angles.z != 0.0f) {
        rotModel = glm::translate (rotModel, this->m_sceneCenter);
        rotModel = glm::rotate (rotModel, -angles.z, glm::vec3 (0.0f, 0.0f, 1.0f));
        rotModel = glm::rotate (rotModel, angles.y, glm::vec3 (0.0f, 1.0f, 0.0f));
        rotModel = glm::rotate (rotModel, -angles.x, glm::vec3 (1.0f, 0.0f, 0.0f));
        rotModel = glm::translate (rotModel, -this->m_sceneCenter);
    }
    this->m_modelViewProjectionScreen
        = scene.getCamera ().getProjection () * scene.getCamera ().getLookAt () * rotModel;
    this->m_modelViewProjectionScreenInverse = glm::inverse (this->m_modelViewProjectionScreen);

    // Also keep the combined MVP for the legacy direct-render path.
    this->m_mvp = this->m_modelViewProjectionScreen;

    const GLfloat sceneSpaceVerts[] = { pos.x, pos.y, 0.0f, pos.x, pos.w, 0.0f, pos.z, pos.y, 0.0f,
                                         pos.z, pos.y, 0.0f, pos.x, pos.w, 0.0f, pos.z, pos.w, 0.0f };

    const GLfloat passSpaceVerts[] = { -1.0f,  1.0f, 0.0f, -1.0f, -1.0f, 0.0f,  1.0f,  1.0f, 0.0f,
                                         1.0f,  1.0f, 0.0f, -1.0f, -1.0f, 0.0f,  1.0f, -1.0f, 0.0f };

    const GLfloat texcoordPassData[] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                                          1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };

    glGenBuffers (1, &this->m_sceneSpacePosition);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_sceneSpacePosition);
    glBufferData (GL_ARRAY_BUFFER, sizeof (sceneSpaceVerts), sceneSpaceVerts, GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_passSpacePosition);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_passSpacePosition);
    glBufferData (GL_ARRAY_BUFFER, sizeof (passSpaceVerts), passSpaceVerts, GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_texcoordPass);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texcoordPass);
    glBufferData (GL_ARRAY_BUFFER, sizeof (texcoordPassData), texcoordPassData, GL_STATIC_DRAW);

    // Build the direct-render geometry (VAO + position + texcoord buffers).
    // UV layout: upper GL vertex (pos.y) → t=1, lower GL vertex (pos.w) → t=0,
    // matching the FBO vflip convention.
    const GLfloat uvs[] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };

    glGenVertexArrays (1, &this->m_vao);
    glBindVertexArray (this->m_vao);

    glGenBuffers (1, &this->m_positionBuffer);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_positionBuffer);
    glBufferData (GL_ARRAY_BUFFER, sizeof (sceneSpaceVerts), sceneSpaceVerts, GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_texcoordBuffer);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texcoordBuffer);
    glBufferData (GL_ARRAY_BUFFER, sizeof (uvs), uvs, GL_STATIC_DRAW);

    glBindVertexArray (0);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

// ─── Inline shader (direct render path only) ─────────────────────────────────

void CText::setupShader () {
    const char* vertSrc = "#version 330\n"
                          "in vec3 a_Position;\n"
                          "in vec2 a_TexCoord;\n"
                          "out vec2 v_TexCoord;\n"
                          "uniform mat4 u_MVP;\n"
                          "void main() {\n"
                          "  gl_Position = u_MVP * vec4(a_Position, 1.0);\n"
                          "  v_TexCoord = a_TexCoord;\n"
                          "}\n";
    const char* fragSrc = "#version 330\n"
                          "uniform sampler2D u_Texture;\n"
                          "uniform float u_Alpha;\n"
                          "in vec2 v_TexCoord;\n"
                          "out vec4 out_Color;\n"
                          "void main() {\n"
                          "  vec4 c = texture(u_Texture, v_TexCoord);\n"
                          "  out_Color = vec4(c.rgb, c.a * u_Alpha);\n"
                          "}\n";

    auto compileShader = [&] (GLenum type, const char* src) -> GLuint {
        GLuint id = glCreateShader (type);
        glShaderSource (id, 1, &src, nullptr);
        glCompileShader (id);
        GLint ok = GL_FALSE;
        glGetShaderiv (id, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[512];
            glGetShaderInfoLog (id, sizeof (buf), nullptr, buf);
            sLog.error ("CText shader compile error: ", buf);
        }
        return id;
    };

    GLuint vert = compileShader (GL_VERTEX_SHADER, vertSrc);
    GLuint frag = compileShader (GL_FRAGMENT_SHADER, fragSrc);

    this->m_shader = glCreateProgram ();
    glAttachShader (this->m_shader, vert);
    glAttachShader (this->m_shader, frag);
    glLinkProgram (this->m_shader);

    glDetachShader (this->m_shader, vert);
    glDetachShader (this->m_shader, frag);
    glDeleteShader (vert);
    glDeleteShader (frag);

    this->m_uniformTexture  = glGetUniformLocation (this->m_shader, "u_Texture");
    this->m_attribPosition  = glGetAttribLocation (this->m_shader, "a_Position");
    this->m_attribTexcoord  = glGetAttribLocation (this->m_shader, "a_TexCoord");
}
