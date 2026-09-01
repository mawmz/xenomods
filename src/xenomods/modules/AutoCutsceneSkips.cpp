#include "AutoCutsceneSkips.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

#include <fmt/format.h>
#include <imgui.h>
#include <nn/os.hpp>
#include <skylaunch/hookng/Hooks.hpp>
#include <skylaunch/utils/cpputils.hpp>
#include <toml++/toml.hpp>

#include <xenomods/InputBuffer.hpp>
#include <xenomods/Logger.hpp>
#include <xenomods/NnFile.hpp>
#include <xenomods/State.hpp>

#include "AutoTutorials.hpp"

namespace fw {
	struct PadData {
		std::uint32_t words[18];
	};
} // namespace fw

namespace xenomods {

	bool AutoCutsceneSkips::Enabled = false;

	namespace {
		constexpr std::uint32_t AButtonMask = 0x00000004u;
		// fw::PadData exposes the physical + button as 0x00000200. The
		// cutscene handler translates that press to its 0x00000400 action bit.
		// Only the physical bit may be injected into the control-pad data.
		constexpr std::uint32_t PhysicalPlusButtonMask = 0x00000200u;
		constexpr std::uint32_t NativePlusActionMask = 0x00000400u;
		constexpr std::uint64_t ConfirmationDelayFrames = 6;
		constexpr double TicksPerMillisecond = 19200.0;
		constexpr std::size_t TraceCapacity = 512;

		// Use exported symbols instead of fixed .text offsets. XC2's runtime layout
		// differs between executable revisions even when the decompiled functions
		// are otherwise identical.
		constexpr const char* FrameManagerSetUseSkipSymbol =
			"_ZN5event12FrameManager10setUseSkipEb";
		constexpr const char* FrameManagerUpdateSymbol =
			"_ZN5event12FrameManager6updateEv";
		constexpr const char* UiManagerOpenSkipSymbol =
			"_ZN5event9UIManager8openSkipERb";
		constexpr const char* GameEventUpdateSymbol =
			"_ZN2gf11GfGameEvent6updateERKN2fw10UpdateInfoE";
		constexpr const char* GameEventFinalizeSymbol =
			"_ZN2gf11GfGameEvent8finalizeEv";
		constexpr const char* PadManagerGetControlPadDataSymbol =
			"_ZN2fw10PadManager17getControlPadDataEi";
		constexpr const char* EventSkipInitializeSymbol =
			"_ZN2gf18GfMenuObjEventSkip10initializeEv";
		constexpr const char* EventSkipFinalizeSymbol =
			"_ZN2gf18GfMenuObjEventSkip8finalizeEv";
		constexpr const char* EventSkipListenerSymbol =
			"_ZN2gf18GfMenuObjEventSkip12ListListener11reciveEventERKN2ui9EventDataEj";
		constexpr const char* EventSkipOpenSymbol =
			"_ZN2gf18GfMenuObjEventSkip4openEPNS_17GfMenuCallContextE";
		constexpr const char* SeqManagerGetPtrSymbol =
			"_ZN2mm3mtl9SingletonIN5event10SeqManagerEE6getPtrEv";
		constexpr const char* SeqManagerStartSceneSymbol =
			"_ZN5event10SeqManager10startSceneEv";
		constexpr const char* SeqManagerEndSymbol =
			"_ZN5event10SeqManager3endEv";

		// AddrFromBase adds its argument directly to g_MainTextAddr. Ghidra's
		// displayed .text address is therefore the required fallback offset.
		constexpr std::uintptr_t FrameManagerSetUseSkipOffset = 0x00254b78;
		constexpr std::uintptr_t FrameManagerUpdateOffset = 0x00252878;
		constexpr std::uintptr_t UiManagerOpenSkipOffset = 0x0021bedc;
		constexpr std::uintptr_t GameEventUpdateOffset = 0x00475e5c;
		constexpr std::uintptr_t GameEventFinalizeOffset = 0x00475ce4;
		constexpr std::uintptr_t PadManagerGetControlPadDataOffset = 0x002c7038;
		constexpr std::uintptr_t EventSkipInitializeOffset = 0x00614dd8;
		constexpr std::uintptr_t EventSkipFinalizeOffset = 0x00614f08;
		constexpr std::uintptr_t EventSkipListenerOffset = 0x00614f10;
		constexpr std::uintptr_t EventSkipOpenOffset = 0x006152d4;
		constexpr std::uintptr_t SeqManagerGetPtrOffset = 0x0025c520;
		constexpr std::uintptr_t SeqManagerStartSceneOffset = 0x00261098;
		constexpr std::uintptr_t SeqManagerEndOffset = 0x00263c38;

