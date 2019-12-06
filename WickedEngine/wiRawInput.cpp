#include "wiRawInput.h"

#if defined(_WIN32) && !defined(WINSTORE_SUPPORT)

#include <vector>
#include <string>

#define NOMINMAX
#include <windows.h>
#include <hidsdi.h>

#pragma comment(lib,"Hid.lib")

namespace wiRawInput
{
	wiInput::KeyboardState keyboard;
	wiInput::MouseState mouse;

	struct Internal_ControllerState
	{
		HANDLE handle = NULL;
		bool is_xinput = false;
		std::wstring name;
		wiInput::ControllerState state;
	};
	std::vector<Internal_ControllerState> controllers;

	void Initialize()
	{
		RAWINPUTDEVICE Rid[4] = {};

		// Register mouse:
		Rid[0].usUsagePage = 0x01;
		Rid[0].usUsage = 0x02;
		Rid[0].dwFlags = 0;
		Rid[0].hwndTarget = 0;

		// Register keyboard:
		Rid[1].usUsagePage = 0x01;
		Rid[1].usUsage = 0x06;
		Rid[1].dwFlags = 0;
		Rid[1].hwndTarget = 0; 
		
		// Register gamepad:
		Rid[2].usUsagePage = 0x01;
		Rid[2].usUsage = 0x05;
		Rid[2].dwFlags = 0;
		Rid[2].hwndTarget = 0;

		// Register joystick:
		Rid[3].usUsagePage = 0x01;
		Rid[3].usUsage = 0x04;
		Rid[3].dwFlags = 0;
		Rid[3].hwndTarget = 0;

		if (RegisterRawInputDevices(Rid, arraysize(Rid), sizeof(Rid[0])) == FALSE) 
		{
			//registration failed. Call GetLastError for the cause of the error
			assert(0);
		}
	}

