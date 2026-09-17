#!/usr/bin/env python3
"""Generate the Vulkan shaders and their uniform block from the OpenGL sources.

The OpenGL GLSL in src/ae3d/shaders/module.ae is the single source of truth. The
Vulkan variants differ only in how uniforms are declared, so they are derived
rather than written twice: a second copy of a seven-hundred-line lighting model
would drift the first time either was touched.

The same declarations produce the std140 uniform block the shader reads and the
C struct the renderer fills, so the two cannot disagree about layout.
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "src/ae3d/shaders/module.ae"
OUT_DIR = Path(__file__).resolve().parent
NATIVE = ROOT / "native"


def write(path, text):
    """Generated sources are checked in, so they end lines the way the
    repository does wherever this happens to be run."""
    with open(path, "w", newline="\n") as handle:
        handle.write(text)


def block(name):
    text = SHADERS.read_text()
    match = re.search(rf"^const {name} = <<GLSL_END\n(.*?)^GLSL_END$", text, re.S | re.M)
    if not match:
        raise SystemExit(f"generate: {name} not found in {SHADERS}")
    return resolve_bones(match.group(1))


def resolve_bones(text):
    """The bone array is sized by a #define, and everything downstream reads
    array sizes as literals: the uniform block generated from them, the C
    offsets generated beside it, and the regex that lifts a uniform into the
    block. The number is taken from the GLSL rather than repeated here, so the
    shader stays the one place it is written down."""
    match = re.search(r"^#define MAX_BONES (\d+)$", text, re.M)
    if not match:
        return text
    text = re.sub(r"^#define MAX_BONES \d+\n", "", text, flags=re.M)
    return text.replace("MAX_BONES", match.group(1))


SCALARS = {"float": (4, 4), "int": (4, 4), "bool": (4, 4)}
VECTORS = {"vec2": (8, 8), "vec3": (16, 12), "vec4": (16, 16), "mat4": (16, 64)}


def light_bytes(light_members):
    """How many bytes the light array occupies at the head of the block."""
    if not light_members:
        return 0
    return light_layout(light_members)[1] * MAX_LIGHTS


ARRAY_STRIDE = 16


def std140(members, base=0):
    """Assign std140 offsets, returning (name, type, offset) and the total size.

    A member may carry a count. An array's element stride is the element rounded
    up to 16, so a run of floats or vec3s costs 16 bytes each -- which is why the
    wave tables cannot be written as a flat run of floats -- and a run of mat4s
    costs the 64 each of them already is.
    """
    offset = base
    placed = []
    for member in members:
        kind, name = member[0], member[1]
        count = member[2] if len(member) > 2 else 0
        align, size = SCALARS.get(kind) or VECTORS[kind]
        if count:
            align = ARRAY_STRIDE
            size = max(ARRAY_STRIDE, (size + 15) // 16 * 16) * count
        offset = (offset + align - 1) // align * align
        placed.append((name, kind, offset))
        offset += size
    return placed, (offset + 15) // 16 * 16


MAX_LIGHTS = 4


def light_struct(source):
    """The Light struct's members, in declaration order, or None."""
    found = re.search(r"struct Light \{(.*?)\};", source, re.S)
    if not found:
        return None
    return re.findall(r"\b(vec3|vec4|vec2|float|int|bool)\s+(\w+)\s*;", found.group(1))


def light_layout(members):
    """std140 offsets within one Light, and the array stride.

    An array element's stride is the struct's size rounded up to 16, which is
    what makes the C side able to step through the array with one multiply.
    """
    placed, size = std140(members)
    stride = (size + 15) // 16 * 16
    return placed, stride


def collect_uniforms(source):
    """Every non-sampler uniform, arrays included. The light array is separate."""
    members = []
    pattern = r"^uniform\s+(vec3|vec4|vec2|float|int|bool|mat4)\s+(\w+)\s*(\[(\d+)\])?\s*;"
    for kind, name, _, count in re.findall(pattern, source, re.M):
        members.append((kind, name, int(count)) if count else (kind, name))
    return members


