# Imports Thornwake's own art into the project (run with Tools/ImportAssets.cmd; safe to run again).
# The source is read, never written. Its folder comes from the environment variable WW_THORNWAKE.
#   /Game/Terrain/T_*           rock textures (pixel art: nearest filtering, as in Thornwake)
#   /Game/Trees/<Species>_L0    full tree model      (TREE_<Species>.glb)
#   /Game/Trees/<Species>_L2    distant tree model   (TREE_<Species>_LOD2.glb)
# See ASSET_MANIFEST.md for provenance.
import os
import unreal

SOURCE = os.environ.get('WW_THORNWAKE', '')
VEGETATION = os.path.join(SOURCE, 'Assets', 'Vegetation')
SPECIES = ['Pine_A', 'Pine_B', 'Pine_C', 'Spruce_A', 'Spruce_B', 'Oak_A', 'Oak_B', 'Oak_C', 'Birch_Green_A', 'Birch_Green_B',
           'Birch_Green_C', 'Maple_Autumn_A', 'Poplar_A', 'Willow_A', 'Sparse_A', 'Birch_Autumn_A', 'Birch_Autumn_B']
ROCKS = ['T_Cliff_Rock_128', 'T_Rock_Granite_128', 'T_Rock_Basalt_128', 'T_Rock_Sandstone_128', 'T_Rock_Mossy_128', 'T_Rock_Icy_128']

tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def task(filename, destination):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = destination
    t.automated = True
    t.replace_existing = True
    t.save = True
    return t


def pixel_art(folder, group):
    """Nearest filtering on every texture under a folder (Thornwake's art is pixel art)."""
    for path in eal.list_assets(folder, recursive=True, include_folder=False):
        asset = eal.load_asset(path)
        if isinstance(asset, unreal.Texture2D):
            asset.set_editor_property('filter', unreal.TextureFilter.TF_NEAREST)
            asset.set_editor_property('lod_group', group)
            eal.save_loaded_asset(asset)


if not os.path.isdir(VEGETATION):
    raise RuntimeError('Set WW_THORNWAKE to the Thornwake project folder (not found: %s)' % VEGETATION)

tasks = [task(os.path.join(VEGETATION, 'Placeholder', name + '.png'), '/Game/Terrain') for name in ROCKS]
for species in SPECIES:
    tasks.append(task(os.path.join(VEGETATION, 'TREE_%s.glb' % species), '/Game/Trees/%s_L0' % species))
    tasks.append(task(os.path.join(VEGETATION, 'TREE_%s_LOD2.glb' % species), '/Game/Trees/%s_L2' % species))
missing = [t.filename for t in tasks if not os.path.isfile(t.filename)]
if missing:
    raise RuntimeError('Missing source files: %s' % ', '.join(missing))
if os.environ.get('WW_SETTINGS_ONLY', '') != '1':  # (1: only re-apply the texture settings to what is imported)
    tools.import_asset_tasks(tasks)
# Rock tiles over whole mountainsides: it needs mipmaps (nearest-filtered, as in Thornwake) or it shimmers far away.
pixel_art('/Game/Terrain', unreal.TextureGroup.TEXTUREGROUP_WORLD)
pixel_art('/Game/Trees', unreal.TextureGroup.TEXTUREGROUP_PIXELS2D)
meshes = [p for p in eal.list_assets('/Game/Trees', recursive=True, include_folder=False) if isinstance(eal.load_asset(p), unreal.StaticMesh)]
unreal.log('WwAssets: imported %d rock textures and %d tree meshes' % (len(ROCKS), len(meshes)))
for path in meshes:
    unreal.log('WwAssets: mesh ' + path)

# The trees are drawn as instances. The importer hangs their materials on the engine's glTF materials, which are not
# compiled for instancing (the default grey would show in the game), so they move to two plain parents of our own:
# texture colour, rough, nearest-filtered by the texture's own setting; the masked one is two-sided with alpha cut-out.
mel = unreal.MaterialEditingLibrary
TEXTURE = 'BaseColorTexture'


