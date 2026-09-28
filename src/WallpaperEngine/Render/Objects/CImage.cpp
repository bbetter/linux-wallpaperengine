#include "CImage.h"

#include "CRenderable.h"

#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Model/ScriptedDynamicValue.h"
#include "WallpaperEngine/Data/Parsers/MaterialParser.h"
#include "WallpaperEngine/Data/Utils/BinaryReader.h"
#include "WallpaperEngine/Render/Objects/CText.h"
#include "WallpaperEngine/Scripting/ObjectScriptContext.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render::Objects;
using namespace WallpaperEngine::Render::Objects::Effects;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Builders;
using namespace WallpaperEngine::Scripting;

CImage::CImage (Wallpapers::CScene& scene, const Image& image) :
    CRenderable (scene, image, *image.model->material), m_sceneSpacePosition (GL_NONE), m_copySpacePosition (GL_NONE),
    m_passSpacePosition (GL_NONE), m_texcoordCopy (GL_NONE), m_texcoordPass (GL_NONE), m_modelViewProjectionScreen (),
    m_modelViewProjectionPass (glm::mat4 (1.0)), m_modelViewProjectionCopy (), m_modelViewProjectionScreenInverse (),
    m_modelViewProjectionPassInverse (glm::inverse (m_modelViewProjectionPass)), m_modelViewProjectionCopyInverse (),
    m_modelMatrix (), m_viewProjectionMatrix (), m_image (image), m_material (nullptr), m_colorBlendMaterial (nullptr),
    m_pos (), m_initialized (false) {
    // get scene width and height to calculate positions
    auto scene_width = static_cast<float> (scene.getWidth ());
    auto scene_height = static_cast<float> (scene.getHeight ());

    // TODO: MAKE USE OF THE USER PROPERTIES POINTER HERE TOO! SO EVERYTHING IS UPDATED ACCORDINGLY
    glm::vec3 origin = this->getImage ().origin->value->getVec3 ();
    glm::vec2 size = this->getSize ();
    glm::vec3 scale = this->getImage ().scale->value->getVec3 ();

    // Compose with the parent's transform: WPE expresses a child's origin/scale in the
    // parent's local space, so both must be scaled by the parent before being added/multiplied.
    // Without this, objects nested under a scaled-down parent render at their raw,
    // un-composed size/position instead of the intended small satellite placement.
    if (this->m_image.parent.has_value ()) {
	const auto* parentObj = this->getScene ().getObject (this->m_image.parent.value ());
	if (parentObj != nullptr) {
	    const glm::vec3 parentOrigin = parentObj->getObject ().origin->value->getVec3 ();
	    glm::vec3 parentScale (1.0f);
	    if (parentObj->is<CImage> ()) {
		parentScale = parentObj->as<CImage> ()->getImage ().scale->value->getVec3 ();
	    } else if (parentObj->is<CText> ()) {
		parentScale = parentObj->as<CText> ()->getText ().scale->value->getVec3 ();
	    }
	    origin = parentOrigin + parentScale * origin;
	    scale *= parentScale;
	}
    }

    this->detectTexture ();

    // detect texture (if any)
    if (this->m_texture == nullptr) {
	if (this->m_image.model->solidlayer && size.x == 0.0f && size.y == 0.0f) {
	    size.x = scene_width;
	    size.y = scene_height;
	}
	// if (this->m_image->isSolid ()) // layer receives cursor events:
	// https://docs.wallpaperengine.io/en/scene/scenescript/reference/event/cursor.html same applies to effects
	// TODO: create a dummy texture of correct size, fbo constructors should be enough, but this should be properly
	// handled
	this->m_texture = std::make_shared<CFBO> (
	    "", TextureFormat_ARGB8888, TextureFlags_NoFlags, 1, size.x, size.y, size.x, size.y
	);
    }

    // If the wallpaper doesn't specify a size, fall back to the texture or model dimensions
    if ((size.x == 0.0f || size.y == 0.0f) && this->m_texture != nullptr) {
	size.x = static_cast<float> (this->m_texture->getRealWidth ());
	size.y = static_cast<float> (this->m_texture->getRealHeight ());
    } else if ((size.x == 0.0f || size.y == 0.0f) && this->getImage ().model->width.has_value ()
	       && this->getImage ().model->height.has_value ()) {
	size.x = static_cast<float> (this->getImage ().model->width.value ());
	size.y = static_cast<float> (this->getImage ().model->height.value ());
    }

    // fullscreen layers should use the whole projection's size
    // TODO: WHAT SHOULD AUTOSIZE DO?
    if (this->getImage ().model->fullscreen) {
	size = { scene_width, scene_height };
	origin = { scene_width / 2, scene_height / 2, 0 };

	// TODO: CHANGE ALIGNMENT TOO?
    }

    // Parse puppet mesh if this object has one
    if (this->getImage ().model->puppet.has_value ()) {
        this->parsePuppetMesh (size);
    }

    glm::vec2 scaledSize = size * glm::vec2 (scale);

    // calculate the center and shift from there
    this->m_pos.x = origin.x - (scaledSize.x / 2);
    this->m_pos.w = origin.y + (scaledSize.y / 2);
    this->m_pos.z = origin.x + (scaledSize.x / 2);
    this->m_pos.y = origin.y - (scaledSize.y / 2);

    if (this->getImage ().alignment.find ("top") != std::string::npos) {
	this->m_pos.y += scaledSize.y / 2;
	this->m_pos.w += scaledSize.y / 2;
    } else if (this->getImage ().alignment.find ("bottom") != std::string::npos) {
	this->m_pos.y -= scaledSize.y / 2;
	this->m_pos.w -= scaledSize.y / 2;
    }

    if (this->getImage ().alignment.find ("left") != std::string::npos) {
	this->m_pos.x += scaledSize.x / 2;
	this->m_pos.z += scaledSize.x / 2;
    } else if (this->getImage ().alignment.find ("right") != std::string::npos) {
	this->m_pos.x -= scaledSize.x / 2;
	this->m_pos.z -= scaledSize.x / 2;
    }

    // wallpaper engine
    this->m_pos.x -= scene_width / 2;
    this->m_pos.y = scene_height / 2 - this->m_pos.y;
    this->m_pos.z -= scene_width / 2;
    this->m_pos.w = scene_height / 2 - this->m_pos.w;

    // register both FBOs into the scene
    std::ostringstream nameA, nameB;

    // TODO: determine when _rt_imageLayerComposite and _rt_imageLayerAlbedo is used
    nameA << "_rt_imageLayerComposite_" << this->getImage ().id << "_a";
    nameB << "_rt_imageLayerComposite_" << this->getImage ().id << "_b";

    this->m_currentMainFBO = this->m_mainFBO = scene.create (
	nameA.str (), TextureFormat_ARGB8888, this->m_texture->getFlags (), 1, { size.x, size.y }, { size.x, size.y }
    );
    this->m_currentSubFBO = this->m_subFBO = scene.create (
	nameB.str (), TextureFormat_ARGB8888, this->m_texture->getFlags (), 1, { size.x, size.y }, { size.x, size.y }
    );

    // build a list of vertices, these might need some change later (or maybe invert the camera)
    GLfloat sceneSpacePosition[] = { this->m_pos.x, this->m_pos.y, 0.0f, this->m_pos.x, this->m_pos.w, 0.0f,
				     this->m_pos.z, this->m_pos.y, 0.0f, this->m_pos.z, this->m_pos.y, 0.0f,
				     this->m_pos.x, this->m_pos.w, 0.0f, this->m_pos.z, this->m_pos.w, 0.0f };

    float width = 1.0f;
    float height = 1.0f;

    if (this->getTexture ()->isAnimated ()) {
	// animated images use different coordinates as they're essentially a texture atlas
	width = static_cast<float> (this->getTexture ()->getRealWidth ())
	    / static_cast<float> (this->getTexture ()->getTextureWidth (0));
	height = static_cast<float> (this->getTexture ()->getRealHeight ())
	    / static_cast<float> (this->getTexture ()->getTextureHeight (0));
    }
    // calculate the correct texCoord limits for the texture based on the texture screen size and real size
    else if (this->getTexture () != nullptr
	     && (this->getTexture ()->getTextureWidth (0) != this->getTexture ()->getRealWidth ()
		 || this->getTexture ()->getTextureHeight (0) != this->getTexture ()->getRealHeight ())) {
	// Account for padding in non-power-of-two textures: clamp UVs to the real content
	width = static_cast<float> (this->getTexture ()->getRealWidth ())
	    / static_cast<float> (this->getTexture ()->getTextureWidth (0));
	height = static_cast<float> (this->getTexture ()->getRealHeight ())
	    / static_cast<float> (this->getTexture ()->getTextureHeight (0));
    }

    // TODO: RECALCULATE THESE POSITIONS FOR PASSTHROUGH SO THEY TAKE THE RIGHT PART OF THE TEXTURE
    float x = 0.0f;
    float y = 0.0f;

    if (this->getTexture ()->isAnimated ()) {
	// animations should be copied completely
	x = 0.0f;
	y = 0.0f;
	width = 1.0f;
	height = 1.0f;
    }

    GLfloat realWidth = size.x;
    GLfloat realHeight = size.y;
    GLfloat realX = 0.0;
    GLfloat realY = 0.0;

    if (this->getImage ().model->passthrough) {
	if (this->getImage ().model->fullscreen) {
	    x = -((this->m_pos.x + (scene_width / 2)) / size.x);
	    y = -((this->m_pos.w + (scene_height / 2)) / size.y);
	    height = (this->m_pos.y + (scene_height / 2)) / size.y;
	    width = (this->m_pos.z + (scene_width / 2)) / size.x;
	    realX = -1.0;
	    realY = -1.0;
	    realWidth = 1.0;
	    realHeight = 1.0;
	} else {
	    // Non-fullscreen composelayer: fill the full local FBO and use scene-space positions
	    // so the copy pass correctly samples the composelayer's region of _rt_FullFrameBuffer.
	    // The old formula divided by image size instead of scene size, producing out-of-range
	    // UVs that caused a huge clipped quad sampling the scene center instead of the correct area.
	    x = 0.0f;
	    y = 0.0f;
	    width = 1.0f;
	    height = 1.0f;
	    realX = this->m_pos.x;
	    realY = this->m_pos.w;
	    realWidth = this->m_pos.z;
	    realHeight = this->m_pos.y;
	}
    }

    GLfloat texcoordCopy[] = { x, height, x, y, width, height, width, height, x, y, width, y };

    GLfloat copySpacePosition[] = { realX,     realHeight, 0.0f, realX, realY, 0.0f, realWidth, realHeight, 0.0f,
				    realWidth, realHeight, 0.0f, realX, realY, 0.0f, realWidth, realY,      0.0f };

    GLfloat texcoordPass[] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };

    GLfloat passSpacePosition[]
	= { -1.0, 1.0, 0.0f, -1.0, -1.0, 0.0f, 1.0, 1.0, 0.0f, 1.0, 1.0, 0.0f, -1.0, -1.0, 0.0f, 1.0, -1.0, 0.0f };

    // bind vertex list to the openGL buffers
    // Use GL_DYNAMIC_DRAW so script-driven position updates can be uploaded efficiently
    glGenBuffers (1, &this->m_sceneSpacePosition);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_sceneSpacePosition);
    glBufferData (GL_ARRAY_BUFFER, sizeof (sceneSpacePosition), sceneSpacePosition, GL_DYNAMIC_DRAW);

    glGenBuffers (1, &this->m_copySpacePosition);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_copySpacePosition);
    glBufferData (GL_ARRAY_BUFFER, sizeof (copySpacePosition), copySpacePosition, GL_STATIC_DRAW);

    // bind pass' vertex list to the openGL buffers
    glGenBuffers (1, &this->m_passSpacePosition);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_passSpacePosition);
    glBufferData (GL_ARRAY_BUFFER, sizeof (passSpacePosition), passSpacePosition, GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_texcoordCopy);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texcoordCopy);
    glBufferData (GL_ARRAY_BUFFER, sizeof (texcoordCopy), texcoordCopy, GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_texcoordPass);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texcoordPass);
    glBufferData (GL_ARRAY_BUFFER, sizeof (texcoordPass), texcoordPass, GL_STATIC_DRAW);

    // compute the center of the image in scene space for rotation
    this->m_sceneCenter = glm::vec3 (
	(this->m_pos.x + this->m_pos.z) / 2.0f,
	(this->m_pos.y + this->m_pos.w) / 2.0f,
	0.0f
    );

    this->m_modelViewProjectionScreen
	= this->getScene ().getCamera ().getProjection () * this->getScene ().getCamera ().getLookAt ();

    if (this->getImage ().model->passthrough && !this->getImage ().model->fullscreen) {
	// Non-fullscreen composelayer copy pass uses the scene camera MVP so copySpacePosition
	// (scene-space vertices) correctly projects to scene UV for sampling _rt_FullFrameBuffer.
	this->m_modelViewProjectionCopy = this->getScene ().getCamera ().getProjection ()
	    * this->getScene ().getCamera ().getLookAt ();
    } else if (this->m_puppetMesh) {
	// Puppet vertices are in centered local space: (-w/2 .. +w/2, -h/2 .. +h/2).
	// Y-axis is flipped relative to OpenGL FBO convention (FBO row 0 = screen bottom,
	// but puppet Y+ = screen top in WPE scene coords → need Y-down ortho so the FBO
	// is upright when rendered via the scene camera which also flips Y).
	this->m_modelViewProjectionCopy = glm::ortho<float> (-size.x / 2, size.x / 2, size.y / 2, -size.y / 2);
    } else {
	this->m_modelViewProjectionCopy = glm::ortho<float> (0.0, size.x, 0.0, size.y);
    }
    this->m_modelViewProjectionCopyInverse = glm::inverse (this->m_modelViewProjectionCopy);
    this->m_modelMatrix = glm::ortho<float> (0.0, size.x, 0.0, size.y);
    this->m_viewProjectionMatrix = glm::mat4 (1.0);

    // ensure the input texture is marked as used
    // this makes video playback start if it's not already
    this->m_texture->incrementUsageCount ();
}

