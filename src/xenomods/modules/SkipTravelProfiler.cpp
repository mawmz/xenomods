#include "SkipTravelProfiler.hpp"
#include "MenuHelper.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <fmt/format.h>
#include <imgui.h>
#include <nn/os.hpp>
#include <skylaunch/hookng/Hooks.hpp>

#include "xenomods/Logger.hpp"
#include "xenomods/engine/ui/UIObjectAcc.hpp"
#include "xenomods/menu/Menu.hpp"

namespace {

	enum class ProfileEvent : std::uint8_t {
		ButtonPressed,
		OpenCommandStarted,
		OpenCommandQueued,
		OpenCommandRejected,
		FullscreenTransitionStarted,
		FullscreenLayerCreated,
		UiFactoryReady,
		FadeInComplete,
		WorldImageRequested,
		WorldImageReady,
		WorldLocatorStarted,
		WorldLocatorReady,
		WorldSeqNextStarted,
		ZoneImageRequested,
		ZoneImageReady,
		ZoneLocatorStarted,
		ZoneLocatorReady,
		ZoneSeqNextStarted,
		FullscreenInputEnabled,
		OperationControlsRequested,
		SeqNextComplete,
		WorldLoadTexCalled,
		WorldLoadTexQueued,
		WorldLoadTexUnchanged,
		PointListStateObserved,
		PointListFlagChanged,
		PointOpenReceived,
		PointAnimationStarted,
		PointAnimationHooked,
		PointAnimationSample,
		PointOpenHandled,
		PointAnimationCompleteReceived,
		PointAnimationCompleteHandled,
		Interrupted,
		Count
	};

	struct EventSample {
		ProfileEvent event {};
		std::uint64_t frame = 0;
		std::uint64_t tick = 0;
		std::uintptr_t address = 0;
		std::array<std::uint32_t, 4> data {};
		float animationTime = 0.f;
		float animationLength = 0.f;
	};

	constexpr std::size_t MaxEvents = 192;
	constexpr std::size_t HistoryCapacity = 8;
	constexpr double TicksPerMillisecond = 19200.0;
	constexpr std::size_t WorldMapPointListOffset = 0x2f0;
	constexpr std::size_t WorldMapHookedAnimationOffset = 0x318;
	constexpr std::size_t WorldMapCachedFloorOffset = 0x2f80;
	constexpr std::size_t PointListSizeOffset = 0x20;
	constexpr std::size_t PointListInputDisabledOffset = 0x79;
	constexpr std::size_t PointListenerWorldMapOffset = 0x20;
	constexpr std::uint16_t AnimationCompleteEvent = 12;
	constexpr std::uint16_t PointOpenEvent = 0x103;
	constexpr std::uint64_t ForcedFullscreenInputFrame = 42;

	struct ProfileRun {
		std::uint64_t id = 0;
		std::uint64_t startFrame = 0;
		std::uint64_t startTick = 0;
		std::array<EventSample, MaxEvents> events {};
		std::size_t eventCount = 0;
		bool fromXButton = false;
		bool running = false;
		bool completed = false;
		std::size_t droppedEvents = 0;
	};

	struct AnimationCandidate {
		std::uint32_t objectId = 0;
		ui::UIObject* object = nullptr;
	};

	struct UIObjectAccProbe {
		void* vtable = nullptr;
		ui::UIObject* uiObject = nullptr;
		std::uint32_t objectId = 0;
	};

	using AnimationPlayingFunction = bool (*)(ui::UIObjectAcc*);
	using AnimationFloatFunction = float (*)(ui::UIObjectAcc*);

	bool showWindow = false;
	bool captureEnabled = true;
	bool insideFullscreenWaitScreen = false;
	bool insideFullscreenWaitFadeIn = false;
	std::uint64_t profilerFrame = 0;
	std::uint64_t nextRunId = 1;
	ProfileRun currentRun {};
	std::array<ProfileRun, HistoryCapacity> history {};
	std::size_t historyWrite = 0;
	std::size_t historyCount = 0;
	int selectedHistoryNewest = 0;
	bool insidePointOpenEvent = false;
	void* trackedWorldMap = nullptr;
	void* trackedPointList = nullptr;
	bool havePointListState = false;
	std::uint8_t lastPointListFlag = 0;
	std::uint32_t lastPointListSize = 0;
	std::uint16_t lastCachedFloor = 0;
	std::array<AnimationCandidate, 16> animationCandidates {};
	std::size_t animationCandidateCount = 0;
	std::uint32_t trackedAnimationObjectId = 0;
	ui::UIObject* trackedAnimationObject = nullptr;
	bool haveAnimationSample = false;
	bool lastAnimationPlaying = false;
	float lastAnimationTime = 0.f;
	float lastAnimationLength = 0.f;
	AnimationPlayingFunction animationIsPlaying = nullptr;
	AnimationFloatFunction animationPlaybackLength = nullptr;
	AnimationFloatFunction animationPlaybackTime = nullptr;
	bool forcedFullscreenInputArmed = false;
	bool forcedFullscreenInputOpenConfirmed = false;
	std::uint64_t forcedFullscreenInputStartFrame = 0;
	std::uint32_t forcedOperationControlsObject = 0;
	std::uint32_t suppressNativeOperationControlsObject = 0;
	bool insideForcedOperationControlsCall = false;

	enum class AuxTraceKind : std::uint8_t {
		ShopRequest,
		OrbListEnter,
		OrbListReturn,
		RecipeEnter,
		RecipeReturn,
		RecipeSuppressed,
		EnableInputEnter,
		EnableInputReturn,
		ResetSelectionEnter,
		ResetSelectionReturn,
		SetupSelectionFromListEnter,
		SetupSelectionFromListReturn,
		ManagerSetupSelectionEnter,
		ManagerSetupSelectionReturn,
		ManagerEnableEnter,
		ManagerEnableReturn,
		ManagerSendEventEnter,
		ManagerSendEventReturn,
		LayerUpdateEnter,
		LayerUpdateReturn,
		PadUpdateEnter,
		PadUpdateReturn,
		MoveSelectEnter,
		MoveSelectReturn,
		PhysicalAction,
		PlaybackAction
	};

	struct AuxTraceSample {
		AuxTraceKind kind {};
		std::uint64_t frame = 0;
		std::uint64_t tick = 0;
		std::uintptr_t address = 0;
		std::uint32_t object = 0;
		std::uint32_t event = 0;
		std::uint32_t action = 0;
		std::uint32_t layer = 0;
		std::uint32_t held = 0;
		std::uint32_t down = 0;
		bool accepted = false;
	};

	constexpr std::size_t AuxTraceCapacity = 1024;
	std::array<AuxTraceSample, AuxTraceCapacity> auxTrace {};
	std::size_t auxTraceCount = 0;
	std::size_t auxTraceDropped = 0;
	std::uint64_t auxTraceStartFrame = 0;
	std::uint64_t auxTraceStartTick = 0;
	std::uint32_t auxRecipeObject = 0;
	std::uint32_t auxListObject = 0;
	bool auxTraceRunning = false;
	bool auxTraceComplete = false;
	bool auxCaptureEnabled = true;
	bool auxHaveLayerSample = false;
	std::uintptr_t auxLastInputLayer = 0;
	std::uint32_t auxLastLayerHandle = 0;
	std::uint16_t auxLastEmittedEvent = 0;
	std::uint32_t auxLastHeldButtons = 0;
	std::uint32_t auxLastDownButtons = 0;
	std::uint32_t insideAuxRecipeHandler = 0;
	std::uint32_t insideAuxListHandler = 0;
	bool auxOrbListHookInstalled = false;
	bool auxResetSelectionHookInstalled = false;
	bool auxSetupSelectionHookInstalled = false;
	bool auxManagerSetupHookInstalled = false;
	bool auxManagerEnableHookInstalled = false;
	bool auxManagerSendHookInstalled = false;
	bool auxPadUpdateHookInstalled = false;
	bool auxMoveSelectHookInstalled = false;

