
//////////////////////////////////////////////////////////////////////
//
//	Game source code (c) Crytek 2001-2003
//	
//	File: GameRadar.cpp
//  
//	History:
//	-December 11,2001: created
//	-October	31,2003: split from GameLoading.cpp and other files
//	
//////////////////////////////////////////////////////////////////////

#include "stdafx.h" 

#include "Game.h"
#include "XNetwork.h"
#include "XServer.h"
#include "XClient.h"

#include "UIHud.h"

#include "XPlayer.h"
#include "PlayerSystem.h"
#include "XServer.h"
#include "WeaponSystemEx.h"
#include "ScriptObjectGame.h"
#include "ScriptObjectInput.h"
#include <IEntitySystem.h>

#include "UISystem.h"
#include "ScriptObjectUI.h"

/* Radar geometry is authored on Far Cry's virtual 800x600 canvas.  A square
   image submitted as w==h is not physically square on a widescreen target:
   the X and Y projection scales differ.  The compass already compensates via
   fScaleY; apply the same contract to every icon and keep centring exact. */
static inline void DrawRadarIcon(IRenderer *pRenderer, float fCenterX, float fCenterY,
  float fSize, float fScaleY, int nTexture, float fAngle,
  float r, float g, float b, float a)
{
  if (!pRenderer || nTexture <= 0 || fSize <= 0.0f || fScaleY <= 0.0f || a <= 0.0f)
    return;
  const float fHeight = fSize*fScaleY;
  pRenderer->Draw2dImage(fCenterX-fSize*0.5f, fCenterY-fHeight*0.5f,
    fSize, fHeight, nTexture, 0, 0, 1, 1, fAngle, r, g, b, a);
}

static inline float RadarDirectionAngle(float fDirectionX, float fDirectionY)
{
  /* atan2 is defined for every non-zero vector and cannot drift outside the
     legal input range like acos did after floating-point normalisation. */
  float fAngle=RAD2DEG(cry_atan2f(-fDirectionX,fDirectionY));
  if(fAngle<0.0f)
    fAngle+=360.0f;
  return fAngle;
}

