#include <xenomods/InputBuffer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

#include <fmt/format.h>
#include <imgui.h>
#include <skylaunch/hookng/Hooks.hpp>
#include <toml++/toml.hpp>

#include <xenomods/Logger.hpp>
#include <xenomods/NnFile.hpp>
#include <xenomods/State.hpp>

namespace fw {
	struct PadData {
		std::uint32_t words[18];
	};

	struct PadManager {
		static PadData* getControlPadData(int pad);
	};
} // namespace fw

namespace xenomods::InputBuffer {

	bool Enabled = false;
	bool BufferLeftStick = false;
	bool BufferRightStick = false;
	int StickBufferFrames = 3;

	namespace {
		struct SavedSettings {
			bool enabled = false;

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
			return {Enabled};
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
		std::uint32_t physicalHeld = 0;
		AcceptedActionCallback acceptedActionCallback = nullptr;
		InputLayerUpdateCallback inputLayerUpdateCallback = nullptr;
		bool acceptedActionCaptureActive = false;
		bool acceptedActionCaptureWaitingForNeutral = false;
		bool playbackOverrideActive = false;
		PlaybackMode playbackMode = PlaybackMode::StandardMenu;
		AcceptedAction playbackAction {};
		bool playbackUiActionOffered = false;
		bool playbackUiDirectActionOffered = false;
		constexpr std::uint32_t EncodedActionFlag = 0x80000000u;
		constexpr std::uint32_t UiPadActionFlag = 0x40000000u;
		constexpr std::uint32_t EncodedActionIndexMask = 0x0fffffffu;
		constexpr std::uint32_t UiPadMask = 0x3fffffffu;
		constexpr std::array<std::uint32_t, 27> UiPadToFwPad {
			0x00000000, 0x00000004, 0x00000002, 0x00000004, 0x00000002,
			0x00000008, 0x00000001, 0x00001000, 0x00004000, 0x00002000,
			0x00008000, 0x00000010, 0x00000040, 0x00000400, 0x00000020,
			0x00000080, 0x00000800, 0x00000200, 0x00000100, 0x01000000,
			0x04000000, 0x02000000, 0x08000000, 0x10000000, 0x40000000,
			0x20000000, 0x80000000
		};
		constexpr std::uint32_t DirectionButtonMask =
			(1u << 12) | (1u << 13) | (1u << 14) | (1u << 15);
		constexpr std::uint32_t LrButtonMask =
			(1u << 4) | (1u << 5);
		constexpr std::uint32_t TriggerButtonMask =
			(1u << 8) | (1u << 9);
		constexpr std::uint64_t AuxCoreAmountRepeatFrames = 3;
		std::uint64_t currentFrameIndex = 0;
		std::uint64_t lastAuxCoreAmountInputFrame =
			std::numeric_limits<std::uint64_t>::max();
		bool auxCoreAmountMenuActive = false;
		std::uint32_t auxCoreAmountMenuObject = 0;
		bool auxCoreNativeAcceptanceActive = false;

		struct LayerRegistration {
			const void* layer = nullptr;
			std::uint32_t handle = 0;
		};

		std::array<LayerRegistration, 256> layerRegistrations {};

		void RegisterInputLayer(const void* layer, std::uint32_t handle) {
			if(layer == nullptr || handle == 0)
				return;
			LayerRegistration* freeRegistration = nullptr;
			for(auto& registration : layerRegistrations) {
				if(registration.layer == layer || registration.handle == handle) {
					registration = {layer, handle};
					return;
				}
				if(freeRegistration == nullptr && registration.layer == nullptr)
					freeRegistration = &registration;
			}
			if(freeRegistration != nullptr)
				*freeRegistration = {layer, handle};
		}

		void ReleaseInputLayer(std::uint32_t handle) {
			for(auto& registration : layerRegistrations) {
				if(registration.handle == handle)
					registration = {};
			}
		}

		std::uint32_t InputLayerHandle(const void* layer) {
			for(const auto registration : layerRegistrations) {
				if(registration.layer == layer)
					return registration.handle;
			}
			return 0;
		}

		bool IsEncodedAction(std::uint32_t action) {
			return (action & EncodedActionFlag) != 0;
		}

		bool IsUiPadAction(std::uint32_t action) {
			return (action & 0xc0000000u) == UiPadActionFlag;
		}

