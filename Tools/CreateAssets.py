# Creates the project's code-defined assets (run with Tools/CreateAssets.cmd; safe to run again).
#   M_WwTerrain       - the ground: a port of Thornwake's classic ground shader (Tools/WwTerrainGround.hlsl)
#   M_WwWater         - the water: a port of Thornwake's water shader (Tools/WwWater.hlsl, WwSeaVertex.hlsl, WwSea.hlsl)
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


def with_sea(filename):
    """A Custom node's code with the shared sea methods (Tools/WwSea.hlsl) put inside its struct."""
    with open(os.path.join(HERE, filename), 'r') as f:
        code = f.read()
    with open(os.path.join(HERE, 'WwSea.hlsl'), 'r') as f:
        sea = f.read()
    if '//#include WwSea' not in code:
        raise RuntimeError('WwAssets: %s has no place for WwSea.hlsl' % filename)
    return code.replace('//#include WwSea', sea)


def water(mat):
    """The water surface (Tools/WwWater.hlsl, WwSeaVertex.hlsl, WwSea.hlsl): a port of Thornwake's water."""
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    mat.set_editor_property('tangent_space_normal', False)
    code = with_sea('WwWater.hlsl')
    cascades = ['D0', 'G0', 'D1', 'G1', 'D2', 'G2']

    def custom(description, names, x, y):
        n = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
        n.set_editor_property('description', description)
        n.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        inputs = []
        for name in names:
            i = unreal.CustomInput()
            i.set_editor_property('input_name', name)
            inputs.append(i)
        n.set_editor_property('inputs', inputs)
        return n

    node = custom('WwWater', ['TileUv', 'Col', 'ColA', 'Flow', 'Shore', 'Shore2', 'Rel', 'RestRel', 'Tile', 'Cells', 'FftX', 'FftZ', 'Debug', 'T', 'Behind',
                              'SunDir', 'SunLight', 'Patches', 'Streaks', 'Net'] + cascades, -500, 0)
    # The vertex stage: the waves move the sheet.
    vertex = custom('WwSeaVertex', ['TileUv', 'Col', 'Shore', 'Shore2', 'Rel', 'Tile', 'FftX', 'FftZ', 'T'] + cascades, -500, 900)
    vertex.set_editor_property('code', with_sea('WwSeaVertex.hlsl'))
    outputs = []
    for name, kind in (('Emissive', unreal.CustomMaterialOutputType.CMOT_FLOAT3), ('WorldNormal', unreal.CustomMaterialOutputType.CMOT_FLOAT3),
                       ('Surface', unreal.CustomMaterialOutputType.CMOT_FLOAT3)):
        o = unreal.CustomOutput()
        o.set_editor_property('output_name', name)
        o.set_editor_property('output_type', kind)
        outputs.append(o)
    node.set_editor_property('additional_outputs', outputs)
    node.set_editor_property('code', code)

    def wire(source, output, name, both=False):
        if not mel.connect_material_expressions(source, output, node, name):
            raise RuntimeError('WwAssets: cannot connect ' + name)
        if both and not mel.connect_material_expressions(source, output, vertex, name):
            raise RuntimeError('WwAssets: cannot connect ' + name + ' (vertex stage)')

    def expr(kind, y, **props):
        e = mel.create_material_expression(mat, kind, -1000, y)
        for key, value in props.items():
            e.set_editor_property(key, value)
        return e

    wire(expr(unreal.MaterialExpressionTextureCoordinate, 0, coordinate_index=1), '', 'TileUv', True)
    colour = expr(unreal.MaterialExpressionVertexColor, 100)
    wire(colour, '', 'Col', True)
    wire(colour, 'A', 'ColA')
    wire(expr(unreal.MaterialExpressionTextureCoordinate, 200, coordinate_index=0), '', 'Flow')
    # Shore data per vertex: the bed's uphill gradient; how open the coast is and the water's fetch.
    wire(expr(unreal.MaterialExpressionTextureCoordinate, 220, coordinate_index=2), '', 'Shore', True)
    wire(expr(unreal.MaterialExpressionTextureCoordinate, 260, coordinate_index=3), '', 'Shore2', True)
    wire(expr(unreal.MaterialExpressionWorldPosition, 300, world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE), '', 'Rel')
    rest = expr(unreal.MaterialExpressionWorldPosition, 350, world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE_NO_OFFSETS)
    wire(rest, '', 'RestRel')
    if not mel.connect_material_expressions(rest, '', vertex, 'Rel'):
        raise RuntimeError('WwAssets: cannot connect Rel (vertex stage)')
    tile = expr(unreal.MaterialExpressionVectorParameter, 400, parameter_name='Tile', default_value=unreal.LinearColor(0, 0, 64, 32))
    wire(tile, '', 'Tile', True)
    wire(tile, 'A', 'Cells')
    wire(expr(unreal.MaterialExpressionVectorParameter, 450, parameter_name='FftX', default_value=unreal.LinearColor(0, 0, 0, 0)), '', 'FftX', True)
    wire(expr(unreal.MaterialExpressionVectorParameter, 500, parameter_name='FftZ', default_value=unreal.LinearColor(0, 0, 0, 0)), '', 'FftZ', True)
    wire(expr(unreal.MaterialExpressionTime, 550), '', 'T', True)
    wire(expr(unreal.MaterialExpressionScalarParameter, 530, parameter_name='Debug', default_value=0.0), '', 'Debug')
    wire(expr(unreal.MaterialExpressionSkyAtmosphereLightDirection, 580), '', 'SunDir')
    wire(expr(unreal.MaterialExpressionSkyAtmosphereLightIlluminance, 610), '', 'SunLight')
    # The simulated sea's textures and the foam net are made by the game when it starts (WwOcean); the small default
    # texture stands in until then.
    default = eal.load_asset(PATH + '/T_WwTileDefault')
    if default is None:
        raise RuntimeError('WwAssets: T_WwTileDefault missing')
    for y, name, parameter in ((1100, 'Net', 'FoamNet'), (1200, 'D0', 'Disp0'), (1300, 'G0', 'Slope0'), (1400, 'D1', 'Disp1'), (1500, 'G1', 'Slope1'),
                               (1600, 'D2', 'Disp2'), (1700, 'G2', 'Slope2')):
        wire(expr(unreal.MaterialExpressionTextureObjectParameter, y, parameter_name=parameter, texture=default,
                  sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR), '', name, name != 'Net')
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
    mel.connect_material_property(vertex, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)


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
SPRAY = """
// A soft, billowing puff (port of Thornwake Game/Rewrite/Spray.gdshader, the drawn billow): a cauliflower edge that
// churns as it ages. Data = (age 0..1, seed, density). Returns the opacity.
struct FWwSpray
{
	float hash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
	float noise(float2 p)
	{
		float2 i = floor(p), f = frac(p);
		float2 w = f * f * (3.0 - 2.0 * f);
		return lerp(lerp(hash(i), hash(i + float2(1.0, 0.0)), w.x), lerp(hash(i + float2(0.0, 1.0)), hash(i + float2(1.0, 1.0)), w.x), w.y);
	}
	float fbm(float2 p) { return 0.5 * noise(p) + 0.3 * noise(p * 2.07 + 5.3) + 0.2 * noise(p * 4.13 + 11.7); }
};
FWwSpray fn;
float age = Data.x, seed = Data.y;
float2 q = Uv * 2.0 - 1.0;
float r = length(q);
float ang = atan2(q.y, q.x);
float lumps = fn.fbm(float2(ang * 1.6 + seed * 40.0, age * 1.8 + seed * 9.0)) - 0.5;
float inner = fn.fbm(q * 2.2 + float2(seed * 31.0, age * 1.3)) - 0.5;
float edge = 0.78 + 0.35 * lumps + 0.12 * inner;
float body = 1.0 - smoothstep(edge - 0.32, edge, r);
body *= 0.7 + 0.6 * (fn.fbm(q * 3.5 + seed * 17.0 + age * 0.9) - 0.2);
return saturate(body) * Data.z;
"""