	const char* AuxTraceName(AuxTraceKind kind) {
		switch(kind) {
			case AuxTraceKind::ShopRequest: return "MainListener shop request";
			case AuxTraceKind::OrbListEnter: return "OrbListListener enter";
			case AuxTraceKind::OrbListReturn: return "OrbListListener return";
			case AuxTraceKind::RecipeEnter: return "OrbRecipeListener enter";
			case AuxTraceKind::RecipeReturn: return "OrbRecipeListener return";
			case AuxTraceKind::RecipeSuppressed: return "OrbRecipeListener suppressed";
			case AuxTraceKind::EnableInputEnter: return "GfMenuObject::enableInput enter";
			case AuxTraceKind::EnableInputReturn: return "GfMenuObject::enableInput return";
			case AuxTraceKind::ResetSelectionEnter: return "GfMenuObject::resetInputSelection enter";
			case AuxTraceKind::ResetSelectionReturn: return "GfMenuObject::resetInputSelection return";
			case AuxTraceKind::SetupSelectionFromListEnter: return "GfMenuObject::setupInputSelectionFromList enter";
			case AuxTraceKind::SetupSelectionFromListReturn: return "GfMenuObject::setupInputSelectionFromList return";
			case AuxTraceKind::ManagerSetupSelectionEnter: return "UIInputManager::setupInputSelection enter";
			case AuxTraceKind::ManagerSetupSelectionReturn: return "UIInputManager::setupInputSelection return";
			case AuxTraceKind::ManagerEnableEnter: return "UIInputManager::enableInput enter";
			case AuxTraceKind::ManagerEnableReturn: return "UIInputManager::enableInput return";
			case AuxTraceKind::ManagerSendEventEnter: return "UIInputManager::sendEvent enter";
			case AuxTraceKind::ManagerSendEventReturn: return "UIInputManager::sendEvent return";
			case AuxTraceKind::LayerUpdateEnter: return "UIInputLayer::update enter";
			case AuxTraceKind::LayerUpdateReturn: return "UIInputLayer::update return";
			case AuxTraceKind::PadUpdateEnter: return "UIInputPad::updateImpl enter";
			case AuxTraceKind::PadUpdateReturn: return "UIInputPad::updateImpl return";
			case AuxTraceKind::MoveSelectEnter: return "UIInputPad::moveSelect enter";
			case AuxTraceKind::MoveSelectReturn: return "UIInputPad::moveSelect return";
			case AuxTraceKind::PhysicalAction: return "Physical action accepted";
			case AuxTraceKind::PlaybackAction: return "Playback action accepted";
		}
		return "Unknown";
	}

	void ClearAuxTrace() {
		auxTraceCount = 0;
		auxTraceDropped = 0;
		auxTraceStartFrame = profilerFrame;
		auxTraceStartTick = nn::os::GetSystemTick();
		auxRecipeObject = 0;
		auxListObject = 0;
		auxTraceRunning = false;
		auxTraceComplete = false;
		auxHaveLayerSample = false;
		auxLastInputLayer = 0;
		auxLastLayerHandle = 0;
		auxLastEmittedEvent = 0;
		auxLastHeldButtons = 0;
		auxLastDownButtons = 0;
		insideAuxRecipeHandler = 0;
		insideAuxListHandler = 0;
	}

	void StartAuxTrace() {
		ClearAuxTrace();
		auxTraceRunning = true;
	}

	void RecordAuxTrace(
		AuxTraceKind kind,
		std::uintptr_t address = 0,
		std::uint32_t object = 0,
		std::uint32_t event = 0,
		std::uint32_t action = 0,
		std::uint32_t layer = 0,
		std::uint32_t held = 0,
		std::uint32_t down = 0,
		bool accepted = false
	) {
		if(!auxTraceRunning)
			return;
		if(auxTraceCount >= auxTrace.size()) {
			auxTraceDropped++;
			return;
		}
		auxTrace[auxTraceCount++] = {
			kind,
			profilerFrame,
			nn::os::GetSystemTick(),
			address,
			object,
			event,
			action,
			layer,
			held,
			down,
			accepted
		};
	}

