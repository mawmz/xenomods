#include "CombatAiDebug.hpp"
#include "ToolWindowLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "xenomods/engine/btl/Character.hpp"
#include "xenomods/engine/gf/Object.hpp"
#include "xenomods/engine/gf/Party.hpp"

namespace {

#if XENOMODS_CODENAME(bf2)
	constexpr int SlotCount = 6;
	constexpr int HistoryCapacity = 64;
	auto* const InvalidHandle = reinterpret_cast<gf::GF_OBJ_HANDLE*>(~std::uintptr_t(0));

	struct SlotSnapshot {
		int artId = 0;
		float recharge = 0.f;
		std::uint32_t failureMask = 0;
		bool present = false;
		bool eligible = false;
		std::uint64_t readyObservedFrame = 0;
	};

	enum class RequestKind : std::uint8_t {
		NativeGeneric,
		NativeSlot,
		ScriptGeneric,
		ScriptSlot,
		ScriptArtId,
	};

	struct DecisionEvent {
		std::uint64_t sequence = 0;
		std::uint64_t frame = 0;
		btl::BattleCharacter* character = nullptr;
		gf::GF_OBJ_HANDLE* handle = InvalidHandle;
		gf::GF_OBJ_HANDLE* targetHandle = InvalidHandle;
		int characterId = -1;
		int targetId = -1;
		int requestedActionId = -1;
		int currentActionRaw = -1;
		float aiDelayTimer = 0.f;
		RequestKind requestKind = RequestKind::NativeGeneric;
		int requestedValue = -1;
		std::array<SlotSnapshot, SlotCount> slots {};
		std::uint8_t candidateMask = 0;
		int candidateCount = 0;
		int selectedSlot = -1;
		int selectedArtId = 0;
		int selectedCandidateIndex = -1;
		bool startObserved = false;
		bool startResult = false;
		bool aiArtsResult = false;
		std::uint64_t observedReadyDelay = 0;
		std::uint16_t rawCancelState = 0;
		std::uint32_t rawTimingState = 0;
	};

	struct ScriptRequest {
		bool active = false;
		bool byId = false;
		gf::GF_OBJ_HANDLE* handle = InvalidHandle;
		int value = -1;
		int targetId = -1;
	};

	struct CharacterReadyTrack {
		btl::BattleCharacter* character = nullptr;
		std::array<std::uint64_t, SlotCount> readySince {};
	};

	std::array<DecisionEvent, HistoryCapacity> History {};
	std::size_t HistoryWrite = 0;
	std::size_t HistoryCount = 0;
	std::uint64_t FrameCounter = 0;
	constexpr std::size_t LoadHistoryCapacity = 32;
	std::array<std::uint64_t, LoadHistoryCapacity> LoadFrameHistory {};
	std::size_t LoadHistoryWrite = 0;
	std::size_t LoadHistoryCount = 0;
	bool TrackLoadFrames = false;
	std::uint64_t DecisionSequence = 0;
	std::array<std::uint64_t, SlotCount> SelectedCounts {};
	std::uint64_t GenericDecisionCount = 0;
	ScriptRequest PendingScriptRequest {};
	DecisionEvent* ActiveDecision = nullptr;
	std::array<CharacterReadyTrack, 16> ReadyTracks {};
	// -2 follows the currently controlled party leader, -1 shows everyone,
	// and non-negative values select one observed AI character ID.
	int FocusCharacterId = -2;

	template<typename T>
	T ReadField(const btl::BattleCharacter* character, std::size_t offset) {
		T value {};
		if(character != nullptr)
			std::memcpy(
				&value,
				reinterpret_cast<const std::uint8_t*>(character) + offset,
				sizeof(T)
			);
		return value;
	}

	CharacterReadyTrack& GetReadyTrack(btl::BattleCharacter* character) {
		for(auto& track : ReadyTracks) {
			if(track.character == character)
				return track;
		}
		for(auto& track : ReadyTracks) {
			if(track.character == nullptr) {
				track.character = character;
				return track;
			}
		}
		ReadyTracks[0] = {};
		ReadyTracks[0].character = character;
		return ReadyTracks[0];
	}