		struct HookHealth {
			const char* name;
			bool installed;
			bool usedSymbol;
		};
		std::array<HookHealth, 12> hookHealth {{
			{"FrameManager::setUseSkip", false, false},
			{"FrameManager::update", false, false},
			{"UIManager::openSkip", false, false},
			{"GfGameEvent::update", false, false},
			{"GfGameEvent::finalize", false, false},
			{"PadManager::getControlPadData", false, false},
			{"EventSkip::initialize", false, false},
			{"EventSkip::finalize", false, false},
			{"EventSkip listener", false, false},
			{"EventSkip::open", false, false},
			{"SeqManager::startScene", false, false},
			{"SeqManager::end", false, false},
		}};

		enum class PlaybackPhase : std::uint8_t {
			Idle,
			WaitingForPlusAcceptance,
			WaitingForAFrame,
			AQueued,
			Finished
		};

		enum class TraceKind : std::uint8_t {
			CutsceneInitialized,
			CutsceneBecameSkippable,
			PlusReadyPoll,
			PlusRequested,
			PlusAccepted,
			ConfirmationOpenRequested,
			ConfirmationInitialized,
			ConfirmationInputLayer,
			NativePadPoll,
			IntentionalBlankFrame,
			AQueued,
			AOffered,
			AAccepted,
			ARejected,
			AMissed,
			ConfirmationFinalized,
			CutsceneCompleted,
			CutsceneCancelled,
			SceneTransition,
			StateCleared
		};

		struct TraceSample {
			TraceKind kind {};
			std::uint64_t frame = 0;
			std::uint64_t tick = 0;
			std::uintptr_t address = 0;
			std::uint32_t object = 0;
			std::uint32_t layer = 0;
			std::uint32_t enabled = 0;
			std::uint32_t held = 0;
			std::uint32_t down = 0;
			std::uint16_t primaryEvent = 0;
			std::uint16_t secondaryEvent = 0;
			std::uint32_t action = 0;
			bool accepted = false;
			bool rejected = false;
		};

		struct LayerSnapshot {
			const void* layer = nullptr;
			std::uint32_t handle = 0;
			std::uint32_t object = 0;
			std::uint32_t enabled = 0;
			std::uint32_t held = 0;
			std::uint32_t down = 0;
			std::uint16_t primaryEvent = 0;
			std::uint16_t secondaryEvent = 0;
			std::uint32_t action = 0;
			bool accepted = false;
		};

		PlaybackPhase phase = PlaybackPhase::Idle;
		bool cutsceneActive = false;
		bool nativeSkippable = false;
		bool plusRequestRecorded = false;
		bool insideGameEventUpdate = false;
		bool aAttemptInProgress = false;
		bool aAcceptedThisAttempt = false;
		bool aAttemptObserved = false;
		bool haveLastPadPoll = false;
		std::uint32_t lastPadHeld = 0;
		std::uint32_t lastPadDown = 0;
		std::uint32_t injectedMask = 0;
		std::uint64_t nativeFrame = 0;
		std::uint64_t plusAcceptedFrame = 0;
		void* confirmationMenu = nullptr;
		std::uint32_t confirmationObject = 0;
		bool lastSavedEnabled = false;
		std::uint64_t frameManagerCalls = 0;
		std::uint64_t gameEventCalls = 0;
		std::uint64_t padPollCalls = 0;
		std::uint64_t openSkipCalls = 0;
		std::uint64_t eventSkipOpenCalls = 0;
		std::uint64_t eventSkipInitializeCalls = 0;
		std::uint64_t eventSkipListenerCalls = 0;

		bool profilerCaptureEnabled = true;
		bool traceRunning = false;
		bool traceComplete = false;
		std::uint64_t traceStartFrame = 0;
		std::uint64_t traceStartTick = 0;
		std::uint32_t traceEventId = 0;
		std::array<char, 64> traceEventName {};
		std::array<TraceSample, TraceCapacity> trace {};
		std::size_t traceCount = 0;
		std::size_t traceDropped = 0;
		std::array<LayerSnapshot, 64> layerSnapshots {};

		using SeqManagerGetPtrFunction = void* (*)();
		SeqManagerGetPtrFunction seqManagerGetPtr = nullptr;

		template<typename Hook>
		void InstallVerifiedHook(
			std::size_t healthIndex,
			const char* symbol,
			std::uintptr_t fallbackOffset
		) {
			auto address = skylaunch::hook::detail::ResolveSymbolBase(symbol);
			const bool usedSymbol = address != skylaunch::hook::INVALID_FUNCTION_PTR;
			if(!usedSymbol)
				address = skylaunch::utils::AddrFromBase(fallbackOffset);
			Hook::HookAt(address);
			hookHealth[healthIndex].installed = Hook::HasApplied();
			hookHealth[healthIndex].usedSymbol = usedSymbol;
			g_Logger->LogInfo(
				"Cutscene profiler hook {}: {} ({})",
				hookHealth[healthIndex].name,
				hookHealth[healthIndex].installed ? "installed" : "FAILED",
				usedSymbol ? "symbol" : "Ghidra offset fallback"
			);
		}

		std::string SettingsPath() {
			return fmt::format(
				XENOMODS_CONFIG_PATH "/{}/tasSettings.toml",
				XENOMODS_CODENAME_STR
			);
		}

