#include <IInput.h>
#include <ISystem.h>
#include <IConsole.h>
#include <ITimer.h>
#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>

#include "../CryInput/XActionMapManager.h"

#include <algorithm>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <vector>

#if defined(FARCRY_VITA3K_LAB)
extern void Vita3KLabInputBridge(SceCtrlData *pad);
#endif

namespace
{
struct KeyName
{
	int key;
	const char *name;
};

static const KeyName kKeyNames[] = {
	{XKEY_ESCAPE,"esc"}, {XKEY_RETURN,"return"}, {XKEY_SPACE,"spacebar"},
	{XKEY_TAB,"tab"}, {XKEY_LEFT,"left"}, {XKEY_RIGHT,"right"},
	{XKEY_UP,"up"}, {XKEY_DOWN,"down"}, {XKEY_PAGE_UP,"pageup"},
	{XKEY_PAGE_DOWN,"pagedown"}, {XKEY_LSHIFT,"lshift"},
	{XKEY_LCONTROL,"lctrl"}, {XKEY_MOUSE1,"mouse1"},
	{XKEY_MOUSE2,"mouse2"}, {XKEY_MWHEEL_UP,"mwheelup"},
	{XKEY_MWHEEL_DOWN,"mwheeldown"}, {XKEY_MAXIS_X,"maxisx"},
	{XKEY_MAXIS_Y,"maxisy"},
	{XKEY_0,"0"}, {XKEY_1,"1"}, {XKEY_2,"2"}, {XKEY_3,"3"},
	{XKEY_4,"4"}, {XKEY_5,"5"}, {XKEY_6,"6"}, {XKEY_7,"7"},
	{XKEY_8,"8"}, {XKEY_9,"9"},
	{XKEY_F1,"f1"}, {XKEY_F7,"f7"},
	{XKEY_A,"a"}, {XKEY_B,"b"}, {XKEY_C,"c"}, {XKEY_D,"d"},
	{XKEY_E,"e"}, {XKEY_F,"f"}, {XKEY_G,"g"}, {XKEY_H,"h"},
	{XKEY_I,"i"}, {XKEY_J,"j"}, {XKEY_K,"k"}, {XKEY_L,"l"},
	{XKEY_M,"m"}, {XKEY_N,"n"}, {XKEY_O,"o"}, {XKEY_P,"p"},
	{XKEY_Q,"q"}, {XKEY_R,"r"}, {XKEY_S,"s"}, {XKEY_T,"t"},
	{XKEY_U,"u"}, {XKEY_V,"v"}, {XKEY_W,"w"}, {XKEY_X,"x"},
	{XKEY_Y,"y"}, {XKEY_Z,"z"},
	{0,0}
};

/* Radial [-1,1] stick deflection with the dead zone removed and remaining
   travel rescaled.  A per-axis dead zone leaves diagonal corners outside the
   square even while the stick is physically at rest; radial handling is what
   prevents that motion from becoming camera drift. */
static void StickVector(unsigned char rawX, unsigned char rawY, float deadZone,
	float &outX, float &outY)
{
	// Console variables are writable from scripts/config files.  Keep a bad
	// value from turning the rescale denominator into zero and poisoning every
	// later input event with infinities or NaNs.
	if (deadZone < 0.0f) deadZone = 0.0f;
	if (deadZone > 0.95f) deadZone = 0.95f;
	float x = ((float)rawX - 127.5f) / 127.5f;
	float y = ((float)rawY - 127.5f) / 127.5f;
	const float magnitude = sqrtf(x*x + y*y);
	if (magnitude <= deadZone)
	{
		outX = outY = 0.0f;
		return;
	}
	const float clamped = magnitude > 1.0f ? 1.0f : magnitude;
	const float scaled = (clamped - deadZone) / (1.0f - deadZone);
	outX = (x / magnitude) * scaled;
	outY = (y / magnitude) * scaled;
}

/* Aim rate at full right-stick deflection, expressed in the mouse-count units
   CXActionMapManager::CheckBind feeds to CXClient::TriggerTurnLR, per second.
   The action map multiplies this by the sensitivity slider (0.2 by default,
   exactly as CXMouse::GetDeltaX does), so 3600 reproduces the turn speed this
   port had when it applied a flat 12 counts per frame at 60Hz. */
static const float kDefaultAimRate = 3600.0f;
static const float kDefaultAimCurve = 2.0f;
static const float kDefaultDeadZone = 0.22f;
static const float kDefaultMoveDeadZone = 0.30f;
static const float kDefaultMoveAxisThreshold = 0.16f;
static const float kDefaultGyroRate = 1100.0f;
static const float kDefaultWalkPoint = 0.65f;
// Menu cursor speed in virtual-screen pixels per second.  Deliberately not tied
// to the aim sensitivity: the retail Options slider is about aiming, and a
// cursor that crawls or bolts with it would make the menus unusable.
static const float kCursorPixelsPerSecond = 720.0f;
// Front/rear touch panel extents (the panels report a fixed grid regardless of
// the 960x544 display), used to map a touch to the 800x600 virtual screen.
static const float kTouchWidth = 1920.0f;
static const float kTouchHeight = 1088.0f;

/* Whether the retail UI is on screen and drawing its cursor.  CXGame pushes
   this every frame (see CXGame::Update); it cannot be inferred from the input
   side, because the main menu registers the UI with AddEventListener while
   only in-game overlays use SetExclusiveListener.  Touch drives the pointer
   and the left button exactly when this is true, so a finger resting on the
   screen during play can never fire the weapon. */
static bool s_uiCursorActive = false;

class CVitaInput;

class CVitaKeyboard : public IKeyboard
{
public:
	explicit CVitaKeyboard(CVitaInput *owner) : m_owner(owner) {}
	void ShutDown();
	bool KeyDown(int key);
	bool KeyPressed(int key);
	bool KeyReleased(int key);
	void ClearKey(int key);
	int GetKeyPressedCode();
	const char *GetKeyPressedName();
	int GetKeyDownCode();
	const char *GetKeyDownName();
	void SetExclusive(bool value, void *hwnd = 0);
	void WaitForKey();
	void ClearKeyState();

private:
	CVitaInput *m_owner;
};

class CVitaInput : public IInput, public IMouse
{
public:
	explicit CVitaInput(ISystem *system)
		: m_system(system), m_exclusive(0), m_postEvents(true), m_keyboard(this),
		  m_mouseX(0.0f), m_mouseY(0.0f), m_vscreenX(400.0f),
		  m_vscreenY(300.0f), m_mouseWheel(0.0f), m_sensitivity(0.2f),
		  m_sensitivityScale(1.0f), m_buttons(0), m_prevButtons(0),
		  m_bufferedInput(false), m_modifierConsumed(false), m_pronePrev(false),
		  m_actionMaps(0), m_cvarsRegistered(false),
		  m_aimRate(0), m_aimCurve(0), m_deadZone(0), m_moveDeadZone(0),
		  m_moveAxisThreshold(0), m_analogWalk(0),
		  m_walkPoint(0), m_touchEnabled(0), m_gyroRate(0),
		  m_touchCursor(false), m_prevTouchCursor(false), m_walking(false),
		  m_bSeedPrevFromCurrent(false), m_gyroEnabled(false),
		  m_gyroChordPrev(false), m_gyroFilteredX(0.0f), m_gyroFilteredY(0.0f)
	{
		memset(m_keys, 0, sizeof(m_keys));
		memset(m_prevKeys, 0, sizeof(m_prevKeys));
		sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
		sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
		sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
	}

