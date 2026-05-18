module;

#include <bgfx/bgfx.h>

export module roboslop.render.material;

import roboslop.render.asset_cache;

namespace roboslop {

// Surface material for the textured forward path. Holds the program
// the draw should run with, the albedo texture handle, and the
// sampler-uniform handle the shader expects. All three are non-owning
// — the AssetCache owns Program/Texture lifetimes; the sampler
// uniform lives on RenderContext (or a dedicated uniform cache).
export struct Material {
    ProgramHandle program{};
    bgfx::TextureHandle albedo{bgfx::kInvalidHandle};
    bgfx::UniformHandle sAlbedo{bgfx::kInvalidHandle};
};

} // namespace roboslop
