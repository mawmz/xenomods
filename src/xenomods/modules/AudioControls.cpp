#include "AudioControls.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <imgui.h>
#include <skylaunch/hookng/Hooks.hpp>

#include <xenomods/Logger.hpp>

namespace xenomods {

	bool AudioControls::BackgroundMusic = true;

#if XENOMODS_CODENAME(bf2)
	namespace {
		using SetVolumeFn = void (*)(void* masterVolume, float volume);

		constexpr const char* GfSoundSingletonSymbol =
			"_ZZN2mm3mtl12PtrSingletonIN2gf7GfSoundEE3sysEvE10s_instance";
		constexpr const char* SetEventBgmVolumeSymbol =
			"_ZN2gf14GfMasterVolume17setEventBgmVolumeEf";
		constexpr const char* SetGameBgmVolumeSymbol =
			"_ZN2gf14GfMasterVolume16setGameBgmVolumeEf";
		constexpr const char* SetMenuBgmVolumeSymbol =
			"_ZN2gf14GfMasterVolume16setMenuBgmVolumeEf";

		// GfMasterVolume is embedded in GfSound. These offsets come directly from
		// GfSound::setupGame and the three native BGM volume setters.
		constexpr std::size_t MasterVolumeOffset = 0x6690;
		constexpr std::size_t EventBgmVolumeOffset = 0x10;
		constexpr std::size_t GameBgmVolumeOffset = 0x18;
		constexpr std::size_t MenuBgmVolumeOffset = 0x38;

		void** GfSoundSingleton = nullptr;
		SetVolumeFn SetEventBgmVolume = nullptr;
		SetVolumeFn SetGameBgmVolume = nullptr;
		SetVolumeFn SetMenuBgmVolume = nullptr;

		void* LastMasterVolume = nullptr;
		void* ReadyGfSound = nullptr;
		bool Muted = false;
		float SavedEventBgmVolume = 1.f;
		float SavedGameBgmVolume = 1.f;
		float SavedMenuBgmVolume = 1.f;

		struct GfSoundSetupGameHook
			: skylaunch::hook::Trampoline<GfSoundSetupGameHook> {
			static void Hook(void* sound) {
				Orig(sound);
				// setupGame is the point at which XC2 has created the master category
				// handle and copied the user's saved volume options into it. A mute
				// restored from toolWindows.toml must not touch the setters before this.
				ReadyGfSound = sound;
			}
		};

		bool ResolveAudioFunctions() {
			const auto singleton = skylaunch::hook::detail::ResolveSymbolBase(
				GfSoundSingletonSymbol
			);
			const auto eventSetter = skylaunch::hook::detail::ResolveSymbolBase(
				SetEventBgmVolumeSymbol
			);
			const auto gameSetter = skylaunch::hook::detail::ResolveSymbolBase(
				SetGameBgmVolumeSymbol
			);
			const auto menuSetter = skylaunch::hook::detail::ResolveSymbolBase(
				SetMenuBgmVolumeSymbol
			);
			if(
				singleton == skylaunch::hook::INVALID_FUNCTION_PTR
				|| eventSetter == skylaunch::hook::INVALID_FUNCTION_PTR
				|| gameSetter == skylaunch::hook::INVALID_FUNCTION_PTR
				|| menuSetter == skylaunch::hook::INVALID_FUNCTION_PTR
			)
				return false;

			GfSoundSingleton = reinterpret_cast<void**>(singleton);
			SetEventBgmVolume = reinterpret_cast<SetVolumeFn>(eventSetter);
			SetGameBgmVolume = reinterpret_cast<SetVolumeFn>(gameSetter);
			SetMenuBgmVolume = reinterpret_cast<SetVolumeFn>(menuSetter);
			return true;
		}

		float ReadVolume(void* masterVolume, std::size_t offset) {
			float value = 1.f;
			std::memcpy(
				&value,
				reinterpret_cast<const std::uint8_t*>(masterVolume) + offset,
				sizeof(value)
			);
			return std::isfinite(value) ? value : 1.f;
		}

		void ApplyVolumes(
			void* masterVolume,
			float eventVolume,
			float gameVolume,
			float menuVolume
		) {
			SetEventBgmVolume(masterVolume, eventVolume);
			SetGameBgmVolume(masterVolume, gameVolume);
			SetMenuBgmVolume(masterVolume, menuVolume);
		}
	} // namespace
#endif

	void AudioControls::MenuSection() {
		ImGui::Checkbox("Background music", &BackgroundMusic);
		if(ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"Toggles field, event, and menu music without muting sound effects or voices."
			);
	}

	void AudioControls::Initialize() {
		UpdatableModule::Initialize();
#if XENOMODS_CODENAME(bf2)
		if(!ResolveAudioFunctions())
			g_Logger->LogError("Background-music control symbols are unavailable");
		else
			g_Logger->LogInfo("Background-music controls installed");
		GfSoundSetupGameHook::HookAt("_ZN2gf7GfSound9setupGameEv");
#endif
	}

	void AudioControls::Update(fw::UpdateInfo*) {
#if XENOMODS_CODENAME(bf2)
		if(
			GfSoundSingleton == nullptr
			|| SetEventBgmVolume == nullptr
			|| SetGameBgmVolume == nullptr
			|| SetMenuBgmVolume == nullptr
		)
			return;

		void* const sound = *GfSoundSingleton;
		if(sound == nullptr) {
			LastMasterVolume = nullptr;
			ReadyGfSound = nullptr;
			Muted = false;
			return;
		}
		if(sound != ReadyGfSound)
			return;

		void* const masterVolume =
			reinterpret_cast<std::uint8_t*>(sound) + MasterVolumeOffset;
		if(masterVolume != LastMasterVolume) {
			LastMasterVolume = masterVolume;
			Muted = false;
		}

		if(!BackgroundMusic) {
			if(!Muted) {
				SavedEventBgmVolume = ReadVolume(
					masterVolume,
					EventBgmVolumeOffset
				);
				SavedGameBgmVolume = ReadVolume(masterVolume, GameBgmVolumeOffset);
				SavedMenuBgmVolume = ReadVolume(masterVolume, MenuBgmVolumeOffset);
				Muted = true;
			}

			// Native option/setup paths may restore these fields during loads. Only
			// call the setters when that happens; normal frames remain untouched.
			if(
				ReadVolume(masterVolume, EventBgmVolumeOffset) != 0.f
				|| ReadVolume(masterVolume, GameBgmVolumeOffset) != 0.f
				|| ReadVolume(masterVolume, MenuBgmVolumeOffset) != 0.f
			)
				ApplyVolumes(masterVolume, 0.f, 0.f, 0.f);
		}
		else if(Muted) {
			ApplyVolumes(
				masterVolume,
				SavedEventBgmVolume,
				SavedGameBgmVolume,
				SavedMenuBgmVolume
			);
			Muted = false;
		}
#endif
	}

#if XENOMODS_CODENAME(bf2)
	XENOMODS_REGISTER_MODULE(AudioControls);
#endif

} // namespace xenomods