	void DrawAuxTrace() {
		if(ImGui::Checkbox("Capture Aux Core tutorial", &auxCaptureEnabled)) {
			if(auxCaptureEnabled)
				StartAuxTrace();
			else
				auxTraceRunning = false;
		}
		ImGui::SameLine();
		if(ImGui::Button("Start / restart now")) {
			auxCaptureEnabled = true;
			StartAuxTrace();
		}
		ImGui::SameLine();
		if(ImGui::Button("Clear Aux trace"))
			ClearAuxTrace();
		ImGui::SameLine();
		if(auxTraceRunning)
			ImGui::TextColored(ImVec4(0.3f, 1.f, 0.4f, 1.f), "CAPTURING");
		else if(auxTraceComplete)
			ImGui::Text("Complete");
		else if(auxCaptureEnabled)
			ImGui::TextDisabled("ARMED");
		else
			ImGui::TextDisabled("Capture disabled");

		ImGui::TextWrapped(
			"Observational only. Capture is armed immediately; the first actual "
			"OrbRecipeListener call identifies the recipe object. Records the recipe "
			"listener, native input enable, matching input-layer polls, "
			"emitted events, and accepted physical/playback actions."
		);
		ImGui::Text(
			"List object: %u   Recipe object: %u   Samples: %u   Dropped: %u",
			auxListObject,
			auxRecipeObject,
			static_cast<unsigned>(auxTraceCount),
			static_cast<unsigned>(auxTraceDropped)
		);
		if(ImGui::TreeNode("Installed native stages")) {
			const struct {
				const char* name;
				bool installed;
			} stages[] = {
				{"OrbListListener", auxOrbListHookInstalled},
				{"resetInputSelection", auxResetSelectionHookInstalled},
				{"setupInputSelectionFromList", auxSetupSelectionHookInstalled},
				{"UIInputManager::setupInputSelection", auxManagerSetupHookInstalled},
				{"UIInputManager::enableInput", auxManagerEnableHookInstalled},
				{"UIInputManager::sendEvent", auxManagerSendHookInstalled},
				{"UIInputPad::updateImpl", auxPadUpdateHookInstalled},
				{"UIInputPad::moveSelect", auxMoveSelectHookInstalled},
			};
			for(const auto& stage : stages) {
				ImGui::TextColored(
					stage.installed
						? ImVec4(0.3f, 1.f, 0.4f, 1.f)
						: ImVec4(1.f, 0.35f, 0.25f, 1.f),
					"%s  %s",
					stage.installed ? "OK" : "MISSING",
					stage.name
				);
			}
			ImGui::TreePop();
		}

		if(ImGui::BeginTable(
			"AuxCoreInputTrace",
			9,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
				| ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
			ImVec2(0.f, 390.f)
		)) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Call", ImGuiTableColumnFlags_WidthFixed, 245.f);
			ImGui::TableSetupColumn("Delta f", ImGuiTableColumnFlags_WidthFixed, 65.f);
			ImGui::TableSetupColumn("Delta ms", ImGuiTableColumnFlags_WidthFixed, 85.f);
			ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthFixed, 70.f);
			ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthFixed, 60.f);
			ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 65.f);
			ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed, 70.f);
			ImGui::TableSetupColumn("Held / Down", ImGuiTableColumnFlags_WidthFixed, 175.f);
			ImGui::TableSetupColumn("Result / Address");
			ImGui::TableHeadersRow();
			for(std::size_t index = 0; index < auxTraceCount; index++) {
				const auto& sample = auxTrace[index];
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				if(sample.kind == AuxTraceKind::PlaybackAction)
					ImGui::TextColored(
						ImVec4(0.3f, 1.f, 0.4f, 1.f),
						"%s",
						AuxTraceName(sample.kind)
					);
				else
					ImGui::TextUnformatted(AuxTraceName(sample.kind));
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%llu", sample.frame - auxTraceStartFrame);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text(
					"%.3f",
					static_cast<double>(sample.tick - auxTraceStartTick)
						/ TicksPerMillisecond
				);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", sample.object);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("0x%X", sample.event);
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%u", sample.action);
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%u", sample.layer);
				ImGui::TableSetColumnIndex(7);
				ImGui::Text("%08X / %08X", sample.held, sample.down);
				ImGui::TableSetColumnIndex(8);
				ImGui::Text(
					"%s 0x%llX",
					sample.accepted ? "accepted" : "-",
					static_cast<unsigned long long>(sample.address)
				);
			}
			ImGui::EndTable();
		}
	}

	bool IsAuxInputLayer(const void* inputLayer) {
		return inputLayer != nullptr
			&& *reinterpret_cast<const std::uint32_t*>(
				reinterpret_cast<const std::uint8_t*>(inputLayer) + 0x40
			) != 0
			&& (
				*reinterpret_cast<const std::uint32_t*>(
					reinterpret_cast<const std::uint8_t*>(inputLayer) + 0x40
				) == auxRecipeObject
				|| *reinterpret_cast<const std::uint32_t*>(
					reinterpret_cast<const std::uint8_t*>(inputLayer) + 0x40
				) == auxListObject
			);
	}

	template<typename THook>
	bool InstallAuxSymbolHook(const char* symbol, const char* stage) {
		const auto address = skylaunch::hook::detail::ResolveSymbolBase(symbol);
		if(address == skylaunch::hook::INVALID_FUNCTION_PTR) {
			xenomods::g_Logger->LogWarning(
				"Aux profiler stage unavailable: {} ({})", stage, symbol
			);
			return false;
		}
		THook::HookAt(symbol);
		return true;
	}

	struct AuxOrbListListenerHook
		: skylaunch::hook::Trampoline<AuxOrbListListenerHook> {
		static void Hook(void* listener, const void* eventData, unsigned int object) {
			const std::uint16_t event = eventData == nullptr
				? 0
				: *reinterpret_cast<const std::uint16_t*>(eventData);
			const std::uint32_t action = eventData == nullptr
				? 0
				: *reinterpret_cast<const std::uint32_t*>(
					reinterpret_cast<const std::uint8_t*>(eventData) + 0xc
				);
			if(auxCaptureEnabled && !auxTraceRunning)
				StartAuxTrace();
			if(object != 0)
				auxListObject = object;
			insideAuxListHandler++;
			RecordAuxTrace(
				AuxTraceKind::OrbListEnter,
				reinterpret_cast<std::uintptr_t>(listener),
				object,
				event,
				action
			);
			Orig(listener, eventData, object);
			RecordAuxTrace(
				AuxTraceKind::OrbListReturn,
				reinterpret_cast<std::uintptr_t>(eventData),
				object,
				event,
				action
			);
			insideAuxListHandler--;
		}
	};

	struct AuxManagerSetupSelectionHook
		: skylaunch::hook::Trampoline<AuxManagerSetupSelectionHook> {
		static void Hook(void* manager, std::uint32_t layer, int selected, std::uint32_t size) {
			const bool trace = auxTraceRunning && (insideAuxRecipeHandler || insideAuxListHandler);
			if(trace)
				RecordAuxTrace(AuxTraceKind::ManagerSetupSelectionEnter, reinterpret_cast<std::uintptr_t>(manager), auxRecipeObject, 0, static_cast<std::uint32_t>(selected), layer, size);
			Orig(manager, layer, selected, size);
			if(trace)
				RecordAuxTrace(AuxTraceKind::ManagerSetupSelectionReturn, reinterpret_cast<std::uintptr_t>(manager), auxRecipeObject, 0, static_cast<std::uint32_t>(selected), layer, size);
		}
	};

	struct AuxResetSelectionHook
		: skylaunch::hook::Trampoline<AuxResetSelectionHook> {
		static void Hook(std::uint32_t object) {
			const bool trace = auxTraceRunning && (insideAuxRecipeHandler || insideAuxListHandler);
			if(trace)
				RecordAuxTrace(AuxTraceKind::ResetSelectionEnter, 0, object);
			Orig(object);
			if(trace)
				RecordAuxTrace(AuxTraceKind::ResetSelectionReturn, 0, object);
		}
	};

	struct AuxSetupSelectionFromListHook
		: skylaunch::hook::Trampoline<AuxSetupSelectionFromListHook> {
		static void Hook(std::uint32_t object, const void* list) {
			const bool trace = auxTraceRunning && (insideAuxRecipeHandler || insideAuxListHandler);
			if(trace)
				RecordAuxTrace(AuxTraceKind::SetupSelectionFromListEnter, reinterpret_cast<std::uintptr_t>(list), object);
			Orig(object, list);
			if(trace)
				RecordAuxTrace(AuxTraceKind::SetupSelectionFromListReturn, reinterpret_cast<std::uintptr_t>(list), object);
		}
	};

	struct AuxManagerEnableHook
		: skylaunch::hook::Trampoline<AuxManagerEnableHook> {
		static void Hook(void* manager, std::uint32_t layer) {
			const bool trace = auxTraceRunning && (insideAuxRecipeHandler || insideAuxListHandler);
			if(trace)
				RecordAuxTrace(AuxTraceKind::ManagerEnableEnter, reinterpret_cast<std::uintptr_t>(manager), auxRecipeObject, 0, 0, layer);
			Orig(manager, layer);
			if(insideAuxListHandler != 0)
				xenomods::MenuHelper::OnAuxCoreListInputEnabled(layer);
			if(trace)
				RecordAuxTrace(AuxTraceKind::ManagerEnableReturn, reinterpret_cast<std::uintptr_t>(manager), auxRecipeObject, 0, 0, layer);
		}
	};

	struct AuxManagerSendEventHook
		: skylaunch::hook::Trampoline<AuxManagerSendEventHook> {
		static void Hook(void* manager, void* layerInfo) {
			void* inputLayer = layerInfo == nullptr
				? nullptr
				: *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(layerInfo) + 0x10);
			const bool trace = auxTraceRunning && (insideAuxRecipeHandler
				|| insideAuxListHandler || IsAuxInputLayer(inputLayer));
			const std::uint32_t layer = layerInfo == nullptr
				? 0
				: *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(layerInfo) + 0x18);
			const std::uint16_t event = inputLayer == nullptr
				? 0
				: *reinterpret_cast<const std::uint16_t*>(reinterpret_cast<const std::uint8_t*>(inputLayer) + 0x10);
			if(trace)
				RecordAuxTrace(AuxTraceKind::ManagerSendEventEnter, reinterpret_cast<std::uintptr_t>(layerInfo), auxRecipeObject, event, 0, layer);
			Orig(manager, layerInfo);
			if(trace)
				RecordAuxTrace(AuxTraceKind::ManagerSendEventReturn, reinterpret_cast<std::uintptr_t>(layerInfo), auxRecipeObject, event, 0, layer);
		}
	};

	struct AuxPadUpdateHook : skylaunch::hook::Trampoline<AuxPadUpdateHook> {
		static std::uint64_t Hook(void* inputPad) {
			const bool trace = auxTraceRunning && IsAuxInputLayer(inputPad);
			if(trace)
				RecordAuxTrace(AuxTraceKind::PadUpdateEnter, reinterpret_cast<std::uintptr_t>(inputPad), auxRecipeObject);
			const auto result = Orig(inputPad);
			if(trace && result != 0)
				RecordAuxTrace(AuxTraceKind::PadUpdateReturn, reinterpret_cast<std::uintptr_t>(inputPad), auxRecipeObject, 0, static_cast<std::uint32_t>(result), 0, 0, 0, true);
			return result;
		}
	};

	struct AuxMoveSelectHook : skylaunch::hook::Trampoline<AuxMoveSelectHook> {
		static std::uint64_t Hook(void* inputPad, const void* padData, std::uint32_t step, int selected) {
			const bool trace = auxTraceRunning && IsAuxInputLayer(inputPad);
			const std::uint32_t held = padData == nullptr ? 0 : *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(padData) + 4);
			const std::uint32_t down = padData == nullptr ? 0 : *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(padData) + 0xc);
			if(trace)
				RecordAuxTrace(AuxTraceKind::MoveSelectEnter, reinterpret_cast<std::uintptr_t>(inputPad), auxRecipeObject, 0, static_cast<std::uint32_t>(selected), step, held, down);
			const auto result = Orig(inputPad, padData, step, selected);
			if(trace)
				RecordAuxTrace(AuxTraceKind::MoveSelectReturn, reinterpret_cast<std::uintptr_t>(inputPad), auxRecipeObject, 0, static_cast<std::uint32_t>(result), step, held, down, result != static_cast<std::uint64_t>(selected));
			return result;
		}
	};

	void ArmForcedFullscreenInput() {
		if(forcedFullscreenInputArmed)
			return;
		forcedFullscreenInputArmed = true;
		forcedFullscreenInputOpenConfirmed = false;
		forcedFullscreenInputStartFrame = profilerFrame;
		forcedOperationControlsObject = 0;
		suppressNativeOperationControlsObject = 0;
	}

	void CancelForcedFullscreenInput() {
		forcedFullscreenInputArmed = false;
		forcedFullscreenInputOpenConfirmed = false;
		forcedFullscreenInputStartFrame = 0;
		forcedOperationControlsObject = 0;
	}

	const char* EventName(ProfileEvent event) {
		switch(event) {
			case ProfileEvent::ButtonPressed:
				return "X button handler";
			case ProfileEvent::OpenCommandStarted:
				return "Open command requested";
			case ProfileEvent::OpenCommandQueued:
				return "Open command queued";
			case ProfileEvent::OpenCommandRejected:
				return "Open command rejected";
			case ProfileEvent::FullscreenTransitionStarted:
				return "Fullscreen transition start";
			case ProfileEvent::FullscreenLayerCreated:
				return "Fullscreen layer created";
			case ProfileEvent::UiFactoryReady:
				return "UI factory finished";
			case ProfileEvent::FadeInComplete:
				return "Fullscreen fade-in complete";
			case ProfileEvent::WorldImageRequested:
				return "World-map image requested";
			case ProfileEvent::WorldImageReady:
				return "World-map image ready";
			case ProfileEvent::WorldLocatorStarted:
				return "World-map locator build";
			case ProfileEvent::WorldLocatorReady:
				return "World-map locators ready";
			case ProfileEvent::WorldSeqNextStarted:
				return "World-map final setup";
			case ProfileEvent::ZoneImageRequested:
				return "Zone-map image requested";
			case ProfileEvent::ZoneImageReady:
				return "Zone-map image ready";
			case ProfileEvent::ZoneLocatorStarted:
				return "Zone-map locator build";
			case ProfileEvent::ZoneLocatorReady:
				return "Zone-map locators ready";
			case ProfileEvent::ZoneSeqNextStarted:
				return "Zone-map final setup";
			case ProfileEvent::FullscreenInputEnabled:
				return "Fullscreen input slot 2 enabled";
			case ProfileEvent::OperationControlsRequested:
				return "Bottom controls requested";
			case ProfileEvent::SeqNextComplete:
				return "Final setup complete";
			case ProfileEvent::WorldLoadTexCalled:
				return "World-map loadTex called";
			case ProfileEvent::WorldLoadTexQueued:
				return "World-map load pass queued";
			case ProfileEvent::WorldLoadTexUnchanged:
				return "World-map loadTex no-op";
			case ProfileEvent::PointListStateObserved:
				return "Point-list state observed";
			case ProfileEvent::PointListFlagChanged:
				return "Point-list input flag changed";
			case ProfileEvent::PointOpenReceived:
				return "Point-list open event received";
			case ProfileEvent::PointAnimationStarted:
				return "Point-name open animation started";
			case ProfileEvent::PointAnimationHooked:
				return "Point-name completion hook installed";
			case ProfileEvent::PointAnimationSample:
				return "Hooked animation state";
			case ProfileEvent::PointOpenHandled:
				return "Point-list open event handled";
			case ProfileEvent::PointAnimationCompleteReceived:
				return "Point animation-complete received";
			case ProfileEvent::PointAnimationCompleteHandled:
				return "Point animation-complete handled";
			case ProfileEvent::Interrupted:
				return "Capture interrupted";
			case ProfileEvent::Count:
				break;
		}
		return "Unknown";
	}

	bool HasEvent(const ProfileRun& run, ProfileEvent event) {
		for(std::size_t index = 0; index < run.eventCount; index++)
			if(run.events[index].event == event)
				return true;
		return false;
	}

	void SaveCurrentRun() {
		if(currentRun.id == 0 || currentRun.eventCount == 0)
			return;
		history[historyWrite] = currentRun;
		historyWrite = (historyWrite + 1) % HistoryCapacity;
		historyCount = std::min(historyCount + 1, HistoryCapacity);
		selectedHistoryNewest = 0;
	}

	void FinishRun(bool completed) {
		if(!currentRun.running)
			return;
		currentRun.running = false;
		currentRun.completed = completed;
		SaveCurrentRun();
		const auto& finalEvent = currentRun.events[currentRun.eventCount - 1];
		const auto totalFrames = finalEvent.frame - currentRun.startFrame;
		const double totalMs = static_cast<double>(
			finalEvent.tick - currentRun.startTick
		) / TicksPerMillisecond;
		xenomods::g_Logger->LogInfo(
			"Skip-travel profile #{} {}: {}f, {:.3f}ms",
			currentRun.id,
			completed ? "complete" : "incomplete",
			totalFrames,
			totalMs
		);
	}

	void FinishRunWhenReady() {
		if(
			currentRun.running
			&& HasEvent(currentRun, ProfileEvent::SeqNextComplete)
			&& HasEvent(currentRun, ProfileEvent::FullscreenInputEnabled)
			&& HasEvent(currentRun, ProfileEvent::OperationControlsRequested)
		)
			FinishRun(true);
	}

	void RecordEvent(
		ProfileEvent event,
		std::uintptr_t address = 0,
		std::uint32_t data0 = 0,
		std::uint32_t data1 = 0,
		std::uint32_t data2 = 0,
		std::uint32_t data3 = 0,
		float animationTime = 0.f,
		float animationLength = 0.f
	) {
		if(!currentRun.running)
			return;
		if(currentRun.eventCount >= currentRun.events.size()) {
			currentRun.droppedEvents++;
			return;
		}
		currentRun.events[currentRun.eventCount++] = {
			event,
			profilerFrame,
			nn::os::GetSystemTick(),
			address,
			{ data0, data1, data2, data3 },
			animationTime,
			animationLength
		};
	}

	template<typename T>
	T ReadAt(const void* object, std::size_t offset) {
		return *reinterpret_cast<const T*>(
			reinterpret_cast<const std::uint8_t*>(object) + offset
		);
	}

	void ResetNativeTracking() {
		insidePointOpenEvent = false;
		trackedWorldMap = nullptr;
		trackedPointList = nullptr;
		havePointListState = false;
		lastPointListFlag = 0;
		lastPointListSize = 0;
		lastCachedFloor = 0;
		animationCandidates = {};
		animationCandidateCount = 0;
		trackedAnimationObjectId = 0;
		trackedAnimationObject = nullptr;
		haveAnimationSample = false;
		lastAnimationPlaying = false;
		lastAnimationTime = 0.f;
		lastAnimationLength = 0.f;
	}

	void ObserveWorldMap(void* map) {
		if(!currentRun.running || map == nullptr)
			return;
		if(trackedWorldMap == map && havePointListState)
			return;

		trackedWorldMap = map;
		trackedPointList = ReadAt<void*>(map, WorldMapPointListOffset);
		if(trackedPointList == nullptr)
			return;

		lastPointListFlag = ReadAt<std::uint8_t>(
			trackedPointList,
			PointListInputDisabledOffset
		);
		lastPointListSize = ReadAt<std::uint32_t>(
			trackedPointList,
			PointListSizeOffset
		);
		lastCachedFloor = ReadAt<std::uint16_t>(
			trackedWorldMap,
			WorldMapCachedFloorOffset
		);
		havePointListState = true;
		RecordEvent(
			ProfileEvent::PointListStateObserved,
			reinterpret_cast<std::uintptr_t>(trackedPointList),
			lastPointListFlag,
			lastPointListSize,
			lastCachedFloor
		);
	}

	void SamplePointListState() {
		if(!currentRun.running || trackedWorldMap == nullptr)
			return;

		const auto pointList = ReadAt<void*>(
			trackedWorldMap,
			WorldMapPointListOffset
		);
		if(pointList == nullptr)
			return;

		trackedPointList = pointList;
		const auto flag = ReadAt<std::uint8_t>(
			trackedPointList,
			PointListInputDisabledOffset
		);
		const auto size = ReadAt<std::uint32_t>(
			trackedPointList,
			PointListSizeOffset
		);
		const auto cachedFloor = ReadAt<std::uint16_t>(
			trackedWorldMap,
			WorldMapCachedFloorOffset
		);

		if(!havePointListState) {
			ObserveWorldMap(trackedWorldMap);
			return;
		}
		if(
			flag != lastPointListFlag
			|| size != lastPointListSize
			|| cachedFloor != lastCachedFloor
		) {
			RecordEvent(
				ProfileEvent::PointListFlagChanged,
				reinterpret_cast<std::uintptr_t>(trackedPointList),
				lastPointListFlag,
				flag,
				size,
				cachedFloor
			);
			lastPointListFlag = flag;
			lastPointListSize = size;
			lastCachedFloor = cachedFloor;
		}
	}

	bool ReadAnimationState(
		ui::UIObject* object,
		std::uint32_t objectId,
		bool& playing,
		float& time,
		float& length
	) {
		if(
			object == nullptr
			|| animationIsPlaying == nullptr
			|| animationPlaybackLength == nullptr
			|| animationPlaybackTime == nullptr
		)
			return false;

		UIObjectAccProbe probe { nullptr, object, objectId };
		auto* accessor = reinterpret_cast<ui::UIObjectAcc*>(&probe);
		playing = animationIsPlaying(accessor);
		time = animationPlaybackTime(accessor);
		length = animationPlaybackLength(accessor);
		return true;
	}

	void SampleTrackedAnimation(bool force = false) {
		if(!currentRun.running || trackedAnimationObject == nullptr)
			return;

		bool playing = false;
		float time = 0.f;
		float length = 0.f;
		if(!ReadAnimationState(
			trackedAnimationObject,
			trackedAnimationObjectId,
			playing,
			time,
			length
		))
			return;

		if(
			force
			|| !haveAnimationSample
			|| playing != lastAnimationPlaying
			|| std::fabs(time - lastAnimationTime) > 0.0001f
			|| std::fabs(length - lastAnimationLength) > 0.0001f
		) {
			RecordEvent(
				ProfileEvent::PointAnimationSample,
				reinterpret_cast<std::uintptr_t>(trackedAnimationObject),
				trackedAnimationObjectId,
				playing ? 1u : 0u,
				0,
				0,
				time,
				length
			);
			haveAnimationSample = true;
			lastAnimationPlaying = playing;
			lastAnimationTime = time;
			lastAnimationLength = length;
		}
	}

	void StartRun(bool fromXButton) {
		if(!captureEnabled)
			return;
		if(currentRun.running) {
			RecordEvent(ProfileEvent::Interrupted);
			FinishRun(false);
		}
		currentRun = {};
		currentRun.id = nextRunId++;
		currentRun.startFrame = profilerFrame;
		currentRun.startTick = nn::os::GetSystemTick();
		currentRun.fromXButton = fromXButton;
		currentRun.running = true;
		ResetNativeTracking();
		if(fromXButton)
			RecordEvent(ProfileEvent::ButtonPressed);
	}

	void EnsureRun() {
		if(captureEnabled && !currentRun.running)
			StartRun(false);
	}

	void DrawRun(const ProfileRun& run) {
		if(run.id == 0 || run.eventCount == 0) {
			ImGui::TextDisabled("Open skip travel to capture a profile.");
			return;
		}

		ImGui::Text(
			"Run #%llu - %s - %s",
			static_cast<unsigned long long>(run.id),
			run.fromXButton ? "X button" : "direct command",
			run.running ? "capturing" : run.completed ? "complete" : "incomplete"
		);
		if(run.droppedEvents != 0) {
			ImGui::TextColored(
				ImVec4(1.f, 0.55f, 0.2f, 1.f),
				"%llu event(s) exceeded the capture buffer",
				static_cast<unsigned long long>(run.droppedEvents)
			);
		}
		if(ImGui::BeginTable(
			"SkipTravelProfileEvents",
			6,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
				| ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
			ImVec2(0.f, -1.f)
		)) {
			ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 235.f);
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.f);
			ImGui::TableSetupColumn("Delta f", ImGuiTableColumnFlags_WidthFixed, 55.f);
			ImGui::TableSetupColumn("Total f", ImGuiTableColumnFlags_WidthFixed, 55.f);
			ImGui::TableSetupColumn("Total ms", ImGuiTableColumnFlags_WidthFixed, 80.f);
			ImGui::TableSetupColumn("Native details", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();
			std::array<std::uint32_t, static_cast<std::size_t>(ProfileEvent::Count)>
				occurrences {};
			for(std::size_t index = 0; index < run.eventCount; index++) {
				const auto& sample = run.events[index];
				const auto eventIndex = static_cast<std::size_t>(sample.event);
				const auto occurrence = ++occurrences[eventIndex];
				const auto previousFrame = index == 0
					? run.startFrame
					: run.events[index - 1].frame;
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(EventName(sample.event));
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", occurrence);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text(
					"%llu",
					static_cast<unsigned long long>(sample.frame - previousFrame)
				);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text(
					"%llu",
					static_cast<unsigned long long>(sample.frame - run.startFrame)
				);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text(
					"%.3f",
					static_cast<double>(sample.tick - run.startTick)
						/ TicksPerMillisecond
				);
				ImGui::TableSetColumnIndex(5);
				switch(sample.event) {
					case ProfileEvent::WorldLoadTexCalled:
						ImGui::Text(
							"map=%p cached floor=%u",
							reinterpret_cast<void*>(sample.address),
							sample.data[0]
						);
						break;
					case ProfileEvent::WorldLoadTexQueued:
					case ProfileEvent::WorldLoadTexUnchanged:
						ImGui::Text(
							"cached floor %u -> %u",
							sample.data[0],
							sample.data[1]
						);
						break;
					case ProfileEvent::PointListStateObserved:
						ImGui::Text(
							"list=%p flag=%u size=%u floor=%u",
							reinterpret_cast<void*>(sample.address),
							sample.data[0],
							sample.data[1],
							sample.data[2]
						);
						break;
					case ProfileEvent::PointListFlagChanged:
						ImGui::Text(
							"list=%p flag %u -> %u, size=%u floor=%u",
							reinterpret_cast<void*>(sample.address),
							sample.data[0],
							sample.data[1],
							sample.data[2],
							sample.data[3]
						);
						break;
					case ProfileEvent::PointOpenReceived:
					case ProfileEvent::PointOpenHandled:
						ImGui::Text(
							"map=%p recipient=%u list flag=%u size=%u hooked=%u",
							reinterpret_cast<void*>(sample.address),
							sample.data[0],
							sample.data[1],
							sample.data[2],
							sample.data[3]
						);
						break;
					case ProfileEvent::PointAnimationStarted:
					case ProfileEvent::PointAnimationSample:
						ImGui::Text(
							"object=%u ptr=%p playing=%s time=%.3f / %.3f s",
							sample.data[0],
							reinterpret_cast<void*>(sample.address),
							sample.data[1] != 0 ? "yes" : "no",
							sample.animationTime,
							sample.animationLength
						);
						break;
					case ProfileEvent::PointAnimationHooked:
						ImGui::Text(
							"listener=%u animation object=%u type=%u ptr=%p",
							sample.data[0],
							sample.data[1],
							sample.data[2],
							reinterpret_cast<void*>(sample.address)
						);
						break;
					case ProfileEvent::PointAnimationCompleteReceived:
					case ProfileEvent::PointAnimationCompleteHandled:
						ImGui::Text(
							"map=%p recipient=%u hooked object=%u",
							reinterpret_cast<void*>(sample.address),
							sample.data[0],
							sample.data[1]
						);
						break;
					default:
						break;
				}
			}
			ImGui::EndTable();
		}
	}

	const ProfileRun* GetHistoryNewest(std::size_t newest) {
		if(newest >= historyCount)
			return nullptr;
		const std::size_t index =
			(historyWrite + HistoryCapacity - 1 - newest) % HistoryCapacity;
		return &history[index];
	}

	struct FullscreenTransitionStartHook
		: skylaunch::hook::Trampoline<FullscreenTransitionStartHook> {
		static void Hook(void* fullscreen) {
			Orig(fullscreen);
			RecordEvent(ProfileEvent::FullscreenTransitionStarted);
		}
	};

	struct WorldLoadTexHook : skylaunch::hook::Trampoline<WorldLoadTexHook> {
		static void Hook(void* map) {
			ObserveWorldMap(map);
			const auto oldFloor = map == nullptr
				? 0u
				: ReadAt<std::uint16_t>(map, WorldMapCachedFloorOffset);
			RecordEvent(
				ProfileEvent::WorldLoadTexCalled,
				reinterpret_cast<std::uintptr_t>(map),
				oldFloor
			);
			Orig(map);
			const auto newFloor = map == nullptr
				? 0u
				: ReadAt<std::uint16_t>(map, WorldMapCachedFloorOffset);
			RecordEvent(
				oldFloor == newFloor
					? ProfileEvent::WorldLoadTexUnchanged
					: ProfileEvent::WorldLoadTexQueued,
				reinterpret_cast<std::uintptr_t>(map),
				oldFloor,
				newFloor
			);
		}
	};

	struct UIObjectStartAnimationHook
		: skylaunch::hook::Trampoline<UIObjectStartAnimationHook> {
		static void Hook(ui::UIObjectAcc* accessor, const char* name, bool loop) {
			const bool trace =
				insidePointOpenEvent
				&& name != nullptr
				&& std::strcmp(name, "open") == 0;
			Orig(accessor, name, loop);
			if(!trace || accessor == nullptr || accessor->uiObject == nullptr)
				return;

			if(animationCandidateCount < animationCandidates.size()) {
				animationCandidates[animationCandidateCount++] = {
					accessor->objectId,
					accessor->uiObject
				};
			}

			bool playing = false;
			float time = 0.f;
			float length = 0.f;
			ReadAnimationState(
				accessor->uiObject,
				accessor->objectId,
				playing,
				time,
				length
			);
			RecordEvent(
				ProfileEvent::PointAnimationStarted,
				reinterpret_cast<std::uintptr_t>(accessor->uiObject),
				accessor->objectId,
				playing ? 1u : 0u,
				0,
				0,
				time,
				length
			);
		}
	};

	struct AnimationEventHook
		: skylaunch::hook::Trampoline<AnimationEventHook> {
		static void Hook(
			std::uint32_t listener,
			std::uint32_t animationObject,
			int type
		) {
			Orig(listener, animationObject, type);
			if(!currentRun.running || !insidePointOpenEvent)
				return;

			trackedAnimationObjectId = animationObject;
			trackedAnimationObject = nullptr;
			for(std::size_t index = 0; index < animationCandidateCount; index++) {
				if(animationCandidates[index].objectId == animationObject) {
					trackedAnimationObject = animationCandidates[index].object;
					break;
				}
			}
			haveAnimationSample = false;
			RecordEvent(
				ProfileEvent::PointAnimationHooked,
				reinterpret_cast<std::uintptr_t>(trackedAnimationObject),
				listener,
				animationObject,
				static_cast<std::uint32_t>(type)
			);
			SampleTrackedAnimation(true);
		}
	};

	struct PointSelectEventHook
		: skylaunch::hook::Trampoline<PointSelectEventHook> {
		static void Hook(
			void* listener,
			const void* eventData,
			std::uint32_t recipient
		) {
			const auto event = eventData == nullptr
				? 0u
				: static_cast<std::uint32_t>(ReadAt<std::uint16_t>(eventData, 0));
			void* map = listener == nullptr
				? nullptr
				: ReadAt<void*>(listener, PointListenerWorldMapOffset);
			ObserveWorldMap(map);

			if(event == PointOpenEvent) {
				if(
					forcedFullscreenInputArmed
					&& forcedFullscreenInputOpenConfirmed
				)
					forcedOperationControlsObject = recipient;
				animationCandidates = {};
				animationCandidateCount = 0;
				trackedAnimationObjectId = 0;
				trackedAnimationObject = nullptr;
				haveAnimationSample = false;
				const auto flag = trackedPointList == nullptr
					? 0u
					: ReadAt<std::uint8_t>(
						trackedPointList,
						PointListInputDisabledOffset
					);
				const auto size = trackedPointList == nullptr
					? 0u
					: ReadAt<std::uint32_t>(trackedPointList, PointListSizeOffset);
				RecordEvent(
					ProfileEvent::PointOpenReceived,
					reinterpret_cast<std::uintptr_t>(map),
					recipient,
					flag,
					size
				);
				insidePointOpenEvent = true;
			}
			if(event == AnimationCompleteEvent) {
				const auto hookedObject = map == nullptr
					? 0u
					: ReadAt<std::uint32_t>(map, WorldMapHookedAnimationOffset);
				SampleTrackedAnimation(true);
				RecordEvent(
					ProfileEvent::PointAnimationCompleteReceived,
					reinterpret_cast<std::uintptr_t>(map),
					recipient,
					hookedObject
				);
			}

			Orig(listener, eventData, recipient);

			if(event == PointOpenEvent) {
				insidePointOpenEvent = false;
				const auto hookedObject = map == nullptr
					? 0u
					: ReadAt<std::uint32_t>(map, WorldMapHookedAnimationOffset);
				const auto flag = trackedPointList == nullptr
					? 0u
					: ReadAt<std::uint8_t>(
						trackedPointList,
						PointListInputDisabledOffset
					);
				const auto size = trackedPointList == nullptr
					? 0u
					: ReadAt<std::uint32_t>(trackedPointList, PointListSizeOffset);
				RecordEvent(
					ProfileEvent::PointOpenHandled,
					reinterpret_cast<std::uintptr_t>(map),
					recipient,
					flag,
					size,
					hookedObject
				);
				SamplePointListState();
				SampleTrackedAnimation(true);
			}
			if(event == AnimationCompleteEvent) {
				const auto hookedObject = map == nullptr
					? 0u
					: ReadAt<std::uint32_t>(map, WorldMapHookedAnimationOffset);
				RecordEvent(
					ProfileEvent::PointAnimationCompleteHandled,
					reinterpret_cast<std::uintptr_t>(map),
					recipient,
					hookedObject
				);
			}
		}
	};

	struct FullscreenLayerCreateHook
		: skylaunch::hook::Trampoline<FullscreenLayerCreateHook> {
		static void Hook(void* fullscreen) {
			Orig(fullscreen);
			RecordEvent(ProfileEvent::FullscreenLayerCreated);
		}
	};

	struct FullscreenWaitScreenHook
		: skylaunch::hook::Trampoline<FullscreenWaitScreenHook> {
		static void Hook(void* fullscreen) {
			const bool previous = insideFullscreenWaitScreen;
			insideFullscreenWaitScreen = currentRun.running;
			Orig(fullscreen);
			insideFullscreenWaitScreen = previous;
		}
	};

	struct UiManagerIsLoadingHook
		: skylaunch::hook::Trampoline<UiManagerIsLoadingHook> {
		static bool Hook(const void* manager) {
			const bool loading = Orig(manager);
			if(insideFullscreenWaitScreen && !loading)
				RecordEvent(ProfileEvent::UiFactoryReady);
			return loading;
		}
	};

	struct FullscreenWaitFadeInHook
		: skylaunch::hook::Trampoline<FullscreenWaitFadeInHook> {
		static void Hook(void* fullscreen) {
			const bool previous = insideFullscreenWaitFadeIn;
			insideFullscreenWaitFadeIn = currentRun.running;
			Orig(fullscreen);
			insideFullscreenWaitFadeIn = previous;
		}
	};

	struct MenuManagerIsFadingHook
		: skylaunch::hook::Trampoline<MenuManagerIsFadingHook> {
		static bool Hook() {
			const bool fading = Orig();
			if(insideFullscreenWaitFadeIn && !fading)
				RecordEvent(ProfileEvent::FadeInComplete);
			return fading;
		}
	};