		void SaveSettingsIfChanged() {
			if(AutoCutsceneSkips::Enabled == lastSavedEnabled)
				return;
			const auto path = SettingsPath();
			toml::table root;
			toml::parse_result existing = toml::parse_file(path);
			if(existing)
				root = std::move(existing).table();
			toml::table settings;
			settings.insert_or_assign("enabled", AutoCutsceneSkips::Enabled);
			root.insert_or_assign("auto_cutscene_skips", std::move(settings));
			std::stringstream stream;
			stream << root;
			const auto contents = stream.str();
			if(NnFile::Preallocate(path, contents.size())) {
				NnFile file(path, nn::fs::OpenMode_Write);
				file.Write(contents.c_str(), contents.size());
				file.Flush();
				lastSavedEnabled = AutoCutsceneSkips::Enabled;
			}
		}

		const char* TraceName(TraceKind kind) {
			switch(kind) {
			case TraceKind::CutsceneBecameSkippable: return "Native-skippable cutscene initialized";
				case TraceKind::CutsceneInitialized: return "Cutscene initialized";
				case TraceKind::PlusReadyPoll: return "Native + readiness poll";
				case TraceKind::PlusRequested: return "Auto + submitted";
				case TraceKind::PlusAccepted: return "Native + accepted";
				case TraceKind::ConfirmationOpenRequested: return "Confirmation UI open requested";
				case TraceKind::ConfirmationInitialized: return "Confirmation UI initialized";
				case TraceKind::ConfirmationInputLayer: return "Confirmation UIInputLayer";
				case TraceKind::NativePadPoll: return "Native control-pad poll";
				case TraceKind::IntentionalBlankFrame: return "Intentional empty frame";
				case TraceKind::AQueued: return "A queued for N+6";
				case TraceKind::AOffered: return "A offered to confirmation layer";
				case TraceKind::AAccepted: return "Native A accepted";
				case TraceKind::ARejected: return "Native A rejected";
				case TraceKind::AMissed: return "A missed: confirmation layer did not poll";
				case TraceKind::ConfirmationFinalized: return "Confirmation UI finalized";
				case TraceKind::CutsceneCompleted: return "Cutscene completed";
				case TraceKind::CutsceneCancelled: return "Cutscene cancelled";
				case TraceKind::SceneTransition: return "Scene transition";
				case TraceKind::StateCleared: return "Playback state cleared";
			}
			return "Unknown";
		}

		void Record(
			TraceKind kind,
			std::uintptr_t address = 0,
			std::uint32_t object = 0,
			std::uint32_t layer = 0,
			std::uint32_t enabled = 0,
			std::uint32_t held = 0,
			std::uint32_t down = 0,
			std::uint16_t primaryEvent = 0,
			std::uint16_t secondaryEvent = 0,
			std::uint32_t action = 0,
			bool accepted = false,
			bool rejected = false
		) {
			if(!traceRunning)
				return;
			if(traceCount >= trace.size()) {
				traceDropped++;
				return;
			}
			trace[traceCount++] = {
				kind, nativeFrame, nn::os::GetSystemTick(), address, object, layer,
				enabled, held, down, primaryEvent, secondaryEvent, action,
				accepted, rejected
			};
		}

		void ReadEventIdentity() {
			traceEventName = {};
			traceEventId = 0;
			if(seqManagerGetPtr == nullptr)
				return;
			const auto manager = reinterpret_cast<const std::uint8_t*>(seqManagerGetPtr());
			if(manager == nullptr)
				return;
			const char* name = reinterpret_cast<const char*>(manager + 0x271c);
			for(std::size_t i = 0; i + 1 < traceEventName.size() && name[i] != '\0'; i++)
				traceEventName[i] = name[i];
			traceEventId = *reinterpret_cast<const std::uint32_t*>(manager + 0x2760);
		}

		void StartTrace() {
			if(!profilerCaptureEnabled)
				return;
			trace = {};
			traceCount = 0;
			traceDropped = 0;
			layerSnapshots = {};
			traceStartFrame = nativeFrame;
			traceStartTick = nn::os::GetSystemTick();
			traceRunning = true;
			traceComplete = false;
			ReadEventIdentity();
			InputBuffer::SetInputLayerPadCapture(true);
			Record(TraceKind::CutsceneInitialized);
		}

		void FinishTrace(TraceKind kind) {
			if(!traceRunning)
				return;
			Record(kind);
			traceRunning = false;
			traceComplete = kind == TraceKind::CutsceneCompleted;
			InputBuffer::SetInputLayerPadCapture(false);
		}

		void ResetPlaybackOnly() {
			if(confirmationObject != 0)
				InputBuffer::CancelAutoUiAction(confirmationObject);
			phase = PlaybackPhase::Idle;
			cutsceneActive = false;
			nativeSkippable = false;
			plusRequestRecorded = false;
			insideGameEventUpdate = false;
			aAttemptInProgress = false;
			aAcceptedThisAttempt = false;
			aAttemptObserved = false;
			injectedMask = 0;
			plusAcceptedFrame = 0;
			confirmationMenu = nullptr;
			confirmationObject = 0;
			haveLastPadPoll = false;
			lastPadHeld = 0;
			lastPadDown = 0;
		}