	void AddEventListener(IInputEventListener *p) { AddUnique(m_listeners, p); }
	void RemoveEventListener(IInputEventListener *p) { Remove(m_listeners, p); }
	void EnableEventPosting(bool enable) { m_postEvents = enable; }
	void AddConsoleEventListener(IInputEventListener *p) { AddUnique(m_consoleListeners, p); }
	void RemoveConsoleEventListener(IInputEventListener *p) { Remove(m_consoleListeners, p); }
	void SetExclusiveListener(IInputEventListener *p) { m_exclusive = p; }
	IInputEventListener *GetExclusiveListener() { return m_exclusive; }

	void Update(bool focus)
	{
		RegisterCVars();

		memcpy(m_prevKeys, m_keys, sizeof(m_keys));
		m_prevButtons = m_buttons;
		memset(m_keys, 0, sizeof(m_keys));
		m_mouseX = m_mouseY = 0.0f;
		m_prevTouchCursor = m_touchCursor;
		if (!focus)
		{
			m_buttons = 0;
			m_touchCursor = false;
			return;
		}

		/* Every rate below is per second, converted here.  The previous code
		   applied fixed per-frame deltas, which made the whole game turn and
		   aim roughly three times faster in a light indoor scene than in dense
		   jungle -- the single biggest control problem on this port. */
		float frameTime = 1.0f / 60.0f;
		if (m_system && m_system->GetITimer())
			frameTime = m_system->GetITimer()->GetFrameTime();
		if (frameTime < 1.0f / 240.0f) frameTime = 1.0f / 240.0f;
		if (frameTime > 1.0f / 15.0f)  frameTime = 1.0f / 15.0f;

		SceCtrlData pad;
		memset(&pad, 0, sizeof(pad));
		/* A failed/non-ready peek leaves the caller's buffer untouched.  Zero is
		   not the centre of a Vita analogue stick: it is full up-left, so the old
		   memset turned a transient sampling miss (most visible while firing in a
		   busy fight) into violent camera drift.  Seed the four axes at centre and
		   only consume real bytes when the API reports a sample. */
		pad.lx = pad.ly = pad.rx = pad.ry = 128;
		const int nPadSamples = sceCtrlPeekBufferPositive(0, &pad, 1);
		if (nPadSamples <= 0)
		{
			/* Keep the previous digital state across a missed sample.  Synthesising
			   a release followed by a press re-fired the Space/Cross SkipCutScene
			   binding several seconds into otherwise healthy cinematics. */
			pad.buttons = m_buttons;
			static unsigned int s_nPadMisses = 0;
			if (++s_nPadMisses == 1 && m_system && m_system->GetILog())
				m_system->GetILog()->LogToFile("\001[VITA][INPUT] controller sample missed; centred analogue fallback active");
		}
#if defined(VITA_DEBUG_AUTOTEST_MENU)
		/* Deterministic Vita3K-only UI verification.  Host keyboard injection
		   into Vita3K is unreliable in the headless runner, so pulse the actual
		   Vita Cross bit once after the retail UISystem has settled.  This still
		   traverses the production Vita mapping, IInput event dispatch, and the
		   stock Lua menu.  The definition is absent from distributable builds. */
		static unsigned int s_menuAutotestFrames = 0;
		++s_menuAutotestFrames;
		if (s_menuAutotestFrames == 150 ||
			s_menuAutotestFrames == 160 ||
			s_menuAutotestFrames == 170)
		{
			pad.buttons |= SCE_CTRL_SELECT;
			sceClibPrintf("[VITA MENU TEST] pulsing Select/Tab at input frame %u\n", s_menuAutotestFrames);
		}
		if (s_menuAutotestFrames == 180)
		{
			pad.buttons |= SCE_CTRL_CROSS;
			sceClibPrintf("[VITA MENU TEST] pulsing Cross at input frame %u\n", s_menuAutotestFrames);
		}
		if (s_menuAutotestFrames >= 210 && s_menuAutotestFrames <= 400 &&
			((s_menuAutotestFrames - 210) % 10) == 0)
			pad.buttons |= SCE_CTRL_SELECT;
#endif
#if defined(VITA_DEBUG_AUTOLOAD_TRAINING) && !defined(FARCRY_VITA3K_LAB)
		/* Vita3K's host keyboard cannot be injected reliably from the headless
		   regression runner.  Autotest builds therefore hold the real Vita left
		   stick forward after startup, through the same mapping used on hardware,
		   so renderer/AI/streaming coverage advances beyond Training's spawn pipe.
		   This definition is absent from distributable builds. */
		static unsigned int s_autotestInputFrames = 0;
		if (++s_autotestInputFrames > 600 && s_autotestInputFrames <= 900)
			pad.ly = 0;
#endif
	#if defined(FARCRY_VITA3K_LAB)
		// Lab input is neutral unless an explicit, expiring request is active.
		// It still passes through the production action maps and physics below.
		Vita3KLabInputBridge(&pad);
	#endif
		m_buttons = pad.buttons;
		const bool gyroChord = (pad.buttons & (SCE_CTRL_TRIANGLE | SCE_CTRL_CIRCLE)) ==
			(SCE_CTRL_TRIANGLE | SCE_CTRL_CIRCLE);
		if (gyroChord && !m_gyroChordPrev)
		{
			if (!m_gyroEnabled)
			{
				m_gyroEnabled = sceMotionStartSampling() >= 0;
				if (m_gyroEnabled)
				{
					sceMotionReset();
					m_gyroFilteredX = m_gyroFilteredY = 0.0f;
				}
			}
			else
			{
				sceMotionStopSampling();
				m_gyroEnabled = false;
				m_gyroFilteredX = m_gyroFilteredY = 0.0f;
			}
			if (m_system && m_system->GetILog())
				m_system->GetILog()->LogToFile("\001[VITA][GYRO] %s Triangle+Circle",
					m_gyroEnabled ? "enabled" : "disabled");
		}
		m_gyroChordPrev = gyroChord;

		const float deadZone = CVarF(m_deadZone, kDefaultDeadZone);
		const float moveDeadZone = CVarF(m_moveDeadZone, kDefaultMoveDeadZone);
		float lx, ly;
		StickVector(pad.lx, pad.ly, moveDeadZone, lx, ly);
		/* Far Cry consumes movement as four digital keys, so the old 0.05
		   threshold turned the first tiny post-dead-zone left/right component
		   into full-speed strafing.  Suppress a minor horizontal component while
		   the player is mainly pushing forward/back, but preserve intentional
		   diagonals and full strafing. */
		const float moveAxisThreshold = CVarF(m_moveAxisThreshold, kDefaultMoveAxisThreshold);
		if (fabsf(lx) < moveAxisThreshold || fabsf(lx) < fabsf(ly) * 0.35f)
			lx = 0.0f;
		if (fabsf(ly) < moveAxisThreshold)
			ly = 0.0f;
		const bool modifier = (pad.buttons & SCE_CTRL_SELECT) != 0;
		const bool modifierChord = modifier && (pad.buttons &
			(SCE_CTRL_CROSS | SCE_CTRL_SQUARE | SCE_CTRL_TRIANGLE |
			 SCE_CTRL_CIRCLE | SCE_CTRL_LEFT | SCE_CTRL_RIGHT |
			 SCE_CTRL_DOWN | SCE_CTRL_START));
		if (!modifier)
			m_modifierConsumed = false;
		else if (modifierChord)
			m_modifierConsumed = true;
		SetKey(XKEY_A, lx < 0.0f); SetKey(XKEY_D, lx > 0.0f);
		SetKey(XKEY_W, ly < 0.0f); SetKey(XKEY_S, ly > 0.0f);

		/* Analog movement.  Far Cry's move actions are digital, but it does
		   model a real walk/run distinction (ACTION_WALK, normally Z), so the
		   left stick's magnitude can drive it: a partly pushed stick walks, a
		   fully pushed one runs.  Suppressed in the "vehicle" action map, where
		   the very same ACTION_WALK means "brake" (CXVehicle::ProcessMovement)
		   and a half-pushed accelerator would fight itself. */
		const float moveMag = sqrtf(lx * lx + ly * ly);
		const float walkPoint = CVarF(m_walkPoint, kDefaultWalkPoint);
		// Hysteresis, so a stick resting near the threshold does not flip between
		// walking and running every frame.
		if (m_walking)
			m_walking = moveMag < walkPoint + 0.08f;
		else
			m_walking = moveMag < walkPoint - 0.08f;
		SetKey(XKEY_Z, CVarI(m_analogWalk, 1) != 0 && moveMag > 0.0f &&
			m_walking && !IsVehicleActionMap());

		const float aimRate = CVarF(m_aimRate, kDefaultAimRate) * frameTime;
		const float aimCurve = CVarF(m_aimCurve, kDefaultAimCurve);
		float rx, ry;
		StickVector(pad.rx, pad.ry, deadZone, rx, ry);
		const float aimMagnitude = sqrtf(rx*rx + ry*ry);
		const float curvedAim = aimMagnitude > 0.0f ? powf(aimMagnitude, aimCurve) : 0.0f;
		m_mouseX = aimMagnitude > 0.0f ? (rx / aimMagnitude) * curvedAim * aimRate : 0.0f;
		m_mouseY = aimMagnitude > 0.0f ? (ry / aimMagnitude) * curvedAim * aimRate : 0.0f;

		/* Motion input is opt-in and only contributes during gameplay.  The small
		   angular dead band rejects sensor bias, while the low-pass filter removes
		   hand tremor without the self-moving camera caused by filtering absolute
		   orientation.  Right-stick aim remains available at the same time. */
		const bool uiOwnsAim = s_uiCursorActive || (m_exclusive != 0);
		if (m_gyroEnabled && !uiOwnsAim)
		{
			SceMotionState motion;
			memset(&motion, 0, sizeof(motion));
			if (sceMotionGetState(&motion) >= 0)
			{
				float targetX = -motion.angularVelocity.y;
				float targetY = -motion.angularVelocity.x;
				if (fabsf(targetX) < 0.025f) targetX = 0.0f;
				if (fabsf(targetY) < 0.025f) targetY = 0.0f;
				const float blend = std::min(1.0f, frameTime * 18.0f);
				m_gyroFilteredX += (targetX - m_gyroFilteredX) * blend;
				m_gyroFilteredY += (targetY - m_gyroFilteredY) * blend;
				const float gyroStep = CVarF(m_gyroRate, kDefaultGyroRate) * frameTime;
				m_mouseX += m_gyroFilteredX * gyroStep;
				m_mouseY += m_gyroFilteredY * gyroStep;
			}
		}

		/* Front touch drives the pointer only while the retail UI is on screen.
		   In gameplay the cursor is invisible and mouse1 is the trigger, so a
		   stray palm on the screen must never fire the weapon. */
		const bool uiActive = s_uiCursorActive || (m_exclusive != 0);
		bool touchClick = false;
		if (CVarI(m_touchEnabled, 1) != 0 && uiActive)
			touchClick = UpdateTouchCursor();
		m_touchCursor = touchClick;
		if (!touchClick)
		{
			const float cursorStep = kCursorPixelsPerSecond * frameTime;
			m_vscreenX = std::max(0.0f, std::min(800.0f,
				m_vscreenX + rx * cursorStep));
			m_vscreenY = std::max(0.0f, std::min(600.0f,
				m_vscreenY + ry * cursorStep));
		}

		/* Space is also bound globally to SkipCutScene.  While the movie system
		   owns controls through player_dead, an ordinary Cross press must not
		   terminate a cinematic; reserve that destructive action for the explicit
		   Select+Start/F7 chord below. */
		const bool cinematicControls = IsPlayerDeadActionMap();
		SetKey(XKEY_SPACE,    !cinematicControls && !modifier && (pad.buttons & SCE_CTRL_CROSS));
		/* Retail UI buttons activate on Return, while gameplay binds jump to
		   Space.  Emit both logical keys for Cross so the same physical button
		   works in the complete menu flow without breaking the stock action map. */
		SetKey(XKEY_RETURN,   !modifier && (pad.buttons & SCE_CTRL_CROSS));
		SetKey(XKEY_R,        !modifier && (pad.buttons & SCE_CTRL_SQUARE));
		SetKey(XKEY_F,        !modifier && !gyroChord && (pad.buttons & SCE_CTRL_TRIANGLE));
		SetKey(XKEY_LCONTROL, !modifier && !gyroChord && (pad.buttons & SCE_CTRL_CIRCLE));
		SetKey(XKEY_ESCAPE,   !modifier && (pad.buttons & SCE_CTRL_START));
		SetKey(XKEY_TAB,      modifier && !m_modifierConsumed);
		SetKey(XKEY_UP,       !modifier && (pad.buttons & SCE_CTRL_UP));
		SetKey(XKEY_DOWN,     !modifier && (pad.buttons & SCE_CTRL_DOWN));
		SetKey(XKEY_LEFT,     !modifier && (pad.buttons & SCE_CTRL_LEFT));
		SetKey(XKEY_RIGHT,    !modifier && (pad.buttons & SCE_CTRL_RIGHT));
		SetKey(XKEY_G,        !modifier && (pad.buttons & SCE_CTRL_LEFT));
		SetKey(XKEY_X,        !modifier && (pad.buttons & SCE_CTRL_RIGHT));

		/* Vita has fewer physical buttons than Far Cry's keyboard layout.  Keep
		   every campaign action reachable with Select as a genuine modifier:
		   Cross sprint, Square flashlight, Triangle binoculars, Circle thermal,
		   D-pad Left cycles grenade type, D-pad Down goes prone, and Start
		   switches between the first and third person camera -- the vehicle
		   action map binds that to F1 only, so it was unreachable here.  The
		   normal button aliases are suppressed while a chord is held, so one
		   press never fires two actions. */
		SetKey(XKEY_LSHIFT, modifier && (pad.buttons & SCE_CTRL_CROSS));
		SetKey(XKEY_L,      modifier && (pad.buttons & SCE_CTRL_SQUARE));
		SetKey(XKEY_B,      modifier && !gyroChord && (pad.buttons & SCE_CTRL_TRIANGLE));
		SetKey(XKEY_T,      modifier && !gyroChord && (pad.buttons & SCE_CTRL_CIRCLE));
		SetKey(XKEY_H,      modifier && (pad.buttons & SCE_CTRL_LEFT));
		/* Drop weapon (J in the retail profile) was the last campaign action
		   with no physical route at all.  Select+D-pad Right was unused. */
		SetKey(XKEY_J,      modifier && (pad.buttons & SCE_CTRL_RIGHT));
		/* Prone is a toggle, not a hold: XPlayer::ProcessActions acts on
		   ACTION_MOVEMODE2 and then removes it from the command the same
		   frame, so a key held down re-triggers it every frame and flips
		   prone on and off continuously -- which looks exactly like the
		   button doing nothing.  Emit one frame on the press edge instead.
		   Crouch is genuinely a hold (it has its own m_bStayCrouch handling),
		   so it stays level-triggered. */
		const bool proneNow = modifier && (pad.buttons & SCE_CTRL_DOWN) != 0;
		SetKey(XKEY_V,      proneNow && !m_pronePrev);
		m_pronePrev = proneNow;
		SetKey(XKEY_F1,     !cinematicControls && modifier && (pad.buttons & SCE_CTRL_START));
		SetKey(XKEY_F7,     cinematicControls && modifier && (pad.buttons & SCE_CTRL_START));

		/* Leaning is the one common FPS action with no button left, and the rear
		   panel is otherwise idle: its left half leans left, its right half
		   leans right.  Gameplay only -- while the UI owns input the panel is
		   ignored so it cannot disturb a menu. */
		if (CVarI(m_touchEnabled, 1) != 0 && !uiActive)
		{
			SceTouchData back;
			memset(&back, 0, sizeof(back));
			if (sceTouchPeek(SCE_TOUCH_PORT_BACK, &back, 1) >= 0)
			{
				bool leanLeft = false, leanRight = false;
				for (unsigned int i = 0; i < back.reportNum; ++i)
				{
					if ((float)back.report[i].x < kTouchWidth * 0.4f) leanLeft = true;
					else if ((float)back.report[i].x > kTouchWidth * 0.6f) leanRight = true;
				}
				SetKey(XKEY_Q, leanLeft);
				SetKey(XKEY_E, leanRight);
			}
		}

		/* ClearKeyState asked for the edges to be swallowed once.  Adopt the
		   state just sampled as the previous state, so a button that was already
		   held when the state was cleared is treated as already consumed and
		   only a real release-then-press counts as a new press. */
		if (m_bSeedPrevFromCurrent)
		{
			m_bSeedPrevFromCurrent = false;
			memcpy(m_prevKeys, m_keys, sizeof(m_keys));
			m_prevButtons = m_buttons;
		}

		for (int key = 1; key < 256; ++key)
		{
			if (m_keys[key] != m_prevKeys[key])
			{
				const bool duplicatePhysicalAlias =
					(key == XKEY_RETURN && m_keys[XKEY_SPACE]) ||
					(key == XKEY_G && m_keys[XKEY_LEFT]) ||
					(key == XKEY_X && m_keys[XKEY_RIGHT]);
				if (m_bufferedInput && m_keys[key] && !duplicatePhysicalAlias && m_bufferedKeys.size() < 32)
					m_bufferedKeys.push_back(key);
				PostKeyEvent(key, m_keys[key]);
			}
		}
	}