	const char* RequestKindName(RequestKind kind) {
		switch(kind) {
			case RequestKind::NativeGeneric: return "Native generic";
			case RequestKind::NativeSlot: return "Native explicit slot";
			case RequestKind::ScriptGeneric: return "Script generic";
			case RequestKind::ScriptSlot: return "Script explicit slot";
			case RequestKind::ScriptArtId: return "Script explicit Art ID";
		}
		return "Unknown";
	}

	void DescribeFailureMask(std::uint32_t mask, char* output, std::size_t size) {
		if(mask == 0) {
			std::snprintf(output, size, "Eligible");
			return;
		}
		output[0] = '\0';
		auto append = [&](const char* text) {
			if(output[0] != '\0')
				std::strncat(output, ", ", size - std::strlen(output) - 1);
			std::strncat(output, text, size - std::strlen(output) - 1);
		};
		if(mask & 0x8) append("empty");
		if(mask & 0x10) append("no target");
		if(mask & 0x20) append("wrong target");
		if(mask & 0x40) append("target dead");
		if(mask & 0x80) append("range");
		if(mask & 0x100) append("recharge");
		if(mask & 0x1000) append("height");
		const auto known = 0x8u | 0x10u | 0x20u | 0x40u | 0x80u | 0x100u | 0x1000u;
		if(mask & ~known) {
			char raw[24] {};
			std::snprintf(raw, sizeof(raw), "other 0x%X", mask & ~known);
			append(raw);
		}
	}

	void SnapshotSlots(DecisionEvent& event) {
		auto& readyTrack = GetReadyTrack(event.character);
		for(int slotIndex = 0; slotIndex < SlotCount; slotIndex++) {
			auto& output = event.slots[slotIndex];
			const auto* slot = event.character->GetArtsSlotData(slotIndex, false, -1);
			if(slot == nullptr)
				continue;
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(slot);
			std::uint16_t packedArtId = 0;
			std::memcpy(&packedArtId, bytes, sizeof(packedArtId));
			std::memcpy(&output.recharge, bytes + 4, sizeof(output.recharge));
			output.artId = packedArtId & 0x7ff;
			output.present = output.artId != 0;
			output.failureMask = event.character->IsEnableArts(
				-1,
				slotIndex,
				0,
				InvalidHandle
			);
			output.eligible = output.failureMask == 0;
			if(output.eligible) {
				event.candidateMask |= 1u << slotIndex;
				event.candidateCount++;
			}
			if(output.recharge >= 1.f) {
				if(readyTrack.readySince[slotIndex] == 0)
					readyTrack.readySince[slotIndex] = FrameCounter;
			} else {
				readyTrack.readySince[slotIndex] = 0;
			}
			output.readyObservedFrame = readyTrack.readySince[slotIndex];
		}
	}

	void PushHistory(const DecisionEvent& event) {
		if(xenomods::CombatAiDebug::FreezeHistory)
			return;
		History[HistoryWrite] = event;
		HistoryWrite = (HistoryWrite + 1) % HistoryCapacity;
		HistoryCount = std::min<std::size_t>(HistoryCount + 1, HistoryCapacity);
	}

	const DecisionEvent* GetHistoryNewest(std::size_t reverseIndex = 0) {
		if(reverseIndex >= HistoryCount)
			return nullptr;
		const auto index = (HistoryWrite + HistoryCapacity - 1 - reverseIndex) % HistoryCapacity;
		return &History[index];
	}

	bool MatchesFocus(
		const DecisionEvent& event,
		gf::GF_OBJ_HANDLE* controlledHandle
	) {
		if(FocusCharacterId == -1)
			return true;
		if(FocusCharacterId == -2)
			return event.handle == controlledHandle;
		return event.characterId == FocusCharacterId;
	}

