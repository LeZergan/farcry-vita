/* Real fix, not a stub: IRenderer.h's LINUX branch reaches for a Splash.h
   that was never included in the shipped source (another abandoned
   Crytek Linux-port artifact -- see compat/README.md). This provides
   exactly what the WIN32 branch declares inline for the same enum, so
   both platforms end up with the identical eSplashType. */
#pragma once

enum eSplashType
{
	EST_Water,
};