	void ShutDown()
	{
		if (m_gyroEnabled)
			sceMotionStopSampling();
		delete this;
	}
	void Shutdown() {}
	void SetMouseExclusive(bool, void * = 0) {}
	void SetKeyboardExclusive(bool, void * = 0) {}
	bool KeyDown(int key) { return ValidKey(key) && m_keys[key]; }
	bool KeyPressed(int key) { return ValidKey(key) && m_keys[key] && !m_prevKeys[key]; }
	bool KeyReleased(int key) { return ValidKey(key) && !m_keys[key] && m_prevKeys[key]; }

	bool MouseDown(int key) { return MouseState(key, false, false); }
	bool MousePressed(int key) { return MouseState(key, true, false); }
	bool MouseDblClick(int) { return false; }
	bool MouseReleased(int key) { return MouseState(key, false, true); }
	/* Scaled exactly like CXMouse::GetDeltaX, so the retail Options sensitivity
	   slider and the sensitivity scale the weapon scripts apply while zoomed
	   (Input:SetSensitivityScale) both work on Vita instead of being ignored. */
	float MouseGetDeltaX() { return m_mouseX * m_sensitivity * m_sensitivityScale; }
	float MouseGetDeltaY() { return m_mouseY * m_sensitivity * m_sensitivityScale; }
	float MouseGetDeltaZ()
	{
		if ((m_buttons & SCE_CTRL_UP) && !(m_prevButtons & SCE_CTRL_UP)) return 1.0f;
		if ((m_buttons & SCE_CTRL_DOWN) && !(m_prevButtons & SCE_CTRL_DOWN)) return -1.0f;
		return 0.0f;
	}
	float MouseGetVScreenX() { return m_vscreenX; }
	float MouseGetVScreenY() { return m_vscreenY; }

