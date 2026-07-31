#include <xenomods/InputBuffer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>

#include <fmt/format.h>
#include <imgui.h>
#include <skylaunch/hookng/Hooks.hpp>
#include <toml++/toml.hpp>

#include <xenomods/Logger.hpp>
#include <xenomods/NnFile.hpp>
#include <xenomods/State.hpp>

namespace xenomods::InputBuffer {

	bool Enabled = false;
	int ButtonBufferFrames = 6;
	bool BufferLeftStick = false;
	bool BufferRightStick = false;
	int StickBufferFrames = 3;

	namespace {
		struct SavedSettings {
			bool enabled = false;
			int buttonBufferFrames = 6;

			bool operator==(const SavedSettings&) const = default;
		};

		SavedSettings lastSavedSettings {};

		std::string SettingsPath() {
			return fmt::format(
				XENOMODS_CONFIG_PATH "/{}/tasSettings.toml",
				XENOMODS_CODENAME_STR
			);
		}

		SavedSettings CurrentSettings() {
			return {Enabled, ButtonBufferFrames};
		}

		void SaveSettingsIfChanged() {
			const SavedSettings settings = CurrentSettings();
			if(settings == lastSavedSettings)
				return;

			const auto path = SettingsPath();
			toml::table root;
			toml::parse_result existing = toml::parse_file(path);
			if(existing)
				root = std::move(existing).table();

			toml::table inputBuffer;
			inputBuffer.insert_or_assign("enabled", settings.enabled);
			inputBuffer.insert_or_assign(
				"button_buffer_frames",
				settings.buttonBufferFrames
			);
			root.insert_or_assign("input_buffer", std::move(inputBuffer));

			std::stringstream stream;
			stream << root;
			const std::string contents = stream.str();
			if(NnFile::Preallocate(path, contents.size())) {
				NnFile file(path, nn::fs::OpenMode_Write);
				file.Write(contents.c_str(), contents.size());
				file.Flush();
				lastSavedSettings = settings;
			}
		}

		std::array<std::uint8_t, 32> buttonFrames {};
		std::uint32_t physicalDown = 0;
		std::uint32_t physicalHeld = 0;

		struct StickState {
			float x = 0.f;
			float y = 0.f;
			float pendingX = 0.f;
			float pendingY = 0.f;
			int frames = 0;
		};

		StickState leftStick {};
		StickState rightStick {};
		constexpr float StickThreshold = 0.05f;

		std::uint32_t GetPendingMask() {
			std::uint32_t result = 0;
			for(std::size_t index = 0; index < buttonFrames.size(); index++) {
				if(buttonFrames[index] != 0)
					result |= std::uint32_t {1} << index;
			}
			return result;
		}

		void Consume(std::uint32_t mask) {
			for(std::size_t index = 0; index < buttonFrames.size(); index++) {
				if((mask & (std::uint32_t {1} << index)) != 0)
					buttonFrames[index] = 0;
			}
		}

		float BufferStickY(
			StickState& state,
			float value,
			bool enabled
		) {
			if(!Enabled || !enabled)
				return value;

			state.pendingY = value;
			const bool active =
				std::abs(state.pendingX) > StickThreshold
				|| std::abs(state.pendingY) > StickThreshold;
			if(active) {
				state.x = state.pendingX;
				state.y = state.pendingY;
				state.frames = std::max(1, StickBufferFrames);
			} else if(state.frames > 0) {
				value = state.y;
				state.frames--;
				if(state.frames == 0) {
					state.x = 0.f;
					state.y = 0.f;
				}
			}
			return value;
		}

		float BufferStickX(
			StickState& state,
			float value,
			bool enabled
		) {
			if(!Enabled || !enabled)
				return value;
			state.pendingX = value;
			if(std::abs(value) <= StickThreshold && state.frames > 0)
				return state.x;
			return value;
		}

		struct GetBtnHook : skylaunch::hook::Trampoline<GetBtnHook> {
			static std::uint32_t Hook(int pad) {
				const std::uint32_t original = Orig(pad);
				if(pad == 0)
					physicalHeld = original;
				return original;
			}
		};

		struct GetDownHook : skylaunch::hook::Trampoline<GetDownHook> {
			static std::uint32_t Hook(int pad) {
				const std::uint32_t original = Orig(pad);
				if(!Enabled || pad != 0) {
					if(pad == 0)
						Clear();
					return original;
				}

				physicalDown = original;
				for(std::size_t index = 0; index < buttonFrames.size(); index++) {
					const std::uint32_t bit = std::uint32_t {1} << index;
					if((physicalHeld & bit) == 0) {
						buttonFrames[index] = 0;
					} else if((original & bit) != 0) {
						buttonFrames[index] = static_cast<std::uint8_t>(
							std::clamp(ButtonBufferFrames, 1, 60)
						);
					} else if(buttonFrames[index] != 0) {
						buttonFrames[index]--;
					}
				}

				const std::uint32_t pending = GetPendingMask();
				return original | pending;
			}
		};