def spray(mat):
    """Spray and mist puffs: lit softly from every side, never grey in shade, fading into what lies behind them."""
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    mat.set_editor_property('two_sided', True)
    node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 0)
    node.set_editor_property('description', 'WwSpray')
    node.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    inputs = []
    for name in ['Uv', 'Data']:
        i = unreal.CustomInput()
        i.set_editor_property('input_name', name)
        inputs.append(i)
    node.set_editor_property('inputs', inputs)
    node.set_editor_property('code', SPRAY)
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1000, 0)
    mel.connect_material_expressions(uv, '', node, 'Uv')
    data = mel.create_material_expression(mat, unreal.MaterialExpressionVertexInterpolator, -800, 200)
    custom = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData3Vector, -1100, 200)
    custom.set_editor_property('data_index', 0)
    mel.connect_material_expressions(custom, '', data, '')
    mel.connect_material_expressions(data, '', node, 'Data')
    fade = mel.create_material_expression(mat, unreal.MaterialExpressionDepthFade, -250, 0)
    fade.set_editor_property('fade_distance_default', 150.0)  # (1.5 m, as Thornwake's soft_distance)
    if not (mel.connect_material_expressions(node, '', fade, 'Opacity') or mel.connect_material_expressions(node, '', fade, 'InOpacity')):
        raise RuntimeError('WwAssets: cannot connect the spray opacity')
    mel.connect_material_property(fade, '', unreal.MaterialProperty.MP_OPACITY)
    tint = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -250, 200)
    tint.set_editor_property('constant', unreal.LinearColor(0.9, 0.95, 0.96, 1))
    mel.connect_material_property(tint, '', unreal.MaterialProperty.MP_BASE_COLOR)
    glow = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, 350)
    glow.set_editor_property('const_b', 0.16)
    mel.connect_material_expressions(tint, '', glow, 'A')
    mel.connect_material_property(glow, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)