	int GetKeyID(const char *name)
	{
		if (!name) return XKEY_NULL;
		for (const KeyName *k = kKeyNames; k->name; ++k)
			if (stricmp(name, k->name) == 0) return k->key;
		return XKEY_NULL;
	}
	void EnableBufferedInput(bool enable)
	{
		m_bufferedInput = enable;
		if (!enable)
			m_bufferedKeys.clear();
	}
	void FeedVirtualKey(int key, long, bool down)
	{
		const bool wasDown = ValidKey(key) && m_keys[key];
		SetKey(key, down);
		if (m_bufferedInput && down && !wasDown && m_bufferedKeys.size() < 32)
			m_bufferedKeys.push_back(key);
	}
	int GetBufferedKey() { return m_bufferedKeys.empty() ? -1 : m_bufferedKeys.front(); }
	const char *GetBufferedKeyName() { return m_bufferedKeys.empty() ? "" : GetKeyName(m_bufferedKeys.front(), 0, true); }
	void PopBufferedKey() { if (!m_bufferedKeys.empty()) m_bufferedKeys.erase(m_bufferedKeys.begin()); }
	void SetMouseInertia(float) {}
	bool JoyButtonPressed(int) { return false; }
	int JoyGetDir() { return 0; }
	int JoyGetHatDir() { return 0; }
	Vec3 JoyGetAnalog1Dir(unsigned int) const { return Vec3(0,0,0); }
	Vec3 JoyGetAnalog2Dir(unsigned int) const { return Vec3(0,0,0); }
	IKeyboard *GetIKeyboard() { return &m_keyboard; }
	IMouse *GetIMouse() { return this; }

