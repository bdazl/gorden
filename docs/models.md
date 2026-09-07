# Models from Blender

Authored 3D models enter Roboslop as glTF binary files (`.glb`) exported
from Blender and read by `roboslop.assets.mesh` (`loadModelFile`). This
page is the export checklist and the file contract; the reasoning is the
2026-09-07 entry in [decisions](decisions.md).

## Files

- One asset per file under `apps/<app>/assets/models/<name>.glb`, with its
  Blender source `<name>.blend` beside it. Both are committed; both are
  binary in `.gitattributes`. Git LFS is not used until sizes make it
  necessary.
- Engine fixtures live in `engine/tests/assets/`. `make_fixture.py`
  regenerates `crate.glb` and `crate.blend` headlessly:
  `blender -b --python make_fixture.py -- engine/tests/assets`.
- Staging into `build/<preset>/assets/models/` is added with the first app
  model, following the `configure_file(... COPYONLY)` pattern the scenes
  use in `apps/gorden/CMakeLists.txt`.

## Export checklist (Blender 5.x, File > Export > glTF 2.0)

- **Format:** glTF Binary (`.glb`). Textures are packed into the file and
  the loader hands them back as encoded PNG/JPEG bytes.
- **Transform:** +Y Up (the default). Blender's +Z up is converted by the
  exporter; never rotate a model to compensate.
- **Units:** metres, scene unit scale 1.0. One Blender unit is one metre in
  the engine; Jolt and the scene format assume metres.
- **Apply Modifiers:** on. An unapplied modifier exports the base mesh.
- **Materials:** Principled BSDF base colour, optionally one Image Texture
  in Base Color. Roughness, metallic and normal maps export but are
  ignored today.
- **Names:** Blender object names become part names. Keep them unique and
  meaningful (`Crate`, `Door`). An object with several material slots
  exports one part per material, all with the object's name.
- **Hierarchy:** parenting is preserved. Node transforms, including
  unapplied rotation and scale, come back as per-part transforms.

Armatures, animations and morph targets export but are ignored. Cameras
and lights inside a model file are ignored. Non-triangle faces are
triangulated on load.

## What the loader returns

`ModelAsset` holds `materials` and `parts`:

- `ModelMaterial`: `name`, `baseColor`, and for textured materials exactly
  one of `texturePath` (relative to the model file) or `textureData`
  (packed image bytes, decode with the in-memory `loadTexture2D`).
- `ModelPart`: `name`, `transform` (part space to model space, column-major
  `glm::mat4`), `material` (index into `materials`), and a `MeshAsset`
  with position/normal/uv vertices and 32-bit indices; upload with the
  `std::uint32_t` overload of `makeStaticMesh`.
- `modelBounds(model)`: an `Aabb` in model space over every transformed
  part, the basis for picking and bounds-derived colliders.

Assimp inserts a `DefaultMaterial` at index 0, so authored materials start
at 1. Coordinates are right-handed, +Y up, metres. UVs keep glTF's
top-left origin, which is what bgfx and stb_image expect; the loader's
`aiProcess_FlipUVs` undoes Assimp's own import flip.

## Limits

- A scene object references a file with `"geometry": "model"` and a
  `model` path relative to the asset root (see
  [scene editing](scene-editor.md)). There is no asset table or stable
  asset id yet; the path is the identity.
- No rig extraction: the animation modules exist without glTF import.
- Only base colour is read; other PBR channels wait for a shader that uses
  them. A material with a texture draws the texture alone (`fs_scene`
  samples the albedo only), so the base colour matters just for untextured
  materials.