	constexpr float deadzone(float x)
	{
		if ((x) > -0.26f && (x) < 0.26f)
			x = 0;
		if (x < -1.0f)
			x = -1.0f;
		if (x > 1.0f)
			x = 1.0f;
		return x;
	}
	void Update()
	{
		keyboard = wiInput::KeyboardState();
		mouse = wiInput::MouseState();
		for (auto& internal_controller : controllers)
		{
			internal_controller.state = wiInput::ControllerState();
		}

		// Enumerate devices to detect lost devices:
		UINT numDevices;
		UINT listResult = GetRawInputDeviceList(NULL, &numDevices, sizeof(RAWINPUTDEVICELIST));
		assert(listResult >= 0);

		static RAWINPUTDEVICELIST devicelist[64];
		listResult = GetRawInputDeviceList(devicelist, &numDevices, sizeof(RAWINPUTDEVICELIST));
		assert(listResult >= 0);
		assert(numDevices <= arraysize(devicelist));

		for (auto& internal_controller : controllers)
		{
			if (internal_controller.handle)
			{
				bool connected = false;
				for (UINT i = 0; i < numDevices; ++i)
				{
					const RAWINPUTDEVICELIST& device = devicelist[i];
					if (device.hDevice == internal_controller.handle)
					{
						// device that was previously registered is still connected, nothing to do here:
						connected = true;
						break;
					}
				}
				if (!connected)
				{
					// device was not found among connected devices, this slot is now marked free:
					internal_controller = Internal_ControllerState();
				}
			}
		}

		// Loop through reading raw input until no events are left
		while (true)
		{
			static RAWINPUT rawBuffer[128];
			UINT rawBufferSize = 0;
			UINT count = GetRawInputBuffer(NULL, &rawBufferSize, sizeof(RAWINPUTHEADER));
			assert(count == 0);
			rawBufferSize *= 8;
			assert(rawBufferSize <= sizeof(rawBuffer));
			if (rawBufferSize == 0)
			{
				return;
			}

			// Fill up buffer:
			count = GetRawInputBuffer(rawBuffer, &rawBufferSize, sizeof(RAWINPUTHEADER));
			if (count == -1)
			{
				HRESULT error = HRESULT_FROM_WIN32(GetLastError());
				assert(0);
				return;
			}

			// Process all the events:
			for (UINT current_raw = 0; current_raw < count; ++current_raw)
			{
				const RAWINPUT& raw = rawBuffer[current_raw];

				if (raw.header.dwType == RIM_TYPEKEYBOARD)
				{
					const RAWKEYBOARD& rawkeyboard = raw.data.keyboard;
					keyboard.buttons[rawkeyboard.VKey] = true;
				}
				else if (raw.header.dwType == RIM_TYPEMOUSE)
				{
					const RAWMOUSE& rawmouse = raw.data.mouse;
					if (raw.data.mouse.usFlags == MOUSE_MOVE_RELATIVE)
					{
						mouse.delta_position.x += rawmouse.lLastX;
						mouse.delta_position.y += rawmouse.lLastY;
						if (rawmouse.usButtonFlags == RI_MOUSE_WHEEL)
						{
							mouse.delta_wheel += float((SHORT)rawmouse.usButtonData) / float(WHEEL_DELTA);
						}
					}
					else if (raw.data.mouse.usFlags == MOUSE_MOVE_ABSOLUTE)
					{
						mouse.position.x += rawmouse.lLastX;
						mouse.position.y += rawmouse.lLastY;
					}
				}
				else if (raw.header.dwType == RIM_TYPEHID)
				{
					RID_DEVICE_INFO info;
					info.cbSize = sizeof(RID_DEVICE_INFO);
					UINT bufferSize = sizeof(RID_DEVICE_INFO);
					UINT result = GetRawInputDeviceInfo(raw.header.hDevice, RIDI_DEVICEINFO, &info, &bufferSize);
					assert(result != -1);
					assert(info.dwType == raw.header.dwType);

					// Try to find if this input device was already registered into a slot:
					int slot = -1;
					for (int i = 0; i < (int)controllers.size(); ++i)
					{
						const Internal_ControllerState& internal_controller = controllers[i];
						if (slot == -1 && internal_controller.handle == NULL)
						{
							slot = i; // take the first empty slot but keep looking for matching device slot
						}
						if (internal_controller.handle == raw.header.hDevice)
						{
							slot = i; // take the matching device slot and accept it
							break;
						}
					}
					if (slot == -1)
					{
						// No empty or matching slot was found, create a new one:
						slot = (int)controllers.size();
						controllers.emplace_back();
					}
					Internal_ControllerState& internal_controller = controllers[slot];

					if (internal_controller.name.empty())
					{
						// If this is the first time we see this device handle, we check if it's an xinput device or not (xinput should have IG_ in its name).
						result = GetRawInputDeviceInfo(raw.header.hDevice, RIDI_DEVICENAME, NULL, &bufferSize);
						assert(result == 0);
						internal_controller.name.resize(bufferSize + 1);
						result = GetRawInputDeviceInfo(raw.header.hDevice, RIDI_DEVICENAME, (void*)internal_controller.name.data(), &bufferSize);
						assert(result != -1);
						internal_controller.is_xinput = internal_controller.name.find(L"IG_") != std::wstring::npos;
						internal_controller.handle = raw.header.hDevice;
					}

					if (!internal_controller.is_xinput) // xinput enabled controller will be handled by xinput API
					{
						result = GetRawInputDeviceInfo(raw.header.hDevice, RIDI_PREPARSEDDATA, NULL, &bufferSize);
						assert(result == 0);

						static uint8_t preparsed_data_buffer[1024 * 16]; // 16 KB scratch buffer
						assert(bufferSize < arraysize(preparsed_data_buffer)); // data must fit into scratch buffer!
						PHIDP_PREPARSED_DATA pPreparsedData = (PHIDP_PREPARSED_DATA)preparsed_data_buffer;
						result = GetRawInputDeviceInfo(raw.header.hDevice, RIDI_PREPARSEDDATA, pPreparsedData, &bufferSize);
						assert(result != -1);

						HIDP_CAPS Caps;
						NTSTATUS status = HidP_GetCaps(pPreparsedData, &Caps);
						assert(status == HIDP_STATUS_SUCCESS);

						static uint8_t buttoncaps_buffer[1024 * 4]; // 4 KB scratch buffer
						assert(sizeof(HIDP_BUTTON_CAPS) * Caps.NumberInputButtonCaps < arraysize(buttoncaps_buffer)); // data must fit into scratch buffer!
						PHIDP_BUTTON_CAPS pButtonCaps = (PHIDP_BUTTON_CAPS)buttoncaps_buffer;
						USHORT capsLength = Caps.NumberInputButtonCaps;
						status = HidP_GetButtonCaps(HidP_Input, pButtonCaps, &capsLength, pPreparsedData);
						assert(status == HIDP_STATUS_SUCCESS);

						static uint8_t valuecaps_buffer[1024 * 4]; // 4 KB scratch buffer
						assert(sizeof(HIDP_VALUE_CAPS) * Caps.NumberInputValueCaps < arraysize(valuecaps_buffer)); // data must fit into scratch buffer!
						ULONG numberOfButtons = pButtonCaps->Range.UsageMax - pButtonCaps->Range.UsageMin + 1;
						PHIDP_VALUE_CAPS pValueCaps = (PHIDP_VALUE_CAPS)valuecaps_buffer;
						capsLength = Caps.NumberInputValueCaps;
						status = HidP_GetValueCaps(HidP_Input, pValueCaps, &capsLength, pPreparsedData);
						assert(status == HIDP_STATUS_SUCCESS);

						static USAGE usage[128];
						assert(numberOfButtons <= arraysize(usage));
						status = HidP_GetUsages(
							HidP_Input, pButtonCaps->UsagePage, 0, usage,
							&numberOfButtons, pPreparsedData,
							(PCHAR)raw.data.hid.bRawData, raw.data.hid.dwSizeHid
						);
						assert(status == HIDP_STATUS_SUCCESS);

						wiInput::ControllerState& controller = internal_controller.state;

						for (ULONG i = 0; i < numberOfButtons; i++)
						{
							int button = usage[i] - pButtonCaps->Range.UsageMin;
							controller.buttons |= 1 << (button + wiInput::GAMEPAD_BUTTON_1 - wiInput::GAMEPAD_RANGE_START - 1);
						}

						for (USHORT i = 0; i < Caps.NumberInputValueCaps; i++)
						{
							ULONG value;
							status = HidP_GetUsageValue(
								HidP_Input, pValueCaps[i].UsagePage, 0,
								pValueCaps[i].Range.UsageMin, &value, pPreparsedData,
								(PCHAR)raw.data.hid.bRawData, raw.data.hid.dwSizeHid
							);
							assert(status == HIDP_STATUS_SUCCESS);

							switch (pValueCaps[i].Range.UsageMin)
							{
							case 0x30:  // X-axis
								controller.thumbstick_L.x += deadzone(((float)value - 128) / 128.0f);
								break;
							case 0x31:  // Y-axis
								controller.thumbstick_L.y += deadzone(-((float)value - 128) / 128.0f);
								break;
							case 0x32: // Z-axis
								controller.thumbstick_R.x += deadzone(((float)value - 128) / 128.0f);
								break;
							case 0x33: // Rotate-X
								controller.trigger_L += deadzone((float)value / 256.0f);
								break;
							case 0x34: // Rotate-Y
								controller.trigger_R += deadzone((float)value / 256.0f);
								break;
							case 0x35: // Rotate-Z
								controller.thumbstick_R.y += deadzone(((float)value - 128) / 128.0f);
								break;
							case 0x39:  // Hat Switch
							{
								enum POV
								{
									POV_UP = 0,
									POV_UPRIGHT = 1,
									POV_RIGHT = 2,
									POV_RIGHTDOWN = 3,
									POV_DOWN = 4,
									POV_DOWNLEFT = 5,
									POV_LEFT = 6,
									POV_LEFTUP = 7,
									POV_IDLE = 8,
								} pov = (POV)value;
								switch (pov)
								{
								case POV_UP:
								case POV_UPRIGHT:
								case POV_LEFTUP:
									controller.buttons |= 1 << (wiInput::GAMEPAD_BUTTON_UP - wiInput::GAMEPAD_RANGE_START - 1);
									break;
								}
								switch (pov)
								{
								case POV_RIGHT:
								case POV_UPRIGHT:
								case POV_RIGHTDOWN:
									controller.buttons |= 1 << (wiInput::GAMEPAD_BUTTON_RIGHT - wiInput::GAMEPAD_RANGE_START - 1);
									break;
								}
								switch (pov)
								{
								case POV_DOWN:
								case POV_RIGHTDOWN:
								case POV_DOWNLEFT:
									controller.buttons |= 1 << (wiInput::GAMEPAD_BUTTON_DOWN - wiInput::GAMEPAD_RANGE_START - 1);
									break;
								}
								switch (pov)
								{
								case POV_LEFT:
								case POV_DOWNLEFT:
								case POV_LEFTUP:
									controller.buttons |= 1 << (wiInput::GAMEPAD_BUTTON_LEFT - wiInput::GAMEPAD_RANGE_START - 1);
									break;
								}
							}
							break;
							}
						}

					}
				}

			}
		}
	}