	// IMouse: the original UI consumes this lower-level interface directly.
	void SetMouseWheelRotation(int value) { m_mouseWheel = (float)value; }
	bool SetExclusive(bool, void * = 0) { return true; }
	float GetDeltaX() { return MouseGetDeltaX(); }
	float GetDeltaY() { return MouseGetDeltaY(); }
	float GetDeltaZ() { return MouseGetDeltaZ() + m_mouseWheel; }
	void SetInertia(float) {}
	void SetVScreenX(float x) { m_vscreenX = std::max(0.0f, std::min(800.0f, x)); }
	void SetVScreenY(float y) { m_vscreenY = std::max(0.0f, std::min(600.0f, y)); }
	float GetVScreenX() { return m_vscreenX; }
	float GetVScreenY() { return m_vscreenY; }
	// Same 1/100 scale CXMouse uses, so a sensitivity written to game.cfg by a
	// PC install (or by this port) means the same thing on both.
	void SetSensitvity(float value) { m_sensitivity = value > 0.0f ? value / 100.0f : 0.0f; }
	float GetSensitvity() { return m_sensitivity * 100.0f; }
	void SetSensitvityScale(float value) { m_sensitivityScale = value; }
	float GetSensitvityScale() { return m_sensitivityScale; }

	const char *GetKeyName(int key, int = 0, bool gui = false)
	{
		if (gui)
		{
			switch (key)
			{
				case XKEY_SPACE: return "Cross";
				case XKEY_RETURN: return "Cross";
				case XKEY_R: return "Square";
				case XKEY_F: return "Triangle";
				case XKEY_G: return "D-Pad Left";
				case XKEY_X: return "D-Pad Right";
				case XKEY_LCONTROL: return "Circle";
				case XKEY_ESCAPE: return "Start";
				case XKEY_TAB: return "Select";
				case XKEY_LSHIFT: return "Select + Cross";
				case XKEY_L: return "Select + Square";
				case XKEY_B: return "Select + Triangle";
				case XKEY_T: return "Select + Circle";
				case XKEY_H: return "Select + D-Pad Left";
				case XKEY_J: return "Select + D-Pad Right";
				case XKEY_V: return "Select + D-Pad Down";
				case XKEY_F1: return "Select + Start";
				case XKEY_Z: return "Left Stick (partial)";
				case XKEY_Q: return "Rear Touch Left";
				case XKEY_E: return "Rear Touch Right";
				case XKEY_W: case XKEY_A: case XKEY_S: case XKEY_D: return "Left Stick";
				case XKEY_MAXIS_X: case XKEY_MAXIS_Y: return "Right Stick";
				case XKEY_MOUSE1: return "R";
				case XKEY_MOUSE2: return "L";
				case XKEY_MWHEEL_UP: return "D-Pad Up";
				case XKEY_MWHEEL_DOWN: return "D-Pad Down";
				case XKEY_UP: return "D-Pad Up";
				case XKEY_DOWN: return "D-Pad Down";
				case XKEY_LEFT: return "D-Pad Left";
				case XKEY_RIGHT: return "D-Pad Right";
			}
		}
		for (const KeyName *k = kKeyNames; k->name; ++k)
			if (k->key == key) return k->name;
		return "";
	}
	bool GetOSKeyName(int key, wchar_t *out, int size)
	{
		if (!out || size <= 0) return false;
		/* CStringTableMgr uses this to build the @control<ID> labels shown by
		   the retail Control Options screen. Expose Vita-facing button names,
		   while action-map serialization continues to use the stable PC tokens. */
		const char *name = GetKeyName(key, 0, true);
		int i = 0;
		for (; name[i] && i + 1 < size; ++i) out[i] = (unsigned char)name[i];
		out[i] = 0;
		return i != 0;
	}
	int GetKeyPressedCode()
	{
		for (int i = 1; i < 256; ++i) if (KeyPressed(i)) return i;
		return XKEY_NULL;
	}
	const char *GetKeyPressedName() { return GetKeyName(GetKeyPressedCode()); }
	int GetKeyDownCode()
	{
		for (int i = 1; i < 256; ++i) if (KeyDown(i)) return i;
		return XKEY_NULL;
	}
	const char *GetKeyDownName() { return GetKeyName(GetKeyDownCode()); }
	void WaitForKey() {}
	IActionMapManager *CreateActionMapManager()
	{
		m_actionMaps = new CXActionMapManager(this);
		return m_actionMaps;
	}
	const char *GetXKeyPressedName() { return GetKeyPressedName(); }
	void ClearKeyState()
	{
		memset(m_keys, 0, sizeof(m_keys));
		memset(m_prevKeys, 0, sizeof(m_prevKeys));
		m_buttons = m_prevButtons = 0;
		/* Zeroing both halves is not enough on its own.  A button that is still
		   physically held reappears on the very next poll with its previous
		   state now zero, which reads as a brand new press -- so the keypress
		   this call exists to discard comes straight back as a fresh edge.
		   That is what killed cut scenes: BeginCutScene clears the key state
		   precisely so the press that triggered the scene cannot also skip it,
		   and one poll later that same held button produced a new press edge
		   and fired the SkipCutScene command bound to it.  The device log shows
		   the result exactly -- "stopping 'first_cutscene' at t=0.00 ... ended
		   early by something else".
		   Suppress edges for one poll so anything already down is treated as
		   already consumed, and only a genuine release-and-press counts. */
		m_bSeedPrevFromCurrent = true;
	}
	unsigned char GetKeyState(int key) { return KeyDown(key) ? 0x80 : 0; }

private:
	template<class T> static void AddUnique(std::vector<T*> &v, T *p)
	{
		if (p && std::find(v.begin(), v.end(), p) == v.end()) v.push_back(p);
	}
	template<class T> static void Remove(std::vector<T*> &v, T *p)
	{
		v.erase(std::remove(v.begin(), v.end(), p), v.end());
	}
	static bool ValidKey(int key) { return key > 0 && key < 256; }
	void SetKey(int key, bool down) { if (ValidKey(key)) m_keys[key] = down; }