		bool IsAuxCoreAmountPlaybackAction() {
			return auxCoreAmountMenuActive
				&& IsUiPadAction(playbackAction.input)
				&& ((playbackAction.input & UiPadMask) & LrButtonMask) != 0;
		}

		bool IsAuxCoreNativePlaybackAction() {
			return auxCoreNativeAcceptanceActive
				&& IsUiPadAction(playbackAction.input)
				&& ((playbackAction.input & UiPadMask) & LrButtonMask) == 0;
		}

		std::uint32_t EncodeAction(std::uint32_t inputType, std::uint32_t index) {
			return EncodedActionFlag
				| ((inputType & 7u) << 28)
				| (index & EncodedActionIndexMask);
		}

		bool ActionMatches(
			std::uint32_t encoded,
			std::uint32_t inputType,
			std::uint32_t index
		) {
			return ((encoded >> 28) & 7u) == inputType
				&& (encoded & EncodedActionIndexMask) == index;
		}

		std::uint32_t PlaybackButtonMask() {
			if(IsEncodedAction(playbackAction.input))
				return 0;
			if(IsUiPadAction(playbackAction.input))
				return 0;
			return playbackAction.input;
		}

		std::uint64_t InputLayerEntryCount(const void* inputLayer) {
			if(inputLayer == nullptr)
				return 0;
			const auto bytes = reinterpret_cast<const std::uint8_t*>(inputLayer);
			const std::uint64_t count = *reinterpret_cast<const std::uint64_t*>(
				bytes + 0x200
			);
			return count <= 27 ? count : 0;
		}

		bool InputLayerAcceptsUiPadAction(const void* inputLayer) {
			if(!IsUiPadAction(playbackAction.input))
				return false;
			const auto bytes = reinterpret_cast<const std::uint8_t*>(inputLayer);
			const std::uint64_t count = InputLayerEntryCount(inputLayer);
			if(bytes == nullptr || count == 0)
				return false;

			const std::uint32_t inputMask = playbackAction.input & UiPadMask;
			for(std::uint64_t index = 0; index < count; index++) {
				const auto entry = bytes + 0x50 + index * 0x10;
				const std::uint16_t uiPad = *reinterpret_cast<const std::uint16_t*>(entry);
				if(
					uiPad < UiPadToFwPad.size()
					&& entry[0xf] != 0
					&& (inputMask & UiPadToFwPad[uiPad]) != 0
				)
					return true;
			}
			return false;
		}

		bool PlaybackLayerMatches(const void* inputLayer) {
			if(inputLayer == nullptr)
				return false;
			const std::uint32_t actualLayer = InputLayerHandle(inputLayer);
			if(actualLayer != 0 && actualLayer == playbackAction.layer)
				return true;
			const std::uint32_t inputMask = playbackAction.input & UiPadMask;
			const std::uint32_t object = *reinterpret_cast<const std::uint32_t*>(
				reinterpret_cast<const std::uint8_t*>(inputLayer) + 0x40
			);
			return auxCoreAmountMenuActive
				&& auxCoreAmountMenuObject != 0
				&& object == auxCoreAmountMenuObject
				&& IsUiPadAction(playbackAction.input)
				&& (inputMask & LrButtonMask) != 0;
		}

		void* updatingInputLayer = nullptr;
		fw::PadData activeUiPadData {};
		bool activeUiPadDataValid = false;

		struct StickState {
			float x = 0.f;
			float y = 0.f;
			float pendingX = 0.f;
			float pendingY = 0.f;
			int frames = 0;
		};

		StickState leftStick {};
		StickState rightStick {};
		bool leftStickOverrideActive = false;
		float leftStickOverrideX = 0.f;
		float leftStickOverrideY = 0.f;
		bool rawButtonOverrideActive = false;
		std::uint32_t rawButtonHeld = 0;
		std::uint32_t rawButtonDown = 0;
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
				if(pad == 0 && playbackOverrideActive)
					return PlaybackButtonMask();
				if(pad == 0 && rawButtonOverrideActive)
					return original | rawButtonHeld;
				return original;
			}
		};