def to_vulkan(source, stage, members, samplers, varyings_in, varyings_out,
              light_members=None):
    """Rewrite an OpenGL shader for Vulkan.

    The uniforms move into an UNNAMED block, so its members keep their original
    names in global scope and every reference in the body still resolves. Naming
    the block would mean rewriting each reference, and the lighting model has
    function parameters that share a uniform's name, so that rewrite cannot be
    done without scope analysis.
    """
    text = source
    text = re.sub(r"^#version\s+\d+\s+core", "#version 450", text, flags=re.M)

    # The struct and its array are re-emitted inside the block, so the source's
    # own declarations go.
    text = re.sub(r"#define MAX_LIGHTS \d+\n", "", text)
    text = re.sub(r"struct Light \{.*?\};\n", "", text, flags=re.S)
    text = re.sub(r"^uniform Light lights\[MAX_LIGHTS\];\n", "", text, flags=re.M)
    text = text.replace("MAX_LIGHTS", str(MAX_LIGHTS))

    # OpenGL clip depth spans -1..1 and needs the half-scale; Vulkan clip depth
    # is already the 0..1 the shadow map stores.
    text = text.replace("return clipZ * 0.5 + 0.5;", "return clipZ;")
    text = text.replace("return depth * 2.0 - 1.0;", "return depth;")

    text = re.sub(r"^uniform\s+(vec3|vec4|vec2|float|int|bool|mat4)\s+\w+\s*(\[\d+\])?\s*;.*$", "", text, flags=re.M)
    text = re.sub(r"^uniform\s+sampler2D\s+\w+\s*;.*$", "", text, flags=re.M)

    placed, size = std140(members, base=light_bytes(light_members))
    lines = []
    if light_members:
        # The array leads the block, so its offset is zero and the C side steps
        # through it with a stride rather than a table of offsets.
        lines.append("struct Light {")
        for kind, name in light_members:
            lines.append(f"    {kind} {name};")
        lines.append("};")
        lines.append("")
    lines.append("layout(std140, set = 0, binding = 0) uniform SceneBlock {")
    if light_members:
        lines.append(f"    Light lights[{MAX_LIGHTS}];")
    counts = {member[1]: (member[2] if len(member) > 2 else 0) for member in members}
    for name, kind, _ in placed:
        count = counts.get(name, 0)
        lines.append(f"    {kind} {name}[{count}];" if count else f"    {kind} {name};")
    lines.append("};")

    # Every shader shares one descriptor set layout, so whichever sampler a
    # shader names sits at binding 1 and the renderer binds the right texture.
    for index, sampler in enumerate(samplers):
        lines.append(f"layout(set = 0, binding = {index + 1}) uniform sampler2D {sampler};")

    for index, (kind, name) in enumerate(varyings_out):
        text = re.sub(rf"^out\s+{kind}\s+{name}\s*;.*$",
                      f"layout(location = {index}) out {kind} {name};", text, flags=re.M)
    for index, (kind, name) in enumerate(varyings_in):
        text = re.sub(rf"^in\s+{kind}\s+{name}\s*;.*$",
                      f"layout(location = {index}) in {kind} {name};", text, flags=re.M)

    # The fragment output is the one varying the source leaves unnumbered.
    if stage == "frag":
        text = re.sub(r"^out\s+vec4\s+(\w+)\s*;",
                      r"layout(location = 0) out vec4 \1;", text, flags=re.M)

    text = text.replace("#version 450", "#version 450\n\n" + "\n".join(lines), 1)
    return text, placed, size