		void BeginCutscene() {
			ResetPlaybackOnly();
			cutsceneActive = true;
			phase = AutoCutsceneSkips::Enabled
				? PlaybackPhase::WaitingForPlusAcceptance
				: PlaybackPhase::Idle;
			if(!traceRunning)
				StartTrace();
		}

		std::uint32_t LayerEnabledMask(const void* inputLayer) {
			if(inputLayer == nullptr)
				return 0;
			constexpr std::array<std::uint32_t, 27> UiPadToFwPad {
				0x00000000, 0x00000004, 0x00000002, 0x00000004, 0x00000002,
				0x00000008, 0x00000001, 0x00001000, 0x00004000, 0x00002000,
				0x00008000, 0x00000010, 0x00000040, 0x00000400, 0x00000020,
				0x00000080, 0x00000800, 0x00000200, 0x00000100, 0x01000000,
				0x04000000, 0x02000000, 0x08000000, 0x10000000, 0x40000000,
				0x20000000, 0x80000000
			};
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(inputLayer);
			const auto count = *reinterpret_cast<const std::uint64_t*>(bytes + 0x200);
			if(count > UiPadToFwPad.size())
				return 0;
			std::uint32_t mask = 0;
			for(std::uint64_t index = 0; index < count; index++) {
				const auto* entry = bytes + 0x50 + index * 0x10;
				const auto uiPad = *reinterpret_cast<const std::uint16_t*>(entry);
				if(uiPad < UiPadToFwPad.size() && entry[0xf] != 0)
					mask |= UiPadToFwPad[uiPad];
			}
			return mask;
		}

		LayerSnapshot* SnapshotFor(const void* layer) {
			LayerSnapshot* free = nullptr;
			for(auto& snapshot : layerSnapshots) {
				if(snapshot.layer == layer)
					return &snapshot;
				if(free == nullptr && snapshot.layer == nullptr)
					free = &snapshot;
			}
			return free;
		}

		struct FrameManagerSetUseSkipHook
			: skylaunch::hook::Trampoline<FrameManagerSetUseSkipHook> {
			static void Hook(void* manager, bool useSkip) {
				Orig(manager, useSkip);
				// In this executable setUseSkip(bool) is an empty marker. Keep the
				// hook as an observational hint, but never use it to begin or end the
				// automation. The real pre-input lifetime begins in startScene(), and
				// UIManager::openSkip is the native proof that + was accepted.
				(void)useSkip;
			}
		};

		struct SeqManagerStartSceneHook
			: skylaunch::hook::Trampoline<SeqManagerStartSceneHook> {
			static void Hook(void* manager) {
				if(traceRunning)
					FinishTrace(TraceKind::SceneTransition);
				ResetPlaybackOnly();
				Orig(manager);
				// startScene is reached while the sequence is being constructed,
				// before the cutscene controller can poll +. Arm here and let the
				// native controller reject the request until/if this scene is
				// actually skippable.
				BeginCutscene();
			}
		};

		struct SeqManagerEndHook
			: skylaunch::hook::Trampoline<SeqManagerEndHook> {
			static void Hook(void* manager) {
				if(traceRunning)
					FinishTrace(TraceKind::CutsceneCompleted);
				ResetPlaybackOnly();
				Orig(manager);
			}
		};

		struct FrameManagerUpdateHook
			: skylaunch::hook::Trampoline<FrameManagerUpdateHook> {
			static void Hook(void* manager) {
				frameManagerCalls++;
				if(traceRunning || cutsceneActive)
					nativeFrame++;
				Orig(manager);
			}
		};

		struct PadManagerGetControlPadDataHook
			: skylaunch::hook::Trampoline<PadManagerGetControlPadDataHook> {
			static fw::PadData* Hook(int pad) {
				fw::PadData* original = Orig(pad);
				if(pad == 0)
					padPollCalls++;
				if(
					pad == 0 && original != nullptr && profilerCaptureEnabled
					&& !traceRunning
					&& ((original->words[0] | original->words[1])
						& PhysicalPlusButtonMask) != 0
				)
					StartTrace();
				if(pad == 0 && traceRunning && original != nullptr) {
					const auto held = original->words[0];
					const auto down = original->words[1];
					if(!haveLastPadPoll || held != lastPadHeld || down != lastPadDown) {
						Record(
							TraceKind::NativePadPoll,
							reinterpret_cast<std::uintptr_t>(original),
							0, 0, 0, held, down
						);
						haveLastPadPoll = true;
						lastPadHeld = held;
						lastPadDown = down;
					}
				}
				std::uint32_t mask = injectedMask;
				if(
					pad == 0 && AutoCutsceneSkips::Enabled && cutsceneActive
					&& phase == PlaybackPhase::WaitingForPlusAcceptance
				)
					mask |= PhysicalPlusButtonMask;
				if(pad != 0 || mask == 0 || original == nullptr)
					return original;
				if(
					(mask & PhysicalPlusButtonMask) != 0
					&& !plusRequestRecorded
				) {
					Record(
						TraceKind::PlusReadyPoll,
						reinterpret_cast<std::uintptr_t>(original),
						0, 0, NativePlusActionMask,
						original->words[0], original->words[1]
					);
					Record(
						TraceKind::PlusRequested,
						reinterpret_cast<std::uintptr_t>(original),
						0, 0, NativePlusActionMask,
						PhysicalPlusButtonMask, PhysicalPlusButtonMask
					);
					plusRequestRecorded = true;
				}
				static fw::PadData injected {};
				std::memcpy(&injected, original, sizeof(injected));
				injected.words[0] |= mask;
				injected.words[1] |= mask;
				injected.words[3] |= mask;
				return &injected;
			}
		};