	const DecisionEvent* GetFocusedHistoryNewest(
		gf::GF_OBJ_HANDLE* controlledHandle,
		std::size_t focusedReverseIndex = 0
	) {
		std::size_t matched = 0;
		for(std::size_t index = 0; index < HistoryCount; index++) {
			const auto* event = GetHistoryNewest(index);
			if(event == nullptr || !MatchesFocus(*event, controlledHandle))
				continue;
			if(matched == focusedReverseIndex)
				return event;
			matched++;
		}
		return nullptr;
	}

	void DriverFocusSelector(gf::GF_OBJ_HANDLE* controlledHandle) {
		int controlledId = -1;
		std::array<int, 16> observedIds {};
		int observedCount = 0;
		for(std::size_t index = 0; index < HistoryCount; index++) {
			const auto* event = GetHistoryNewest(index);
			if(event == nullptr)
				continue;
			if(event->handle == controlledHandle)
				controlledId = event->characterId;
			if(event->characterId < 0)
				continue;
			bool duplicate = false;
			for(int observed = 0; observed < observedCount; observed++) {
				if(observedIds[observed] == event->characterId) {
					duplicate = true;
					break;
				}
			}
			if(!duplicate && observedCount < static_cast<int>(observedIds.size()))
				observedIds[observedCount++] = event->characterId;
		}

		char currentLabel[64] {};
		if(FocusCharacterId == -2) {
			if(controlledId >= 0)
				std::snprintf(currentLabel, sizeof(currentLabel), "Controlled Driver (ID %d)", controlledId);
			else
				std::snprintf(currentLabel, sizeof(currentLabel), "Controlled Driver");
		} else if(FocusCharacterId == -1) {
			std::snprintf(currentLabel, sizeof(currentLabel), "All Drivers");
		} else {
			std::snprintf(currentLabel, sizeof(currentLabel), "Driver ID %d", FocusCharacterId);
		}

		ImGui::SetNextItemWidth(ImGui::GetFrameHeight() * 12.f);
		if(ImGui::BeginCombo("Focus", currentLabel)) {
			if(ImGui::Selectable("Controlled Driver", FocusCharacterId == -2))
				FocusCharacterId = -2;
			if(ImGui::Selectable("All Drivers", FocusCharacterId == -1))
				FocusCharacterId = -1;
			for(int index = 0; index < observedCount; index++) {
				char label[48] {};
				std::snprintf(
					label,
					sizeof(label),
					"Driver ID %d%s",
					observedIds[index],
					observedIds[index] == controlledId ? " (controlled)" : ""
				);
				if(ImGui::Selectable(label, FocusCharacterId == observedIds[index]))
					FocusCharacterId = observedIds[index];
			}
			ImGui::EndCombo();
		}
	}

	void ClearHistory() {
		History = {};
		HistoryWrite = 0;
		HistoryCount = 0;
		SelectedCounts = {};
		GenericDecisionCount = 0;
		ReadyTracks = {};
	}