make('M_WwSpray', spray)
ROCK = """
// Rocks, boulders and cliff slabs (port of Thornwake Game/Rewrite/Rock.gdshader): facets in the mesh's own stone and
// moss colours, the tileable pixel rock texture projected from three sides, tinted per rock with the biome's rock colour.
// Pos = world position wrapped (cm), N = world normal, Vc = mesh colour, Tint = (rock colour, look).
// BIG (rocks tens of metres tall: cliff blocks and formations): the texture every 19 m and, turned, every 5.9 m; Tom's
// cliff rock textures on their sides every 14 m; most of the biome's tint taken out, pale lichen in patches, dark
// streaks where water runs down.
// Not ported: rain. One difference: Thornwake picks a rock's cliff texture by where the rock stands; here by where each
// point of it is (the choice changes over hundreds of metres, so a rock is rarely cut by it).
struct FWwRock
{
	float rock_hash(float2 p) { p = p - 997.0 * floor(p / 997.0); float3 q = frac(float3(p.x, p.y, p.x) * 0.1031); q += dot(q, q.yzx + 33.33); return frac((q.x + q.y) * q.z); }
	float rock_noise(float2 p)
	{
		float2 i = floor(p), f = frac(p), u = f * f * (3.0 - 2.0 * f);
		return lerp(lerp(rock_hash(i), rock_hash(i + float2(1.0, 0.0)), u.x), lerp(rock_hash(i + float2(0.0, 1.0)), rock_hash(i + float2(1.0, 1.0)), u.x), u.y);
	}
};
FWwRock fn;
// (Thornwake's axes: x, up, z)
float3 p = float3(Pos.x, Pos.z, Pos.y) / 100.0;
float3 n = float3(N.x, N.z, N.y);
float3 w = pow(abs(n), 4.0);
w /= w.x + w.y + w.z;
float look = clamp(round(Tint.a), 0.0, 5.0);
#if BIG
const float tile = 19.0, second_tile = 5.9, cliff_tile = 14.0;
#else
const float tile = 4.0;
#endif
float3 tx = RockArr.Sample(RockArrSampler, float3(p.z / tile, -p.y / tile, look)).rgb;
float3 ty = RockArr.Sample(RockArrSampler, float3(p.x / tile, p.z / tile, look)).rgb;
float3 tz = RockArr.Sample(RockArrSampler, float3(p.x / tile, -p.y / tile, look)).rgb;
float3 t = (tx * w.x + ty * w.y + tz * w.z) / 0.5;
float3 base = Vc * Tint.rgb;
#if BIG
{
	// (turn = mat2((0.8, 0.6), (-0.6, 0.8)))
	float2 qx = float2(0.8 * p.z - 0.6 * p.y, 0.6 * p.z + 0.8 * p.y) / second_tile;
	float2 qy = float2(0.8 * p.x - 0.6 * p.z, 0.6 * p.x + 0.8 * p.z) / second_tile;
	float2 qz = float2(0.8 * p.x - 0.6 * p.y, 0.6 * p.x + 0.8 * p.y) / second_tile;
	float3 sx = RockArr.Sample(RockArrSampler, float3(qx, look)).rgb, sy = RockArr.Sample(RockArrSampler, float3(qy, look)).rgb, sz = RockArr.Sample(RockArrSampler, float3(qz, look)).rgb;
	t = lerp(t, (sx * w.x + sy * w.y + sz * w.z) / 0.5, 0.45);
	// Tom's cliff rocks on the sides: per rock look a first and a second texture, one over whole stretches of cliff.
	const int cliff_first[6] = { 0, 3, 2, 3, 4, 1 }, cliff_second[6] = { 1, 1, 4, 5, 0, 2 };
	const float cliff_means[6] = { 0.275, 0.427, 0.395, 0.514, 0.269, 0.468 }, cliff_contrast[6] = { 0.55, 0.85, 0.85, 0.85, 0.7, 0.85 };
	int lk = int(look);
	int layer = fn.rock_noise(p.xz / 260.0) > 0.58 ? cliff_second[lk] : cliff_first[lk];
	float sides = w.x + w.z;
	float3 cx = CliffArr.Sample(CliffArrSampler, float3(p.z / cliff_tile, -p.y / cliff_tile, layer)).rgb;
	float3 cz = CliffArr.Sample(CliffArrSampler, float3(p.x / cliff_tile, -p.y / cliff_tile, layer)).rgb;
	float3 cliff = (cx * w.x + cz * w.z) / max(sides, 1e-4) / cliff_means[layer];
	cliff = lerp(float3(1.0, 1.0, 1.0), cliff, cliff_contrast[layer]);
	float cliff_grey = dot(cliff, float3(0.333, 0.333, 0.333));
	cliff = lerp(float3(cliff_grey, cliff_grey, cliff_grey), cliff, 0.45); // (less colour: the sandstone at full came out a hot orange-red)
	t = lerp(t, cliff, smoothstep(0.2, 0.6, sides));
	// Less bland: most of the biome's tint taken out, a little lighter, pale lichen in patches (now and then orange),
	// dark streaks where water runs down.
	float grey = dot(base, float3(0.333, 0.333, 0.333));
	base = lerp(base, float3(grey, grey, grey), 0.85) * 1.12;
	float2 side = abs(n.x) > abs(n.z) ? p.zy : p.xy;
	float patches = fn.rock_noise(side * 0.28 + p.y * 0.05) * 0.6 + fn.rock_noise(side * 0.9 + 7.0) * 0.4;
	float lichen = smoothstep(0.6, 0.7, patches) * (0.55 + 0.45 * max(n.y, 0.0));
	float3 lichen_col = fn.rock_noise(side * 0.05 + 3.0) > 0.78 ? float3(0.78, 0.55, 0.3) : float3(0.68, 0.7, 0.6);
	base = lerp(base, lichen_col, lichen * 0.65);
	float streak = smoothstep(0.55, 0.8, fn.rock_noise(float2(side.x * 0.45, p.y * 0.012))) * (1.0 - max(n.y, 0.0));
	base *= 1.0 - 0.3 * streak;
}
#endif
return pow(saturate(base * t), 2.2);
"""