	/* Console variables are created on the first Update rather than in the
	   constructor: CSystem builds the input system before the console exists. */
	void RegisterCVars()
	{
		if (m_cvarsRegistered || !m_system || !m_system->GetIConsole())
			return;
		m_cvarsRegistered = true;
		IConsole *pConsole = m_system->GetIConsole();
		m_aimRate = pConsole->CreateVariable("i_vita_aim_rate", "3600", 0,
			"Right stick turn rate, in mouse counts per second at full deflection");
		m_aimCurve = pConsole->CreateVariable("i_vita_aim_curve", "2", 0,
			"Right stick response exponent; 1 is linear, higher is finer near centre");
		m_deadZone = pConsole->CreateVariable("i_vita_deadzone", "0.22", 0,
			"Right-stick dead zone, 0 to 1");
		m_moveDeadZone = pConsole->CreateVariable("i_vita_move_deadzone", "0.30", 0,
			"Left-stick radial dead zone, 0 to 1");
		m_moveAxisThreshold = pConsole->CreateVariable("i_vita_move_axis_threshold", "0.16", 0,
			"Minimum post-dead-zone left-stick axis before a digital move key is pressed");
		m_analogWalk = pConsole->CreateVariable("i_vita_analog_walk", "1", 0,
			"Partly pushed left stick walks instead of running");
		m_walkPoint = pConsole->CreateVariable("i_vita_walk_point", "0.65", 0,
			"Left stick deflection above which the player runs");
		m_touchEnabled = pConsole->CreateVariable("i_vita_touch", "1", 0,
			"Front touch drives the menu pointer, rear touch leans");
		m_gyroRate = pConsole->CreateVariable("i_vita_gyro_rate", "1100", 0,
			"Gyro aim sensitivity in mouse counts per radian");
	}