#define XENOMODS_PROFILE_SEQUENCE_HOOK(name, eventName) \
	struct name : skylaunch::hook::Trampoline<name> { \
		static bool Hook(void* map) { \
			ObserveWorldMap(map); \
			const bool finished = Orig(map); \
			if(finished) \
				RecordEvent(ProfileEvent::eventName); \
			return finished; \
		} \
	}

	XENOMODS_PROFILE_SEQUENCE_HOOK(WorldImageRequestHook, WorldImageRequested);
	XENOMODS_PROFILE_SEQUENCE_HOOK(WorldImageWaitHook, WorldImageReady);
	XENOMODS_PROFILE_SEQUENCE_HOOK(WorldLocatorStartHook, WorldLocatorStarted);
	XENOMODS_PROFILE_SEQUENCE_HOOK(WorldLocatorWaitHook, WorldLocatorReady);
	XENOMODS_PROFILE_SEQUENCE_HOOK(ZoneImageRequestHook, ZoneImageRequested);
	XENOMODS_PROFILE_SEQUENCE_HOOK(ZoneImageWaitHook, ZoneImageReady);
	XENOMODS_PROFILE_SEQUENCE_HOOK(ZoneLocatorStartHook, ZoneLocatorStarted);
	XENOMODS_PROFILE_SEQUENCE_HOOK(ZoneLocatorWaitHook, ZoneLocatorReady);

