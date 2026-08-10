//////////////////////////////////////////////////////////////////////
//
//	FarCry Game Source code
//	
//	File:Hud.cpp
//  Description: Player Hud implementation
//
//	History:
//	-June 03,2001:Created by Marco Corbetta
//
//////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////
#include "stdafx.h"

#include "UIHud.h"
#include <IFont.h>
#include <IScriptSystem.h>

//////////////////////////////////////////////////////////////////////////
CUIHud::CUIHud(CXGame *pGame,ISystem *pISystem)
{	
	m_pGame=pGame;
	m_pISystem = pISystem;
	m_init = false;
	m_pHudScriptObj=NULL;
	m_pScriptSystem=m_pISystem->GetIScriptSystem();

	ICryFont *pICryFont=m_pISystem->GetICryFont();

	m_pFont = 0;
	if(pICryFont)
	{
		/* GetFont can return null if the font was never registered, and the
		   SetEffect call that followed dereferenced it unconditionally -- a
		   null deref during HUD construction. */
		m_pFont = pICryFont->GetFont("Default");
		if(m_pFont)
			m_pFont->SetEffect("default");
	}
}

//////////////////////////////////////////////////////////////////////////
CUIHud::~CUIHud()
{
	ShutDown();
	if(m_pHudScriptObj)
		m_pHudScriptObj->Release();
}

bool CUIHud::Reset()
{
	char filename[512];
	sprintf(filename,"Scripts/$GT$/Hud/%s", m_pISystem->GetIConsole()->GetCVar("cl_hud_name")->GetString());
		
	if (!m_pGame->ExecuteScript(filename,true))
	{
		m_pISystem->GetILog()->Log("Cannot load script %s", filename);
		return false;
	}
	
	if (!m_pGame->ExecuteScript("Scripts/$GT$/Hud/scoreboard.lua",true))
	{
		m_pISystem->GetILog()->Log("Cannot load script Scripts/$GT$/Hud/scoreboard.lua");
		return false;
	}
	
	// initialize hud
	m_pHudScriptObj=m_pScriptSystem->CreateEmptyObject();
	if(!m_pScriptSystem->GetGlobalValue("Hud",m_pHudScriptObj))
	{
#if defined(__vita__)
		/* Silence here is ambiguous: no error in the log reads the same whether
		   the HUD script loaded fine or Reset was never called at all.  Both end
		   with nothing drawn, and CUIHud::Update calls Hud:OnUpdate regardless of
		   whether the Hud table exists, so a missing table fails silently too. */
		m_pISystem->GetILog()->LogToFile("\001[VITA][HUD] '%s' executed but defined no global Hud table -- nothing will draw", filename);
#endif
		return false;
	}
#if defined(__vita__)
	m_pISystem->GetILog()->LogToFile("\001[VITA][HUD] loaded '%s' and found the Hud table; calling Hud:OnInit", filename);
#endif

	m_pScriptSystem->BeginCall("Hud","OnInit");
	m_pScriptSystem->PushFuncParam(m_pHudScriptObj);
	m_pScriptSystem->EndCall();
	return true;

}
// create and initialize the hud
//////////////////////////////////////////////////////////////////////////
bool CUIHud::Init(IScriptSystem *pScriptSystem)
{
	if (m_init)
		return true;

	
	m_init=true;//Reset();
		
	return (true);	
}

//////////////////////////////////////////////////////////////////////////
void CUIHud::SetFont(const char *pszFontName, const char *pszEffectName)
{
	m_pFont=m_pISystem->GetICryFont()->GetFont(pszFontName);
	if (!m_pFont)
	{
		// The "Default" fallback can be absent too -- it was dereferenced here
		// without a check, so a missing font took the game down instead of
		// leaving the HUD text undrawn.
		m_pFont=m_pISystem->GetICryFont()->GetFont("Default");
		if (m_pFont)
			m_pFont->SetEffect("default");
	}else
		m_pFont->SetEffect(pszEffectName);
}

// write a number on the screen using special textures numbers
//////////////////////////////////////////////////////////////////////////
void CUIHud::WriteNumber(int px, int py, int number, float r, float g, float b, float a, float xsize/* =1 */, float ysize/* =1 */)
{
	if (!m_pFont)
		return;	// no font registered: draw nothing rather than dereference null
	m_pFont->Reset();
	vector2f hsize(xsize,ysize);
	m_pFont->SetSize(hsize);
	color4f hcolor(r,g,b,a);
	m_pFont->SetColor(hcolor);

	char szText[16];

	sprintf(szText, "%d", number);

	m_pFont->DrawString((float)(px),(float)(py),szText);
}

//////////////////////////////////////////////////////////////////////////
void CUIHud::WriteString(int px, int py, const wchar_t *swStr, float r, float g, float b, float a, float xsize/* =1 */, float ysize/* =1 */, float fWrapWidth)
{
	if (!m_pFont)
		return;	// no font registered: draw nothing rather than dereference null
	m_pFont->Reset();
	vector2f hsize (xsize,ysize);
	m_pFont->SetSize(hsize);
	color4f hcolor(r,g,b,a);
	m_pFont->SetColor(hcolor);

	if (fWrapWidth > 0)
	{
		m_pFont->DrawWrappedStringW((float)(px),(float)(py), fWrapWidth, swStr );
	}
	else
	{
		m_pFont->DrawStringW((float)(px),(float)(py),swStr );
	}
}

//////////////////////////////////////////////////////////////////////////
//void CUIHud::WriteStringFixed(int px, int py, char *pszStr, float r, float g, float b, float a,float xsize/* =1 */, float ysize/* =1 */, float fWidthScale)
void CUIHud::WriteStringFixed(int px, int py, const wchar_t *swStr, float r, float g, float b, float a,float xsize/* =1 */, float ysize/* =1 */, float fWidthScale)
{
	if (!m_pFont)
		return;	// no font registered: draw nothing rather than dereference null
	m_pFont->Reset();
	m_pFont->SetSameSize(TRUE);
	m_pFont->SetCharWidthScale(fWidthScale);
	vector2f hsize(xsize,ysize);
	m_pFont->SetSize(hsize);
	color4f hcolor(r,g,b,a);
	m_pFont->SetColor(hcolor);
	m_pFont->DrawStringW((float)(px),(float)(py),swStr );
}

// update the hud every frame
//////////////////////////////////////////////////////////////////////////
bool CUIHud::Update()
{
	if (!m_init)
		return false;
	m_pScriptSystem->BeginCall("Hud","OnUpdate");
	m_pScriptSystem->PushFuncParam(m_pHudScriptObj);
	m_pScriptSystem->EndCall();
	return true;
}

//////////////////////////////////////////////////////////////////////////
void CUIHud::ShutDown()
{
	if (!m_init)
		return; 
	
	m_pScriptSystem->BeginCall("Hud","OnShutdown");
	m_pScriptSystem->PushFuncParam(m_pHudScriptObj);
	m_pScriptSystem->EndCall();
}