		struct GetDownHook : skylaunch::hook::Trampoline<GetDownHook> {
			static std::uint32_t Hook(int pad) {
				const std::uint32_t original = Orig(pad);
				if(pad == 0 && playbackOverrideActive)
					return PlaybackButtonMask();
				if(pad == 0 && rawButtonOverrideActive)
					return original | rawButtonDown;
				if(!Enabled || pad != 0) {
					if(pad == 0)
						Clear();
					return original;
				}

				for(std::size_t index = 0; index < buttonFrames.size(); index++) {
					const std::uint32_t bit = std::uint32_t {1} << index;
					if((physicalHeld & bit) == 0) {
						buttonFrames[index] = 0;
					} else if((original & bit) != 0) {
						buttonFrames[index] = 1;
					}
				}

				const std::uint32_t pending = GetPendingMask();
				return original | pending;
			}
		};

		struct GetUpHook : skylaunch::hook::Trampoline<GetUpHook> {
			static std::uint32_t Hook(int pad) {
				const std::uint32_t original = Orig(pad);
				if(pad == 0 && playbackOverrideActive)
					return 0;
				return pad == 0 && rawButtonOverrideActive
					? original & ~rawButtonHeld
					: original;
			}
		};

		struct GetALXHook : skylaunch::hook::Trampoline<GetALXHook> {
			static float Hook(int pad, bool deadzone) {
				const float original = Orig(pad, deadzone);
				if(pad == 0 && playbackOverrideActive)
					return 0.f;
				if(pad == 0 && leftStickOverrideActive)
					return leftStickOverrideX;
				return pad == 0 ? BufferStickX(leftStick, original, BufferLeftStick) : original;
			}
		};

		struct GetALYHook : skylaunch::hook::Trampoline<GetALYHook> {
			static float Hook(int pad, bool deadzone) {
				const float original = Orig(pad, deadzone);
				if(pad == 0 && playbackOverrideActive)
					return 0.f;
				if(pad == 0 && leftStickOverrideActive)
					return leftStickOverrideY;
				return pad == 0 ? BufferStickY(leftStick, original, BufferLeftStick) : original;
			}
		};

		struct GetARXHook : skylaunch::hook::Trampoline<GetARXHook> {
			static float Hook(int pad, bool deadzone) {
				const float original = Orig(pad, deadzone);
				if(pad == 0 && playbackOverrideActive)
					return 0.f;
				return pad == 0 ? BufferStickX(rightStick, original, BufferRightStick) : original;
			}
		};

		struct GetARYHook : skylaunch::hook::Trampoline<GetARYHook> {
			static float Hook(int pad, bool deadzone) {
				const float original = Orig(pad, deadzone);
				if(pad == 0 && playbackOverrideActive)
					return 0.f;
				return pad == 0 ? BufferStickY(rightStick, original, BufferRightStick) : original;
			}
		};

		struct UIUtilGetPadDataHook : skylaunch::hook::Trampoline<UIUtilGetPadDataHook> {
			static fw::PadData* Hook() {
				fw::PadData* original = Orig();
				if(
					acceptedActionCaptureActive
					&& acceptedActionCaptureWaitingForNeutral
					&& original != nullptr
				) {
					const std::uint32_t activeMask =
						(original->words[0] | original->words[1] | original->words[3])
						& UiPadMask;
					if(activeMask == 0)
						acceptedActionCaptureWaitingForNeutral = false;
				}
				if(
					acceptedActionCaptureActive
					&& !acceptedActionCaptureWaitingForNeutral
					&& updatingInputLayer != nullptr
					&& original != nullptr
				) {
					std::memcpy(&activeUiPadData, original, sizeof(activeUiPadData));
					activeUiPadDataValid = true;
				}
				if(!playbackOverrideActive)
					return original;
				static fw::PadData injected {};
				if(original != nullptr)
					std::memcpy(&injected, original, sizeof(injected));
				else
					injected = {};

				injected.words[0] = 0;
				injected.words[1] = 0;
				injected.words[3] = 0;
				const std::uint32_t inputMask = playbackAction.input & UiPadMask;
				const bool navigationAction = (inputMask & DirectionButtonMask) != 0;
				const bool auxCoreAmountAction =
					IsAuxCoreAmountPlaybackAction();
				const bool directAction = InputLayerAcceptsUiPadAction(updatingInputLayer);
				const bool actionAvailable =
					navigationAction || directAction || auxCoreAmountAction;
				if(
					IsUiPadAction(playbackAction.input)
					&& PlaybackLayerMatches(updatingInputLayer)
					&& actionAvailable
				) {
					injected.words[0] = inputMask;
					injected.words[1] = inputMask;
					injected.words[3] = inputMask;
					playbackUiActionOffered = true;
					playbackUiDirectActionOffered = directAction;
				}
				return &injected;
			}
		};

