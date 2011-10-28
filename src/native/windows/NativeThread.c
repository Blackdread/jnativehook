/* JNativeHook: Global keyboard and mouse hooking for Java.
 * Copyright (C) 2006-2011 Alexander Barker.  All Rights Received.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <w32api.h>
#define WINVER Windows2000
#define _WIN32_WINNT WINVER
#include <windows.h>

#include "JNativeHook.h"
#include "JConvertToNative.h"
#include "NativeThread.h"
#include "WinKeyCodes.h"

//The handle to the DLL module pulled in DllMain on DLL_PROCESS_ATTACH
extern HINSTANCE hInst;

//Thread and hook handles.
static DWORD hookThreadId = 0;
static HANDLE hookThreadHandle = NULL, hookEventHandle = NULL;
static HHOOK handleKeyboardHook = NULL, handleMouseHook = NULL;

//GlobalScreen object and dispatch id.
static jobject objGlobalScreen = NULL;
static jmethodID idDispatchEvent;

//Java callback classes and constructor id's
static jclass clsKeyEvent, clsMouseEvent, clsMouseWheelEvent;
static jmethodID idKeyEvent, idMouseButtonEvent, idMouseMotionEvent;

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
	//We should already be attached to the JVM at this point.  This should only
	//be a formality that causes a NOOP.
	JNIEnv * env = NULL;
	if ((*jvm)->AttachCurrentThread(jvm, (void **)(&env), NULL) == JNI_OK) {
		//MS Keyboard Event Struct Data
		KBDLLHOOKSTRUCT * kbhook = (KBDLLHOOKSTRUCT *) lParam;

		//Java Event Data
		JKeyDatum jkey;
		jint modifiers;

		//Jave Key Event Object.
		jobject objKeyEvent;

		switch(wParam) {
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				#ifdef DEBUG
					fprintf(stdout, "LowLevelKeyboardProc: Key pressed (%i)\n", (unsigned int) kbhook->vkCode);
				#endif

				//Check and setup modifiers
				if (kbhook->vkCode == VK_LSHIFT)		setModifierMask(MOD_LSHIFT);
				else if (kbhook->vkCode == VK_RSHIFT)	setModifierMask(MOD_RSHIFT);
				else if (kbhook->vkCode == VK_LCONTROL)	setModifierMask(MOD_LCONTROL);
				else if (kbhook->vkCode == VK_RCONTROL)	setModifierMask(MOD_RCONTROL);
				else if (kbhook->vkCode == VK_LMENU)	setModifierMask(MOD_LALT);
				else if (kbhook->vkCode == VK_RMENU)	setModifierMask(MOD_RALT);
				else if (kbhook->vkCode == VK_LWIN)		setModifierMask(MOD_LWIN);
				else if (kbhook->vkCode == VK_RWIN)		setModifierMask(MOD_RWIN);

				jkey = NativeToJKey(kbhook->vkCode);
				modifiers = getModifiers();

				//Fire key pressed event.
				objKeyEvent = (*env)->NewObject(env, clsKeyEvent, idKeyEvent, JK_NATIVE_KEY_PRESSED, (jlong) kbhook->time, modifiers, kbhook->scanCode, jkey.keycode, jkey.location);
				(*env)->CallVoidMethod(env, objGlobalScreen, idDispatchEvent, objKeyEvent);
			break;

			case WM_KEYUP:
			case WM_SYSKEYUP:
				#ifdef DEBUG
					fprintf(stdout, "LowLevelKeyboardProc: Key released (%i)\n", (unsigned int) kbhook->vkCode);
				#endif

				//Check and setup modifiers
				if (kbhook->vkCode == VK_LSHIFT)		unsetModifierMask(MOD_LSHIFT);
				else if (kbhook->vkCode == VK_RSHIFT)	unsetModifierMask(MOD_RSHIFT);
				else if (kbhook->vkCode == VK_LCONTROL)	unsetModifierMask(MOD_LCONTROL);
				else if (kbhook->vkCode == VK_RCONTROL)	unsetModifierMask(MOD_RCONTROL);
				else if (kbhook->vkCode == VK_LMENU)	unsetModifierMask(MOD_LALT);
				else if (kbhook->vkCode == VK_RMENU)	unsetModifierMask(MOD_RALT);
				else if (kbhook->vkCode == VK_LWIN)		unsetModifierMask(MOD_LWIN);
				else if (kbhook->vkCode == VK_RWIN)		unsetModifierMask(MOD_RWIN);

				jkey = NativeToJKey(kbhook->vkCode);
				modifiers = getModifiers();

				//Fire key released event.
				objKeyEvent = (*env)->NewObject(env, clsKeyEvent, idKeyEvent, JK_NATIVE_KEY_RELEASED, (jlong) kbhook->time, modifiers, kbhook->scanCode, jkey.keycode, jkey.location);
				(*env)->CallVoidMethod(env, objGlobalScreen, idDispatchEvent, objKeyEvent);
			break;
		}

		//Handle any possible JNI issue that may have occured.
		if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
			#ifdef DEBUG
				fprintf(stderr, "LowLevelKeyboardProc: JNI error occurred!\n");
				(*env)->ExceptionDescribe(env);
			#endif
			(*env)->ExceptionClear(env);
		}
	}

	return CallNextHookEx(handleKeyboardHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
	//We should already be attached to the JVM at this point.  This should only
	//be a formality that causes a NOOP.
	JNIEnv * env = NULL;
	if ((*jvm)->AttachCurrentThread(jvm, (void **)(&env), NULL) == JNI_OK) {
		//MS Mouse Event Struct Data
		MSLLHOOKSTRUCT * mshook = (MSLLHOOKSTRUCT *) lParam;

		//Java Event Data
		jint jbutton;
		jint modifiers;

		//Java Mouse Event Object.
		jobject objMouseEvent;

		switch(wParam) {
			case WM_LBUTTONDOWN:
				jbutton = NativeToJButton(VK_LBUTTON);
				setModifierMask(MOD_LBUTTON);
			goto BUTTONDOWN;

			case WM_RBUTTONDOWN:
				jbutton = NativeToJButton(VK_RBUTTON);
				setModifierMask(MOD_RBUTTON);
			goto BUTTONDOWN;

			case WM_MBUTTONDOWN:
				jbutton = NativeToJButton(VK_MBUTTON);
				setModifierMask(MOD_MBUTTON);
			goto BUTTONDOWN;

			case WM_XBUTTONDOWN:
			case WM_NCXBUTTONDOWN:
				if (HIWORD(mshook->mouseData) == XBUTTON1) {
					jbutton = NativeToJButton(VK_XBUTTON1);
					setModifierMask(MOD_XBUTTON1);
				}
				else if (HIWORD(mshook->mouseData) == XBUTTON2) {
					jbutton = NativeToJButton(VK_XBUTTON2);
					setModifierMask(MOD_XBUTTON2);
				}
				else {
					jbutton = NativeToJButton(HIWORD(mshook->mouseData));
				}

			BUTTONDOWN:
				#ifdef DEBUG
					fprintf(stdout, "LowLevelMouseProc: Button pressed (%i)\n", (int) jbutton);
				#endif

				modifiers = getModifiers();

				//Fire mouse pressed event.
				objMouseEvent = (*env)->NewObject(env, clsMouseEvent, idMouseButtonEvent, JK_NATIVE_MOUSE_PRESSED, (jlong) mshook->time, modifiers, (jint) mshook->pt.x, (jint) mshook->pt.y, jbutton);
				(*env)->CallVoidMethod(env, objGlobalScreen, idDispatchEvent, objMouseEvent);
			break;

			case WM_LBUTTONUP:
				jbutton = NativeToJButton(VK_LBUTTON);
				unsetModifierMask(MOD_LBUTTON);
			goto BUTTONUP;

			case WM_RBUTTONUP:
				jbutton = NativeToJButton(VK_RBUTTON);
				unsetModifierMask(MOD_RBUTTON);
			goto BUTTONUP;

			case WM_MBUTTONUP:
				jbutton = NativeToJButton(VK_MBUTTON);
				unsetModifierMask(MOD_MBUTTON);
			goto BUTTONUP;

			case WM_XBUTTONUP:
			case WM_NCXBUTTONUP:
				if (HIWORD(mshook->mouseData) == XBUTTON1) {
					jbutton = NativeToJButton(VK_XBUTTON1);
					unsetModifierMask(MOD_XBUTTON1);
				}
				else if (HIWORD(mshook->mouseData) == XBUTTON2) {
					jbutton = NativeToJButton(VK_XBUTTON2);
					unsetModifierMask(MOD_XBUTTON2);
				}
				else {
					jbutton = NativeToJButton(HIWORD(mshook->mouseData));
				}

			BUTTONUP:
				#ifdef DEBUG
					fprintf(stdout, "LowLevelMouseProc: Button released (%i)\n", (int) jbutton);
				#endif

				modifiers = getModifiers();

				//Fire mouse released event.
				objMouseEvent = (*env)->NewObject(env, clsMouseEvent, idMouseButtonEvent, JK_NATIVE_MOUSE_RELEASED, (jlong) mshook->time, modifiers, (jint) mshook->pt.x, (jint) mshook->pt.y, jbutton);
				(*env)->CallVoidMethod(env, objGlobalScreen, idDispatchEvent, objMouseEvent);
			break;

			case WM_MOUSEMOVE:
				#ifdef DEBUG
					fprintf(stdout, "LowLevelMouseProc: Motion Notified (%li, %li)\n", mshook->pt.x, mshook->pt.y);
				#endif

				modifiers = getModifiers();

				//Fire mouse moved event.
				objMouseEvent = (*env)->NewObject(env, clsMouseEvent, idMouseMotionEvent, JK_NATIVE_MOUSE_MOVED, (jlong) mshook->time, modifiers, (jint) mshook->pt.x, (jint) mshook->pt.y);
				(*env)->CallVoidMethod(env, objGlobalScreen, idDispatchEvent, objMouseEvent);
			break;

			case WM_MOUSEWHEEL:
				#ifdef DEBUG
					fprintf(stdout, "LowLevelMouseProc: WM_MOUSEWHEEL (%i / %i)\n", HIWORD(mshook->mouseData), WHEEL_DELTA);
				#endif

				modifiers = getModifiers();

				//Delta HIWORD(mshook->mouseData)
				//A positive value indicates that the wheel was rotated forward, away from the user; a negative
				//value indicates that the wheel was rotated backward, toward the user. One wheel click is
				//defined as WHEEL_DELTA, which is 120.


				/*

	if (platformLines == WHEEL_PAGESCROLL) {
		scrollType = java_awt_event_MouseWheelEvent_WHEEL_BLOCK_SCROLL;
		scrollLines = 1;
	}
	else {
		scrollType = java_awt_event_MouseWheelEvent_WHEEL_UNIT_SCROLL;
		scrollLines = platformLines;
	}


	UINT GetLinesToScrollUserSetting() {
		UINT userSetting;

		// Retrieve the lines-to-scroll user setting.
		BOOL success = SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &userSetting, 0);

		//Use a default value if the API failed.
		if (success == FALSE) {
			userSetting = DEFAULT_LINES_TO_SCROLL;
		}

		return userSetting;
	}

	UINT GetCharsToScrollUserSetting() {
		UINT userSetting;

		// Retrieve the characters-to-scroll user setting.
		BOOL success = SystemParametersInfo(SPI_GETWHEELSCROLLCHARS, 0, &userSetting, 0);

		// If SystemParametersInfo fails assign the correct value to
		// user setting based on the OS version.
		if(success == FALSE) {
			//Get the OS version.
			OSVERSIONINFO version = { 0 };
			version.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
			GetVersionEx(&version);

			if (version.dwPlatformId == VER_PLATFORM_WIN32_NT && version.dwMajorVersion < 6 ) {
				// On Windows 2000 and Windows XP set the value to 1 in
				// order to work correctly with IntelliType Pro and
				// IntelliPoint emulating the WM_MOUSEWHEEL message.
				userSetting = DEFAULT_CHARS_TO_SCROLL_WITH_EMULATION;
			}
			else {
				// On Longhorn and above use the default value of 3.
				userSetting = DEFAULT_CHARS_TO_SCROLL;
			}
		}

		return userSetting;
	}



			// Settings change message notifies us when change is made
			// through the SystemParametersInfo() API
			case WM_SETTINGCHANGE:
				return OnSettingChange( (UINT)wParam );
				 */

			default:
				#ifdef DEBUG
					fprintf(stdout, "Unhandled mouse event (%X).\n", (unsigned int) wParam);
				#endif
			break;
		}

		//Handle any possible JNI issue that may have occured.
		if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
			#ifdef DEBUG
				fprintf(stderr, "LowLevelMouseProc: JNI error occurred!\n");
				(*env)->ExceptionDescribe(env);
			#endif
			(*env)->ExceptionClear(env);
		}
	}

	return CallNextHookEx(handleMouseHook, nCode, wParam, lParam);
}