def c_struct(placed, size, light_placed=None, light_stride=0):
    lines = [
        "/* Generated by native/shaders/generate.py - do not edit. */",
        "#ifndef AE3D_VK_UNIFORMS_H",
        "#define AE3D_VK_UNIFORMS_H",
        "",
        "#include <string.h>",
        "",
        f"#define AE3D_VK_SCENE_SIZE {size}",
        "",
        "/* std140 layout, offsets computed from the same declarations the shader",
        "   block is generated from, so the two cannot disagree. */",
        "typedef struct {",
        f"    unsigned char bytes[{size}];",
        "} ae3d_vk_scene;",
        "",
    ]
    for name, kind, offset in placed:
        lines.append(f"#define AE3D_VK_OFF_{name.upper()} {offset}")
    if light_placed:
        lines.append("")
        lines.append(f"#define AE3D_VK_MAX_LIGHTS {MAX_LIGHTS}")
        lines.append(f"#define AE3D_VK_LIGHT_STRIDE {light_stride}")
        for name, kind, offset in light_placed:
            lines.append(f"#define AE3D_VK_LIGHT_{name.upper()} {offset}")
    # A model carries uniforms by name, so the renderer needs the reverse of the
    # table above. Sorted, so the lookup is a binary search rather than a walk.
    lines += [
        "",
        "typedef struct {",
        "    const char *name;",
        "    int offset;",
        "} ae3d_vk_uniform_slot;",
        "",
        "static const ae3d_vk_uniform_slot ae3d_vk_uniform_slots[] = {",
    ]
    for name, kind, offset in sorted(placed, key=lambda entry: entry[0]):
        lines.append(f'    {{ "{name}", {offset} }},')
    lines += [
        "};",
        "",
        f"#define AE3D_VK_UNIFORM_SLOT_COUNT {len(placed)}",
        "",
        "static inline int ae3d_vk_uniform_offset(const char *name) {",
        "    int low = 0;",
        "    int high = AE3D_VK_UNIFORM_SLOT_COUNT - 1;",
        "    if (!name) return -1;",
        "    while (low <= high) {",
        "        int mid = (low + high) / 2;",
        "        int order = strcmp(name, ae3d_vk_uniform_slots[mid].name);",
        "        if (order == 0) return ae3d_vk_uniform_slots[mid].offset;",
        "        if (order < 0) high = mid - 1; else low = mid + 1;",
        "    }",
        "    return -1;",
        "}",
    ]
    lines += [
        "",
        "static inline void ae3d_vk_set_float(ae3d_vk_scene *s, int offset, float v) {",
        "    memcpy(s->bytes + offset, &v, sizeof(v));",
        "}",
        "static inline void ae3d_vk_set_int(ae3d_vk_scene *s, int offset, int v) {",
        "    memcpy(s->bytes + offset, &v, sizeof(v));",
        "}",
        "static inline void ae3d_vk_set_vec3(ae3d_vk_scene *s, int offset, double x, double y, double z) {",
        "    float v[3];",
        "    v[0] = (float)x; v[1] = (float)y; v[2] = (float)z;",
        "    memcpy(s->bytes + offset, v, sizeof(v));",
        "}",
        "static inline void ae3d_vk_set_mat4(ae3d_vk_scene *s, int offset, const double *m) {",
        "    float v[16];",
        "    int i;",
        "    for (i = 0; i < 16; i++) v[i] = (float)m[i];",
        "    memcpy(s->bytes + offset, v, sizeof(v));",
        "}",
        "",
        "#endif",
    ]
    return "\n".join(lines) + "\n"


def verify_offsets(path, placed):
    """Check the computed std140 offsets against the ones glslang assigns.

    The C side writes at these offsets and the shader reads at glslang's, so a
    disagreement is a silent corruption of every uniform after the first bad
    one. Asking the compiler is cheap; assuming is not.
    """
    result = subprocess.run(["glslangValidator", "-V", "-q", "-o", str(path) + ".reflect",
                             str(path)], capture_output=True, text=True)
    Path(str(path) + ".reflect").unlink(missing_ok=True)
    reported = {}
    for line in result.stdout.splitlines():
        match = re.match(r"^(\w+): offset (-?\d+), type \w+, size \d+, index 0,", line)
        if match and int(match.group(2)) >= 0:
            reported[match.group(1)] = int(match.group(2))

    expected = {name: offset for name, _, offset in placed}
    mismatched = [(n, expected[n], reported[n]) for n in reported
                  if n in expected and expected[n] != reported[n]]
    if mismatched:
        for name, ours, theirs in mismatched:
            sys.stderr.write(f"generate: {name} at {ours}, glslang says {theirs}\n")
        raise SystemExit("generate: std140 layout disagrees with the compiler")
    return len(reported)