CImage::~CImage () {
    this->m_texture->decrementUsageCount ();

    // delete passes first as they depend on the image's data
    for (auto* pass : this->m_passes) {
	delete pass;
    }

    this->m_passes.clear ();

    // free any gl resources
    glDeleteBuffers (1, &this->m_sceneSpacePosition);
    glDeleteBuffers (1, &this->m_copySpacePosition);
    glDeleteBuffers (1, &this->m_passSpacePosition);
    glDeleteBuffers (1, &this->m_texcoordCopy);
    glDeleteBuffers (1, &this->m_texcoordPass);

    if (this->m_puppetMesh) {
        glDeleteBuffers (1, &this->m_puppetMesh->vbo);
        glDeleteBuffers (1, &this->m_puppetMesh->ibo);
    }
}

void CImage::parsePuppetMesh (const glm::vec2& size) {
    using namespace WallpaperEngine::Data::Utils;

    const auto& puppetPath = this->getImage ().model->puppet.value ();

    ReadStreamSharedPtr stream;
    try {
        stream = this->getAssetLocator ().read (puppetPath);
    } catch (const std::exception& e) {
        sLog.error ("Puppet MDL not found: ", puppetPath, " (", e.what (), ")");
        return;
    }

    BinaryReader reader (stream);

    // Skip null-terminated magic header ("MDLV0013")
    (void)reader.nextNullTerminatedString ();
    // Read first, second, third DWORDs
    const uint32_t hdr1 = reader.nextUInt32 ();
    const uint32_t hdr2 = reader.nextUInt32 ();
    const uint32_t hdr3 = reader.nextUInt32 ();
    // Read null-terminated JSON path
    const std::string jsonPath = reader.nextNullTerminatedString ();
    // Read fourth DWORD
    const uint32_t hdr4 = reader.nextUInt32 ();
    sLog.out ("MDL header DWORDs: ", hdr1, " ", hdr2, " ", hdr3, " | jsonPath=", jsonPath, " | hdr4=", hdr4);

    const uint32_t vertexByteLength = reader.nextUInt32 ();
    // Each MDL vertex is 52 bytes: VECTOR3(12) + BLENDINDICES(16) + VECTOR4(16) + VECTOR2(8)
    constexpr uint32_t MDL_VERTEX_STRIDE = 52;
    const uint32_t numVertices = vertexByteLength / MDL_VERTEX_STRIDE;

    // Build compact VBO: float[5] per vertex = {x, y, z, u, v} (20 bytes, stride 20)
    // UV v is flipped (MDL uses V=0=top, OpenGL uses V=0=bottom)
    std::vector<float> vboData;
    vboData.reserve (numVertices * 5);

    // MDL vertex layout (52 bytes):
    //   offset  0: VECTOR3 position (3 × float = 12 bytes)
    //   offset 12: BLENDINDICES (4 × uint32 = 16 bytes)
    //   offset 28: BLENDWEIGHTS (4 × float  = 16 bytes)
    //   offset 44: TEXCOORD     (2 × float  =  8 bytes)
    char vtxBuf[52];
    for (uint32_t i = 0; i < numVertices; ++i) {
        reader.next (vtxBuf, 52);

        float x, y, z, u, v;
        memcpy (&x, vtxBuf + 0,  sizeof (float));
        memcpy (&y, vtxBuf + 4,  sizeof (float));
        memcpy (&z, vtxBuf + 8,  sizeof (float));
        memcpy (&u, vtxBuf + 44, sizeof (float));
        memcpy (&v, vtxBuf + 48, sizeof (float));

        vboData.push_back (x);
        vboData.push_back (y);
        vboData.push_back (z);
        vboData.push_back (u);
        vboData.push_back (v); // no V flip: WPE .tex stored top-row first, OpenGL maps first row to V=0=visual top
    }

    const uint32_t indicesByteLength = reader.nextUInt32 ();
    // Each triangle = 3 × uint16 = 6 bytes
    const uint32_t numIndices = indicesByteLength / 2;

    std::vector<uint16_t> iboData;
    iboData.reserve (numIndices);

    for (uint32_t i = 0; i < numIndices; ++i) {
        char buf[2];
        reader.next (buf, 2);
        uint16_t idx = static_cast<uint16_t> ((buf[1] & 0xFF) << 8 | (buf[0] & 0xFF));
        iboData.push_back (idx);
    }

    auto mesh = std::make_unique<PuppetMesh> ();
    mesh->numIndices = static_cast<GLsizei> (numIndices);

    glGenBuffers (1, &mesh->vbo);
    glBindBuffer (GL_ARRAY_BUFFER, mesh->vbo);
    glBufferData (GL_ARRAY_BUFFER, static_cast<GLsizeiptr> (vboData.size () * sizeof (float)), vboData.data (),
                  GL_STATIC_DRAW);
    glBindBuffer (GL_ARRAY_BUFFER, 0);

    glGenBuffers (1, &mesh->ibo);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, mesh->ibo);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr> (iboData.size () * sizeof (uint16_t)),
                  iboData.data (), GL_STATIC_DRAW);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);

    this->m_puppetMesh = std::move (mesh);

    sLog.out ("Puppet mesh loaded: ", numVertices, " vertices, ", numIndices / 3, " triangles from ", puppetPath);
}