	void GetKeyboardState(wiInput::KeyboardState* state)
	{
		*state = keyboard;
	}

	void GetMouseState(wiInput::MouseState* state)
	{
		*state = mouse;
	}

	int GetMaxControllerCount()
	{
		return (int)controllers.size();
	}
	bool GetControllerState(wiInput::ControllerState* state, int index)
	{
		if (index < controllers.size() && controllers[index].handle)
		{
			if (state != nullptr)
			{
				*state = controllers[index].state;
			}
			return true;
		}
		return false;
	}
	void SetControllerFeedback(const wiInput::ControllerFeedback& data, int index)
	{
		if (index < controllers.size() && controllers[index].handle)
		{
			HANDLE hid_device = CreateFile(
				controllers[index].name.c_str(), 
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
				OPEN_EXISTING, 0, NULL
			);
			assert(hid_device != INVALID_HANDLE_VALUE);
			uint8_t buf[32] = {};
			buf[0] = 0x05;
			buf[1] = 0xFF;
			buf[4] = uint8_t(std::max(0.0f, std::min(1.0f, data.motor_right)) * 255);
			buf[5] = uint8_t(std::max(0.0f, std::min(1.0f, data.motor_left)) * 255);
			buf[6] = data.led_color.getR();
			buf[7] = data.led_color.getG();
			buf[8] = data.led_color.getB();
			DWORD bytes_written;
			BOOL result = WriteFile(hid_device, buf, sizeof(buf), &bytes_written, NULL);
			assert(result == TRUE);
			assert(bytes_written == arraysize(buf));
			CloseHandle(hid_device);
		}
	}
}

#else
namespace wiRawInput
{
	void Initialize() {}
	void Update() {}
	void GetKeyboardState(wiInput::KeyboardState* state) {}
	void GetMouseState(wiInput::MouseState* state) {}
	int GetMaxControllerCount() { return 0; }
	bool GetControllerState(wiInput::ControllerState* state, int index) { return false; }
	void SetControllerFeedback(const wiInput::ControllerFeedback& data, int index) {}
}
#endif // _WIN32