def aether_offsets(placed, size, light_placed=None, light_stride=0):
    """The same offsets as an Aether module, so the renderer names uniforms."""
    names = [f"OFF_{name.upper()}" for name, _, _ in placed]
    if light_placed:
        names += ["MAX_LIGHTS", "LIGHT_STRIDE"]
        names += [f"LIGHT_{name.upper()}" for name, _, _ in light_placed]
    lines = [
        "// Generated by native/shaders/generate.py - do not edit.",
        "//",
        "// Byte offsets into the scene uniform block, from the same declarations",
        "// that generate the shader's block and the C struct behind it.",
        "",
        "exports (",
    ]
    row = "   "
    for index, name in enumerate(names + ["SCENE_SIZE"]):
        piece = name + ("," if index < len(names) else "")
        if len(row) + len(piece) + 1 > 92:
            lines.append(row)
            row = "   "
        row += " " + piece
    lines.append(row)
    lines.append(")")
    lines.append("")
    for name, _, offset in placed:
        lines.append(f"const OFF_{name.upper()} = {offset}")
    if light_placed:
        lines.append(f"const MAX_LIGHTS = {MAX_LIGHTS}")
        lines.append(f"const LIGHT_STRIDE = {light_stride}")
        for name, _, offset in light_placed:
            lines.append(f"const LIGHT_{name.upper()} = {offset}")
    lines.append(f"const SCENE_SIZE = {size}")
    return "\n".join(lines) + "\n"