//
// Handle the WM_SETTINGCHANGE message and cache changes to
// SPI_GETWHEELSCROLLLINES and SPI_GETWHEELSCROLLCHARS which indicate
// the amount to scroll when a scrolling message is handled.
//
/*
static LRESULT CALLBACK SettingChangeProc(UINT setting) {
	if (setting == SPI_SETWHEELSCROLLLINES) {
		linesToScrollUserSetting = GetLinesToScrollUserSetting();
	}
	else if (setting == SPI_SETWHEELSCROLLCHARS) {
		charsToScrollUserSetting = GetCharsToScrollUserSetting();
	}

	return 0;
}
*/

static void CreateJNIGlobals(JNIEnv * env) {
	//Class and getInstance method id for the GlobalScreen Object
	jclass clsGlobalScreen = (*env)->FindClass(env, "org/jnativehook/GlobalScreen");
	jmethodID getInstance_ID = (*env)->GetStaticMethodID(env, clsGlobalScreen, "getInstance", "()Lorg/jnativehook/GlobalScreen;");

	//A reference to the GlobalScreen Object
	jobject objScreen = (*env)->CallStaticObjectMethod(env, clsGlobalScreen, getInstance_ID);
	objGlobalScreen = (*env)->NewGlobalRef(env, objScreen);

	//Get the ID of the GlobalScreen Objects dispatch event method.
	idDispatchEvent = (*env)->GetMethodID(env, clsGlobalScreen, "dispatchEvent", "(Lorg/jnativehook/NativeInputEvent;)V");

	//Class and Constructor for the NativeKeyEvent Object
	jclass clsLocalKeyEvent = (*env)->FindClass(env, "org/jnativehook/keyboard/NativeKeyEvent");
	clsKeyEvent = (*env)->NewGlobalRef(env, clsLocalKeyEvent);
	idKeyEvent = (*env)->GetMethodID(env, clsKeyEvent, "<init>", "(IJIIII)V");

	//Class and Constructor for the NativeMouseEvent Object
	jclass clsLocalMouseEvent = (*env)->FindClass(env, "org/jnativehook/mouse/NativeMouseEvent");
	clsMouseEvent = (*env)->NewGlobalRef(env, clsLocalMouseEvent);
	idMouseButtonEvent = (*env)->GetMethodID(env, clsMouseEvent, "<init>", "(IJIIII)V");
	idMouseMotionEvent = (*env)->GetMethodID(env, clsMouseEvent, "<init>", "(IJIII)V");

	//Class and Constructor for the NativeMouseWheelEvent Object
	jclass clsLocalMouseWheelEvent = (*env)->FindClass(env, "org/jnativehook/mouse/NativeMouseWheelEvent");
	clsMouseWheelEvent = (*env)->NewGlobalRef(env, clsLocalMouseWheelEvent);
	//FIXME need to change the constructor signature
	//idMouseWheelEvent = (*env)->GetMethodID(env, clsMouseEvent, "<init>", "(IJIII)V");
}