def tree_parent(name, masked):
    full = '/Game/Trees/' + name
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    mat = tools.create_asset(name, '/Game/Trees', unreal.Material, unreal.MaterialFactoryNew())
    sample = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    sample.set_editor_property('parameter_name', TEXTURE)
    mel.connect_material_property(sample, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 300)
    rough.set_editor_property('r', 0.9)
    mel.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    spec = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 400)
    spec.set_editor_property('r', 0.2)
    mel.connect_material_property(spec, '', unreal.MaterialProperty.MP_SPECULAR)
    if masked:
        mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        mat.set_editor_property('two_sided', True)
        mel.connect_material_property(sample, 'A', unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


opaque = tree_parent('M_WwTreeOpaque', False)
masked = tree_parent('M_WwTreeMasked', True)
count = 0
for path in eal.list_assets('/Game/Trees', recursive=True, include_folder=False):
    asset = eal.load_asset(path)
    if not isinstance(asset, unreal.MaterialInstanceConstant):
        continue
    parent = asset.get_editor_property('parent')
    parent_name = parent.get_name() if parent else ''
    if parent_name.startswith('M_WwTree'):
        continue  # (already moved by an earlier run)
    texture = None
    for name in mel.get_texture_parameter_names(asset):
        if str(name) == TEXTURE:
            texture = mel.get_material_instance_texture_parameter_value(asset, name)
    if texture is None:
        unreal.log_warning('WwAssets: %s has no %s (parameters: %s)' % (path, TEXTURE, [str(n) for n in mel.get_texture_parameter_names(asset)]))
        continue
    mel.set_material_instance_parent(asset, masked if 'Mask' in parent_name or 'Blend' in parent_name else opaque)
    mel.set_material_instance_texture_parameter_value(asset, TEXTURE, texture)
    mel.update_material_instance(asset)
    eal.save_loaded_asset(asset)
    count += 1
unreal.log('WwAssets: %d tree materials moved to M_WwTreeOpaque / M_WwTreeMasked' % count)

# Noor's ground textures (prepared by Tools/PrepareGround.py: 256 px layers in the shader's order) as one texture
# array, /Game/Terrain/TA_WwGround. The shader works on the stored display values (no sRGB decoding, as in
# Thornwake), nearest-filtered with mipmaps, uncompressed (block compression smears pixel art).
GROUND = os.path.join(unreal.Paths.project_saved_dir(), 'WwAssets', 'Ground')
layers = sorted(f for f in os.listdir(GROUND) if f.endswith('.png')) if os.path.isdir(GROUND) else []
if len(layers) != 16:
    raise RuntimeError('Ground layers missing in %s: run Tools/PrepareGround.py (Tools/ImportAssets.cmd does)' % GROUND)
tools.import_asset_tasks([task(os.path.join(GROUND, f), '/Game/Terrain/Ground') for f in layers])
sources = []
for f in layers:
    texture = eal.load_asset('/Game/Terrain/Ground/' + f[:-4])
    texture.set_editor_property('srgb', False)
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    texture.set_editor_property('filter', unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
    eal.save_loaded_asset(texture)
    sources.append(texture)


def existing_or_new(name, folder, kind, factory):
    """An asset other assets point at is updated in place (deleting it fails while it is referenced)."""
    asset = eal.load_asset(folder + '/' + name) if eal.does_asset_exist(folder + '/' + name) else None
    return asset if asset is not None else tools.create_asset(name, folder, kind, factory)


array = existing_or_new('TA_WwGround', '/Game/Terrain', unreal.Texture2DArray, unreal.Texture2DArrayFactory())
array.set_editor_property('source_textures', sources)
array.set_editor_property('srgb', False)
array.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
array.set_editor_property('filter', unreal.TextureFilter.TF_NEAREST)
array.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
eal.save_loaded_asset(array)
unreal.log('WwAssets: ground array with %d layers' % len(array.get_editor_property('source_textures')))

# Grass and small plants (Thornwake Assets/Flora): 28 pixel-art sprites as one texture array, the two sprite meshes
# of Flora.cs, and the material after Flora.gdshader (cut-out, two-sided, a quarter desaturated, darkened to the
# ground's level; per plant the sprite layer and a colour). Not ported yet: wind, the underwater sway, leaf translucency.
PLANTS = os.path.join(unreal.Paths.project_saved_dir(), 'WwAssets', 'Plants')
sprites = sorted(f for f in os.listdir(PLANTS) if f.endswith('.png')) if os.path.isdir(PLANTS) else []
if len(sprites) != 28:
    raise RuntimeError('Plant sprites missing in %s: run Tools/PrepareGround.py (Tools/ImportAssets.cmd does)' % PLANTS)
tools.import_asset_tasks([task(os.path.join(PLANTS, f), '/Game/Plants/Sprites') for f in sprites])
sources = []
for f in sprites:
    texture = eal.load_asset('/Game/Plants/Sprites/' + f[:-4])
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)  # uncompressed, sRGB
    texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
    eal.save_loaded_asset(texture)
    sources.append(texture)
plants = existing_or_new('TA_WwPlants', '/Game/Plants', unreal.Texture2DArray, unreal.Texture2DArrayFactory())
plants.set_editor_property('source_textures', sources)
plants.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
plants.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
plants.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
plants.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
eal.save_loaded_asset(plants)

mat = existing_or_new('M_WwPlant', '/Game/Plants', unreal.Material, unreal.MaterialFactoryNew())
mel.delete_all_material_expressions(mat)
mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
mat.set_editor_property('two_sided', True)
mat.set_editor_property('opacity_mask_clip_value', 0.5)


def node(kind, x, y, **props):
    e = mel.create_material_expression(mat, kind, x, y)
    for key, value in props.items():
        e.set_editor_property(key, value)
    return e


# (per-instance values are read in the vertex stage and carried to the pixels)
layer = node(unreal.MaterialExpressionVertexInterpolator, -900, 200)
mel.connect_material_expressions(node(unreal.MaterialExpressionPerInstanceCustomData, -1150, 200, data_index=0), '', layer, '')
tint = node(unreal.MaterialExpressionVertexInterpolator, -900, 400)
mel.connect_material_expressions(node(unreal.MaterialExpressionPerInstanceCustomData3Vector, -1150, 400, data_index=1,
                                      const_default_value=unreal.LinearColor(1, 1, 1, 1)), '', tint, '')
uvw = node(unreal.MaterialExpressionAppendVector, -700, 0)
mel.connect_material_expressions(node(unreal.MaterialExpressionTextureCoordinate, -900, 0), '', uvw, 'A')
mel.connect_material_expressions(layer, '', uvw, 'B')
sample = node(unreal.MaterialExpressionTextureSample, -500, 0, texture=plants, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
mel.connect_material_expressions(uvw, '', sample, 'UVs')
grey = node(unreal.MaterialExpressionDesaturation, -250, 0, luminance_factors=unreal.LinearColor(0.3, 0.59, 0.11, 0))
mel.connect_material_expressions(sample, 'RGB', grey, '')
quarter = node(unreal.MaterialExpressionConstant, -450, 250, r=0.25)
mel.connect_material_expressions(quarter, '', grey, 'Fraction')
tinted = node(unreal.MaterialExpressionMultiply, -50, 100)
mel.connect_material_expressions(grey, '', tinted, 'A')
mel.connect_material_expressions(tint, '', tinted, 'B')
level = node(unreal.MaterialExpressionMultiply, 150, 100, const_b=0.64)
mel.connect_material_expressions(tinted, '', level, 'A')
mel.connect_material_property(level, '', unreal.MaterialProperty.MP_BASE_COLOR)
mel.connect_material_property(sample, 'A', unreal.MaterialProperty.MP_OPACITY_MASK)
mel.connect_material_property(node(unreal.MaterialExpressionConstant, 150, 300, r=1.0), '', unreal.MaterialProperty.MP_ROUGHNESS)
mel.connect_material_property(node(unreal.MaterialExpressionConstant, 150, 400, r=0.0), '', unreal.MaterialProperty.MP_SPECULAR)
# Both faces of a sprite light the same way, by the mesh's upward-leaning normal (the engine turns the normal over on
# back faces, which made half the tufts black; multiplying by the face sign turns it back).
facing = node(unreal.MaterialExpressionMultiply, 150, 550)
mel.connect_material_expressions(node(unreal.MaterialExpressionConstant3Vector, -100, 550, constant=unreal.LinearColor(0, 0, 1, 0)), '', facing, 'A')
mel.connect_material_expressions(node(unreal.MaterialExpressionTwoSidedSign, -100, 700), '', facing, 'B')
mel.connect_material_property(facing, '', unreal.MaterialProperty.MP_NORMAL)
mel.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
mel.recompile_material(mat)
eal.save_loaded_asset(mat)

tools.import_asset_tasks([task(os.path.join(PLANTS, name + '.gltf'), '/Game/Plants/Import') for name in ('SM_WwPlantUpright', 'SM_WwPlantFlat')])
found = 0
for path in eal.list_assets('/Game/Plants/Import', recursive=True, include_folder=False):
    mesh = eal.load_asset(path)
    if isinstance(mesh, unreal.StaticMesh):
        name = 'SM_WwPlantUpright' if 'Upright' in path else 'SM_WwPlantFlat'
        mesh.set_material(0, mat)
        eal.save_loaded_asset(mesh)
        if eal.does_asset_exist('/Game/Plants/' + name):
            eal.delete_asset('/Game/Plants/' + name)
        eal.rename_asset(path.split('.')[0], '/Game/Plants/' + name)
        unreal.log('WwAssets: plant mesh %s bounds %s' % (name, mesh.get_bounds().box_extent))
        found += 1
eal.delete_directory('/Game/Plants/Import')
unreal.log('WwAssets: plant array with %d sprites, %d plant meshes' % (len(plants.get_editor_property('source_textures')), found))