void CImage::setup () {
    // do not double-init stuff, that's bad!
    if (this->m_initialized) {
	return;
    }

    // TODO: CHECK ORDER OF THINGS, 2419444134'S ID 27 DEPENDS ON 104'S COMPOSITE_A WHEN OUR LAST RENDER IS ON
    // COMPOSITE_B
    // Passthrough layers still need their base material passes even when they have no extra effects.
    // Skipping them drops compose layers entirely in some scene wallpapers.

    // copy pass to the composite layer
    for (const auto& cur : this->getImage ().model->material->passes) {
	this->m_passes.push_back (
	    new CPass (*this, std::make_shared<FBOProvider> (this), *cur, std::nullopt, std::nullopt, std::nullopt)
	);
    }

    // prepare the passes list
    if (!this->getImage ().effects.empty ()) {
	// generate the effects used by this material
	for (const auto& cur : this->m_image.effects) {
	    // do not add non-visible effects, this might need some adjustements tho as some effects might not be
	    // visible but affect the output of the image...
	    if (!cur->visible->value->getBool ()) {
		continue;
	    }

	    const auto fboProvider = std::make_shared<FBOProvider> (this);

	    // create all the fbos for this effect
	    for (const auto& fbo : cur->effect->fbos) {
		fboProvider->create (*fbo, this->m_texture->getFlags (), this->getSize ());
		// WPE scene.json can reference unique FBOs by a scoped name: baseName_objectId_effectId.
		// Register that alias so override texture resolution finds the local FBO without an error log.
		if (fbo->unique) {
		    std::string scopedName = fbo->name + "_" + std::to_string (this->getImage ().id)
			+ "_" + std::to_string (cur->id);
		    fboProvider->alias (scopedName, fbo->name);
		}
	    }

	    // TODO: MAKE USE OF ZIP OPERATOR IN BOOST? WAY OVERKILL JUST FOR THIS...

	    auto curEffect = cur->effect->passes.begin ();
	    auto endEffect = cur->effect->passes.end ();
	    auto curOverride = cur->passOverrides.begin ();
	    auto endOverride = cur->passOverrides.end ();

	    for (; curEffect != endEffect; ++curEffect) {
		if (!(*curEffect)->material.has_value ()) {
		    if (!(*curEffect)->command.has_value ()) {
			sLog.error ("Pass without material and command not supported");
			continue;
		    }

		    if (!(*curEffect)->source.has_value ()) {
			sLog.error ("Pass without material and source not supported");
			continue;
		    }

		    if (!(*curEffect)->target.has_value ()) {
			sLog.error ("Pass without material and target not supported");
			continue;
		    }

		    if ((*curEffect)->command != Command_Copy) {
			sLog.error ("Only copy command is supported for pass without material");
			continue;
		    }

		    auto virtualPass
			= std::make_unique<MaterialPass> (MaterialPass { .blending = BlendingMode_Normal,
									 .cullmode = CullingMode_Disable,
									 .depthtest = DepthtestMode_Disabled,
									 .depthwrite = DepthwriteMode_Disabled,
									 .shader = "commands/copy",
									 .textures = { { 0, *(*curEffect)->source } },
									 .combos = {},
									 .constants = {} });

		    const auto& config = *this->m_virtualPassess.emplace_back (std::move (virtualPass));

		    // build a pass for a copy shader
		    this->m_passes.push_back (new CPass (
			*this, fboProvider, config, std::nullopt, std::nullopt, (*curEffect)->target.value ()
		    ));
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

		    if (curOverride != endOverride) {
			++curOverride;
		    }
		}
	    }
	}
    }

    // extra render pass if there's any blending to be done
    if (this->m_image.colorBlendMode > 0) {
	this->m_materials.colorBlending.material
	    = MaterialParser::load (this->getScene ().getScene ().project, "materials/util/effectpassthrough.json");
	this->m_materials.colorBlending.override = std::make_unique<ImageEffectPassOverride> (ImageEffectPassOverride {
            .id = -1,
            .combos = {
                {"BLENDMODE", this->m_image.colorBlendMode},
            },
            .constants = {},
            .textures = {},
        });

	this->m_passes.push_back (new CPass (
	    *this, std::make_shared<FBOProvider> (this), **this->m_materials.colorBlending.material->passes.begin (),
	    *this->m_materials.colorBlending.override, std::nullopt, std::nullopt
	));
    }

    // if there's more than one pass the blendmode has to be moved from the beginning to the end
    if (this->m_passes.size () > 1) {
	const auto first = this->m_passes.begin ();
	const auto last = this->m_passes.rbegin ();

	(*last)->setBlendingMode ((*first)->getBlendingMode ());
	(*first)->setBlendingMode (BlendingMode_Normal);
    }

    CRenderable::setup ();

    this->setupPasses ();

    // Detect object scripts (thisScene.createLayer / thisScene.getLayerIndex)
    // and create a persistent ObjectScriptContext for this CImage
    const auto* sdv = dynamic_cast<const ScriptedDynamicValue*> (this->m_image.visible->value.get ());
    if (sdv != nullptr && sdv->isObjectScript ()) {
        std::map<std::string, DynamicValue*> rawProps;
        for (const auto& [k, v] : sdv->getScriptProps ()) {
            rawProps[k] = v.get ();
        }
        this->m_objectScriptCtx = std::make_unique<ObjectScriptContext> (*this, sdv->getScriptSource (), rawProps);
        this->m_objectScriptCtx->init ();
    }

    this->m_initialized = true;
}

