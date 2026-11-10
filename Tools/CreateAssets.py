# Creates the project's code-defined assets (run with Tools/CreateAssets.cmd; safe to run again).
#   M_WwTerrain       - the ground: a port of Thornwake's classic ground shader (Tools/WwTerrainGround.hlsl)
#   M_WwWater         - translucent lit water coloured by vertex colour (sea, lake, river, pond tints)
#   T_WwTileDefault   - a small linear texture: the default of the per-tile ground-colour texture
# Run Tools/ImportAssets.cmd first (the ground uses the rock texture it imports).
import os
import struct
import zlib
import unreal

PATH = '/Game/Materials'
HERE = os.path.dirname(os.path.abspath(__file__))
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def constant(mat, value, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, x, y)
    node.set_editor_property('r', value)
    return node


def make(name, build):
    full = PATH + '/' + name
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    mat = tools.create_asset(name, PATH, unreal.Material, unreal.MaterialFactoryNew())
    build(mat)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    unreal.log('WwAssets: created ' + full)


def default_tile_texture():
    """A 4x4 mid-green linear texture (grass + no snow), written as a PNG and imported."""
    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xffffffff)
    row = b'\x00' + bytes([110, 120, 80, 0]) * 4
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 4, 4, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(row * 4)) + chunk(b'IEND', b'')
    folder = os.path.join(unreal.Paths.project_saved_dir(), 'WwAssets')
    os.makedirs(folder, exist_ok=True)
    filename = os.path.join(folder, 'T_WwTileDefault.png')
    with open(filename, 'wb') as f:
        f.write(png)
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = PATH
    task.automated = True
    task.replace_existing = True
    task.save = True
    tools.import_asset_tasks([task])
    texture = eal.load_asset(PATH + '/T_WwTileDefault')
    texture.set_editor_property('srgb', False)
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    eal.save_loaded_asset(texture)
    return texture