	struct TraceAiArts : skylaunch::hook::Trampoline<TraceAiArts> {
		static bool Hook(
			btl::BattleCharacter* character,
			btl::BattleCharacter::ACTION_ID actionId,
			int requestedSlot
		) {
			if(!xenomods::CombatAiDebug::CaptureEnabled)
				return Orig(character, actionId, requestedSlot);

			DecisionEvent event {};
			event.sequence = ++DecisionSequence;
			event.frame = FrameCounter;
			event.character = character;
			event.requestedActionId = static_cast<int>(actionId);
			event.currentActionRaw = ReadField<std::uint8_t>(character, 0xfec);
			event.aiDelayTimer = ReadField<float>(character, 0x1168);
			event.handle = ReadField<gf::GF_OBJ_HANDLE*>(character, 0x118);
			event.targetHandle = ReadField<gf::GF_OBJ_HANDLE*>(character, 0x120);
			if(event.handle != nullptr && event.handle != InvalidHandle)
				event.characterId = btl::Utility::AI_GetCharacterID(event.handle);
			event.requestedValue = requestedSlot;
			event.requestKind = requestedSlot < 0
				? RequestKind::NativeGeneric
				: RequestKind::NativeSlot;
			if(PendingScriptRequest.active && PendingScriptRequest.handle == event.handle) {
				event.targetId = PendingScriptRequest.targetId;
				event.requestedValue = PendingScriptRequest.value;
				event.requestKind = PendingScriptRequest.byId
					? RequestKind::ScriptArtId
					: (PendingScriptRequest.value < 0
						? RequestKind::ScriptGeneric
						: RequestKind::ScriptSlot);
			}
			SnapshotSlots(event);
			if(requestedSlot < 0)
				GenericDecisionCount++;

			auto* previousActiveDecision = ActiveDecision;
			ActiveDecision = &event;
			event.aiArtsResult = Orig(character, actionId, requestedSlot);
			ActiveDecision = previousActiveDecision;

			if(event.selectedSlot >= 0 && event.selectedSlot < SlotCount) {
				event.selectedArtId = event.slots[event.selectedSlot].artId;
				SelectedCounts[event.selectedSlot]++;
				int candidateIndex = 0;
				for(int slot = 0; slot < SlotCount; slot++) {
					if((event.candidateMask & (1u << slot)) == 0)
						continue;
					if(slot == event.selectedSlot) {
						event.selectedCandidateIndex = candidateIndex;
						break;
					}
					candidateIndex++;
				}
				const auto readyFrame = event.slots[event.selectedSlot].readyObservedFrame;
				if(readyFrame != 0 && event.frame >= readyFrame)
					event.observedReadyDelay = event.frame - readyFrame;
			}
			event.rawCancelState = ReadField<std::uint16_t>(character, 0x1076);
			event.rawTimingState = ReadField<std::uint32_t>(character, 0x107c);
			PushHistory(event);

			if(xenomods::CombatAiDebug::LogEvents) {
				xenomods::g_Logger->LogInfo(
					"[Combat AI] frame {} char {} candidates 0x{:02X} selected slot {} art {} start {}",
					event.frame,
					event.characterId,
					event.candidateMask,
					event.selectedSlot,
					event.selectedArtId,
					event.startResult
				);
			}
			return event.aiArtsResult;
		}
	};

	struct TraceStartArts : skylaunch::hook::Trampoline<TraceStartArts> {
		static bool Hook(
			btl::BattleCharacter* character,
			int slot,
			bool unk1,
			bool unk2
		) {
			const bool result = Orig(character, slot, unk1, unk2);
			if(ActiveDecision != nullptr && ActiveDecision->character == character) {
				ActiveDecision->startObserved = true;
				ActiveDecision->selectedSlot = slot;
				ActiveDecision->startResult = result;
			}
			return result;
		}
	};

	struct TraceScriptArts : skylaunch::hook::Trampoline<TraceScriptArts> {
		static bool Hook(gf::GF_OBJ_HANDLE* handle, int slot, int targetId) {
			const auto previous = PendingScriptRequest;
			if(!PendingScriptRequest.byId)
				PendingScriptRequest = {true, false, handle, slot, targetId};
			const bool result = Orig(handle, slot, targetId);
			PendingScriptRequest = previous;
			return result;
		}
	};

	struct TraceScriptArtsById : skylaunch::hook::Trampoline<TraceScriptArtsById> {
		static bool Hook(gf::GF_OBJ_HANDLE* handle, int artId, int targetId) {
			const auto previous = PendingScriptRequest;
			PendingScriptRequest = {true, true, handle, artId, targetId};
			const bool result = Orig(handle, artId, targetId);
			PendingScriptRequest = previous;
			return result;
		}
	};
#endif

} // namespace

namespace xenomods {

	bool CombatAiDebug::ShowOverlay = false;
	bool CombatAiDebug::CaptureEnabled = false;
	bool CombatAiDebug::FreezeHistory = false;
	bool CombatAiDebug::LogEvents = false;
	bool CombatAiDebug::ShowFrameCounter = false;