void CImage::setupPasses () {
    // do a pass on everything and setup proper inputs and values
    std::shared_ptr<const CFBO> drawTo = this->m_currentMainFBO;
    std::shared_ptr<const TextureProvider> asInput = this->getTexture ();
    GLuint texcoord = this->getTexCoordCopy ();

    auto cur = this->m_passes.begin ();
    auto end = this->m_passes.end ();
    bool first = true;

    for (; cur != end; ++cur) {
	// TODO: PROPERLY CHECK EFFECT'S VISIBILITY AND TAKE IT INTO ACCOUNT
	// TODO: THIS REQUIRES ON-THE-FLY EVALUATION OF EFFECTS VISIBILITY TO FIGURE OUT
	// TODO: WHICH ONE IS THE LAST + A FEW OTHER THINGS
	Effects::CPass* pass = *cur;
	std::shared_ptr<const CFBO> prevDrawTo = drawTo;
	GLuint spacePosition = (first) ? this->getCopySpacePosition () : this->getPassSpacePosition ();
	const glm::mat4* projection = (first) ? &this->m_modelViewProjectionCopy : &this->m_modelViewProjectionPass;
	const glm::mat4* inverseProjection
	    = (first) ? &this->m_modelViewProjectionCopyInverse : &this->m_modelViewProjectionPassInverse;
	const bool isCopyPass = first;
	first = false;

	pass->setModelMatrix (&this->m_modelMatrix);
	pass->setViewProjectionMatrix (&this->m_viewProjectionMatrix);

	// set viewport and target texture if needed
	if (pass->getTarget ().has_value ()) {
	    // setup target texture
	    std::string target = pass->getTarget ().value ();
	    drawTo = pass->getFBOProvider ()->find (target);
	    // spacePosition = this->getPassSpacePosition ();

	    // not a local fbo, try to find a scene fbo with the same name
	    if (drawTo == nullptr) {
		// this one throws if no fbo was found
		drawTo = this->getScene ().findFBO (target);
	    }
	}
	// determine if it's the last element in the list as this is a screen-copy-like process
	// TODO: PROPERLY CHECK IF THIS IS ALL THAT'S NEEDED
	else if (std::next (cur) == end && this->getImage ().visible->value->getBool ()) {
	    // TODO: PROPERLY CHECK EFFECT'S VISIBILITY AND TAKE IT INTO ACCOUNT
	    spacePosition = this->getSceneSpacePosition ();
	    drawTo = this->getScene ().getFBO ();
	    projection = &this->m_modelViewProjectionScreen;
	    inverseProjection = &this->m_modelViewProjectionScreenInverse;
	}

	pass->setDestination (drawTo);
	pass->setInput (asInput);
	pass->setPosition (spacePosition);
	pass->setTexCoord (texcoord);
	pass->setModelViewProjectionMatrix (projection);
	pass->setModelViewProjectionMatrixInverse (inverseProjection);

	// Hook puppet mesh geometry into the copy pass
	if (isCopyPass && this->m_puppetMesh) {
	    const GLuint programID = pass->getProgramID ();
	    const GLint posLoc = glGetAttribLocation (programID, "a_Position");
	    const GLint uvLoc = glGetAttribLocation (programID, "a_TexCoord");
	    const GLuint vbo = this->m_puppetMesh->vbo;
	    const GLuint ibo = this->m_puppetMesh->ibo;
	    const GLsizei numIndices = this->m_puppetMesh->numIndices;

	    pass->setGeometryCallback (
		// setupAttribs: bind interleaved puppet VBO with stride 20
		[posLoc, uvLoc, vbo] () {
		    if (posLoc >= 0) {
			glEnableVertexAttribArray (posLoc);
			glBindBuffer (GL_ARRAY_BUFFER, vbo);
			glVertexAttribPointer (posLoc, 3, GL_FLOAT, GL_FALSE, 20, nullptr);
		    }
		    if (uvLoc >= 0) {
			glEnableVertexAttribArray (uvLoc);
			glBindBuffer (GL_ARRAY_BUFFER, vbo);
			glVertexAttribPointer (uvLoc, 2, GL_FLOAT, GL_FALSE, 20,
					       reinterpret_cast<const void*> (3 * sizeof (float)));
		    }
		},
		// drawGeometry: indexed draw from IBO
		[ibo, numIndices] () {
		    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, ibo);
		    glDrawElements (GL_TRIANGLES, numIndices, GL_UNSIGNED_SHORT, nullptr);
		    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
		},
		// cleanupAttribs: disable attrib arrays
		[posLoc, uvLoc] () {
		    if (posLoc >= 0)
			glDisableVertexAttribArray (posLoc);
		    if (uvLoc >= 0)
			glDisableVertexAttribArray (uvLoc);
		}
	    );
	}

	texcoord = this->getTexCoordPass ();
	drawTo = prevDrawTo;

	if (!pass->getTarget ().has_value ()) {
	    this->pinpongFramebuffer (&drawTo, &asInput);
	}
    }
}