		struct UiManagerOpenSkipHook
			: skylaunch::hook::Trampoline<UiManagerOpenSkipHook> {
			static std::uint64_t Hook(void* manager, bool* result) {
				openSkipCalls++;
				const bool firstNativeAcceptance = cutsceneActive && manager != nullptr
					&& (*reinterpret_cast<const std::uint32_t*>(
						reinterpret_cast<const std::uint8_t*>(manager) + 0x10
					) & 1u) == 0;
				if(firstNativeAcceptance && profilerCaptureEnabled && !traceRunning)
					StartTrace();
				const auto value = Orig(manager, result);
				if(firstNativeAcceptance) {
					if(!nativeSkippable) {
						nativeSkippable = true;
						Record(TraceKind::CutsceneBecameSkippable);
					}
					Record(
						TraceKind::PlusAccepted,
						reinterpret_cast<std::uintptr_t>(manager),
						0, 0, NativePlusActionMask,
						PhysicalPlusButtonMask, PhysicalPlusButtonMask,
						0, 0, 0, true
					);
				}
				if(
					firstNativeAcceptance
					&& phase == PlaybackPhase::WaitingForPlusAcceptance
				) {
					plusAcceptedFrame = nativeFrame;
					phase = PlaybackPhase::WaitingForAFrame;
					// The native branch has consumed +. Remove it immediately so any
					// later pad poll in this same event update sees a neutral pad.
					injectedMask = 0;
				}
				return value;
			}
		};

		struct GameEventUpdateHook
			: skylaunch::hook::Trampoline<GameEventUpdateHook> {
			static void Hook(fw::UpdateInfo* updateInfo) {
				gameEventCalls++;
				insideGameEventUpdate = true;
				injectedMask = 0;
				// A is scheduled by the confirmation layer itself, not by assuming an
				// ordering between the event and menu managers. Once the native clock
				// has moved beyond N+6 without that layer offering A, the single valid
				// attempt was genuinely missed.
				if(
					AutoCutsceneSkips::Enabled && cutsceneActive
					&& (phase == PlaybackPhase::WaitingForAFrame
						|| (phase == PlaybackPhase::AQueued && !aAttemptObserved))
					&& nativeFrame > plusAcceptedFrame + ConfirmationDelayFrames
				) {
					Record(
						TraceKind::AMissed, 0, confirmationObject, 0,
						AButtonMask, 0, 0, 0, 0, 0, false, true
					);
					InputBuffer::CancelAutoUiAction(confirmationObject);
					aAttemptInProgress = false;
					phase = PlaybackPhase::Finished;
				}
				std::uint64_t confirmationElapsed = 0;
				if(AutoCutsceneSkips::Enabled && cutsceneActive) {
					if(phase == PlaybackPhase::WaitingForPlusAcceptance) {
						// + is supplied directly at the cutscene controller's native pad
						// poll. That poll is not always nested inside GfGameEvent::update.
					} else if(phase == PlaybackPhase::WaitingForAFrame) {
						confirmationElapsed = nativeFrame - plusAcceptedFrame;
						if(confirmationElapsed >= 1 && confirmationElapsed <= 5) {
							Record(TraceKind::IntentionalBlankFrame);
						}
					}
				}
				Orig(updateInfo);
				injectedMask = 0;
				insideGameEventUpdate = false;
			}
		};

		struct GameEventFinalizeHook
			: skylaunch::hook::Trampoline<GameEventFinalizeHook> {
			static void Hook(void* gameEvent) {
				if(cutsceneActive)
					FinishTrace(TraceKind::CutsceneCancelled);
				ResetPlaybackOnly();
				Orig(gameEvent);
			}
		};

		struct EventSkipOpenHook
			: skylaunch::hook::Trampoline<EventSkipOpenHook> {
			static int Hook(void* context) {
				eventSkipOpenCalls++;
				if(profilerCaptureEnabled && !traceRunning)
					StartTrace();
				Record(
					TraceKind::ConfirmationOpenRequested,
					reinterpret_cast<std::uintptr_t>(context)
				);
				return Orig(context);
			}
		};