// render game radar
// notes: clean up/optimize code, lots of redundant stuff
//////////////////////////////////////////////////////////////////////////
void CXGame::DrawRadar(float x, float y, float w, float h, float fRange, INT_PTR *pRadarTextures, _SmartScriptObject *pEntities, const char *pRadarObjective)
{
  // 0 - Radar
  // 1 - RadarMask
  // 2 - RadarPlayerIcon
  // 3 - RadarEnemyInRangeIcon
  // 4 - RadarEnemyOutRangeIcon
  // 5 - RadarSoundIcon
	// 6 - RadarObjectiveIcon

  // check if data ok
  if (!m_pRenderer || !m_pEntitySystem || !m_pScriptSystem ||
      !pRadarTextures || !pRadarObjective || !pEntities || !(*pEntities))
  {
    return;
  }

  IEntity *pPlayer=GetMyPlayer();

  if (!pPlayer)
  {
    return;
  }

  // clamp minimum value
  if(fRange<10.0f) 
  {
    fRange=10.0f;
  }

  /* Validate every dependency before opening the renderer's 2D scope.  The
     previous timer failure path returned after Set2DMode(true), leaking the
     projection/matrix stack and corrupting all later HUD and scene draws. */
  ITimer *pTimer=m_pSystem ? m_pSystem->GetITimer() : 0;
  if (!pTimer)
  {
    return;
  }
  
  ICVar *pFadeAmount=0;
  if(m_pSystem && m_pSystem->GetIConsole()) 
  {
    pFadeAmount=m_pSystem->GetIConsole()->GetCVar("hud_fadeamount");
  }

  float fFadeAmount=1;
  if(pFadeAmount)
  {
     fFadeAmount=pFadeAmount->GetFVal();
  }
  fFadeAmount=CLAMP(fFadeAmount,0.0f,1.0f);
  
  // set render state
  m_pRenderer->SetState(GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA | GS_NODEPTHTEST);
  m_pRenderer->Set2DMode(true, 800, 600);

  // radar texture id's
  INT_PTR iRadarID=pRadarTextures[0], 
      iRadarMaskID=pRadarTextures[1], 
      iPlayerIconID=pRadarTextures[2], 
      iEnemyInRangeIconID=pRadarTextures[3],
      iEnemyOutRangeIconID=pRadarTextures[4],
      iSoundIconID=pRadarTextures[5],
			iObjectiveIconID=pRadarTextures[6];

  // last radar alpha/used to mask out sound events
  static float fLastAlpha=0.0f, fLastAvgDist=0.0f;

  float fCurrPosStep=fLastAlpha*5.0f; 

  if(fCurrPosStep>1.0f)
  {
    fCurrPosStep=1.0f;
  }

  Vec3d fMapCenter(x+w*0.5f, y+h*0.5f, 0.5f);

  // render radar/compass

  int iPx=0, iPy=0, iW=0, iH=0;
  m_pRenderer->GetViewport(&iPx, &iPy, &iW, &iH);

  /* A transient zero viewport during mode changes must not become a divide by
     zero/NaN that poisons every radar vertex for the rest of the frame. */
  if(iW<=0 || iH<=0)
  {
    iW=800;
    iH=600;
  }

  // used to scale y coordinates correctly acoording to aspect ratio
  float fScaleY=((float)iW/(float)iH)*0.75f;
  
  // get radar mask data
  float fMaskPosX=fMapCenter.x-0.5f*186.0f;
  float fMaskPosY=fMapCenter.y-(0.5f*186.0f)*fScaleY;
  float fMaskW=186.0f;
  float fMaskH=186.0f*fScaleY;

  // get radar data
  float fRadarPosX=fMapCenter.x-0.5f*130.0f;
  float fRadarPosY=fMapCenter.y-(0.5f*130.0f)*fScaleY;

  float fRadarW=130.0f;
  float fRadarH=(130.0f)*fScaleY;
  float fCurrAngle=pPlayer->GetAngles().z;
  static float fCurrCompassAngle=0.0f;
  static float fLastCompassDrawTime=-1000.0f;
  static IEntity *pLastCompassPlayer=0;
  const float fCompassDrawTime=pTimer->GetCurrTime();
  /* A hidden HUD, cut scene or level change can leave this static smoother
     holding a heading from seconds (or a different player entity) ago.  On
     reappearance that looks like a broken locator slowly rotating to reality.
     Snap only across a real discontinuity; continuous gameplay still uses the
     authored smoothing below. */
  if(pLastCompassPlayer!=pPlayer || fCompassDrawTime<fLastCompassDrawTime ||
     fCompassDrawTime-fLastCompassDrawTime>0.5f)
  {
    fCurrCompassAngle=fCurrAngle;
    pLastCompassPlayer=pPlayer;
  }
  fLastCompassDrawTime=fCompassDrawTime;
  /* Interpolate through the shortest arc.  The old direct subtraction made
     north crossings (359 -> 0 degrees) spin the compass almost a full turn. */
  float fCompassDelta=fCurrAngle-fCurrCompassAngle;
  while(fCompassDelta>180.0f) fCompassDelta-=360.0f;
  while(fCompassDelta<-180.0f) fCompassDelta+=360.0f;
  float fCompassStep=pTimer->GetFrameTime()*8.0f;
  if(fCompassStep>1.0f) fCompassStep=1.0f;
  if(fCompassStep<0.0f) fCompassStep=0.0f;
  fCurrCompassAngle+=fCompassDelta*fCompassStep;
  while(fCurrCompassAngle>=360.0f) fCurrCompassAngle-=360.0f;
  while(fCurrCompassAngle<0.0f) fCurrCompassAngle+=360.0f;
#if defined(__vita__)
  // Vita: compass.dds receives a circular alpha channel while loading.  Draw it
  // directly; destination-alpha masking corrupted the rest of the framebuffer
  // on this fixed-function backend and produced the broken black locator tile.
  m_pRenderer->SetState(GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA | GS_NODEPTHTEST);
  m_pRenderer->Draw2dImage(fRadarPosX, fRadarPosY, fRadarW, fRadarH, iRadarID, 0, 1, 1, 0, fCurrCompassAngle+90.0f, fFadeAmount,fFadeAmount, fFadeAmount, 1);

  // The old animated noise reused the mask texture as an opaque colour pass.
  // Omitting it makes the navigation markings readable and saves one HUD draw.
#else
  // clear radar background alpha
  m_pRenderer->SetState(GS_BLSRC_ONE | GS_BLDST_ZERO | GS_NODEPTHTEST | GS_COLMASKONLYALPHA);
  m_pRenderer->Draw2dImage(fMaskPosX, fMaskPosY, fMaskW, fMaskH, iRadarMaskID, 0, 0.1f, 0.1f, 0, 0, 1, 1, 1, 1);
  // render radar mask into alpha channel
  m_pRenderer->Draw2dImage(fRadarPosX, fRadarPosY, fRadarW, fRadarH, iRadarMaskID, 0, 1, 1, 0, 0, 1,1, 1, 1); 
  // add the radar
  m_pRenderer->SetState(GS_BLSRC_ONEMINUSDSTALPHA | GS_BLDST_DSTALPHA | GS_NODEPTHTEST);
  m_pRenderer->Draw2dImage(fRadarPosX, fRadarPosY, fRadarW, fRadarH, iRadarID, 0, 1, 1, 0, fCurrCompassAngle+90.0f, fFadeAmount,fFadeAmount, fFadeAmount, 1); 
  // add radar noise..
  static float fNoiseMove=0.0f;
  fNoiseMove+=pTimer->GetFrameTime()*2.0f;      
  m_pRenderer->Draw2dImage(fRadarPosX, fRadarPosY, fRadarW, fRadarH, iRadarMaskID, 0+fNoiseMove, 0, 0.1f+fNoiseMove, 1.5f, 0, 1, 1.0f, 1.0f, 1.0f);  
#endif

  // render player icon
  float fPlayerSize=16.0f;
  m_pRenderer->SetState(GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA | GS_NODEPTHTEST);
  float fPlayerAlpha=(fLastAlpha<=0.6f)? 0.6f: fLastAlpha;
  /* RadarPlayer.dds is directional, unlike the symmetric dot/ring markers.
     Preserve the retail half-texel inset and its opposite V orientation.  The
     generic aspect helper accidentally replaced both with full 0..1 UVs,
     flipping the local-player arrow and sampling the transparent border. */
  if(iPlayerIconID>0 && fPlayerAlpha>0.0f)
  {
    const float fPlayerHeight=fPlayerSize*fScaleY;
    const float fTexOffset=0.5f/fPlayerSize;
    m_pRenderer->Draw2dImage(fMapCenter.x-fPlayerSize*0.5f,
      fMapCenter.y-fPlayerHeight*0.5f, fPlayerSize, fPlayerHeight,
      (int)iPlayerIconID, fTexOffset, 1.0f-fTexOffset,
      1.0f-fTexOffset, fTexOffset, 0.0f,
      fPlayerAlpha*fFadeAmount, fPlayerAlpha*fFadeAmount,
      fPlayerAlpha*fFadeAmount, fPlayerAlpha);
  }

  // get player position
  Vec3d pPlayerPos=pPlayer->GetPos();

  // draw sound-events
  CXClient *pClient=GetClient(); 
  m_pRenderer->SetState(GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA | GS_NODEPTHTEST);
  if (pClient) //&& fLastAlpha>0.0f)
  {
    // The client owns this list and radar only reads it.  Copying the whole
    // event vector every HUD frame wastes CPU and creates needless heap churn.
    TSoundList &SoundList=pClient->GetSoundEventList();
        
    float fScale=(w*0.5f); ///fRange;        
    Matrix33 mtxTransformNoMove;
    mtxTransformNoMove.SetScale(Vec3(fScale , (fScale)*fScaleY, 0.0f));

    mtxTransformNoMove=mtxTransformNoMove*Matrix33::CreateRotationZ(DEG2RAD(-pPlayer->GetAngles().z));
    Matrix34 mtxTransform=mtxTransformNoMove;
    Matrix34 TransferVector;
    TransferVector.SetTranslationMat(-pPlayer->GetPos());
    mtxTransform=mtxTransform*TransferVector;

    for (TSoundListIt It=SoundList.begin();It!=SoundList.end();++It)
    {
      SSoundInfo &SoundInfo=(*It);
      if (SoundInfo.fThread==0.0f)
      {
        // only display threatening sounds
        continue;
      }

      Vec3d pPos=mtxTransform*SoundInfo.Pos;
      
      Vec3d pSoundPos=SoundInfo.Pos;
      float fCurrDist=cry_sqrtf((pPlayerPos.x-pSoundPos.x)*(pPlayerPos.x-pSoundPos.x)+(pPlayerPos.y-pSoundPos.y)*(pPlayerPos.y-pSoundPos.y)+(pPlayerPos.z-pSoundPos.z)*(pPlayerPos.z-pSoundPos.z));    

      float fRadius=fScale*SoundInfo.fRadius/fRange;
      const float fSoundTimeout=(pClient->cl_sound_event_timeout &&
        pClient->cl_sound_event_timeout->GetFVal()>0.001f) ?
        pClient->cl_sound_event_timeout->GetFVal() : 0.001f;
      float fPhase=SoundInfo.fTimeout/fSoundTimeout;
      fPhase=CLAMP(fPhase,0.0f,1.0f);
      pPos.x=-pPos.x;
      float fSoundIconSize; 
      
      if(fCurrDist>fRange)  
      {
        if(fCurrDist==0.0f)
        {
          fCurrDist=1.0f;  
        }

        fSoundIconSize=20.0f*(1.0f-fPhase);
        fCurrDist=1.0f/fCurrDist;
        DrawRadarIcon(m_pRenderer, fMapCenter.x+pPos.x*fCurrDist,
          fMapCenter.y+pPos.y*fCurrDist, fSoundIconSize, fScaleY,
          (int)iSoundIconID, 0, fFadeAmount, fFadeAmount, fFadeAmount, fPhase);
      }
      else
      {
        fCurrDist/=15.0f; 
        if(fCurrDist<1.2f && fCurrDist>0.0f)
        {
          fCurrDist=1.2f; 
        }
        else
          if(fCurrDist==0.0f)
          {
            fCurrDist=1.0f;  
          }

        fSoundIconSize=fRadius*(1.0f-fPhase);
        fCurrDist=1.0f/fCurrDist;         
        DrawRadarIcon(m_pRenderer, fMapCenter.x+(pPos.x/fRange),
          fMapCenter.y+(pPos.y/fRange), fSoundIconSize*fCurrDist, fScaleY,
          (int)iSoundIconID, 0, fFadeAmount, fFadeAmount, fFadeAmount, fPhase);
      }
    }
  }

  // draw players
  float PlayerZAngle=pPlayer->GetAngles().z;
  int nCount=1;
  _SmartScriptObject pEntitySO(m_pScriptSystem, true);
  _SmartScriptObject pColor(m_pScriptSystem, true);
  (*pEntities)->BeginIteration();

  // compute average distance to player, for dinamyc radar scale adjustment   
  float fAvgDist=0.0f;
  int  iTotalEntities=1;

  float fScale=(w*0.5f);
  ASSERT(fScale>0.0f);
  Matrix33 mtxTransformNoMove;
  mtxTransformNoMove.SetScale(Vec3(fScale, (fScale)*fScaleY, 0.0f));
  mtxTransformNoMove=mtxTransformNoMove*Matrix33::CreateRotationZ(DEG2RAD(-pPlayer->GetAngles().z));
  Matrix34 mtxTransform=mtxTransformNoMove; 
  Matrix34 TransferVector;
  TransferVector.SetTranslationMat(-pPlayer->GetPos());
  mtxTransform=mtxTransform*TransferVector;

  m_pRenderer->SetState(GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA | GS_NODEPTHTEST);

  // show radar objective direction
  // get radar objective position
  Vec3d pObjectivePos(0,0,0);
  int iHaveObjective= strcmp("NoObjective", pRadarObjective);
  if(iHaveObjective)
  {
    if(sscanf(pRadarObjective, "%f %f %f", &pObjectivePos.x, &pObjectivePos.y, &pObjectivePos.z)!=3)
    {
      /* Never escape after BeginIteration/Set2DMode: one malformed script value
         used to leave both scopes open and corrupt every later HUD draw. */
      iHaveObjective=0;
    }
  }
  if(iHaveObjective)
  {
    Vec3d pObjectiveScreenPos=mtxTransform*pObjectivePos;
    pObjectiveScreenPos.x=-pObjectiveScreenPos.x;// invert x due to different mapping

    // compute distance and sum values    
    float fCurrDist=cry_sqrtf((pPlayerPos.x-pObjectivePos.x)*(pPlayerPos.x-pObjectivePos.x)+(pPlayerPos.y-pObjectivePos.y)*(pPlayerPos.y-pObjectivePos.y));    

     
    float r=0.0f, g=1.0f, b=1.0f, a=1.0f;   


    if(fCurrDist>fRange)
    {
      /* Remove the virtual-canvas aspect stretch before normalising direction,
         then apply it exactly once when positioning on the radar rim. */
      float fPosX=pObjectiveScreenPos.x;
      float fPosY=fScaleY>0.0f ? pObjectiveScreenPos.y/fScaleY : pObjectiveScreenPos.y;
      float fLen=cry_sqrtf(fPosX*fPosX + fPosY*fPosY);
      if(fLen)
      {
        fPosX/=fLen;
        fPosY/=fLen;
      }

      const float fOutRangeAngle=RadarDirectionAngle(fPosX,fPosY);

      DrawRadarIcon(m_pRenderer, fMapCenter.x+fPosX*52.0f,
        fMapCenter.y+fPosY*52.0f*fScaleY, 10.0f, fScaleY,
        (int)iEnemyOutRangeIconID, fOutRangeAngle,
        r*fFadeAmount, g*fFadeAmount, b*fFadeAmount, a*0.5f);

     }
    else
    {

      float r=0.0f, g=1.0f, b=1.0f, a=1.0f;
      fCurrDist/=15.0f; 
      if(fCurrDist<1.2f && fCurrDist>0.0f)
      {
        fCurrDist=1.2f; 
      }
      else
        if(fCurrDist==0.0f)
        {
          fCurrDist=1.0f;  
        }

        fCurrDist=1.0f/fCurrDist;

        float fVerticalDist=fabsf(pPlayerPos.z-pObjectivePos.z);
        // to distant on vertical range, must be on diferent floor/level, put icons gray
        if(fVerticalDist>fRange*0.15f) 
        {
          r*=0.5f;
          g*=0.5f;
          b*=0.5f; 
        }

        static float fCurrSize=0.0f;
        fCurrSize+=pTimer->GetFrameTime()*2.0f;
        if(fCurrSize>1.0f)
        {
          fCurrSize=0.0f;
        }        
        DrawRadarIcon(m_pRenderer, fMapCenter.x+(pObjectiveScreenPos.x/fRange),
          fMapCenter.y+(pObjectiveScreenPos.y/fRange), 10.0f*fCurrSize, fScaleY,
          (int)iObjectiveIconID, 0.0f,
          r*a*fFadeAmount, g*a*fFadeAmount, b*a*fFadeAmount, a);
    } 
  }

  // render entities 
  while ((*pEntities)->MoveNext())
  {
    if (!(*pEntities)->GetCurrent(*pEntitySO))
    {
      continue;
    }

    int nId;
    if (!pEntitySO->GetValue("nId", nId))
    {
      continue;
    }

    IEntity *pEntity=m_pEntitySystem->GetEntity(nId);
    if (!pEntity)
    {
      continue;
    }

    Vec3d Pos=mtxTransform*pEntity->GetPos();
    Pos.x=-Pos.x;// invert x due to different mapping

    float r=1.0f, g=1.0f, b=1.0f, a=1.0f;
    if (pEntitySO->GetValue("Color", *pColor))
    {
      pColor->GetValue("r", r);
      pColor->GetValue("g", g);
      pColor->GetValue("b", b);
      pColor->GetValue("a", a);
    }

    fLastAlpha=a;

    // no need to do more stuff..
    if(fLastAlpha==0.0f)
    {
      continue;
    }

    Matrix33 IconMatrix;
    float fAngleZ=PlayerZAngle-pEntity->GetAngles(true).z;

    // [marco] temporay fix to be able
    // to run a version with debug info
    // on and do not get an assert every frame
    if (fAngleZ<-360.0f)
    {
      fAngleZ+=360;
    }
    else
      if (fAngleZ>360.0f)
      {
        fAngleZ-=360; 
      }

      float fEnemyInRangeSize=10.0f, 
        fEnemyOutRangeSize=5.0f; 

      // compute distance and sum values
      Vec3d pEntityPos=pEntity->GetPos();
      float fCurrDist=cry_sqrtf((pPlayerPos.x-pEntityPos.x)*(pPlayerPos.x-pEntityPos.x)+(pPlayerPos.y-pEntityPos.y)*(pPlayerPos.y-pEntityPos.y));    

      // skip player..
      if(fCurrDist<0.01)
      {
        continue;
      }

      // render enemy icons
      if(fCurrDist>fRange)
      {    
        if(fCurrDist==0.0f)
        {
          fCurrDist=1.0f;  
        }

        fCurrDist=1.0f/fCurrDist;

        float fPosX=Pos.x;
        float fPosY=fScaleY>0.0f ? Pos.y/fScaleY : Pos.y;
        float fLen=cry_sqrtf(fPosX*fPosX + fPosY*fPosY);
        if(fLen)
        {
          fPosX/=fLen;
          fPosY/=fLen;
        }

        const float fOutRangeAngle=RadarDirectionAngle(fPosX,fPosY);

        DrawRadarIcon(m_pRenderer, fMapCenter.x+Pos.x*fCurrDist*0.98f,
          fMapCenter.y+Pos.y*fCurrDist*0.98f, fEnemyOutRangeSize, fScaleY,
          (int)iEnemyOutRangeIconID, fOutRangeAngle, 0, a*fFadeAmount, 0, a);
      }
      else
      {
        fCurrDist/=15.0f; 
        if(fCurrDist<1.2f && fCurrDist>0.0f)
        {
          fCurrDist=1.2f; 
        }
        else
          if(fCurrDist==0.0f)
          {
            fCurrDist=1.0f;  
          }

          fCurrDist=1.0f/fCurrDist;

          float fVerticalDist=fabsf(pPlayerPos.z-pEntityPos.z);
          // to distant on vertical range, must be on diferent floor/level, put icons gray
          if(fVerticalDist>fRange*0.15f) 
          {
            r*=0.5f;
            g*=0.5f;
            b*=0.5f; 
          }

          DrawRadarIcon(m_pRenderer, fMapCenter.x+(Pos.x*0.98f/fRange),
            fMapCenter.y+(Pos.y*0.98f/fRange), fEnemyInRangeSize*fCurrDist, fScaleY,
            (int)iEnemyInRangeIconID, fAngleZ+180.0f,
            r*a*fFadeAmount, g*a*fFadeAmount, b*a*fFadeAmount, a);
      }
  }

  (*pEntities)->EndIteration();

  // reset states 
  m_pRenderer->SetState(GS_NODEPTHTEST);  
  m_pRenderer->Set2DMode(false, 800, 600);   
}