void CImage::pinpongFramebuffer (std::shared_ptr<const CFBO>* drawTo, std::shared_ptr<const TextureProvider>* asInput) {
    // temporarily store FBOs used
    std::shared_ptr<const CFBO> currentMainFBO = this->m_currentMainFBO;
    std::shared_ptr<const CFBO> currentSubFBO = this->m_currentSubFBO;

    if (drawTo != nullptr) {
	*drawTo = currentSubFBO;
    }
    if (asInput != nullptr) {
	*asInput = currentMainFBO;
    }

    // swap the FBOs
    this->m_currentMainFBO = currentSubFBO;
    this->m_currentSubFBO = currentMainFBO;
}

void CImage::render () {
    // do not try to render something that did not initialize successfully
    // non-visible materials do need to be rendered
    if (!this->m_initialized) {
	return;
    }

    // TODO: DO NOT DRAW IMAGES THAT ARE NOT VISIBLE AND NOTHING DEPENDS ON THEM

    glColorMask (true, true, true, true);

    // Recompute VBO if a script has updated origin/scale/alignment
    if (this->m_positionDirty) {
        this->recomputePositionAndUploadVBO ();
    }

    // Always update screen transform (handles rotation + parallax dynamically)
    this->updateScreenSpacePosition ();

#if !NDEBUG
    std::string str = "Rendering ";

    if (this->getScene ().getScene ().camera.bloom.enabled->value->getBool () && this->getId () == -1) {
	str += "bloom";
    } else {
	str += this->getImage ().name + " (" + std::to_string (this->getId ()) + ", "
	    + this->getImage ().model->material->filename + ")";
    }

    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, str.c_str ());