		struct UIInputLayerUpdateHook : skylaunch::hook::Trampoline<UIInputLayerUpdateHook> {
			static bool Hook(void* inputLayer) {
				std::uint32_t object = 0;
				std::uint32_t layerHandle = 0;
				if(inputLayerUpdateCallback != nullptr && inputLayer != nullptr) {
					object = *reinterpret_cast<const std::uint32_t*>(
						reinterpret_cast<const std::uint8_t*>(inputLayer) + 0x40
					);
					layerHandle = InputLayerHandle(inputLayer);
					inputLayerUpdateCallback(
						object, inputLayer, layerHandle, false, false, 0, 0, 0
					);
				}
				void* previousInputLayer = updatingInputLayer;
				updatingInputLayer = inputLayer;
				activeUiPadDataValid = false;
				playbackUiActionOffered = false;
				playbackUiDirectActionOffered = false;
				const bool accepted = Orig(inputLayer);
				const bool offeredPlaybackAction = playbackUiActionOffered;
				const bool offeredDirectAction = playbackUiDirectActionOffered;
				const std::uint16_t emittedEvent = *reinterpret_cast<const std::uint16_t*>(
					reinterpret_cast<const std::uint8_t*>(inputLayer) + 0x10
				);
				if(inputLayerUpdateCallback != nullptr && inputLayer != nullptr) {
					inputLayerUpdateCallback(
						object,
						inputLayer,
						layerHandle,
						true,
						accepted,
						emittedEvent,
						activeUiPadDataValid ? activeUiPadData.words[1] : 0,
						activeUiPadDataValid ? activeUiPadData.words[3] : 0
					);
				}
				updatingInputLayer = previousInputLayer;

				if(
					accepted
					&& acceptedActionCaptureActive
					&& acceptedActionCallback != nullptr
					&& activeUiPadDataValid
				) {
					const std::uint32_t inputMask =
						(activeUiPadData.words[1] | activeUiPadData.words[3]) & UiPadMask;
					const std::uint32_t layerHandle = InputLayerHandle(inputLayer);
					if(inputMask != 0 && layerHandle != 0) {
						acceptedActionCallback(
							{
								UiPadActionFlag | inputMask,
								layerHandle,
								inputMask
							},
							ActionSource::Physical
						);
					}
				}
				if(
					accepted
					&& playbackOverrideActive
					&& !IsAuxCoreAmountPlaybackAction()
					&& IsUiPadAction(playbackAction.input)
					&& PlaybackLayerMatches(inputLayer)
					&& offeredPlaybackAction
					&& (offeredDirectAction || emittedEvent == 6)
				) {
					AcceptedAction acceptedAction = playbackAction;
					playbackAction = {};
					if(acceptedActionCallback != nullptr)
						acceptedActionCallback(acceptedAction, ActionSource::Playback);
				}
				return accepted;
			}
		};