def rock(big):
    def build(mat):
        node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 0)
        node.set_editor_property('description', 'WwRock')
        node.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        inputs = []
        for name in ['Pos', 'N', 'Vc', 'Tint', 'RockArr'] + (['CliffArr'] if big else []):
            i = unreal.CustomInput()
            i.set_editor_property('input_name', name)
            inputs.append(i)
        node.set_editor_property('inputs', inputs)
        node.set_editor_property('code', ROCK.replace('#if BIG', '#if %d' % (1 if big else 0)))

        def expr(kind, x, y, **props):
            e = mel.create_material_expression(mat, kind, x, y)
            for key, value in props.items():
                e.set_editor_property(key, value)
            return e

        # (the world position wrapped every 4,096 m before it loses its precision, as Thornwake wraps it)
        wrap = expr(unreal.MaterialExpressionFmod, -900, 0)
        mel.connect_material_expressions(expr(unreal.MaterialExpressionWorldPosition, -1200, 0), '', wrap, 'A')
        mel.connect_material_expressions(expr(unreal.MaterialExpressionConstant, -1200, 150, r=409600.0), '', wrap, 'B')
        mel.connect_material_expressions(wrap, '', node, 'Pos')
        mel.connect_material_expressions(expr(unreal.MaterialExpressionVertexNormalWS, -900, 200), '', node, 'N')
        mel.connect_material_expressions(expr(unreal.MaterialExpressionVertexColor, -900, 300), '', node, 'Vc')
        tint = expr(unreal.MaterialExpressionVertexInterpolator, -900, 450)
        data = expr(unreal.MaterialExpressionAppendVector, -1100, 450)
        mel.connect_material_expressions(expr(unreal.MaterialExpressionPerInstanceCustomData3Vector, -1400, 420, data_index=0,
                                              const_default_value=unreal.LinearColor(1, 1, 1, 1)), '', data, 'A')
        mel.connect_material_expressions(expr(unreal.MaterialExpressionPerInstanceCustomData, -1400, 560, data_index=3), '', data, 'B')
        mel.connect_material_expressions(data, '', tint, '')
        mel.connect_material_expressions(tint, '', node, 'Tint')
        for y, name, asset in ((650, 'RockArr', 'TA_WwRock'),) + (((800, 'CliffArr', 'TA_WwCliff'),) if big else ()):
            array = eal.load_asset('/Game/Terrain/' + asset)
            if array is None:
                raise RuntimeError('WwAssets: /Game/Terrain/%s missing: run Tools/ImportAssets.cmd first' % asset)
            mel.connect_material_expressions(expr(unreal.MaterialExpressionTextureObject, -900, y, texture=array, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR), '', node, name)
        mel.connect_material_property(node, '', unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(constant(mat, 0.9, -150, 200), '', unreal.MaterialProperty.MP_ROUGHNESS)
        mel.connect_material_property(constant(mat, 0.3, -150, 300), '', unreal.MaterialProperty.MP_SPECULAR)
        mel.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    return build


def prop(mat):
    """Bushes, stumps and logs: the mesh's own colours (display values), matte."""
    colour = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -600, 0)
    power = mel.create_material_expression(mat, unreal.MaterialExpressionPower, -350, 0)
    power.set_editor_property('const_exponent', 2.2)
    mel.connect_material_expressions(colour, '', power, 'Base')
    mel.connect_material_property(power, '', unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(constant(mat, 0.95, -350, 200), '', unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(constant(mat, 0.2, -350, 300), '', unreal.MaterialProperty.MP_SPECULAR)
    mel.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)


make('M_WwRock', rock(False))
make('M_WwRockBig', rock(True))
make('M_WwProp', prop)
make('M_WwTerrain', terrain)
make('M_WwWater', water)
