
//////////////////////////////////////////////////////////////////////
//
//	Crytek Source code (c) Crytek 2001-2004
// 
//	File: Validator.h	
// 
//	History:
//	-Feb 09,2004:Created 
//
//////////////////////////////////////////////////////////////////////

#ifndef VALIDATOR_H
#define VALIDATOR_H

#if _MSC_VER > 1000
# pragma once
#endif

//////////////////////////////////////////////////////////////////////////
// Default validator implementation.
//
// NOTE: CSystem is still just forward-declared at this point in the
// include chain (System.h includes IPhysics.h, which pulls this header
// in via physinterface.h, before System.h's own CSystem class is
// defined further down). Report()'s body touches CSystem members
// directly (m_sysWarnings, GetIConsole()), so it can't be defined
// inline here -- it's declared here and defined out-of-line in
// System.h right after CSystem becomes a complete type.
//////////////////////////////////////////////////////////////////////////
class CSystem;

struct SDefaultValidator : public IValidator
{
	CSystem *m_pSystem;
	SDefaultValidator( CSystem *system ) : m_pSystem(system) {};
	virtual void Report( SValidatorRecord &record );
};

#endif // VALIDATOR_H