		struct PadRelayCheckActionHook
			: skylaunch::hook::Trampoline<PadRelayCheckActionHook> {
			static bool Hook(void* relay, const void* action) {
				const auto descriptor = action == nullptr
					? nullptr
					: *reinterpret_cast<const std::uint32_t* const*>(action);
				const bool descriptorValid = descriptor != nullptr && descriptor[0] < 8;
				const bool resourceEnabled = action != nullptr
					&& *reinterpret_cast<const std::uint32_t*>(
						reinterpret_cast<const std::uint8_t*>(action) + 8
					) != 0;
				const std::uint32_t playbackUiMask = playbackAction.input & UiPadMask;
				const bool playbackUiDirection =
					(playbackUiMask & DirectionButtonMask) != 0;
				const bool playbackUiShoulderTrigger =
					(playbackUiMask & TriggerButtonMask) != 0;
				const bool auxCoreAmountPlaybackAction =
					IsAuxCoreAmountPlaybackAction();
				const bool auxCoreNativePlaybackAction =
					IsAuxCoreNativePlaybackAction();
				// Once the Aux amount counter finishes, its A resource is visible to
				// PadRelay before the following UI states can actually consume their
				// inputs. Keep every non-L/R action on native layer acceptance for the
				// rest of this Aux sequence; L/R retains its direct handler exception.
				const bool playbackUiRelayType = descriptorValid
					&& !auxCoreAmountPlaybackAction
					&& !auxCoreNativePlaybackAction
					&& !playbackUiDirection
					&& (descriptor[0] == 2
						|| (playbackUiShoulderTrigger
							&& (descriptor[0] == 0 || descriptor[0] == 4)));
				if(
					playbackOverrideActive
					&& playbackMode == PlaybackMode::StandardMenu
					&& IsUiPadAction(playbackAction.input)
					&& descriptorValid
					&& resourceEnabled
					&& playbackUiRelayType
					&& descriptor[1] < 32
					&& (playbackUiMask
						& (std::uint32_t {1} << descriptor[1])) != 0
				) {
					const AcceptedAction acceptedAction = playbackAction;
					playbackAction = {};
					if(acceptedActionCallback != nullptr)
						acceptedActionCallback(acceptedAction, ActionSource::Playback);
					return true;
				}

				if(
					playbackOverrideActive
					&& playbackMode == PlaybackMode::StandardMenu
					&& IsUiPadAction(playbackAction.input)
					&& !auxCoreAmountPlaybackAction
					&& !auxCoreNativePlaybackAction
					&& ((playbackAction.input & UiPadMask) & DirectionButtonMask) == 0
				)
					return false;

				if(playbackOverrideActive && IsEncodedAction(playbackAction.input)) {
					if(
						descriptorValid
						&& resourceEnabled
						&& ActionMatches(playbackAction.input, descriptor[0], descriptor[1])
					) {
						const AcceptedAction acceptedAction = playbackAction;
						playbackAction = {};
						if(acceptedActionCallback != nullptr)
							acceptedActionCallback(acceptedAction, ActionSource::Playback);
						return true;
					}
					return false;
				}

				const bool accepted = Orig(relay, action);
				if(!accepted || !descriptorValid)
					return accepted;

				const std::uint32_t buttonIndex = descriptor[1];
				if(buttonIndex < 32) {
					const std::uint32_t bit = std::uint32_t {1} << buttonIndex;
					if(
						playbackOverrideActive
						&& !IsUiPadAction(playbackAction.input)
						&& !IsEncodedAction(playbackAction.input)
						&& (playbackAction.input & bit) != 0
					) {
						const AcceptedAction acceptedAction = playbackAction;
						playbackAction = {};
						if(acceptedActionCallback != nullptr)
							acceptedActionCallback(acceptedAction, ActionSource::Playback);
					}
					if(Enabled && (GetPendingMask() & bit) != 0)
						Consume(bit);
				}
				return accepted;
			}
		};

		struct UIInputManagerRegisterHook
			: skylaunch::hook::Trampoline<UIInputManagerRegisterHook> {
			static std::uint32_t Hook(
				void* manager,
				void* inputLayer,
				const char* name
			) {
				const std::uint32_t handle = Orig(manager, inputLayer, name);
				RegisterInputLayer(inputLayer, handle);
				return handle;
			}
		};