#undef XENOMODS_PROFILE_SEQUENCE_HOOK

	struct WorldSeqNextHook : skylaunch::hook::Trampoline<WorldSeqNextHook> {
		static bool Hook(void* map) {
			ObserveWorldMap(map);
			RecordEvent(ProfileEvent::WorldSeqNextStarted);
			const bool finished = Orig(map);
			if(finished)
				RecordEvent(ProfileEvent::SeqNextComplete);
			return finished;
		}
	};

	struct ZoneSeqNextHook : skylaunch::hook::Trampoline<ZoneSeqNextHook> {
		static bool Hook(void* map) {
			RecordEvent(ProfileEvent::ZoneSeqNextStarted);
			const bool finished = Orig(map);
			if(finished) {
				RecordEvent(ProfileEvent::SeqNextComplete);
			}
			return finished;
		}
	};

	struct FullscreenInputEnableHook
		: skylaunch::hook::Trampoline<FullscreenInputEnableHook> {
		static void Hook(unsigned int input) {
			Orig(input);
			if(input == 2) {
				CancelForcedFullscreenInput();
				RecordEvent(ProfileEvent::FullscreenInputEnabled);
			}
		}
	};

	struct OperationControlsAddHook
		: skylaunch::hook::Trampoline<OperationControlsAddHook> {
		static void Hook(
			unsigned int object,
			unsigned int first,
			unsigned int second,
			unsigned int third
		) {
			if(
				!insideForcedOperationControlsCall
				&& object == suppressNativeOperationControlsObject
				&& first == 0
				&& second == 0
				&& third == 0
			) {
				suppressNativeOperationControlsObject = 0;
				return;
			}
			Orig(object, first, second, third);
			if(
				currentRun.running
				&& HasEvent(currentRun, ProfileEvent::FullscreenInputEnabled)
			) {
				RecordEvent(ProfileEvent::OperationControlsRequested);
			}
		}
	};

} // namespace