#endif /* DEBUG */

    auto cur = this->m_passes.begin ();

    for (const auto end = this->m_passes.end (); cur != end; ++cur) {
	if (std::next (cur) == end) {
	    glColorMask (true, true, true, false);
	}

	(*cur)->render ();
    }

#if !NDEBUG
    glPopDebugGroup ();
#endif /* DEBUG */
}

const float& CImage::getBrightness () const { return this->m_image.brightness; }

const float& CImage::getUserAlpha () const { return this->m_image.alpha->value->getFloat (); }

const float& CImage::getAlpha () const { return this->m_image.alpha->value->getFloat (); }

const glm::vec3& CImage::getColor () const { return this->m_image.color->value->getVec3 (); }

const glm::vec4& CImage::getColor4 () const { return this->m_image.color->value->getVec4 (); }

const glm::vec3& CImage::getCompositeColor () const { return this->m_image.color->value->getVec3 (); }

void CImage::updateScreenSpacePosition () {
    // Use script-overridden angles (in radians) if available, otherwise use data model angles
    glm::vec3 angles = this->m_overrideAngles.has_value ()
                           ? this->m_overrideAngles.value ()
                           : this->getImage ().angles->value->getVec3 ();

    glm::mat4 rotModel = glm::mat4 (1.0f);
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

    // Apply parallax displacement if enabled
    // Use script-overridden parallax depth if available
    if (this->getScene ().getScene ().camera.parallax.enabled
	&& !this->getImage ().model->fullscreen
	&& !this->getScene ().getContext ().getApp ().getContext ().settings.mouse.disableparallax) {
	const double parallaxAmount = this->getScene ().getScene ().camera.parallax.amount->value->getFloat ();
	const glm::vec2 depth = this->m_overrideParallaxDepth.has_value ()
	                            ? this->m_overrideParallaxDepth.value ()
	                            : this->getImage ().parallaxDepth->value->getVec2 ();
	const glm::vec2* displacement = this->getScene ().getParallaxDisplacement ();
	float x = (depth.x + static_cast<float> (parallaxAmount)) * displacement->x * this->getSize ().x;
	float y = (depth.y + static_cast<float> (parallaxAmount)) * displacement->y * this->getSize ().x;
	mvp = glm::translate (mvp, { x, y, 0.0f });
    }

    this->m_modelViewProjectionScreen = mvp;
    this->m_modelViewProjectionScreenInverse = glm::inverse (mvp);
}