		struct UIInputManagerReleaseHook
			: skylaunch::hook::Trampoline<UIInputManagerReleaseHook> {
			static void Hook(void* manager, std::uint32_t handle) {
				Orig(manager, handle);
				ReleaseInputLayer(handle);
			}
		};
	} // namespace

	void Clear() {
		buttonFrames.fill(0);
		leftStick = {};
		rightStick = {};
	}

	std::uint32_t PendingButtons() {
		return GetPendingMask();
	}

	void SetLeftStickOverride(bool active, float x, float y) {
		leftStickOverrideActive = active;
		leftStickOverrideX = active ? std::clamp(x, -1.f, 1.f) : 0.f;
		leftStickOverrideY = active ? std::clamp(y, -1.f, 1.f) : 0.f;
	}

	void SetRawButtonOverride(bool active, std::uint32_t heldMask, bool down) {
		rawButtonOverrideActive = active && heldMask != 0;
		rawButtonHeld = rawButtonOverrideActive ? heldMask : 0;
		rawButtonDown = rawButtonOverrideActive && down ? heldMask : 0;
	}

	void SetAcceptedActionCallback(AcceptedActionCallback callback) {
		acceptedActionCallback = callback;
	}

	void SetInputLayerUpdateCallback(InputLayerUpdateCallback callback) {
		inputLayerUpdateCallback = callback;
	}

	void SetAcceptedActionCapture(bool active) {
		acceptedActionCaptureActive = active;
		acceptedActionCaptureWaitingForNeutral = active;
		activeUiPadDataValid = false;
	}

	bool AcceptedActionCaptureWaitingForNeutral() {
		return acceptedActionCaptureActive && acceptedActionCaptureWaitingForNeutral;
	}

	void SetPlaybackOverride(bool active, PlaybackMode mode) {
		playbackOverrideActive = active;
		playbackMode = active ? mode : PlaybackMode::StandardMenu;
		if(!active) {
			playbackAction = {};
			auxCoreNativeAcceptanceActive = false;
		}
		playbackUiActionOffered = false;
		playbackUiDirectActionOffered = false;
	}

	void SetPlaybackAction(AcceptedAction action) {
		playbackAction = playbackOverrideActive ? action : AcceptedAction {};
	}

	bool PlaybackActionPending() {
		return playbackOverrideActive && playbackAction.input != 0;
	}

	void SetFrameIndex(std::uint64_t frame) {
		currentFrameIndex = frame;
	}

	void SetAuxCoreAmountMenu(bool active, std::uint32_t object) {
		auxCoreAmountMenuActive = active;
		auxCoreAmountMenuObject = active ? object : 0;
		if(!active)
			ResetAuxCoreAmountInput();
	}

	bool AcceptAuxCoreAmountPlayback() {
		if(
			!auxCoreAmountMenuActive
			|| !playbackOverrideActive
			|| !IsUiPadAction(playbackAction.input)
			|| ((playbackAction.input & UiPadMask) & LrButtonMask) == 0
		)
			return false;
		const AcceptedAction acceptedAction = playbackAction;
		auxCoreNativeAcceptanceActive = true;
		playbackAction = {};
		if(acceptedActionCallback != nullptr)
			acceptedActionCallback(acceptedAction, ActionSource::Playback);
		return true;
	}

	bool AcceptAuxCoreAmountInput() {
		if(!Enabled && !playbackOverrideActive)
			return true;
		if(
			lastAuxCoreAmountInputFrame != std::numeric_limits<std::uint64_t>::max()
			&& currentFrameIndex - lastAuxCoreAmountInputFrame
				< AuxCoreAmountRepeatFrames
		)
			return false;
		lastAuxCoreAmountInputFrame = currentFrameIndex;
		return true;
	}

	void ResetAuxCoreAmountInput() {
		lastAuxCoreAmountInputFrame = std::numeric_limits<std::uint64_t>::max();
	}

	void DrawMenu() {
		if(ImGui::Checkbox("Enable input buffering", &Enabled) && !Enabled)
			Clear();
		ImGui::PushItemWidth(120.f);
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
		}
		lastSavedSettings = CurrentSettings();

		GetBtnHook::HookAt("_ZN2ml6DevPad6getBtnEi");
		GetDownHook::HookAt("_ZN2ml6DevPad7getDownEi");
		GetUpHook::HookAt("_ZN2ml6DevPad5getUpEi");
		GetALXHook::HookAt("_ZN2ml6DevPad6getALXEib");
		GetALYHook::HookAt("_ZN2ml6DevPad6getALYEib");
		GetARXHook::HookAt("_ZN2ml6DevPad6getARXEib");
		GetARYHook::HookAt("_ZN2ml6DevPad6getARYEib");
		UIUtilGetPadDataHook::HookAt("_ZN2ui6UIUtil10getPadDataEv");
		UIInputLayerUpdateHook::HookAt("_ZN2ui12UIInputLayer6updateEv");
		UIInputManagerRegisterHook::HookAt(
			"_ZN2ui14UIInputManager16registInputLayerEPNS_12UIInputLayerEPKc"
		);
		UIInputManagerReleaseHook::HookAt(
			"_ZN2ui14UIInputManager17releaseInputLayerEj"
		);
		PadRelayCheckActionHook::HookAt(
			"_ZN2fw8PadRelay11checkActionERKNS_9ResActionE"
		);
		g_Logger->LogInfo("Input buffer hooks installed");
#endif
	}

} // namespace xenomods::InputBuffer