static void DestroyJNIGlobals(JNIEnv * env) {
	(*env)->DeleteGlobalRef(env, clsKeyEvent);
	(*env)->DeleteGlobalRef(env, clsMouseEvent);
	(*env)->DeleteGlobalRef(env, clsMouseWheelEvent);
	(*env)->DeleteGlobalRef(env, objGlobalScreen);
}

static DWORD WINAPI ThreadProc(LPVOID UNUSED(lpParameter)) {
	DWORD status = EXIT_FAILURE;

	//Attach the current thread to the JVM.
	JNIEnv * env = NULL;
	if ((*jvm)->AttachCurrentThread(jvm, (void **)(&env), NULL) == JNI_OK) {
		//Create all the global references up front to save time in the callbacks.
		CreateJNIGlobals(env);

		//Create the native hooks.
		handleKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInst, 0);
		#ifdef DEBUG
		if (handleKeyboardHook != NULL) {
			fprintf(stdout, "SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInst, 0) successful.\n");
		}
		else {
			fprintf(stderr, "SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInst, 0) failed!\n");
		}
		#endif

		handleMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInst, 0);
		#ifdef DEBUG
		if (handleMouseHook != NULL) {
			fprintf(stdout, "SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInst, 0) successful.\n");
		}
		else {
			fprintf(stderr, "SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInst, 0) failed!\n");
		}
		#endif


		//If we did not encounter a problem, start processing events.
		if (handleKeyboardHook != NULL && handleMouseHook != NULL) {
			//Set the exit status.
			status = EXIT_SUCCESS;

			//Signal that we have passed the thread initialization.
			SetEvent(hookEventHandle);

			//Block until the thread receives an WM_QUIT request.
			//Blocking will occur even if SetWindowsHookEx fails.
			MSG message;
			while (GetMessage(&message, (HWND) -1, 0, 0 ) > 0) {
				TranslateMessage(&message);
				DispatchMessage(&message);
			}
		}


		//Destroy the native hooks.
		if (handleKeyboardHook != NULL) {
			UnhookWindowsHookEx(handleKeyboardHook);
			handleKeyboardHook = NULL;
		}

		if (handleMouseHook != NULL) {
			UnhookWindowsHookEx(handleMouseHook);
			handleMouseHook = NULL;
		}

		//Make sure we clean up the global objects.
		DestroyJNIGlobals(env);

		//Dettach the current thread to the JVM.
		if ((*jvm)->DetachCurrentThread(jvm) != JNI_OK) {
			#ifdef DEBUG
				fprintf(stderr, "DetachCurrentThread(jvm, (void **)(&env), NULL) failed!\n");
			#endif
		}
	}
	else {
		//We cant do a whole lot of anythign if we cant attach to the current thread.
		#ifdef DEBUG
			fprintf(stderr, "AttachCurrentThread(jvm, (void **)(&env), NULL) failed!\n");
		#endif
	}

	#ifdef DEBUG
		fprintf(stdout, "ThreadProc() has completed.\n");
	#endif

	//Make sure we signal that we have passed any exception throwing code.
	//This should only make a difference if we had an initialization exception.
	SetEvent(hookEventHandle);

	ExitThread(status);
}