const Image& CImage::getImage () const { return this->m_image; }

const std::vector<CEffect*>& CImage::getEffects () const { return this->m_effects; }

const Effects::CMaterial* CImage::getMaterial () const { return this->m_material; }

glm::vec2 CImage::getSize () const {
    if (this->m_texture == nullptr) {
	return this->getImage ().size;
    }

    return { this->m_texture->getRealWidth (), this->m_texture->getRealHeight () };
}

GLuint CImage::getSceneSpacePosition () const { return this->m_sceneSpacePosition; }

GLuint CImage::getCopySpacePosition () const { return this->m_copySpacePosition; }

GLuint CImage::getPassSpacePosition () const { return this->m_passSpacePosition; }

GLuint CImage::getTexCoordCopy () const { return this->m_texcoordCopy; }

GLuint CImage::getTexCoordPass () const { return this->m_texcoordPass; }

// ---- Script-driven property overrides ----

void CImage::setScriptOrigin (const glm::vec3& v) {
    this->m_overrideOrigin = v;
    this->m_positionDirty = true;
}

void CImage::setScriptScale (const glm::vec3& v) {
    this->m_overrideScale = v;
    this->m_positionDirty = true;
}

void CImage::setScriptAngles (const glm::vec3& degrees) {
    // WPE JS API uses degrees; store in radians for the render pipeline
    this->m_overrideAngles = glm::radians (degrees);
}

