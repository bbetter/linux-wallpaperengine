#include "CText.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <map>
#include <sstream>
#include <vector>

#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Render::Objects;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Scripting;

CText::CText (Wallpapers::CScene& scene, const Text& text) :
    CObject (scene, text), m_text (text) {
    const glm::vec3 scale = text.scale->value->getVec3 ();
    sLog.debug (
        "CText '", text.name, "': size=", text.size.x, "x", text.size.y,
        " scale=", scale.x, " pointSize=", text.pointSize,
        " hasScript=", text.script.has_value ()
    );
    this->setupShader ();
    this->renderTextToTexture ();
    this->setupGeometry ();
}

CText::~CText () {
    if (m_texture)
	glDeleteTextures (1, &m_texture);
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

std::string CText::resolveText () const {
    if (this->m_text.script.has_value ()) {
	DynamicValue seed;
	seed.update (this->m_text.value);
	std::map<std::string, DynamicValue*> scriptPropPtrs;
	for (const auto& [name, val] : this->m_text.scriptProperties) {
	    if (val) {
		scriptPropPtrs[name] = val.get ();
	    }
	}
	auto result = ScriptEngine::instance ().evaluate (this->m_text.script.value (), scriptPropPtrs, seed);
	if (result->getType () == DynamicValue::String) {
	    return result->getString ();
	}
	return result->toString ();
    }
    return this->m_text.value;
}

void CText::renderTextToTexture () {
    const glm::vec2 size = this->m_text.size;
    this->m_texWidth = static_cast<int> (size.x > 0 ? size.x : 256);
    this->m_texHeight = static_cast<int> (size.y > 0 ? size.y : 64);

    // Allocate RGBA pixel buffer (initialised to 0 = transparent)
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

    // Load font from the virtual asset container
    std::vector<uint8_t> fontData;
    try {
	const auto stream = this->getAssetLocator ().read (this->m_text.font);
	fontData.assign (std::istreambuf_iterator<char> (*stream), std::istreambuf_iterator<char> ());
    } catch (const std::exception& e) {
	sLog.error ("CText: cannot load font '", this->m_text.font, "': ", e.what ());
	// Upload blank texture so it at least doesn't crash
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

	// Use pixel height based on pointsize (DPI assumed 96: pixels = points * 96/72)
	const auto pixelSize = static_cast<FT_UInt> (this->m_text.pointSize * 96.0f / 72.0f);
	FT_Set_Pixel_Sizes (face, 0, pixelSize);

	const glm::vec3 col = this->m_text.color->value->getVec3 ();
	const uint8_t colR = static_cast<uint8_t> (col.r * 255);
	const uint8_t colG = static_cast<uint8_t> (col.g * 255);
	const uint8_t colB = static_cast<uint8_t> (col.b * 255);

	const float padding = this->m_text.padding;

	// Split text into lines
	const std::string textStr = this->resolveText ();
	this->m_lastRenderedText = textStr;
	std::vector<std::string> lines;
	{
	    std::istringstream ss (textStr);
	    std::string line;
	    while (std::getline (ss, line))
		lines.push_back (line);
	    if (lines.empty ())
		lines.push_back ("");
	}

	// Measure line height from font metrics
	const float lineHeight = static_cast<float> (face->size->metrics.height) / 64.0f;

	// Measure each line's pixel width
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
	const float availW = static_cast<float> (this->m_texWidth) - 2.0f * padding;

	// Determine vertical start position based on verticalAlign
	float baselineY;
	const float ascender = static_cast<float> (face->size->metrics.ascender) / 64.0f;
	if (this->m_text.verticalAlign == "top") {
	    baselineY = padding + ascender;
	} else if (this->m_text.verticalAlign == "bottom") {
	    baselineY = static_cast<float> (this->m_texHeight) - padding - totalTextHeight + ascender;
	} else { // center
	    baselineY = (static_cast<float> (this->m_texHeight) - totalTextHeight) / 2.0f + ascender;
	}

	for (size_t li = 0; li < lines.size (); li++) {
	    const std::string& ln = lines[li];
	    const float lineW = lineWidths[li];

	    // Determine horizontal start based on horizontalAlign
	    float penX;
	    if (this->m_text.horizontalAlign == "left") {
		penX = padding;
	    } else if (this->m_text.horizontalAlign == "right") {
		penX = padding + availW - lineW;
	    } else { // center
		penX = (static_cast<float> (this->m_texWidth) - lineW) / 2.0f;
	    }

	    const float penY = baselineY + lineHeight * static_cast<float> (li);

	    for (const char ch : ln) {
		if (FT_Load_Char (face, static_cast<FT_ULong> (ch), FT_LOAD_RENDER) != 0)
		    continue;

		FT_GlyphSlot glyph = face->glyph;
		const int bmpW = static_cast<int> (glyph->bitmap.width);
		const int bmpH = static_cast<int> (glyph->bitmap.rows);
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
			const int idx = (py * this->m_texWidth + px) * 4;
			// Alpha-blend glyph pixel over existing background
			const float a = alpha / 255.0f;
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
    if (this->m_texture == 0) {
	glGenTextures (1, &this->m_texture);
	glBindTexture (GL_TEXTURE_2D, this->m_texture);
	glTexImage2D (
	    GL_TEXTURE_2D, 0, GL_RGBA, this->m_texWidth, this->m_texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
	    pixels.data ()
	);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
	glBindTexture (GL_TEXTURE_2D, this->m_texture);
	glTexSubImage2D (
	    GL_TEXTURE_2D, 0, 0, 0, this->m_texWidth, this->m_texHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data ()
	);
    }
    glBindTexture (GL_TEXTURE_2D, 0);
}

void CText::updateTextureContent () {
    this->renderTextToTexture ();
}

void CText::setupGeometry () {
    const auto& scene = this->getScene ();
    const float sceneW = static_cast<float> (scene.getWidth ());
    const float sceneH = static_cast<float> (scene.getHeight ());

    glm::vec3 origin = this->m_text.origin->value->getVec3 ();
    const glm::vec3 scale = this->m_text.scale->value->getVec3 ();
    const glm::vec2 size = {
	static_cast<float> (this->m_texWidth) * scale.x,
	static_cast<float> (this->m_texHeight) * scale.y,
    };

    // Same position calculation as CImage
    glm::vec4 pos;
    pos.x = origin.x - size.x / 2.0f;
    pos.w = origin.y + size.y / 2.0f;
    pos.z = origin.x + size.x / 2.0f;
    pos.y = origin.y - size.y / 2.0f;

    // Alignment adjustments (same logic as CImage — fixed)
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

    // Convert to centred OpenGL coordinates
    pos.x -= sceneW / 2.0f;
    pos.y = sceneH / 2.0f - pos.y;
    pos.z -= sceneW / 2.0f;
    pos.w = sceneH / 2.0f - pos.w;

    // Rotation around scene-centre of the quad
    const glm::vec3 sceneCenter = { (pos.x + pos.z) / 2.0f, (pos.y + pos.w) / 2.0f, 0.0f };
    glm::mat4 rotModel (1.0f);
    const glm::vec3 angles = this->m_text.angles->value->getVec3 ();
    if (angles.x != 0.0f || angles.y != 0.0f || angles.z != 0.0f) {
	rotModel = glm::translate (rotModel, sceneCenter);
	rotModel = glm::rotate (rotModel, -angles.z, glm::vec3 (0.0f, 0.0f, 1.0f));
	rotModel = glm::rotate (rotModel, angles.y, glm::vec3 (0.0f, 1.0f, 0.0f));
	rotModel = glm::rotate (rotModel, -angles.x, glm::vec3 (1.0f, 0.0f, 0.0f));
	rotModel = glm::translate (rotModel, -sceneCenter);
    }

    this->m_mvp = scene.getCamera ().getProjection () * scene.getCamera ().getLookAt () * rotModel;

    const GLfloat verts[] = {
	pos.x, pos.y, 0.0f, pos.x, pos.w, 0.0f, pos.z, pos.y, 0.0f,
	pos.z, pos.y, 0.0f, pos.x, pos.w, 0.0f, pos.z, pos.w, 0.0f,
    };
    // UV layout matches CImage texcoordCopy: upper GL vertex (pos.y) → t=1, lower GL vertex (pos.w) → t=0
    // On GLFW/Wayland (vflip=true) the FBO is presented flipped so the lower GL vertex appears at the
    // top of the screen; t=0 (FT row 0 = text top) there gives right-side-up text.
    const GLfloat uvs[] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };

    glGenVertexArrays (1, &this->m_vao);
    glBindVertexArray (this->m_vao);

    glGenBuffers (1, &this->m_positionBuffer);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_positionBuffer);
    glBufferData (GL_ARRAY_BUFFER, sizeof (verts), verts, GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_texcoordBuffer);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texcoordBuffer);
    glBufferData (GL_ARRAY_BUFFER, sizeof (uvs), uvs, GL_STATIC_DRAW);

    glBindVertexArray (0);
}

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

    this->m_uniformTexture = glGetUniformLocation (this->m_shader, "u_Texture");
    this->m_attribPosition = glGetAttribLocation (this->m_shader, "a_Position");
    this->m_attribTexcoord = glGetAttribLocation (this->m_shader, "a_TexCoord");
}

void CText::render () {
    if (!this->m_text.visible->value->getBool ())
	return;

    // Re-render texture when script output changes (e.g. live clock)
    if (this->m_text.script.has_value ()) {
	const std::string current = this->resolveText ();
	if (current != this->m_lastRenderedText) {
	    this->m_lastRenderedText = current;
	    this->updateTextureContent ();
	}
    }

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
    glBindTexture (GL_TEXTURE_2D, this->m_texture);
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
