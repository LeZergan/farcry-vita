#include "FixedFunctionTestTypes.h"
#include <cassert>
#include <cstdio>
#include <map>

static std::map<int, float> state;
static float constantColor[4];
static void glTexEnvi(int target, int parameter, int value)
{
    assert(target == GL_TEXTURE_ENV);
    state[parameter] = float(value);
}
static void glTexEnvf(int target, int parameter, float value)
{
    assert(target == GL_TEXTURE_ENV);
    state[parameter] = value;
}
static void glTexEnvfv(int target, int parameter, const float *values)
{
    assert(target == GL_TEXTURE_ENV && parameter == GL_TEXTURE_ENV_COLOR);
    for (int i = 0; i < 4; ++i) constantColor[i] = values[i];
}
#include "VitaFixedFunction.h"

int main()
{
    float color[4] = {0.25f, 0.5f, 0.75f, 0.4f};
    const unsigned char decal = eCA_Texture | (eCA_Constant << 3);
    const unsigned char vertex = eCA_Texture | (eCA_Diffuse << 3);
    // Terrain decals use a constant tint/opacity, independently of vertices.
    VitaFixedFunction::ApplyColorOp(eCO_MODULATE, eCO_MODULATE, decal, decal, color);
    assert(state[GL_TEXTURE_ENV_MODE] == GL_COMBINE);
    assert(state[GL_SRC0_RGB] == GL_TEXTURE && state[GL_SRC1_RGB] == GL_CONSTANT);
    assert(state[GL_SRC1_ALPHA] == GL_CONSTANT);
    for (int i = 0; i < 4; ++i) assert(constantColor[i] == color[i]);
    // An RGB tint must not replace a separately selected vertex alpha source.
    VitaFixedFunction::ApplyColorOp(eCO_MODULATE, eCO_MODULATE, decal, vertex, color);
    assert(state[GL_SRC1_RGB] == GL_CONSTANT);
    assert(state[GL_SRC1_ALPHA] == GL_PRIMARY_COLOR);
    // A brightened pass followed by an ordinary pass must reset RGB scaling.
    VitaFixedFunction::ApplyColorOp(eCO_MODULATE4X, eCO_MODULATE, vertex, vertex, color);
    assert(state[GL_RGB_SCALE] == 4);
    VitaFixedFunction::ApplyColorOp(eCO_MODULATE, eCO_MODULATE, vertex, vertex, color);
    assert(state[GL_RGB_SCALE] == 1);
    // Packed third arguments must not overwrite the blend's alpha selector.
    VitaFixedFunction::ApplyColorOp(eCO_BLENDDIFFUSEALPHA, eCO_MODULATE, decal, vertex, color);
    assert(state[GL_COMBINE_RGB] == GL_INTERPOLATE);
    assert(state[GL_SRC2_RGB] == GL_PRIMARY_COLOR);
    assert(state[GL_OPERAND2_RGB] == GL_SRC_ALPHA);
    VitaFixedFunction::ApplyColorOp(eCO_BLENDTEXTUREALPHA, eCO_MODULATE, vertex, vertex, color);
    assert(state[GL_SRC2_RGB] == GL_TEXTURE);
    // The legacy 255 sentinel preserves requested channel state.
    const std::map<int, float> previous = state;
    VitaFixedFunction::ApplyColorOp(255, 255, 255, 255, color);
    assert(previous == state);
    std::puts("Fixed-function material checks passed");
}