	static float CVarF(ICVar *pVar, float fallback) { return pVar ? pVar->GetFVal() : fallback; }
	static int CVarI(ICVar *pVar, int fallback) { return pVar ? pVar->GetIVal() : fallback; }

	bool IsVehicleActionMap() const
	{
		return m_actionMaps &&
			strcmp(m_actionMaps->GetCurrentActionMapName(), "vehicle") == 0;
	}

	bool IsPlayerDeadActionMap() const
	{
		return m_actionMaps &&
			strcmp(m_actionMaps->GetCurrentActionMapName(), "player_dead") == 0;
	}

	//! Places the virtual cursor under the finger and reports whether the panel
	//! is being touched, which the UI consumes as a held left mouse button.
	bool UpdateTouchCursor()
	{
		SceTouchData front;
		memset(&front, 0, sizeof(front));
		if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &front, 1) < 0 || front.reportNum == 0)
			return false;
		m_vscreenX = std::max(0.0f, std::min(800.0f,
			(float)front.report[0].x * (800.0f / kTouchWidth)));
		m_vscreenY = std::max(0.0f, std::min(600.0f,
			(float)front.report[0].y * (600.0f / kTouchHeight)));
		return true;
	}

	bool MouseState(int key, bool pressed, bool released) const
	{
		bool now = false, before = false;
		if (key == XKEY_MOUSE1)
		{
			now = (m_buttons & SCE_CTRL_RTRIGGER) != 0 || m_touchCursor;
			before = (m_prevButtons & SCE_CTRL_RTRIGGER) != 0 || m_prevTouchCursor;
		}
		else if (key == XKEY_MOUSE2) { now = m_buttons & SCE_CTRL_LTRIGGER; before = m_prevButtons & SCE_CTRL_LTRIGGER; }
		else if (key == XKEY_MWHEEL_UP) { now = m_buttons & SCE_CTRL_UP; before = m_prevButtons & SCE_CTRL_UP; }
		else if (key == XKEY_MWHEEL_DOWN) { now = m_buttons & SCE_CTRL_DOWN; before = m_prevButtons & SCE_CTRL_DOWN; }
		else if (key == XKEY_MAXIS_X) { if (released) return false; return fabsf(m_mouseX) > 0.001f; }
		else if (key == XKEY_MAXIS_Y) { if (released) return false; return fabsf(m_mouseY) > 0.001f; }
		if (pressed) return now && !before;
		if (released) return !now && before;
		return now;
	}

	void PostKeyEvent(int key, bool down)
	{
		if (!m_postEvents && m_consoleListeners.empty()) return;
		SInputEvent event;
		event.type = down ? SInputEvent::KEY_PRESS : SInputEvent::KEY_RELEASE;
		event.key = key;
		event.keyname = GetKeyName(key);
		event.timestamp = m_system && m_system->GetITimer()
			? (unsigned int)(m_system->GetITimer()->GetCurrTime() * 1000.0f) : 0;
		for (size_t i = 0; i < m_consoleListeners.size(); ++i)
			if (m_consoleListeners[i]->OnInputEvent(event)) return;
		if (!m_postEvents) return;
		if (m_exclusive) { m_exclusive->OnInputEvent(event); return; }
		for (size_t i = 0; i < m_listeners.size(); ++i)
			if (m_listeners[i]->OnInputEvent(event)) return;
	}

	ISystem *m_system;
	IInputEventListener *m_exclusive;
	std::vector<IInputEventListener*> m_listeners;
	std::vector<IInputEventListener*> m_consoleListeners;
	bool m_postEvents;
	CVitaKeyboard m_keyboard;
	bool m_keys[256];
	bool m_prevKeys[256];
	// Set by ClearKeyState: seed prev state from the current physical state on
	// the next poll so held buttons do not produce a spurious press edge.
	bool m_bSeedPrevFromCurrent;
	float m_mouseX, m_mouseY;
	float m_vscreenX, m_vscreenY;
	float m_mouseWheel, m_sensitivity, m_sensitivityScale;
	unsigned int m_buttons, m_prevButtons;
	bool m_bufferedInput;
	bool m_modifierConsumed;
	bool m_pronePrev;
	std::vector<int> m_bufferedKeys;
	CXActionMapManager *m_actionMaps;
	bool m_cvarsRegistered;
	ICVar *m_aimRate;
	ICVar *m_aimCurve;
	ICVar *m_deadZone;
	ICVar *m_moveDeadZone;
	ICVar *m_moveAxisThreshold;
	ICVar *m_analogWalk;
	ICVar *m_walkPoint;
	ICVar *m_touchEnabled;
	ICVar *m_gyroRate;
	bool m_touchCursor;
	bool m_prevTouchCursor;
	bool m_walking;
	bool m_gyroEnabled;
	bool m_gyroChordPrev;
	float m_gyroFilteredX;
	float m_gyroFilteredY;
};