void CImage::setScriptParallaxDepth (const glm::vec2& v) {
    this->m_overrideParallaxDepth = v;
}

void CImage::setScriptAlignment (const std::string& s) {
    if (!s.empty () && s != this->m_overrideAlignment.value_or (this->m_image.alignment)) {
        this->m_overrideAlignment = s;
        this->m_positionDirty = true;
    }
}

glm::vec3 CImage::getEffectiveOrigin () const {
    return this->m_overrideOrigin.has_value () ? this->m_overrideOrigin.value ()
                                               : this->m_image.origin->value->getVec3 ();
}

glm::vec3 CImage::getEffectiveScale () const {
    return this->m_overrideScale.has_value () ? this->m_overrideScale.value ()
                                              : this->m_image.scale->value->getVec3 ();
}

void CImage::runObjectScriptUpdate () {
    if (this->m_objectScriptCtx)
        this->m_objectScriptCtx->update ();
}

void CImage::recomputePositionAndUploadVBO () {
    const auto scene_width = static_cast<float> (this->getScene ().getWidth ());
    const auto scene_height = static_cast<float> (this->getScene ().getHeight ());

    const glm::vec3 origin = this->getEffectiveOrigin ();
    const glm::vec2 size = this->getSize ();
    const glm::vec3 scale = this->getEffectiveScale ();
    const std::string alignment = this->m_overrideAlignment.value_or (this->m_image.alignment);

    const glm::vec2 scaledSize = size * glm::vec2 (scale);

    this->m_pos.x = origin.x - (scaledSize.x / 2.0f);
    this->m_pos.w = origin.y + (scaledSize.y / 2.0f);
    this->m_pos.z = origin.x + (scaledSize.x / 2.0f);
    this->m_pos.y = origin.y - (scaledSize.y / 2.0f);

    if (alignment.find ("top") != std::string::npos) {
        this->m_pos.y += scaledSize.y / 2.0f;
        this->m_pos.w += scaledSize.y / 2.0f;
    } else if (alignment.find ("bottom") != std::string::npos) {
        this->m_pos.y -= scaledSize.y / 2.0f;
        this->m_pos.w -= scaledSize.y / 2.0f;
    }

    if (alignment.find ("left") != std::string::npos) {
        this->m_pos.x += scaledSize.x / 2.0f;
        this->m_pos.z += scaledSize.x / 2.0f;
    } else if (alignment.find ("right") != std::string::npos) {
        this->m_pos.x -= scaledSize.x / 2.0f;
        this->m_pos.z -= scaledSize.x / 2.0f;
    }

    this->m_pos.x -= scene_width / 2.0f;
    this->m_pos.y = scene_height / 2.0f - this->m_pos.y;
    this->m_pos.z -= scene_width / 2.0f;
    this->m_pos.w = scene_height / 2.0f - this->m_pos.w;

    // Update scene center for rotation
    this->m_sceneCenter = glm::vec3 (
        (this->m_pos.x + this->m_pos.z) / 2.0f,
        (this->m_pos.y + this->m_pos.w) / 2.0f,
        0.0f
    );

    // Upload updated vertices to GL
    const GLfloat sceneSpacePosition[] = {
        this->m_pos.x, this->m_pos.y, 0.0f, this->m_pos.x, this->m_pos.w, 0.0f,
        this->m_pos.z, this->m_pos.y, 0.0f, this->m_pos.z, this->m_pos.y, 0.0f,
        this->m_pos.x, this->m_pos.w, 0.0f, this->m_pos.z, this->m_pos.w, 0.0f,
    };

    glBindBuffer (GL_ARRAY_BUFFER, this->m_sceneSpacePosition);
    glBufferSubData (GL_ARRAY_BUFFER, 0, sizeof (sceneSpacePosition), sceneSpacePosition);

    this->m_positionDirty = false;
}
