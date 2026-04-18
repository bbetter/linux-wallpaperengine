#include "FBOProvider.h"
#include <gmpxx.h>

using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Data::Model;

static TextureFormat fboFormatFromString (const std::string& fmt) {
    if (fmt == "rgba8888" || fmt == "argb8888") return TextureFormat_ARGB8888;
    if (fmt == "rgb888")                         return TextureFormat_RGB888;
    if (fmt == "rgb565")                         return TextureFormat_RGB565;
    if (fmt == "rgba16f" || fmt == "rgba16161616f") return TextureFormat_RGBA16161616f;
    if (fmt == "rgb16f"  || fmt == "rgb161616f")    return TextureFormat_RGB161616f;
    if (fmt == "rg16f"   || fmt == "rg1616f")       return TextureFormat_RG1616f;
    if (fmt == "r16f")                           return TextureFormat_R16f;
    if (fmt == "rg88")                           return TextureFormat_RG88;
    if (fmt == "r8")                             return TextureFormat_R8;
    if (fmt == "bc7")                            return TextureFormat_BC7;
    if (fmt == "rgba1010102")                    return TextureFormat_RGBa1010102;
    // unknown format — fall back to ARGB8888 so the FBO is always usable
    return TextureFormat_ARGB8888;
}

FBOProvider::FBOProvider (const FBOProvider* parent) : m_parent (parent) { }

std::shared_ptr<CFBO> FBOProvider::create (const FBO& base, uint32_t flags, const glm::vec2 size) {
    return this->m_fbos[base.name] = std::make_shared<CFBO> (
	       base.name,
	       fboFormatFromString (base.format), flags, base.scale, size.x / base.scale, size.y / base.scale,
	       size.x / base.scale, size.y / base.scale
	   );
}

std::shared_ptr<CFBO> FBOProvider::create (
    const std::string& name, TextureFormat format, uint32_t flags, float scale, glm::vec2 realSize,
    glm::vec2 textureSize
) {
    return this->m_fbos[name] = std::make_shared<CFBO> (
	       name, format, flags, scale, realSize.x, realSize.y, textureSize.x, textureSize.y
	   );
}

std::shared_ptr<CFBO> FBOProvider::alias (const std::string& newName, const std::string& original) {
    return this->m_fbos[newName] = this->m_fbos[original];
}

std::shared_ptr<CFBO> FBOProvider::find (const std::string& name) const {
    if (const auto it = this->m_fbos.find (name); it != this->m_fbos.end ()) {
	return it->second;
    }

    if (this->m_parent == nullptr) {
	return nullptr;
    }

    return this->m_parent->find (name);
}