# Generates the engine test fixture: a crate with two materials, a textured
# child plane with an unapplied rotation/scale, and an icosphere with a
# modifier that must be applied on export. Blender 5.2, run with:
#   blender -b --python make_fixture.py -- <output-dir>
import bpy, os, sys, math
out = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)
img = bpy.data.images.new("checker", 4, 4, alpha=False)
img.generated_type = 'COLOR_GRID'
img.pack()

def material(name, rgb, textured=False):
    m = bpy.data.materials.new(name); m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*rgb, 1)
    if textured:
        tex = m.node_tree.nodes.new("ShaderNodeTexImage"); tex.image = img
        m.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return m

red = material("Red", (0.8, 0.1, 0.1)); blue = material("Blue", (0.1, 0.2, 0.9))
checker = material("Checker", (1, 1, 1), textured=True)

bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 0.5))
crate = bpy.context.object; crate.name = "Crate"; crate.scale = (2, 2, 1)
crate.data.materials.append(red); crate.data.materials.append(blue)
for poly in crate.data.polygons:
    poly.material_index = 1 if poly.normal.z > 0.5 else 0

bpy.ops.mesh.primitive_plane_add(size=1, location=(0, 0, 2.5))
sign = bpy.context.object; sign.name = "Sign"; sign.rotation_euler = (math.radians(90), 0, 0)
sign.scale = (2, 1, 1); sign.data.materials.append(checker)
sign.parent = crate; sign.matrix_parent_inverse = crate.matrix_world.inverted()

bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.5, location=(3, 0, 0.5))
ball = bpy.context.object; ball.name = "Ball"; ball.data.materials.append(blue)
ball.modifiers.new("Subsurf", 'SUBSURF').levels = 1

bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, "crate.blend"), compress=True)
bpy.ops.export_scene.gltf(filepath=os.path.join(out, "crate.glb"), export_format='GLB',
                          export_apply=True, export_yup=True)
