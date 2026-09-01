#include "AutoTutorials.hpp"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

#include <fmt/format.h>
#include <imgui.h>
#include <skylaunch/hookng/Hooks.hpp>
#include <toml++/toml.hpp>

#include <xenomods/Logger.hpp>
#include <xenomods/NnFile.hpp>
#include <xenomods/State.hpp>
#include <xenomods/InputBuffer.hpp>

#include "SkipTravelProfiler.hpp"

namespace xenomods {

	bool AutoTutorials::Enabled = false;

	namespace {
		constexpr std::uint32_t UiAButtonMask = 0x00000004u;

		enum class TutorialFamily {
			Standard,
			Dummy,
			Battle,
			Tips
		};

		struct TutorialRuntime {
			void* menu = nullptr;
			std::uint32_t sender = 0;
			bool advanceArmed = true;
			bool observedBusy = false;
		};

		TutorialRuntime standardRuntime {};
		TutorialRuntime dummyRuntime {};
		TutorialRuntime battleRuntime {};
		TutorialRuntime tipsRuntime {};

		bool lastSavedEnabled = false;

		std::string SettingsPath() {
			return fmt::format(
				XENOMODS_CONFIG_PATH "/{}/tasSettings.toml",
				XENOMODS_CODENAME_STR
			);
		}

		void SaveSettingsIfChanged() {
			if(AutoTutorials::Enabled == lastSavedEnabled)
				return;

			const auto path = SettingsPath();
			toml::table root;
			toml::parse_result existing = toml::parse_file(path);
			if(existing)
				root = std::move(existing).table();

			toml::table settings;
			settings.insert_or_assign("enabled", AutoTutorials::Enabled);
			root.insert_or_assign("auto_tutorials", std::move(settings));

			std::stringstream stream;
			stream << root;
			const std::string contents = stream.str();
			if(NnFile::Preallocate(path, contents.size())) {
				NnFile file(path, nn::fs::OpenMode_Write);
				file.Write(contents.c_str(), contents.size());
				file.Flush();
				lastSavedEnabled = AutoTutorials::Enabled;
			}
		}

		TutorialRuntime& RuntimeFor(TutorialFamily family) {
			switch(family) {
				case TutorialFamily::Standard:
					return standardRuntime;
				case TutorialFamily::Dummy:
					return dummyRuntime;
				case TutorialFamily::Battle:
					return battleRuntime;
				case TutorialFamily::Tips:
				default:
					return tipsRuntime;
			}
		}

		std::uint64_t SequenceCount(TutorialFamily family, const void* menu) {
			if(menu == nullptr)
				return 0;
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(menu);
			switch(family) {
				case TutorialFamily::Standard:
					return *reinterpret_cast<const std::uint64_t*>(bytes + 0x128);
				case TutorialFamily::Battle:
				case TutorialFamily::Tips:
					return *reinterpret_cast<const std::uint64_t*>(bytes + 0x1f0);
				case TutorialFamily::Dummy:
				default:
					return 0;
			}
		}

		void BeginTutorial(TutorialFamily family, void* menu) {
			auto& runtime = RuntimeFor(family);
			runtime = {};
			runtime.menu = menu;
			SkipTravelProfiler::OnTutorialBegin(
				static_cast<std::uint32_t>(family), menu
			);
		}

		void EndTutorial(TutorialFamily family, void* menu) {
			SkipTravelProfiler::OnTutorialEnd(
				static_cast<std::uint32_t>(family), menu
			);
			auto& runtime = RuntimeFor(family);
			if(runtime.menu == menu) {
				InputBuffer::CancelAutoUiAction(runtime.sender);
				runtime = {};
			}
		}

