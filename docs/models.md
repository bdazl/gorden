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
- `apps/gorden/CMakeLists.txt` stages each model into
  `build/<preset>/assets/models/` with `configure_file(... COPYONLY)`. Unlike
  the seeded scene file, models are always overwritten: they are authored in
  Blender, never in the build tree. Add a line per new model.

## First player asset

`apps/gorden/assets/models/player.blend` contains the first stylised human
player: blue work overalls, orange safety bands, gloves and reinforced boots.
The active `Player Studio` scene keeps the editable parts in `Player Asset`
under the `Player` root, with a separate studio collection for the portrait
camera, lights and floor. The original Blender scene is preserved separately.

`player.glb` exports only the asset collection's objects, with modifiers
applied: 2,236 triangles across 57 mesh parts, using solid base colours.
Select the `Player` root and all its children and enable **Selected Objects**
when re-exporting; exclude the studio. There is no rig or animation.

The model is 1.8 m tall, 0.735 m wide and 0.3815 m deep. Its origin is on
the floor between the feet; forward is Blender -Y, exported as glTF +Z.
The player controller is centred on its 1.8 m capsule. `gorden.player_visual`
applies a local vertical offset of -0.9 m and a half turn around Y to face
gameplay -Z at unit scale. Gloves extend slightly beyond the capsule's
0.7 m width.

CMake stages the asset into the build's model directory for the editor and
the running player. The player borrows a `ModelInstance` from
`SceneRuntime::instantiateModel`; the runtime caches its GPU resources
across `clear`/`replace`, so scene reloads do not invalidate the actor. The
instance must not outlive that runtime.

## First Gorden asset

`apps/gorden/assets/models/gorden.blend` contains a two-wheel service robot
with small grippers, a light grey shell, petrol blue panels, orange details
and cyan eyes on a dark face panel. The palette matches the player.
The active `Gorden Studio` scene separates `Gorden Asset` (the `Gorden`
root and its mesh children) from the portrait studio. Earlier scenes remain
in the source file; only the robot belongs in its export.

`gorden.glb` has 5,220 triangles across 61 mesh parts with eight solid
base-colour materials. It is 1.0115 m tall, 0.806 m wide and 0.4915 m deep.
The origin is on the ground between the wheels; forward is Blender -Y,
exported as glTF +Z. Eyes use base colour, so they remain visible with the
current material pipeline without requiring emissive support. There is no
rig, wheel rotation or arm animation.

For re-export, select the `Gorden` root and all its children, enable
**Selected Objects** and **Apply Modifiers**, and exclude the studio.
CMake stages the asset for editor use. The runtime robot still uses the
checkerboard cube; attaching the model is a separate integration step.

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