		struct EventSkipInitializeHook
			: skylaunch::hook::Trampoline<EventSkipInitializeHook> {
			static void Hook(void* menu) {
				eventSkipInitializeCalls++;
				if(profilerCaptureEnabled && !traceRunning)
					StartTrace();
				confirmationMenu = menu;
				Orig(menu);
				Record(
					TraceKind::ConfirmationInitialized,
					reinterpret_cast<std::uintptr_t>(menu)
				);
			}
		};

		struct EventSkipFinalizeHook
			: skylaunch::hook::Trampoline<EventSkipFinalizeHook> {
			static void Hook(void* menu) {
				Record(
					TraceKind::ConfirmationFinalized,
					reinterpret_cast<std::uintptr_t>(menu),
					confirmationObject
				);
				if(confirmationMenu == menu) {
					if(confirmationObject != 0)
						InputBuffer::CancelAutoUiAction(confirmationObject);
					confirmationMenu = nullptr;
					confirmationObject = 0;
				}
				Orig(menu);
			}
		};

		struct EventSkipListenerHook
			: skylaunch::hook::Trampoline<EventSkipListenerHook> {
			static void Hook(
				void* listener,
				const void* eventData,
				std::uint32_t sender
			) {
				eventSkipListenerCalls++;
				const auto event = eventData == nullptr
					? 0
					: *reinterpret_cast<const std::uint16_t*>(eventData);
				const auto action = eventData == nullptr
					? 0
					: *reinterpret_cast<const std::uint32_t*>(
						reinterpret_cast<const std::uint8_t*>(eventData) + 0xc
					);
				if(sender != 0)
					confirmationObject = sender;
				Orig(listener, eventData, sender);
				if(aAttemptInProgress && event == 8 && action == 1) {
					aAcceptedThisAttempt = true;
					injectedMask = 0;
					Record(
						TraceKind::AAccepted,
						reinterpret_cast<std::uintptr_t>(listener),
						sender, 0, AButtonMask, AButtonMask, AButtonMask,
						event, 8, action, true
					);
				}
			}
		};
	} // namespace

	void AutoCutsceneSkips::SetEnabled(bool enabled) {
		if(Enabled == enabled)
			return;
		Enabled = enabled;
		if(Enabled && cutsceneActive) {
			phase = PlaybackPhase::WaitingForPlusAcceptance;
			plusRequestRecorded = false;
		} else if(!Enabled) {
			phase = cutsceneActive ? PlaybackPhase::Finished : PlaybackPhase::Idle;
			injectedMask = 0;
			if(confirmationObject != 0)
				InputBuffer::CancelAutoUiAction(confirmationObject);
			aAttemptInProgress = false;
			aAcceptedThisAttempt = false;
		}
		SaveSettingsIfChanged();
	}

	void AutoCutsceneSkips::MenuSection() {
		AutoTutorials::MenuSection();
		bool enabled = Enabled;
		if(ImGui::Checkbox("Auto Cutscene Skips", &enabled))
			SetEnabled(enabled);
	}