def terrain(mat):
    with open(os.path.join(HERE, 'WwTerrainGround.hlsl'), 'r') as f:
        code = f.read()
    # Each ground layer's mean colour (Tools/PrepareGround.py), so a texture can be used as detail over a biome colour.
    with open(os.path.join(HERE, 'WwGroundMeans.txt'), 'r') as f:
        means = ['float3(%s)' % ', '.join(line.split()) for line in f.read().splitlines() if line.strip()]
    code = code.replace('GROUND_MEANS', ', '.join(means))
    node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 0)
    node.set_editor_property('description', 'WwGround')
    node.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    names = ['TileUv', 'Col', 'ColA', 'Ex', 'N', 'Rel', 'AbsZ', 'Tile', 'Cells', 'Sea', 'GroundTex', 'RockTex', 'NoorTex', 'Style']
    inputs = []
    for name in names:
        i = unreal.CustomInput()
        i.set_editor_property('input_name', name)
        inputs.append(i)
    node.set_editor_property('inputs', inputs)
    node.set_editor_property('code', code)

    def wire(source, output, name):
        if not mel.connect_material_expressions(source, output, node, name):
            raise RuntimeError('WwAssets: cannot connect ' + name)

    def expr(kind, y, **props):
        e = mel.create_material_expression(mat, kind, -1000, y)
        for key, value in props.items():
            e.set_editor_property(key, value)
        return e

    wire(expr(unreal.MaterialExpressionTextureCoordinate, 0, coordinate_index=1), '', 'TileUv')
    vertex_colour = expr(unreal.MaterialExpressionVertexColor, 100)
    wire(vertex_colour, '', 'Col')
    wire(vertex_colour, 'A', 'ColA')
    wire(expr(unreal.MaterialExpressionTextureCoordinate, 200, coordinate_index=0), '', 'Ex')
    wire(expr(unreal.MaterialExpressionVertexNormalWS, 300), '', 'N')
    wire(expr(unreal.MaterialExpressionWorldPosition, 400, world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE), '', 'Rel')
    height = expr(unreal.MaterialExpressionComponentMask, 500, r=False, g=False, b=True, a=False)
    mel.connect_material_expressions(expr(unreal.MaterialExpressionWorldPosition, 550), '', height, '')
    wire(height, '', 'AbsZ')
    tile = expr(unreal.MaterialExpressionVectorParameter, 650, parameter_name='Tile', default_value=unreal.LinearColor(0, 0, 64, 32))
    wire(tile, '', 'Tile')
    wire(tile, 'A', 'Cells')
    wire(expr(unreal.MaterialExpressionScalarParameter, 800, parameter_name='SeaLevel', default_value=0.0), '', 'Sea')
    wire(expr(unreal.MaterialExpressionTextureObjectParameter, 900, parameter_name='TileData', texture=default_tile_texture(),
              sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR), '', 'GroundTex')
    rock = eal.load_asset('/Game/Terrain/T_Cliff_Rock_128')
    if rock is None:
        raise RuntimeError('WwAssets: /Game/Terrain/T_Cliff_Rock_128 missing: run Tools/ImportAssets.cmd first')
    wire(expr(unreal.MaterialExpressionTextureObject, 1050, texture=rock, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR), '', 'RockTex')

    noor = eal.load_asset('/Game/Terrain/TA_WwGround')
    if noor is None:
        raise RuntimeError('WwAssets: /Game/Terrain/TA_WwGround missing: run Tools/ImportAssets.cmd first')
    wire(expr(unreal.MaterialExpressionTextureObject, 1200, texture=noor, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR), '', 'NoorTex')
    # x: ground style (0 classic colours, 1 Noor's textures: what Tom plays with), y: ground variety (bare and tint patches)
    wire(expr(unreal.MaterialExpressionVectorParameter, 1350, parameter_name='GroundStyle', default_value=unreal.LinearColor(1, 1, 0, 0)), '', 'Style')

    colour = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -150, 0)
    for key, value in (('r', True), ('g', True), ('b', True), ('a', False)):
        colour.set_editor_property(key, value)
    mel.connect_material_expressions(node, '', colour, '')
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -150, 150)
    for key, value in (('r', False), ('g', False), ('b', False), ('a', True)):
        rough.set_editor_property(key, value)
    mel.connect_material_expressions(node, '', rough, '')
    mel.connect_material_property(colour, '', unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(constant(mat, 0.3, -150, 300), '', unreal.MaterialProperty.MP_SPECULAR)


def water(mat):
    """The water surface (Tools/WwWater.hlsl): a first port of Thornwake's inland water."""
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    mat.set_editor_property('tangent_space_normal', False)
    with open(os.path.join(HERE, 'WwWater.hlsl'), 'r') as f:
        code = f.read()
    node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 0)
    node.set_editor_property('description', 'WwWater')
    node.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    names = ['TileUv', 'Col', 'ColA', 'Flow', 'Rel', 'Tile', 'Cells', 'T', 'Behind', 'Patches', 'Streaks']
    inputs = []
    for name in names:
        i = unreal.CustomInput()
        i.set_editor_property('input_name', name)
        inputs.append(i)
    node.set_editor_property('inputs', inputs)
    outputs = []
    for name, kind in (('Emissive', unreal.CustomMaterialOutputType.CMOT_FLOAT3), ('WorldNormal', unreal.CustomMaterialOutputType.CMOT_FLOAT3),
                       ('Surface', unreal.CustomMaterialOutputType.CMOT_FLOAT3)):
        o = unreal.CustomOutput()
        o.set_editor_property('output_name', name)
        o.set_editor_property('output_type', kind)
        outputs.append(o)
    node.set_editor_property('additional_outputs', outputs)
    node.set_editor_property('code', code)

    def wire(source, output, name):
        if not mel.connect_material_expressions(source, output, node, name):
            raise RuntimeError('WwAssets: cannot connect ' + name)

    def expr(kind, y, **props):
        e = mel.create_material_expression(mat, kind, -1000, y)
        for key, value in props.items():
            e.set_editor_property(key, value)
        return e

    wire(expr(unreal.MaterialExpressionTextureCoordinate, 0, coordinate_index=1), '', 'TileUv')
    colour = expr(unreal.MaterialExpressionVertexColor, 100)
    wire(colour, '', 'Col')
    wire(colour, 'A', 'ColA')
    wire(expr(unreal.MaterialExpressionTextureCoordinate, 200, coordinate_index=0), '', 'Flow')
    wire(expr(unreal.MaterialExpressionWorldPosition, 300, world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE), '', 'Rel')
    tile = expr(unreal.MaterialExpressionVectorParameter, 400, parameter_name='Tile', default_value=unreal.LinearColor(0, 0, 64, 32))
    wire(tile, '', 'Tile')
    wire(tile, 'A', 'Cells')
    wire(expr(unreal.MaterialExpressionTime, 550), '', 'T')
    # (the node makes the engine provide the scene behind the water; the shader reads it at its own, refracted place)
    wire(expr(unreal.MaterialExpressionSceneColor, 650), '', 'Behind')
    for y, name, asset in ((750, 'Patches', 'river_foam_broken_patches_tiled'), (900, 'Streaks', 'river_foam_flowing_streaks_tiled')):
        texture = eal.load_asset('/Game/Water/' + asset)
        if texture is None:
            raise RuntimeError('WwAssets: /Game/Water/%s missing: run Tools/ImportAssets.cmd first' % asset)
        wire(expr(unreal.MaterialExpressionTextureObject, y, texture=texture, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE), '', name)

    def part(output, r, g, b, y):
        mask = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -150, y)
        for key, value in (('r', r), ('g', g), ('b', b), ('a', False)):
            mask.set_editor_property(key, value)
        mel.connect_material_expressions(node, output, mask, '')
        return mask

    mel.connect_material_property(node, '', unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(node, 'Emissive', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(node, 'WorldNormal', unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(part('Surface', True, False, False, 300), '', unreal.MaterialProperty.MP_OPACITY)
    mel.connect_material_property(part('Surface', False, True, False, 400), '', unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(part('Surface', False, False, True, 500), '', unreal.MaterialProperty.MP_SPECULAR)


def waterfall(foam_disc):
    """Falling water (Tools/WwWaterfall.hlsl): the curtain, or the white water on the pool at its foot."""
    def build(mat):
        mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
        mat.set_editor_property('two_sided', True)
        with open(os.path.join(HERE, 'WwWaterfall.hlsl'), 'r') as f:
            code = f.read().replace('#if FOAM_DISC', '#if %d' % (1 if foam_disc else 0))
        node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 0)
        node.set_editor_property('description', 'WwWaterfall')
        node.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
        inputs = []
        for name in ['Uv', 'Travel', 'Col', 'ColA', 'T']:
            i = unreal.CustomInput()
            i.set_editor_property('input_name', name)
            inputs.append(i)
        node.set_editor_property('inputs', inputs)
        rough = unreal.CustomOutput()
        rough.set_editor_property('output_name', 'Rough')
        rough.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        node.set_editor_property('additional_outputs', [rough])
        node.set_editor_property('code', code)

        def expr(kind, y, **props):
            e = mel.create_material_expression(mat, kind, -1000, y)
            for key, value in props.items():
                e.set_editor_property(key, value)
            return e

        mel.connect_material_expressions(expr(unreal.MaterialExpressionTextureCoordinate, 0, coordinate_index=0), '', node, 'Uv')
        travel = expr(unreal.MaterialExpressionComponentMask, 100, r=True, g=False, b=False, a=False)
        mel.connect_material_expressions(expr(unreal.MaterialExpressionTextureCoordinate, 150, coordinate_index=1), '', travel, '')
        mel.connect_material_expressions(travel, '', node, 'Travel')
        colour = expr(unreal.MaterialExpressionVertexColor, 250)
        mel.connect_material_expressions(colour, '', node, 'Col')
        mel.connect_material_expressions(colour, 'A', node, 'ColA')
        mel.connect_material_expressions(expr(unreal.MaterialExpressionTime, 350), '', node, 'T')
        rgb = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -150, 0)
        alpha = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -150, 150)
        for mask, flags in ((rgb, (True, True, True, False)), (alpha, (False, False, False, True))):
            for key, value in zip(('r', 'g', 'b', 'a'), flags):
                mask.set_editor_property(key, value)
            mel.connect_material_expressions(node, '', mask, '')
        mel.connect_material_property(rgb, '', unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY)
        mel.connect_material_property(node, 'Rough', unreal.MaterialProperty.MP_ROUGHNESS)
        mel.connect_material_property(constant(mat, 0.5, -150, 300), '', unreal.MaterialProperty.MP_SPECULAR)
        # A little of its own light (Thornwake: emission 5 % and light through white water; in a cliff's shade it was a blue strip).
        glow = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -150, 450)
        glow.set_editor_property('const_b', 0.05)
        mel.connect_material_expressions(rgb, '', glow, 'A')
        mel.connect_material_property(glow, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        # Pulled toward the camera a little, more with distance: far away the terrain's coarser mesh across a steep wall
        # stands metres above the falling water and buried it.
        pull = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 700)
        pull.set_editor_property('description', 'WwWaterfallPull')
        pull.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        rel = unreal.CustomInput()
        rel.set_editor_property('input_name', 'Rel')
        pull.set_editor_property('inputs', [rel])
        pull.set_editor_property('code', 'float d = length(Rel); float bias = (%s) * 100.0; return -normalize(Rel) * min(bias, d * 0.5);'
                                 % ('0.3 + 0.004 * d / 100.0' if foam_disc else '0.6 + 0.012 * d / 100.0'))
        mel.connect_material_expressions(expr(unreal.MaterialExpressionWorldPosition, 700, world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE), '', pull, 'Rel')
        mel.connect_material_property(pull, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    return build


make('M_WwWaterfall', waterfall(False))
make('M_WwWaterfallFoam', waterfall(True))
make('M_WwTerrain', terrain)
make('M_WwWater', water)