		void ObserveListener(
			TutorialFamily family,
			void* listener,
			const void* eventData,
			std::uint32_t sender,
			bool after
		) {
			if(listener == nullptr)
				return;
			auto& runtime = RuntimeFor(family);
			void* parent = *reinterpret_cast<void**>(
				reinterpret_cast<std::uint8_t*>(listener) + 0x20
			);
			const std::uint16_t event = eventData == nullptr
				? 0
				: *reinterpret_cast<const std::uint16_t*>(eventData);
			const std::uint32_t action = eventData == nullptr
				? 0
				: *reinterpret_cast<const std::uint32_t*>(
					reinterpret_cast<const std::uint8_t*>(eventData) + 0xc
				);
			SkipTravelProfiler::OnTutorialListenerEvent(
				static_cast<std::uint32_t>(family),
				parent,
				listener,
				event,
				action,
				sender,
				SequenceCount(family, parent),
				after
			);
			// GfMenuObjTutorial is a persistent field-menu object: initialize() runs
			// when its UI layer is created, not when each modal tutorial opens. The
			// object's start() command sends event 0x104 to this listener, making that
			// event the authoritative per-popup entry point. Reconstruct the runtime
			// here so scene transitions cannot leave Auto Tutorials detached from the
			// live object and its input owner.
			if(
				family == TutorialFamily::Standard
				&& !after
				&& event == 0x104
				&& parent != nullptr
				&& sender != 0
			) {
				InputBuffer::CancelAutoUiAction(runtime.sender);
				runtime = {};
				runtime.menu = parent;
				runtime.sender = sender;
				SkipTravelProfiler::OnTutorialBegin(
					static_cast<std::uint32_t>(family), parent
				);
				SkipTravelProfiler::OnTutorialListenerRegistered(
					static_cast<std::uint32_t>(family),
					parent,
					listener,
					sender
				);
			}
			if(runtime.menu == nullptr || runtime.menu != parent)
				return;
			// Listener callbacks may be emitted by animation children. Preserve the
			// object the listener was registered against; that is the UIInputLayer
			// owner, while an event sender is not necessarily an input owner.
			if(runtime.sender == 0 && sender != 0)
				runtime.sender = sender;
			// Animation/call-context completion is the native re-arm signal for a
			// newly displayed tutorial page. Do not manufacture a frame cooldown.
			if(event >= 0x101 && event <= 0x105 && SequenceCount(family, parent) == 0) {
				runtime.advanceArmed = true;
				runtime.observedBusy = false;
			}
			// closeAll() sends 0x22 to this same persistent listener. Clear only
			// after the native handler returns so the complete close event remains
			// visible in the trace and no queued A can escape into field control.
			if(
				family == TutorialFamily::Standard
				&& after
				&& event == 0x22
			) {
				InputBuffer::CancelAutoUiAction(runtime.sender);
				SkipTravelProfiler::OnTutorialEnd(
					static_cast<std::uint32_t>(family), parent
				);
				runtime = {};
			}
		}

		void ObserveRegisteredListener(void* listener, std::uint32_t sender) {
			if(listener == nullptr || sender == 0)
				return;
			void* parent = *reinterpret_cast<void**>(
				reinterpret_cast<std::uint8_t*>(listener) + 0x20
			);
			for(auto entry : {
				std::pair {TutorialFamily::Standard, &standardRuntime},
				std::pair {TutorialFamily::Dummy, &dummyRuntime},
				std::pair {TutorialFamily::Battle, &battleRuntime},
				std::pair {TutorialFamily::Tips, &tipsRuntime}
			}) {
				auto* runtime = entry.second;
				if(runtime->menu == parent) {
					runtime->sender = sender;
					SkipTravelProfiler::OnTutorialListenerRegistered(
						static_cast<std::uint32_t>(entry.first),
						parent,
						listener,
						sender
					);
				return;
				}
			}
		}