DWORD GetThreadStatus() {
	DWORD status;
	GetExitCodeThread(hookThreadHandle, &status);

	return status;
}

int StartNativeThread() {
	int status = EXIT_SUCCESS;

	//Make sure the native thread is not already running.
	if (IsNativeThreadRunning() != true) {
		//Create event handle for the thread hook.
		hookEventHandle = CreateEvent(NULL, TRUE, FALSE, "hookEventHandle");

		LPTHREAD_START_ROUTINE lpStartAddress = &ThreadProc;
		hookThreadHandle = CreateThread(NULL, 0, lpStartAddress, NULL, 0, &hookThreadId);
		if (hookThreadHandle != INVALID_HANDLE_VALUE) {
			#ifdef DEBUG
				fprintf(stdout, "ThreadProc() has started.\n");
			#endif

			//Wait for any possible thread exceptions to get thrown into the queue.
			WaitForSingleObject(hookEventHandle, INFINITE);

			//TODO Set the reutrn status to the thread exit code.
			if (IsNativeThreadRunning()) {
				#ifdef DEBUG
					fprintf(stdout, "ThreadProc() initialization successful.\n");
				#endif
			}
			else {
				#ifdef DEBUG
					fprintf(stderr, "ThreadProc() initialization failure!\n");
				#endif

				status = EXIT_FAILURE;
			}
		}
		else {
			#ifdef DEBUG
				fprintf(stderr, "ThreadProc() start failure!\n");
			#endif

			status = EXIT_FAILURE;
		}
	}

	return status;
}

int StopNativeThread() {
	int status = EXIT_SUCCESS;

	if (IsNativeThreadRunning() == true) {
		//Try to exit the thread naturally.
		PostThreadMessage(hookThreadId, WM_QUIT, (WPARAM) NULL, (LPARAM) NULL);
		WaitForSingleObject(hookThreadHandle, 5000);

		if (CloseHandle(hookThreadHandle)) {
			#ifdef DEBUG
				fprintf(stdout, "CloseHandle successful.\n");
			#endif

			hookThreadHandle = NULL;
		}
		else {
			#ifdef DEBUG
				fprintf(stderr, "CloseHandle failure!\n");
			#endif

			status = EXIT_FAILURE;
		}

		CloseHandle(hookEventHandle);
	}

	return status;
}

bool IsNativeThreadRunning() {
	return GetThreadStatus() == STILL_ACTIVE;
}