void CVitaKeyboard::ShutDown() {}
bool CVitaKeyboard::KeyDown(int key) { return m_owner->KeyDown(key); }
bool CVitaKeyboard::KeyPressed(int key) { return m_owner->KeyPressed(key); }
bool CVitaKeyboard::KeyReleased(int key) { return m_owner->KeyReleased(key); }
void CVitaKeyboard::ClearKey(int key) { m_owner->FeedVirtualKey(key, 0, false); }
int CVitaKeyboard::GetKeyPressedCode() { return m_owner->GetKeyPressedCode(); }
const char *CVitaKeyboard::GetKeyPressedName() { return m_owner->GetKeyPressedName(); }
int CVitaKeyboard::GetKeyDownCode() { return m_owner->GetKeyDownCode(); }
const char *CVitaKeyboard::GetKeyDownName() { return m_owner->GetKeyDownName(); }
void CVitaKeyboard::SetExclusive(bool value, void *hwnd) { m_owner->SetKeyboardExclusive(value, hwnd); }
void CVitaKeyboard::WaitForKey() { m_owner->WaitForKey(); }
void CVitaKeyboard::ClearKeyState() { m_owner->ClearKeyState(); }
}

IInput *CreateVitaInput(ISystem *system)
{
	return new CVitaInput(system);
}

//! Called once per frame by CXGame::Update with the retail UI's own visibility
//! condition, so the touch pointer follows the cursor the player can actually see.
extern "C" void Vita_SetUICursorActive(int active)
{
	s_uiCursorActive = active != 0;
}