		void AdvanceIfReady(TutorialFamily family, void* menu) {
			if(!AutoTutorials::Enabled)
				return;
			auto& runtime = RuntimeFor(family);
			if(
				runtime.menu != menu
				|| runtime.sender == 0
			)
				return;

			// The standard field-tutorial object's presentation sequence remains
			// busy long after its UIInputLayer has enabled A. Do not use that visual
			// sequence as an input gate. Keep A queued against the exact listener
			// object; InputBuffer advances Pending -> Neutral -> Idle and offers the
			// next press only when that object's native layer exposes and accepts 0x4.
			if(family == TutorialFamily::Standard) {
				SkipTravelProfiler::OnTutorialAdvanceRequested(
					static_cast<std::uint32_t>(family),
					menu,
					runtime.sender,
					SequenceCount(family, menu)
				);
				InputBuffer::RequestAutoUiAction(runtime.sender, UiAButtonMask);
				return;
			}

			const bool busy = SequenceCount(family, menu) != 0;
			if(!runtime.advanceArmed) {
				if(busy)
					runtime.observedBusy = true;
				else if(runtime.observedBusy) {
					runtime.advanceArmed = true;
					runtime.observedBusy = false;
				}
			}
			if(!runtime.advanceArmed || busy)
				return;

			SkipTravelProfiler::OnTutorialAdvanceRequested(
				static_cast<std::uint32_t>(family),
				menu,
				runtime.sender,
				SequenceCount(family, menu)
			);
			InputBuffer::RequestAutoUiAction(runtime.sender, UiAButtonMask);
		}

		void OnAutoUiActionAccepted(
			std::uint32_t object,
			std::uint32_t layerHandle
		) {
			SkipTravelProfiler::OnTutorialAdvanceAccepted(object, layerHandle);
			for(auto* runtime : {
				&standardRuntime, &dummyRuntime, &battleRuntime, &tipsRuntime
			}) {
				if(runtime->menu != nullptr && runtime->sender == object) {
					runtime->advanceArmed = false;
					runtime->observedBusy = false;
					return;
				}
			}
		}

		struct StandardListenerHook
			: skylaunch::hook::Trampoline<StandardListenerHook> {
			static void Hook(void* listener, const void* eventData, std::uint32_t sender) {
				ObserveListener(TutorialFamily::Standard, listener, eventData, sender, false);
				Orig(listener, eventData, sender);
				ObserveListener(TutorialFamily::Standard, listener, eventData, sender, true);
			}
		};

		struct DummyListenerHook : skylaunch::hook::Trampoline<DummyListenerHook> {
			static void Hook(void* listener, const void* eventData, std::uint32_t sender) {
				ObserveListener(TutorialFamily::Dummy, listener, eventData, sender, false);
				Orig(listener, eventData, sender);
				ObserveListener(TutorialFamily::Dummy, listener, eventData, sender, true);
			}
		};

		struct BattleListenerHook : skylaunch::hook::Trampoline<BattleListenerHook> {
			static void Hook(void* listener, const void* eventData, std::uint32_t sender) {
				ObserveListener(TutorialFamily::Battle, listener, eventData, sender, false);
				Orig(listener, eventData, sender);
				ObserveListener(TutorialFamily::Battle, listener, eventData, sender, true);
			}
		};

		struct TipsListenerHook : skylaunch::hook::Trampoline<TipsListenerHook> {
			static void Hook(void* listener, const void* eventData, std::uint32_t sender) {
				ObserveListener(TutorialFamily::Tips, listener, eventData, sender, false);
				Orig(listener, eventData, sender);
				ObserveListener(TutorialFamily::Tips, listener, eventData, sender, true);
			}
		};

		struct RegisterObjectListenerHook
			: skylaunch::hook::Trampoline<RegisterObjectListenerHook> {
			static void Hook(void* manager, std::uint32_t sender, void* listener) {
				ObserveRegisteredListener(listener, sender);
				Orig(manager, sender, listener);
			}
		};

#define AUTO_TUTORIAL_LIFECYCLE_HOOKS(Name, Family)                              \
		struct Name##InitializeHook                                                \
			: skylaunch::hook::Trampoline<Name##InitializeHook> {                   \
			static void Hook(void* menu) {                                           \
				BeginTutorial(TutorialFamily::Family, menu);                            \
				Orig(menu);                                                             \
			}                                                                        \
		};                                                                           \
		struct Name##FinalizeHook : skylaunch::hook::Trampoline<Name##FinalizeHook> { \
			static void Hook(void* menu) {                                           \
				EndTutorial(TutorialFamily::Family, menu);                              \
				Orig(menu);                                                             \
			}                                                                        \
		};                                                                           \
		struct Name##UpdateHook : skylaunch::hook::Trampoline<Name##UpdateHook> {   \
			static void Hook(void* menu) {                                           \
				Orig(menu);                                                             \
				AdvanceIfReady(TutorialFamily::Family, menu);                           \
			}                                                                        \
		}

		AUTO_TUTORIAL_LIFECYCLE_HOOKS(Standard, Standard);
		AUTO_TUTORIAL_LIFECYCLE_HOOKS(Dummy, Dummy);
		AUTO_TUTORIAL_LIFECYCLE_HOOKS(Battle, Battle);
		AUTO_TUTORIAL_LIFECYCLE_HOOKS(Tips, Tips);