def spirv(path, stage):
    out = path.with_suffix(path.suffix + ".spv")
    result = subprocess.run(["glslangValidator", "-V", str(path), "-o", str(out)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit(f"generate: {path.name} failed to compile")
    return out.read_bytes()


def carray(name, data):
    lines = [f"static const unsigned char {name}[] = {{"]
    for index in range(0, len(data), 12):
        chunk = ", ".join(str(b) for b in data[index:index + 12])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines)


# The skybox and the fullscreen passes name their attributes differently from
# the scene shader. Renaming them onto the scene's names lets every pipeline
# share one vertex input description and one descriptor set layout, so the
# renderer has a single binding path instead of one per pass.
ATTRIBUTE_RENAMES = [
    (r"in vec2 aPos\s*;", "in vec3 inPosition;"),
    (r"vec4\(aPos, 0\.0, 1\.0\)", "vec4(inPosition.xy, 0.0, 1.0)"),
    (r"\baTexCoords\b", "inTexCoord"),
    (r"\baTexCoord\b", "inTexCoord"),
    (r"\baNormal\b", "inNormal"),
    (r"\baPos\b", "inPosition"),
]

SCENE_OUT = [("vec2", "fragTexCoord"), ("vec3", "Normal"),
             ("vec3", "FragPos"), ("vec3", "InstanceColor"),
             ("vec4", "FragPosLightSpace"), ("float", "Occlusion")]
SKY_OUT = [("vec3", "TexCoords")]
SCREEN_OUT = [("vec2", "TexCoords")]
WATER_OUT = [("vec2", "fragTexCoord"), ("vec3", "fragNormal"), ("vec3", "fragPosition")]

AUXILIARY = [
    ("depth_vk.vert", "VERTEX_DEPTH", "vert", [], [], []),
    ("depth_vk.frag", "FRAGMENT_DEPTH", "frag", [], [], []),
    ("sky_vk.vert", "VERTEX_SKYBOX", "vert", [], [], SKY_OUT),
    ("sky_vk.frag", "FRAGMENT_SKYBOX", "frag", ["skybox"], SKY_OUT, []),
    ("screen_vk.vert", "VERTEX_SCREEN", "vert", [], [], SCREEN_OUT),
    ("passthrough_vk.frag", "FRAGMENT_PASSTHROUGH", "frag", ["screenTexture"], SCREEN_OUT, []),
    ("fxaa_vk.frag", "FRAGMENT_FXAA", "frag", ["screenTexture"], SCREEN_OUT, []),
    ("bloom_vk.frag", "FRAGMENT_BLOOM", "frag", ["screenTexture"], SCREEN_OUT, []),
    ("ssr_vk.frag", "FRAGMENT_SSR", "frag", ["screenTexture", "depthTexture"], SCREEN_OUT, []),
    ("water_vk.vert", "VERTEX_WATER", "vert", [], [], WATER_OUT),
    # The water reads the scene depth at binding 4, the slot the crowd's pose
    # bank takes: whichever auxiliary image a draw needs sits there.
    ("water_vk.frag", "FRAGMENT_WATER", "frag",
     ["textureSampler", "shadowMap", "normalMap", "sceneDepth"], WATER_OUT, []),
    # The crowd's vertex shaders read the pose bank, a sampler in the vertex
    # stage, at binding 4: past the three the fragment shader has, so the
    # one descriptor set layout serves them too. The three before it are
    # named so the bank lands on 4, not so the vertex shader reads them.
    ("crowd_vk.vert", "VERTEX_CROWD", "vert",
     ["textureSampler", "shadowMap", "normalMap", "poseBank"], [], SCENE_OUT),
    ("crowd_depth_vk.vert", "VERTEX_CROWD_DEPTH", "vert",
     ["textureSampler", "shadowMap", "normalMap", "poseBank"], [], []),
]


def main():
    vertex = block("VERTEX_DEFAULT")
    fragment = block("FRAGMENT_DEFAULT")

    light_members = light_struct(fragment)
    members = collect_uniforms(vertex) + collect_uniforms(fragment)
    for _, source, _, _, _, _ in AUXILIARY:
        members += collect_uniforms(block(source))
    # One block serves every pipeline, so a name declared twice has to mean the
    # same thing in both. Two shaders disagreeing about a type would lay the
    # block out for one of them and corrupt what the other reads.
    seen = {}
    ordered = []
    for member in members:
        kind, name = member[0], member[1]
        shape = (kind,) + tuple(member[2:])
        if name in seen:
            if seen[name] != shape:
                raise SystemExit(f"generate: {name} declared as {seen[name]} and {shape}")
            continue
        seen[name] = shape
        ordered.append(member)

    vertex_out = SCENE_OUT
    vertex_in = [("vec3", "inPosition"), ("vec2", "inTexCoord"), ("vec3", "inNormal")]

    # The vertex inputs already carry explicit locations in the OpenGL source,
    # so they pass through untouched; only the varyings need numbering.
    vk_vertex, placed, size = to_vulkan(vertex, "vert", ordered, [], [], vertex_out,
                                        light_members)

    vk_fragment, _, _ = to_vulkan(fragment, "frag", ordered,
                                  ["textureSampler", "shadowMap", "normalMap"], vertex_out, [],
                                  light_members)

    written = {"scene_vk.vert": vk_vertex, "scene_vk.frag": vk_fragment}
    for name, source, stage, samplers, vin, vout in AUXILIARY:
        # These pipelines reuse the scene's vertex input, so their attributes
        # are renamed onto it rather than described a second time.
        text = block(source)
        for pattern, replacement in ATTRIBUTE_RENAMES:
            text = re.sub(pattern, replacement, text)
        code, _, _ = to_vulkan(text, stage, ordered, samplers, vin, vout, light_members)
        written[name] = code

    light_placed, light_stride = (light_layout(light_members) if light_members else ([], 0))
    module = ROOT / "src/ae3d/vkscene"
    outputs = {OUT_DIR / name: code for name, code in written.items()}
    outputs[NATIVE / "ae3d_vk_uniforms.h"] = c_struct(placed, size, light_placed, light_stride)
    outputs[module / "module.ae"] = aether_offsets(placed, size, light_placed, light_stride)

    # --check: is what is checked in what the shader source produces? An
    # edit to the GLSL without this script run after it leaves Vulkan on the
    # previous shader, which then fails parity in ways that look like real
    # bugs. Needs no glslang, so it runs wherever the source does.
    if "--check" in sys.argv[1:]:
        stale = [path for path, text in outputs.items()
                 if not path.exists() or path.read_text() != text]
        if stale:
            for path in stale:
                print(f"generate: stale {path.relative_to(ROOT)}")
            raise SystemExit("generate: run native/shaders/generate.py and commit the result")
        print(f"generate: {len(outputs)} generated files are current")
        return

    for path, text in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        write(path, text)

    checked = 0
    header = ["/* Generated by native/shaders/generate.py - do not edit. */",
              "#ifndef AE3D_VK_SCENE_SHADERS_H", "#define AE3D_VK_SCENE_SHADERS_H", ""]
    total = 0
    for name in written:
        path = OUT_DIR / name
        checked += verify_offsets(path, placed)
        data = spirv(path, name.rsplit(".", 1)[1])
        total += len(data)
        header += [carray("ae3d_vk_" + name.replace("_vk.", "_").replace(".", "_") + "_spv", data), ""]
    header += ["#endif"]
    write(NATIVE / "ae3d_vk_scene_shaders.h", "\n".join(header) + "\n")

    print(f"generate: {len(ordered)} uniforms, block {size} bytes, "
          f"{checked} offsets verified against glslang, "
          f"{len(written)} shaders, {total}B of SPIR-V")


main()