	void AutoCutsceneSkips::ProfilerSection() {
		const auto installedHooks = std::count_if(
			hookHealth.begin(), hookHealth.end(),
			[](const HookHealth& health) { return health.installed; }
		);
		if(installedHooks == hookHealth.size()) {
			ImGui::TextColored(
				ImVec4(0.3f, 1.f, 0.4f, 1.f),
				"Native hooks ready (%llu/%llu)",
				static_cast<unsigned long long>(installedHooks),
				static_cast<unsigned long long>(hookHealth.size())
			);
		} else {
			ImGui::TextColored(
				ImVec4(1.f, 0.3f, 0.2f, 1.f),
				"Native hooks incomplete (%llu/%llu)",
				static_cast<unsigned long long>(installedHooks),
				static_cast<unsigned long long>(hookHealth.size())
			);
			if(ImGui::TreeNode("Missing cutscene hooks")) {
				for(const auto& health : hookHealth) {
					if(!health.installed)
						ImGui::BulletText("%s", health.name);
				}
				ImGui::TreePop();
			}
		}
		if(ImGui::Checkbox("Capture", &profilerCaptureEnabled)) {
			if(!profilerCaptureEnabled && traceRunning)
				FinishTrace(TraceKind::CutsceneCancelled);
		}
		ImGui::SameLine();
		if(ImGui::Button("Restart now")) {
			if(cutsceneActive)
				StartTrace();
		}
		ImGui::SameLine();
		if(ImGui::Button("Clear cutscene trace")) {
			trace = {};
			traceCount = 0;
			traceDropped = 0;
			traceRunning = false;
			traceComplete = false;
		}
		ImGui::SameLine();
		if(traceRunning)
			ImGui::TextColored(ImVec4(0.3f, 1.f, 0.4f, 1.f), "CAPTURING");
		else if(traceComplete)
			ImGui::TextColored(ImVec4(0.3f, 1.f, 0.4f, 1.f), "COMPLETE");
		else
			ImGui::TextDisabled("Waiting for cutscene sequence");

		ImGui::Text(
			"Event: %s  ID: %u  Native frame: %llu  Samples: %llu  Dropped: %llu",
			traceEventName[0] == '\0' ? "<unknown>" : traceEventName.data(),
			traceEventId,
			static_cast<unsigned long long>(nativeFrame),
			static_cast<unsigned long long>(traceCount),
			static_cast<unsigned long long>(traceDropped)
		);
		if(ImGui::BeginTable(
			"CutsceneSkipTrace",
			8,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
				| ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
			ImVec2(0.f, -1.f)
		)) {
			ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 230.f);
			ImGui::TableSetupColumn("Df", ImGuiTableColumnFlags_WidthFixed, 42.f);
			ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 80.f);
			ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthFixed, 65.f);
			ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed, 85.f);
			ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 85.f);
			ImGui::TableSetupColumn("Held / Down", ImGuiTableColumnFlags_WidthFixed, 180.f);
			ImGui::TableSetupColumn("Events / Action / Result", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();
			for(std::size_t i = 0; i < traceCount; i++) {
				const auto& sample = trace[i];
				const bool colored = sample.accepted || sample.rejected;
				if(colored)
					ImGui::PushStyleColor(
						ImGuiCol_Text,
						sample.accepted
							? ImVec4(0.25f, 1.f, 0.35f, 1.f)
							: ImVec4(1.f, 0.35f, 0.25f, 1.f)
					);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(TraceName(sample.kind));
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%llu", static_cast<unsigned long long>(sample.frame - traceStartFrame));
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%.3f", static_cast<double>(sample.tick - traceStartTick) / TicksPerMillisecond);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", sample.object);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%u", sample.layer);
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%08X", sample.enabled);
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%08X / %08X", sample.held, sample.down);
				ImGui::TableSetColumnIndex(7);
				ImGui::Text(
					"%X / %X  action=%u  %s  addr=%p",
					sample.primaryEvent,
					sample.secondaryEvent,
					sample.action,
					sample.accepted ? "accepted" : sample.rejected ? "rejected" : "-",
					reinterpret_cast<void*>(sample.address)
				);
				if(colored)
					ImGui::PopStyleColor();
			}
			ImGui::EndTable();
		}
	}

	void AutoCutsceneSkips::ClearRuntimeState(bool interrupted) {
		if(traceRunning && interrupted)
			FinishTrace(TraceKind::StateCleared);
		ResetPlaybackOnly();
	}

	void AutoCutsceneSkips::OnInputLayerUpdate(
		std::uint32_t object,
		const void* inputLayer,
		std::uint32_t layerHandle,
		bool afterUpdate,
		bool accepted,
		std::uint16_t emittedEvent,
		std::uint16_t secondaryEvent,
		std::uint32_t secondaryAction,
		std::uint32_t heldButtons,
		std::uint32_t downButtons
	) {
		if(
			inputLayer == nullptr
			|| (phase != PlaybackPhase::WaitingForAFrame && !aAttemptInProgress)
		)
			return;
		const auto enabled = LayerEnabledMask(inputLayer);
		// The confirmation layer is the one exposing A. Before the listener reports
		// its sender, this mask is the strongest native discriminator available.
		if((enabled & AButtonMask) == 0 && object != confirmationObject)
			return;
		auto* snapshot = SnapshotFor(inputLayer);
		if(snapshot == nullptr) {
			traceDropped++;
			return;
		}
		if(!afterUpdate) {
			// Queue from inside the exact confirmation layer's N+6 update. The
			// shared UI hook invokes this callback immediately before calling the
			// native layer, so the action is available to that same poll regardless
			// of whether this cutscene updates menus before or after game events.
			if(
				AutoCutsceneSkips::Enabled && cutsceneActive
				&& phase == PlaybackPhase::WaitingForAFrame
				&& object == confirmationObject
				&& (enabled & AButtonMask) != 0
				&& nativeFrame == plusAcceptedFrame + ConfirmationDelayFrames
			) {
				aAttemptInProgress = true;
				aAcceptedThisAttempt = false;
				aAttemptObserved = false;
				phase = PlaybackPhase::AQueued;
				InputBuffer::RequestAutoUiAction(confirmationObject, AButtonMask);
				Record(
					TraceKind::AQueued,
					reinterpret_cast<std::uintptr_t>(inputLayer),
					object, layerHandle, enabled, 0, 0
				);
			}
			return;
		}
		if(
			aAttemptInProgress && !aAttemptObserved
			&& object == confirmationObject
			&& ((heldButtons | downButtons) & AButtonMask) != 0
		) {
			aAttemptObserved = true;
			Record(
				TraceKind::AOffered,
				reinterpret_cast<std::uintptr_t>(inputLayer),
				object, layerHandle, enabled, heldButtons, downButtons,
				emittedEvent, secondaryEvent, secondaryAction
			);
			const bool nativeAccepted = accepted
				&& secondaryEvent == 8 && secondaryAction == 1;
			if(nativeAccepted) {
				aAcceptedThisAttempt = true;
				Record(
					TraceKind::AAccepted,
					reinterpret_cast<std::uintptr_t>(inputLayer),
					object, layerHandle, enabled, heldButtons, downButtons,
					emittedEvent, secondaryEvent, secondaryAction, true
				);
			} else {
				Record(
					TraceKind::ARejected,
					reinterpret_cast<std::uintptr_t>(inputLayer),
					object, layerHandle, enabled, heldButtons, downButtons,
					emittedEvent, secondaryEvent, secondaryAction, false, true
				);
				InputBuffer::CancelAutoUiAction(confirmationObject);
			}
			aAttemptInProgress = false;
			phase = PlaybackPhase::Finished;
		}
		const bool changed = snapshot->layer == nullptr
			|| snapshot->handle != layerHandle || snapshot->object != object
			|| snapshot->enabled != enabled || snapshot->held != heldButtons
			|| snapshot->down != downButtons || snapshot->primaryEvent != emittedEvent
			|| snapshot->secondaryEvent != secondaryEvent
			|| snapshot->action != secondaryAction || snapshot->accepted != accepted;
		*snapshot = {
			inputLayer, layerHandle, object, enabled, heldButtons, downButtons,
			emittedEvent, secondaryEvent, secondaryAction, accepted
		};
		if(changed)
			Record(
				TraceKind::ConfirmationInputLayer,
				reinterpret_cast<std::uintptr_t>(inputLayer),
				object, layerHandle, enabled, heldButtons, downButtons,
				emittedEvent, secondaryEvent, secondaryAction, accepted
			);
	}

	void AutoCutsceneSkips::Initialize() {
#if XENOMODS_CODENAME(bf2)
		const toml::parse_result settings = toml::parse_file(SettingsPath());
		if(settings)
			Enabled = settings["auto_cutscene_skips"]["enabled"].value_or(false);
		lastSavedEnabled = Enabled;
		auto seqManagerAddress = skylaunch::hook::detail::ResolveSymbolBase(
			SeqManagerGetPtrSymbol
		);
		if(seqManagerAddress == skylaunch::hook::INVALID_FUNCTION_PTR)
			seqManagerAddress = skylaunch::utils::AddrFromBase(SeqManagerGetPtrOffset);
		seqManagerGetPtr = reinterpret_cast<SeqManagerGetPtrFunction>(seqManagerAddress);

		InstallVerifiedHook<FrameManagerSetUseSkipHook>(
			0, FrameManagerSetUseSkipSymbol, FrameManagerSetUseSkipOffset
		);
		InstallVerifiedHook<FrameManagerUpdateHook>(
			1, FrameManagerUpdateSymbol, FrameManagerUpdateOffset
		);
		InstallVerifiedHook<UiManagerOpenSkipHook>(
			2, UiManagerOpenSkipSymbol, UiManagerOpenSkipOffset
		);
		InstallVerifiedHook<GameEventUpdateHook>(
			3, GameEventUpdateSymbol, GameEventUpdateOffset
		);
		InstallVerifiedHook<GameEventFinalizeHook>(
			4, GameEventFinalizeSymbol, GameEventFinalizeOffset
		);
		InstallVerifiedHook<PadManagerGetControlPadDataHook>(
			5, PadManagerGetControlPadDataSymbol, PadManagerGetControlPadDataOffset
		);
		InstallVerifiedHook<EventSkipInitializeHook>(
			6, EventSkipInitializeSymbol, EventSkipInitializeOffset
		);
		InstallVerifiedHook<EventSkipFinalizeHook>(
			7, EventSkipFinalizeSymbol, EventSkipFinalizeOffset
		);
		InstallVerifiedHook<EventSkipListenerHook>(
			8, EventSkipListenerSymbol, EventSkipListenerOffset
		);
		InstallVerifiedHook<EventSkipOpenHook>(
			9, EventSkipOpenSymbol, EventSkipOpenOffset
		);
		InstallVerifiedHook<SeqManagerStartSceneHook>(
			10, SeqManagerStartSceneSymbol, SeqManagerStartSceneOffset
		);
		InstallVerifiedHook<SeqManagerEndHook>(
			11, SeqManagerEndSymbol, SeqManagerEndOffset
		);
		g_Logger->LogInfo(
			"Auto cutscene profiler installed {}/{} verified native hooks",
			std::count_if(
				hookHealth.begin(), hookHealth.end(),
				[](const HookHealth& health) { return health.installed; }
			),
			hookHealth.size()
		);
#endif
		UpdatableModule::Initialize();
	}

	void AutoCutsceneSkips::OnSceneTransition() {
		if(traceRunning)
			FinishTrace(TraceKind::SceneTransition);
		ResetPlaybackOnly();
	}

	XENOMODS_REGISTER_MODULE(AutoCutsceneSkips);

} // namespace xenomods