namespace xenomods {

	void SkipTravelProfiler::TopBarButton() {
		if(ImGui::MenuItem("Profiler", nullptr, showWindow))
			showWindow = !showWindow;
	}

	void SkipTravelProfiler::ProfilerWindow() {
		if(!showWindow)
			return;

		ImGui::SetNextWindowSize(ImVec2(1120.f, 600.f), ImGuiCond_Appearing);
		if(!ImGui::Begin("Profiler", &showWindow)) {
			ImGui::End();
			return;
		}

		if(ImGui::BeginTabBar("ProfilerKinds")) {
		if(ImGui::BeginTabItem("Skip Travel")) {
		if(ImGui::Checkbox("Capture", &captureEnabled) && !captureEnabled) {
			if(currentRun.running) {
				RecordEvent(ProfileEvent::Interrupted);
				FinishRun(false);
			}
		}
		ImGui::SameLine();
		if(ImGui::Button("Clear")) {
			currentRun = {};
			history = {};
			historyWrite = 0;
			historyCount = 0;
			selectedHistoryNewest = 0;
		}
		ImGui::SameLine();
		if(currentRun.running)
			ImGui::TextColored(ImVec4(0.3f, 1.f, 0.4f, 1.f), "CAPTURING");
		else
			ImGui::TextDisabled("Waiting for skip travel");

		ImGui::TextWrapped(
			"Observational only. This records the native fullscreen, image-streaming, "
			"locator, and final input-handoff stages without changing TravelTAS playback."
		);

		if(ImGui::BeginTabBar("SkipTravelProfilerTabs")) {
			if(ImGui::BeginTabItem("Latest")) {
				DrawRun(currentRun);
				ImGui::EndTabItem();
			}
			if(ImGui::BeginTabItem("History")) {
				if(historyCount == 0) {
					ImGui::TextDisabled("No completed captures.");
				} else {
					ImGui::SetNextItemWidth(180.f);
					if(ImGui::BeginCombo(
						"Run",
						fmt::format("{}", selectedHistoryNewest + 1).c_str()
					)) {
						for(std::size_t newest = 0; newest < historyCount; newest++) {
							const auto* run = GetHistoryNewest(newest);
							const bool selected = selectedHistoryNewest == static_cast<int>(newest);
							const auto label = fmt::format(
								"{}: run #{} ({})",
								newest + 1,
								run->id,
								run->completed ? "complete" : "incomplete"
							);
							if(ImGui::Selectable(label.c_str(), selected))
								selectedHistoryNewest = static_cast<int>(newest);
						}
						ImGui::EndCombo();
					}
					if(const auto* run = GetHistoryNewest(selectedHistoryNewest))
						DrawRun(*run);
				}
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("Aux Core Input")) {
			DrawAuxTrace();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
		}

		ImGui::End();
	}

	void SkipTravelProfiler::OnAuxShopRequest(
		std::uint16_t event,
		std::uint32_t shop,
		std::uint32_t object,
		const void* eventData
	) {
		if(event != 0x21 || !auxCaptureEnabled)
			return;
		if(!auxTraceRunning)
			StartAuxTrace();
		RecordAuxTrace(
			AuxTraceKind::ShopRequest,
			reinterpret_cast<std::uintptr_t>(eventData),
			object,
			event,
			shop
		);
	}

	void SkipTravelProfiler::OnAuxRecipeEvent(
		bool after,
		std::uint16_t event,
		std::uint32_t action,
		std::uint32_t object,
		const void* listener,
		const void* eventData,
		bool dispatched
	) {
		if(!auxCaptureEnabled)
			return;
		if(!auxTraceRunning)
			StartAuxTrace();
		if(!after)
			insideAuxRecipeHandler++;
		if((event == 0x40 || event == 0x42) && object != 0)
			auxRecipeObject = object;
		else if(auxRecipeObject == 0 && object != 0)
			auxRecipeObject = object;
		RecordAuxTrace(
			!dispatched
				? AuxTraceKind::RecipeSuppressed
				: after ? AuxTraceKind::RecipeReturn : AuxTraceKind::RecipeEnter,
			reinterpret_cast<std::uintptr_t>(after ? eventData : listener),
			object,
			event,
			action
		);
		if(after && insideAuxRecipeHandler != 0)
			insideAuxRecipeHandler--;
		if(after && dispatched && event == 1) {
			auxTraceRunning = false;
			auxTraceComplete = true;
		}
	}

	void SkipTravelProfiler::OnAuxEnableInput(bool after, std::uint32_t object) {
		if(!auxTraceRunning)
			return;
		RecordAuxTrace(
			after ? AuxTraceKind::EnableInputReturn : AuxTraceKind::EnableInputEnter,
			0,
			object
		);
	}

	void SkipTravelProfiler::OnAuxInputLayerUpdate(
		std::uint32_t object,
		const void* inputLayer,
		std::uint32_t layerHandle,
		bool afterUpdate,
		bool accepted,
		std::uint16_t emittedEvent,
		std::uint32_t heldButtons,
		std::uint32_t downButtons
	) {
		if(
			!auxTraceRunning
			|| (object != auxRecipeObject && object != auxListObject)
		)
			return;
		const auto inputLayerAddress = reinterpret_cast<std::uintptr_t>(inputLayer);
		if(!afterUpdate) {
			if(auxHaveLayerSample && auxLastInputLayer == inputLayerAddress)
				return;
			RecordAuxTrace(
				AuxTraceKind::LayerUpdateEnter,
				inputLayerAddress,
				object,
				0,
				0,
				layerHandle
			);
			return;
		}
		const bool firstSample = !auxHaveLayerSample
			|| auxLastInputLayer != inputLayerAddress;
		const bool stateChanged = firstSample
			|| auxLastLayerHandle != layerHandle
			|| auxLastEmittedEvent != emittedEvent
			|| auxLastHeldButtons != heldButtons
			|| auxLastDownButtons != downButtons;
		auxHaveLayerSample = true;
		auxLastInputLayer = inputLayerAddress;
		auxLastLayerHandle = layerHandle;
		auxLastEmittedEvent = emittedEvent;
		auxLastHeldButtons = heldButtons;
		auxLastDownButtons = downButtons;
		if(!stateChanged && !accepted)
			return;
		RecordAuxTrace(
			AuxTraceKind::LayerUpdateReturn,
			inputLayerAddress,
			object,
			emittedEvent,
			0,
			layerHandle,
			heldButtons,
			downButtons,
			accepted
		);
	}

	void SkipTravelProfiler::OnAuxAcceptedAction(
		InputBuffer::AcceptedAction action,
		InputBuffer::ActionSource source
	) {
		if(!auxTraceRunning)
			return;
		RecordAuxTrace(
			source == InputBuffer::ActionSource::Playback
				? AuxTraceKind::PlaybackAction
				: AuxTraceKind::PhysicalAction,
			0,
			auxRecipeObject,
			0,
			action.input,
			action.layer,
			action.display,
			0,
			true
		);
	}

	void SkipTravelProfiler::OnOpenButtonPressed() {
		ArmForcedFullscreenInput();
		StartRun(true);
	}

	void SkipTravelProfiler::OnOpenButtonFinished() {
		if(!forcedFullscreenInputOpenConfirmed)
			CancelForcedFullscreenInput();
		if(
			currentRun.running
			&& !HasEvent(currentRun, ProfileEvent::OpenCommandQueued)
		) {
			RecordEvent(ProfileEvent::OpenCommandRejected);
			FinishRun(false);
		}
	}

	void SkipTravelProfiler::OnOpenCommandStarted() {
		ArmForcedFullscreenInput();
		EnsureRun();
		RecordEvent(ProfileEvent::OpenCommandStarted);
	}

	void SkipTravelProfiler::OnOpenCommandResult(bool created) {
		if(created)
			forcedFullscreenInputOpenConfirmed = forcedFullscreenInputArmed;
		else
			CancelForcedFullscreenInput();
		if(!currentRun.running)
			return;
		RecordEvent(
			created
				? ProfileEvent::OpenCommandQueued
				: ProfileEvent::OpenCommandRejected
		);
		if(!created)
			FinishRun(false);
	}

	void SkipTravelProfiler::Initialize() {
		UpdatableModule::Initialize();
#if XENOMODS_CODENAME(bf2)
		FullscreenTransitionStartHook::HookAt(
			"_ZN2gf16GfMenuFullScreen16switchLayerStartEv"
		);
		FullscreenLayerCreateHook::HookAt(
			"_ZN2gf16GfMenuFullScreen21switchLayerOpenScreenEv"
		);
		FullscreenWaitScreenHook::HookAt(
			"_ZN2gf16GfMenuFullScreen21switchLayerWaitScreenEv"
		);
		UiManagerIsLoadingHook::HookAt("_ZNK2ui9UIManager9isLoadingEv");
		FullscreenWaitFadeInHook::HookAt(
			"_ZN2gf16GfMenuFullScreen21switchLayerWaitFadeInEv"
		);
		MenuManagerIsFadingHook::HookAt("_ZN2gf13GfMenuManager8isFadingEv");

		WorldImageRequestHook::HookAt(
			"_ZN2gf17GfMenuObjWorldMap10seqTexLoadEv"
		);
		WorldImageWaitHook::HookAt(
			"_ZN2gf17GfMenuObjWorldMap14seqTexLoadWaitEv"
		);
		WorldLocatorStartHook::HookAt(
			"_ZN2gf17GfMenuObjWorldMap10seqTexDrawEv"
		);
		WorldLocatorWaitHook::HookAt(
			"_ZN2gf17GfMenuObjWorldMap8seqGimikEv"
		);
		WorldSeqNextHook::HookAt("_ZN2gf17GfMenuObjWorldMap7seqNextEv");
		WorldLoadTexHook::HookAt("_ZN2gf17GfMenuObjWorldMap7loadTexEv");
		PointSelectEventHook::HookAt(
			"_ZN2gf17GfMenuObjWorldMap19PointSelectListener11reciveEventERKN2ui9EventDataEj"
		);
		UIObjectStartAnimationHook::HookAt(
			"_ZN2ui11UIObjectAcc14startAnimationEPKcb"
		);
		AnimationEventHook::HookAt(
			"_ZN2gf12GfMenuObject18hookAnimationEventEjji"
		);

		ZoneImageRequestHook::HookAt(
			"_ZN2gf16GfMenuObjZoneMap10seqTexLoadEv"
		);
		ZoneImageWaitHook::HookAt(
			"_ZN2gf16GfMenuObjZoneMap14seqTexLoadWaitEv"
		);
		ZoneLocatorStartHook::HookAt(
			"_ZN2gf16GfMenuObjZoneMap10seqTexDrawEv"
		);
		ZoneLocatorWaitHook::HookAt(
			"_ZN2gf16GfMenuObjZoneMap8seqGimikEv"
		);
		ZoneSeqNextHook::HookAt("_ZN2gf16GfMenuObjZoneMap7seqNextEv");

		FullscreenInputEnableHook::HookAt(
			"_ZN2gf12GfMenuObject13enableFSInputEj"
		);
		OperationControlsAddHook::HookAt(
			"_ZN2gf22GfMenuObjOperationInfo3addEjjjj"
		);

		auxOrbListHookInstalled = InstallAuxSymbolHook<AuxOrbListListenerHook>(
			"_ZN2gf13GfMenuObjShop15OrbListListener11reciveEventERKN2ui9EventDataEj",
			"OrbListListener"
		);
		auxResetSelectionHookInstalled = InstallAuxSymbolHook<AuxResetSelectionHook>(
			"_ZN2gf12GfMenuObject19resetInputSelectionEj",
			"resetInputSelection"
		);
		auxSetupSelectionHookInstalled =
			InstallAuxSymbolHook<AuxSetupSelectionFromListHook>(
				"_ZN2gf12GfMenuObject27setupInputSelectionFromListEjRKN2ui14UIListInfoBaseE",
				"setupInputSelectionFromList"
			);
		auxManagerSetupHookInstalled =
			InstallAuxSymbolHook<AuxManagerSetupSelectionHook>(
				"_ZN2ui14UIInputManager19setupInputSelectionEjij",
				"UIInputManager::setupInputSelection"
			);
		auxManagerEnableHookInstalled = InstallAuxSymbolHook<AuxManagerEnableHook>(
			"_ZN2ui14UIInputManager11enableInputEj",
			"UIInputManager::enableInput"
		);
		auxManagerSendHookInstalled = InstallAuxSymbolHook<AuxManagerSendEventHook>(
			"_ZN2ui14UIInputManager9sendEventERNS0_14InputLayerInfoE",
			"UIInputManager::sendEvent"
		);
		auxPadUpdateHookInstalled = InstallAuxSymbolHook<AuxPadUpdateHook>(
			"_ZN2ui10UIInputPad10updateImplEv",
			"UIInputPad::updateImpl"
		);
		auxMoveSelectHookInstalled = InstallAuxSymbolHook<AuxMoveSelectHook>(
			"_ZN2ui10UIInputPad10moveSelectERKN2fw7PadDataEji",
			"UIInputPad::moveSelect"
		);

		const auto isPlayingAddress =
			skylaunch::hook::detail::ResolveSymbolBase(
				"_ZN2ui11UIObjectAcc18isPlayingAnimationEv"
			);
		const auto playbackLengthAddress =
			skylaunch::hook::detail::ResolveSymbolBase(
				"_ZN2ui11UIObjectAcc26getAnimationPlaybackLengthEv"
			);
		const auto playbackTimeAddress =
			skylaunch::hook::detail::ResolveSymbolBase(
				"_ZN2ui11UIObjectAcc24getAnimationPlaybackTimeEv"
			);
		if(
			isPlayingAddress != skylaunch::hook::INVALID_FUNCTION_PTR
			&& playbackLengthAddress != skylaunch::hook::INVALID_FUNCTION_PTR
			&& playbackTimeAddress != skylaunch::hook::INVALID_FUNCTION_PTR
		) {
			animationIsPlaying = std::bit_cast<AnimationPlayingFunction>(
				isPlayingAddress
			);
			animationPlaybackLength = std::bit_cast<AnimationFloatFunction>(
				playbackLengthAddress
			);
			animationPlaybackTime = std::bit_cast<AnimationFloatFunction>(
				playbackTimeAddress
			);
		} else {
			g_Logger->LogWarning(
				"Skip-travel profiler animation-state symbols are unavailable"
			);
		}

		g_Menu->RegisterTopBarCallback(&TopBarButton);
		g_Menu->RegisterRenderCallback(&ProfilerWindow, true);
		if(auxCaptureEnabled)
			StartAuxTrace();
		g_Logger->LogInfo("Skip-travel profiler hooks installed");
#endif
	}

	void SkipTravelProfiler::Update(fw::UpdateInfo*) {
		profilerFrame++;
		if(
			forcedFullscreenInputArmed
			&& forcedFullscreenInputOpenConfirmed
			&& forcedOperationControlsObject != 0
			&& profilerFrame - forcedFullscreenInputStartFrame
				>= ForcedFullscreenInputFrame
		) {
			const auto controlsObject = forcedOperationControlsObject;
			// Clear before entering the hook so its normal slot-2 observation cannot
			// re-arm or repeat this one-shot native call.
			CancelForcedFullscreenInput();
			FullscreenInputEnableHook::Hook(2);
			insideForcedOperationControlsCall = true;
			OperationControlsAddHook::Hook(controlsObject, 0, 0, 0);
			insideForcedOperationControlsCall = false;
			suppressNativeOperationControlsObject = controlsObject;
		}
		SamplePointListState();
		SampleTrackedAnimation();
		FinishRunWhenReady();
	}

	void SkipTravelProfiler::OnSceneTransition() {
		if(currentRun.running) {
			RecordEvent(ProfileEvent::Interrupted);
			FinishRun(false);
		}
		insideFullscreenWaitScreen = false;
		insideFullscreenWaitFadeIn = false;
		CancelForcedFullscreenInput();
		suppressNativeOperationControlsObject = 0;
		insideForcedOperationControlsCall = false;
		ResetNativeTracking();
		if(auxCaptureEnabled)
			StartAuxTrace();
	}

#if XENOMODS_CODENAME(bf2)
	XENOMODS_REGISTER_MODULE(SkipTravelProfiler);
#endif

} // namespace xenomods