		struct GetALXHook : skylaunch::hook::Trampoline<GetALXHook> {
			static float Hook(int pad, bool deadzone) {
				const float original = Orig(pad, deadzone);
				return pad == 0 ? BufferStickX(leftStick, original, BufferLeftStick) : original;
			}
		};

		struct GetALYHook : skylaunch::hook::Trampoline<GetALYHook> {
			static float Hook(int pad, bool deadzone) {
				const float original = Orig(pad, deadzone);
				return pad == 0 ? BufferStickY(leftStick, original, BufferLeftStick) : original;
			}
		};

		struct GetARXHook : skylaunch::hook::Trampoline<GetARXHook> {
			static float Hook(int pad, bool deadzone) {
				const float original = Orig(pad, deadzone);
				return pad == 0 ? BufferStickX(rightStick, original, BufferRightStick) : original;
			}
		};

		struct GetARYHook : skylaunch::hook::Trampoline<GetARYHook> {
			static float Hook(int pad, bool deadzone) {
				const float original = Orig(pad, deadzone);
				return pad == 0 ? BufferStickY(rightStick, original, BufferRightStick) : original;
			}
		};

		struct PadRelayCheckActionHook
			: skylaunch::hook::Trampoline<PadRelayCheckActionHook> {
			static bool Hook(void* relay, const void* action) {
				const bool accepted = Orig(relay, action);
				if(!Enabled || !accepted || GetPendingMask() == 0 || action == nullptr)
					return accepted;

				const auto descriptor =
					*reinterpret_cast<const std::uint32_t* const*>(action);
				if(descriptor == nullptr)
					return accepted;

				const std::uint32_t inputType = descriptor[0];
				const std::uint32_t buttonIndex = descriptor[1];
				if(inputType < 8 && buttonIndex < 32) {
					const std::uint32_t bit = std::uint32_t {1} << buttonIndex;
					if((GetPendingMask() & bit) != 0)
						Consume(bit);
				}
				return accepted;
			}
		};
	} // namespace

	void Clear() {
		buttonFrames.fill(0);
		physicalDown = 0;
		physicalHeld = 0;
		leftStick = {};
		rightStick = {};
	}

	std::uint32_t PendingButtons() {
		return GetPendingMask();
	}

	void DrawMenu() {
		if(ImGui::Checkbox("Enable input buffering", &Enabled) && !Enabled)
			Clear();
		ImGui::PushItemWidth(120.f);
		ImGui::SliderInt("Button buffer frames", &ButtonBufferFrames, 1, 30);
		ImGui::Checkbox("Buffer left-stick flicks", &BufferLeftStick);
		ImGui::Checkbox("Buffer right-stick flicks", &BufferRightStick);
		if(BufferLeftStick || BufferRightStick)
			ImGui::SliderInt("Stick buffer frames", &StickBufferFrames, 1, 15);
		ImGui::PopItemWidth();
		ImGui::TextDisabled("Pending buttons: 0x%08X", PendingButtons());
		SaveSettingsIfChanged();
	}

	void Initialize() {
#if XENOMODS_CODENAME(bf2)
		const toml::parse_result settings = toml::parse_file(SettingsPath());
		if(settings) {
			const auto inputBuffer = settings["input_buffer"];
			Enabled = inputBuffer["enabled"].value_or(false);
			ButtonBufferFrames = std::clamp(
				inputBuffer["button_buffer_frames"].value_or(6),
				1,
				30
			);
		}
		lastSavedSettings = CurrentSettings();

		GetBtnHook::HookAt("_ZN2ml6DevPad6getBtnEi");
		GetDownHook::HookAt("_ZN2ml6DevPad7getDownEi");
		GetALXHook::HookAt("_ZN2ml6DevPad6getALXEib");
		GetALYHook::HookAt("_ZN2ml6DevPad6getALYEib");
		GetARXHook::HookAt("_ZN2ml6DevPad6getARXEib");
		GetARYHook::HookAt("_ZN2ml6DevPad6getARYEib");
		PadRelayCheckActionHook::HookAt(
			"_ZN2fw8PadRelay11checkActionERKNS_9ResActionE"
		);
		g_Logger->LogInfo("Input buffer hooks installed");
#endif
	}

} // namespace xenomods::InputBuffer