	void CombatAiDebug::TopBarButton() {
#if XENOMODS_CODENAME(bf2)
		if(ImGui::MenuItem("Combat AI", nullptr, ShowOverlay)) {
			ShowOverlay = !ShowOverlay;
			if(ShowOverlay)
				CaptureEnabled = true;
		}
#endif
	}

	void CombatAiDebug::MenuSection() {
#if XENOMODS_CODENAME(bf2)
		ImGui::Checkbox("Show combat AI trace", &ShowOverlay);
		ImGui::Checkbox("Capture combat AI decisions", &CaptureEnabled);
		ImGui::Checkbox("Freeze combat AI history", &FreezeHistory);
		ImGui::Checkbox("Log combat AI decisions", &LogEvents);
		ImGui::Checkbox("Show frame counter", &ShowFrameCounter);
		ImGui::TextWrapped("This is observational. It does not force Arts or modify party AI.");
#endif
	}

	void CombatAiDebug::Overlay() {
#if XENOMODS_CODENAME(bf2)
		if(!ShowOverlay)
			return;
		ImGui::SetNextWindowPos(ImVec2(570.f, 20.f), ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(720.f, 510.f), ImGuiCond_Appearing);
		if(!ImGui::Begin("Combat AI Trace", &ShowOverlay)) {
			ImGui::End();
			return;
		}
		ImGui::Checkbox("Capture", &CaptureEnabled);
		ImGui::SameLine();
		ImGui::Checkbox("Freeze", &FreezeHistory);
		ImGui::SameLine();
		ImGui::Checkbox("Log", &LogEvents);
		ImGui::SameLine();
		if(ImGui::Button("Clear"))
			ClearHistory();
		ImGui::SameLine();
		ImGui::Text("Frames: %llu  Decisions: %llu", FrameCounter, DecisionSequence);
		auto* controlledHandle = gf::GfGameParty::getLeader();
		DriverFocusSelector(controlledHandle);

		if(ImGui::BeginTabBar("CombatAiTabs")) {
			if(ImGui::BeginTabItem("Latest")) {
				const auto* event = GetFocusedHistoryNewest(controlledHandle);
				if(event == nullptr) {
					ImGui::TextWrapped("Waiting for an Art decision from the focused Driver...");
				} else {
					ImGui::Text(
						"Frame %llu  Driver ID %d  Object %p",
						event->frame,
						event->characterId,
						event->character
					);
					ImGui::Text(
						"Request: %s  value %d  target ID %d / handle %p",
						RequestKindName(event->requestKind),
						event->requestedValue,
						event->targetId,
						event->targetHandle
					);
					ImGui::Text(
						"Current/previous action raw: %d  requested ACTION_ID: %d  AI delay: %.3f",
						event->currentActionRaw,
						event->requestedActionId,
						event->aiDelayTimer
					);
					ImGui::Text(
						"Candidates: %d (mask 0x%02X)  chosen candidate: %d",
						event->candidateCount,
						event->candidateMask,
						event->selectedCandidateIndex
					);
					ImGui::Text(
						"Selected slot %d / Art %d  StartArts: %s  AI_Arts: %s",
						event->selectedSlot,
						event->selectedArtId,
						event->startObserved
							? (event->startResult ? "success" : "rejected")
							: "not called",
						event->aiArtsResult ? "true" : "false"
					);
					ImGui::Text(
						"Ready-to-use delay: %llu game frames since first observed ready",
						event->observedReadyDelay
					);

					if(ImGui::BeginTable(
						"CombatAiSlots",
						6,
						ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
					)) {
						ImGui::TableSetupColumn("Slot");
						ImGui::TableSetupColumn("Art ID");
						ImGui::TableSetupColumn("Recharge");
						ImGui::TableSetupColumn("Ready");
						ImGui::TableSetupColumn("Mask");
						ImGui::TableSetupColumn("Result");
						ImGui::TableHeadersRow();
						for(int slot = 0; slot < SlotCount; slot++) {
							const auto& snapshot = event->slots[slot];
							char reason[96] {};
							DescribeFailureMask(snapshot.failureMask, reason, sizeof(reason));
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0); ImGui::Text("%d%s", slot, slot == event->selectedSlot ? " *" : "");
							ImGui::TableSetColumnIndex(1); ImGui::Text("%d", snapshot.artId);
							ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", snapshot.recharge);
							ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(snapshot.recharge >= 1.f ? "Yes" : "No");
							ImGui::TableSetColumnIndex(4); ImGui::Text("0x%X", snapshot.failureMask);
							ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(reason);
						}
						ImGui::EndTable();
					}
					ImGui::Text(
						"Cancel evidence: unresolved  raw timing 0x%08X / state 0x%04X",
						event->rawTimingState,
						event->rawCancelState
					);
					ImGui::TextDisabled("Combo state and exact cancel success still require verified field mappings.");
				}
				ImGui::EndTabItem();
			}

			if(ImGui::BeginTabItem("History")) {
				if(ImGui::BeginTable(
					"CombatAiHistory",
					9,
					ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
					ImVec2(0.f, 390.f)
				)) {
					const char* headers[] = {"Frame", "Driver", "Mode", "Eligible", "RNG", "Slot", "Art", "Start", "Delay"};
					for(const auto* header : headers)
						ImGui::TableSetupColumn(header);
					ImGui::TableHeadersRow();
					for(std::size_t index = 0; ; index++) {
						const auto* event = GetFocusedHistoryNewest(controlledHandle, index);
						if(event == nullptr)
							break;
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::Text("%llu", event->frame);
						ImGui::TableSetColumnIndex(1); ImGui::Text("%d", event->characterId);
						ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(RequestKindName(event->requestKind));
						ImGui::TableSetColumnIndex(3); ImGui::Text("%d / 0x%02X", event->candidateCount, event->candidateMask);
						ImGui::TableSetColumnIndex(4); ImGui::Text("%d", event->selectedCandidateIndex);
						ImGui::TableSetColumnIndex(5); ImGui::Text("%d", event->selectedSlot);
						ImGui::TableSetColumnIndex(6); ImGui::Text("%d", event->selectedArtId);
						ImGui::TableSetColumnIndex(7); ImGui::TextUnformatted(event->startObserved ? (event->startResult ? "Yes" : "Rejected") : "No call");
						ImGui::TableSetColumnIndex(8); ImGui::Text("%llu", event->observedReadyDelay);
					}
					ImGui::EndTable();
				}
				ImGui::EndTabItem();
			}

			if(ImGui::BeginTabItem("Statistics")) {
				std::uint64_t focusedGenericCount = 0;
				std::array<std::uint64_t, SlotCount> focusedSelectedCounts {};
				for(std::size_t index = 0; index < HistoryCount; index++) {
					const auto* event = GetHistoryNewest(index);
					if(event == nullptr || !MatchesFocus(*event, controlledHandle))
						continue;
					if(
						event->requestKind == RequestKind::NativeGeneric
						|| event->requestKind == RequestKind::ScriptGeneric
					) {
						focusedGenericCount++;
						if(event->selectedSlot >= 0 && event->selectedSlot < SlotCount)
							focusedSelectedCounts[event->selectedSlot]++;
					}
				}
				ImGui::Text("Focused generic decisions observed: %llu", focusedGenericCount);
				for(int slot = 0; slot < SlotCount; slot++) {
					const double percent = focusedGenericCount == 0
						? 0.0
						: static_cast<double>(focusedSelectedCounts[slot]) * 100.0 /
							static_cast<double>(focusedGenericCount);
					ImGui::Text(
						"Slot %d selected: %llu (%.2f%% of generic decisions)",
						slot,
						focusedSelectedCounts[slot],
						percent
					);
				}
				ImGui::Separator();
				ImGui::TextWrapped(
					"The RNG column is the selected Art's zero-based position in the eligible candidate list. "
					"For generic AI selection this is the observable result of mtRand(candidate count)."
				);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::End();
#endif
	}

	void CombatAiDebug::FrameCounterOverlay() {
#if XENOMODS_CODENAME(bf2)
		if(!ShowFrameCounter)
			return;
		const ImGuiIO& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(
			ImVec2(io.DisplaySize.x - 8.f, 20.f),
			ImGuiCond_Always,
			ImVec2(1.f, 0.f)
		);
		ImGui::SetNextWindowSizeConstraints(ImVec2(230.f, 0.f), ImVec2(230.f, 210.f));
		if(ImGui::Begin("Frame Counter", &ShowFrameCounter, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Since most recent load: %llu", FrameCounter);
			ImGui::Text("Nominal 30 FPS time: %.3f s", static_cast<double>(FrameCounter) / 30.0);
			if(ImGui::Button("Reset counter"))
				FrameCounter = 0;
			ImGui::SameLine();
			if(ImGui::Button("Track"))
				TrackLoadFrames = !TrackLoadFrames;
			ImGui::SameLine();
			if(TrackLoadFrames)
				ImGui::TextColored(ImVec4(0.3f, 1.f, 0.4f, 1.f), "ON");
			else
				ImGui::TextDisabled("OFF");

			ImGui::SeparatorText("History");
			const float historyHeight = ImGui::GetTextLineHeightWithSpacing() * 4.f;
			if(ImGui::BeginChild("LoadFrameHistory", ImVec2(0.f, historyHeight), true)) {
				if(LoadHistoryCount == 0) {
					ImGui::TextDisabled("No tracked loads");
				} else {
					for(std::size_t newest = 0; newest < LoadHistoryCount; newest++) {
						const std::size_t index =
							(LoadHistoryWrite + LoadHistoryCapacity - 1 - newest)
							% LoadHistoryCapacity;
						ImGui::Text("%zu: %lluf", newest + 1, LoadFrameHistory[index]);
					}
				}
			}
			ImGui::EndChild();
		}
		ImGui::End();
#endif
	}

	void CombatAiDebug::Initialize() {
		UpdatableModule::Initialize();
#if XENOMODS_CODENAME(bf2)
		g_Logger->LogDebug("Setting up XC2 combat AI tracer...");
		TraceAiArts::HookAt("_ZN3btl15BattleCharacter7AI_ArtsENS0_9ACTION_IDEi");
		TraceStartArts::HookAt("_ZN3btl15BattleCharacter9StartArtsEibb");
		TraceScriptArts::HookAt("_ZN3btl7Utility18AI_StartActionArtsEPN2gf13GF_OBJ_HANDLEEii");
		TraceScriptArtsById::HookAt("_ZN3btl7Utility22AI_StartActionArtsByIDEPN2gf13GF_OBJ_HANDLEEii");

		g_Menu->RegisterTopBarCallback(&TopBarButton);
		g_Menu->RegisterRenderCallback(&Overlay, true);
		g_Menu->RegisterRenderCallback(&FrameCounterOverlay, true);
#endif
	}

	void CombatAiDebug::OnMapChange(unsigned short mapId) {
#if XENOMODS_CODENAME(bf2)
		if(TrackLoadFrames && FrameCounter > 0) {
			LoadFrameHistory[LoadHistoryWrite] = FrameCounter;
			LoadHistoryWrite = (LoadHistoryWrite + 1) % LoadHistoryCapacity;
			LoadHistoryCount = std::min(
				LoadHistoryCount + 1,
				LoadHistoryCapacity
			);
		}
		FrameCounter = 0;
		ClearHistory();
#endif
	}

	void CombatAiDebug::Update(fw::UpdateInfo* updateInfo) {
#if XENOMODS_CODENAME(bf2)
		FrameCounter++;
#endif
	}

#if XENOMODS_CODENAME(bf2)
	XENOMODS_REGISTER_MODULE(CombatAiDebug);
#endif

} // namespace xenomods