#undef AUTO_TUTORIAL_LIFECYCLE_HOOKS
	} // namespace

	void AutoTutorials::MenuSection() {
		if(ImGui::Checkbox("Auto Tutorials", &Enabled)) {
			ClearRuntimeState();
			SaveSettingsIfChanged();
		}
	}

	void AutoTutorials::ClearRuntimeState() {
		InputBuffer::CancelAutoUiAction();
		standardRuntime = {};
		dummyRuntime = {};
		battleRuntime = {};
		tipsRuntime = {};
	}

	void AutoTutorials::Initialize() {
#if XENOMODS_CODENAME(bf2)
		const toml::parse_result settings = toml::parse_file(SettingsPath());
		if(settings)
			Enabled = settings["auto_tutorials"]["enabled"].value_or(false);
		lastSavedEnabled = Enabled;
		InputBuffer::SetAutoUiActionAcceptedCallback(&OnAutoUiActionAccepted);

		StandardInitializeHook::HookAt(
			"_ZN2gf17GfMenuObjTutorial10initializeEv"
		);
		StandardFinalizeHook::HookAt("_ZN2gf17GfMenuObjTutorial8finalizeEv");
		StandardUpdateHook::HookAt("_ZN2gf17GfMenuObjTutorial6updateEv");
		StandardListenerHook::HookAt(
			"_ZN2gf17GfMenuObjTutorial12MainListener11reciveEventERKN2ui9EventDataEj"
		);

		DummyInitializeHook::HookAt(
			"_ZN2gf22GfMenuObjDummyTutorial10initializeEv"
		);
		DummyFinalizeHook::HookAt(
			"_ZN2gf22GfMenuObjDummyTutorial8finalizeEv"
		);
		DummyUpdateHook::HookAt("_ZN2gf22GfMenuObjDummyTutorial6updateEv");
		DummyListenerHook::HookAt(
			"_ZN2gf22GfMenuObjDummyTutorial12MainListener11reciveEventERKN2ui9EventDataEj"
		);

		BattleInitializeHook::HookAt(
			"_ZN2gf22GfMenuObjBtlchTutorial10initializeEv"
		);
		BattleFinalizeHook::HookAt(
			"_ZN2gf22GfMenuObjBtlchTutorial8finalizeEv"
		);
		BattleUpdateHook::HookAt("_ZN2gf22GfMenuObjBtlchTutorial6updateEv");
		BattleListenerHook::HookAt(
			"_ZN2gf22GfMenuObjBtlchTutorial16TutorialListener11reciveEventERKN2ui9EventDataEj"
		);

		TipsInitializeHook::HookAt("_ZN2gf13GfMenuObjTips10initializeEv");
		TipsFinalizeHook::HookAt("_ZN2gf13GfMenuObjTips8finalizeEv");
		TipsUpdateHook::HookAt("_ZN2gf13GfMenuObjTips6updateEv");
		TipsListenerHook::HookAt(
			"_ZN2gf13GfMenuObjTips16TutorialListener11reciveEventERKN2ui9EventDataEj"
		);
		RegisterObjectListenerHook::HookAt(
			"_ZN2ui14UIEventManager20registObjectListenerEjPNS_16UIObjectListenerE"
		);

		g_Logger->LogInfo("Auto Tutorials hooks installed");
#endif
		UpdatableModule::Initialize();
	}

	void AutoTutorials::OnSceneTransition() {
		ClearRuntimeState();
	}

	XENOMODS_REGISTER_MODULE(AutoTutorials);

} // namespace xenomods
