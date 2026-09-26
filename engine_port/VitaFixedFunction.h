/*
 * Crytek OpenGL fixed-function texture combiner adaptation.
 * Copyright (c) 2001 Crytek Studios. All Rights Reserved.
 *
 * Derived from CGLRenderer::EF_SetColorOp in NearChuckle Android, commit
 * 82b2aca74e3f7904cf61ae72f69cef9ba614ea9f, file
 * SourceCode/RenderDll/XRenderOGL/GLRendPipeline.cpp.
 * Original SDK terms are preserved in the repository's README.txt.
 *
 * Vita changes: use core GL enum spellings, receive the constant color directly,
 * omit desktop renderer/cache dependencies, and preserve the interpolation
 * selector after packed-argument setup. The caller selects the texture unit.
 * Requires CryEngine EColorOp/EColorArg and GL declarations before inclusion.
 */
#ifndef FARCRY_VITA_FIXED_FUNCTION_H
#define FARCRY_VITA_FIXED_FUNCTION_H
namespace VitaFixedFunction
{
inline void ApplyColorOp(unsigned char eCo, unsigned char eAo, unsigned char eCa, unsigned char eAa, float color[4])
{
  // Desktop cache state is deliberately local: Vita's existing lightmap and
  // HUD paths also write texture-environment state directly. Reapply the
  // requested channels instead of trusting a cache they cannot invalidate.
  const int stage = 0;
  int m_eCurColorOp[1] = {255}, m_eCurAlphaOp[1] = {255};
  int m_eCurColorArg[1] = {255}, m_eCurAlphaArg[1] = {255};
  float m_fCurRGBScale[1] = {-1};
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
  glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color);

  if (eCo != 255 && eCo != m_eCurColorOp[stage])
  {
    float fScale = 1.0f;
    if (eCo == eCO_MODULATE2X)
      fScale = 2.0f;
    else
    if (eCo == eCO_MODULATE4X)
      fScale = 4.0f;
    if (m_fCurRGBScale[stage] != fScale)
    {
      m_fCurRGBScale[stage] = fScale;
      glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, fScale);
    }
    switch (eCo)
    {
      case eCO_MODULATE:
      default:
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        break;
      case eCO_REPLACE:
      case eCO_DECAL:
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
        break;
      case eCO_MODULATE2X:
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        break;
      case eCO_MODULATE4X:
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        break;
      case eCO_ADD:
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_ADD);
        break;
      case eCO_ADDSIGNED:
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_ADD_SIGNED);
        break;
      case eCO_BLENDDIFFUSEALPHA:
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_RGB, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);
        break;
      case eCO_BLENDTEXTUREALPHA:
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);
        break;
      case eCO_MULTIPLYADD:
        break;
      case eCO_BUMPENVMAP:
        break;
      case eCO_DISABLE:
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
        break;
    }
    m_eCurColorOp[stage] = eCo;
  }

  if (eAo != 255 && eAo != m_eCurAlphaOp[stage])
  {
    switch (eAo)
    {
      case eCO_MODULATE:
      case eCO_MODULATE2X:
      case eCO_MODULATE4X:
      default:
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
        break;
      case eCO_BLENDDIFFUSEALPHA:
        break;
      case eCO_BLENDTEXTUREALPHA:
        break;
      case eCO_ADD:
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_ADD);
        break;
      case eCO_ADDSIGNED:
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_ADD_SIGNED);
        break;
      case eCO_MULTIPLYADD:
        break;
      case eCO_REPLACE:
      case eCO_DECAL:
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
        break;
      case eCO_BUMPENVMAP:
        break;
      case eCO_DISABLE:
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
        break;
    }
    m_eCurAlphaOp[stage] = eAo;
  }

  if (eCa != 255 && eCa != m_eCurColorArg[stage])
  {
    if ((eCa & 7) != (m_eCurColorArg[stage] & 7))
    {
      switch (eCa & 7)
      {
        case eCA_Texture:
        default:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
          break;
        case eCA_Diffuse:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PRIMARY_COLOR);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
          break;
        case eCA_Specular:
          break;
        case eCA_Previous:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PREVIOUS);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
          break;
        case eCA_Constant:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_CONSTANT);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
          break;
      }
    }
    if (((eCa>>3) & 7) != ((m_eCurColorArg[stage]>>3) & 7))
    {
      switch ((eCa >> 3) & 7)
      {
        case eCA_Texture:
        default:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_TEXTURE);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
          break;
        case eCA_Diffuse:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PRIMARY_COLOR);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
          break;
        case eCA_Specular:
          break;
        case eCA_Previous:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PREVIOUS);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
          break;
        case eCA_Constant:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_CONSTANT);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
          break;
      }
    }
    if ((eCa >> 6) != (m_eCurColorArg[stage] >> 6))
    {
      switch (eCa >> 6)
      {
        case eCA_Texture:
        default:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_RGB, GL_TEXTURE);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_COLOR);
          break;
        case eCA_Diffuse:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_RGB, GL_PRIMARY_COLOR);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_COLOR);
          break;
        case eCA_Specular:
          break;
        case eCA_Previous:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_RGB, GL_PREVIOUS);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_COLOR);
          break;
        case eCA_Constant:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_RGB, GL_CONSTANT);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_COLOR);
          break;
      }
    }
    m_eCurColorArg[stage] = eCa;
  }

  if (eAa != 255 && eAa != m_eCurAlphaArg[stage])
  {
    if ((eAa & 7) != (m_eCurAlphaArg[stage] & 7))
    {
      switch (eAa & 7)
      {
        case eCA_Texture:
        default:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_TEXTURE);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
          break;
        case eCA_Diffuse:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_PRIMARY_COLOR);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
          break;
        case eCA_Specular:
          break;
        case eCA_Previous:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_PREVIOUS);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
          break;
        case eCA_Constant:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_CONSTANT);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
          break;
      }
    }
    if (((eAa>>3) & 7) != ((m_eCurAlphaArg[stage]>>3) & 7))
    {
      switch ((eAa >> 3) & 7)
      {
        case eCA_Texture:
        default:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_TEXTURE);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
          break;
        case eCA_Diffuse:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_PRIMARY_COLOR);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
          break;
        case eCA_Specular:
          break;
        case eCA_Previous:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_PREVIOUS);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
          break;
        case eCA_Constant:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_CONSTANT);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
          break;
      }
    }
    if ((eAa >> 6) != (m_eCurAlphaArg[stage] >> 6))
    {
      switch (eAa >> 6)
      {
        case eCA_Texture:
        default:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_ALPHA, GL_TEXTURE);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_ALPHA, GL_SRC_ALPHA);
          break;
        case eCA_Diffuse:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_ALPHA, GL_PRIMARY_COLOR);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_ALPHA, GL_SRC_ALPHA);
          break;
        case eCA_Specular:
          break;
        case eCA_Previous:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_ALPHA, GL_PREVIOUS);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_ALPHA, GL_SRC_ALPHA);
          break;
        case eCA_Constant:
          glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_ALPHA, GL_CONSTANT);
          glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_ALPHA, GL_SRC_ALPHA);
          break;
      }
    }
    m_eCurAlphaArg[stage] = eAa;
  }
  if (eCo == eCO_BLENDDIFFUSEALPHA || eCo == eCO_BLENDTEXTUREALPHA)
  {
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_RGB,
      eCo == eCO_BLENDDIFFUSEALPHA ? GL_PRIMARY_COLOR : GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);
  }
}
}
#endif
