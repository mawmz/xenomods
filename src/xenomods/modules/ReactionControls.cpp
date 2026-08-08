#include "ReactionControls.hpp"

#include <cstdint>
#include <cstring>

#include <imgui.h>
#include <skylaunch/hookng/Hooks.hpp>

#include "xenomods/Logger.hpp"
#include "xenomods/engine/btl/Character.hpp"
#include "xenomods/engine/gf/Party.hpp"
#include "xenomods/menu/Menu.hpp"

namespace xenomods {

	bool ReactionControls::ShowWindow = false;

#if XENOMODS_CODENAME(bf2)
	namespace {
		using GetCharacterConstFn = btl::BattleCharacter* (*)(
			void* manager,
			gf::GF_OBJ_HANDLE* handle
		);
		using GetControlCharacterFn = btl::BattleCharacter* (*)(void* manager);
		using SetActionFn = void (*)(
			btl::BattleCharacter* character,
			btl::BattleCharacter::ACTION_ID action,
			bool force,
			bool unknown,
			int actionParam,
			float blendTime
		);
		using GetReactionBlowParamFn = bool (*)(
			const btl::BattleCharacter* character,
			int reaction,
			int powerLevel,
			float& horizontal,
			float& vertical
		);
		using SetBlowFn = bool (*)(
			btl::BattleCharacter* character,
			const mm::Vec3& direction,
			float horizontal,
			float vertical,
			float duration
		);

		constexpr const char* CharacterManagerSingletonSymbol =
			"_ZZN2mm3mtl12PtrSingletonIN3btl16CharacterManagerEE3sysEvE10s_instance";
		constexpr const char* GetCharacterConstSymbol =
			"_ZNK3btl16CharacterManager17GetCharacterConstEPN2gf13GF_OBJ_HANDLEE";
		constexpr const char* GetControlCharacterSymbol =
			"_ZN3btl16CharacterManager19GetControlCharacterEv";
		constexpr const char* SetActionSymbol =
			"_ZN3btl15BattleCharacter9SetActionENS0_9ACTION_IDEbbif";
		constexpr const char* GetReactionBlowParamSymbol =
			"_ZNK3btl15BattleCharacter20GetReactionBlowParamEiiRfS1_";
		constexpr const char* SetBlowSymbol =
			"_ZN3btl15BattleCharacter7SetBlowERKN2mm4Vec3Efff";

		void** CharacterManagerSingleton = nullptr;
		GetCharacterConstFn GetCharacterConst = nullptr;
		GetControlCharacterFn GetControlCharacter = nullptr;
		SetActionFn SetAction = nullptr;
		GetReactionBlowParamFn GetReactionBlowParam = nullptr;
		SetBlowFn SetBlow = nullptr;
		int SelectedKnockbackPower = 1;
		int SelectedBlowdownPower = 1;

		struct PendingReactionData {
			int reaction = 0;
			int power = 1;
			const char* name = nullptr;
		};

		PendingReactionData PendingReaction {};

		// Recovered from btl::SirenBattleManager::ForceBlow. Field characters do
		// not execute BattleCharacter reaction actions; the retail game sends this
		// notification to the character object's component at +0xE0 instead.
		struct alignas(8) FieldReactionMessage {
			std::uint32_t messageId = 0x01000035;
			std::uint32_t payloadSize = 0x20;
			std::uint32_t unknown08 = 0;
			std::uint32_t unknown0C = 0;
			std::uint32_t unknown10 = 0;
			std::uint32_t padding14 = 0;
			const mm::Vec3* position = nullptr;
			gf::GF_OBJ_HANDLE* attacker = nullptr;
			std::uint32_t reactionAndPower = 0;
			std::uint16_t unknown2C = 0;
			std::uint8_t enabled = 1;
			std::uint8_t unknown2F = 0;
			std::uint16_t unknown30 = 0;
			std::uint16_t padding32 = 0;
			std::uint32_t unknown34 = 0;
			std::uint8_t unknown38 = 0;
			std::uint8_t padding39[7] {};
		};

		static_assert(offsetof(FieldReactionMessage, position) == 0x18);
		static_assert(offsetof(FieldReactionMessage, attacker) == 0x20);
		static_assert(offsetof(FieldReactionMessage, reactionAndPower) == 0x28);

		using FieldReactionNotifyFn = void (*)(
			void* component,
			void* sender,
			FieldReactionMessage* message
		);
		template<typename T>
		T ReadCharacterField(
			const btl::BattleCharacter* character,
			std::size_t offset
		) {
			T value {};
			if(character != nullptr) {
				std::memcpy(
					&value,
					reinterpret_cast<const std::uint8_t*>(character) + offset,
					sizeof(T)
				);
			}
			return value;
		}

		bool ResolveReactionFunctions() {
			const auto singleton = skylaunch::hook::detail::ResolveSymbolBase(
				CharacterManagerSingletonSymbol
			);
			const auto getCharacter = skylaunch::hook::detail::ResolveSymbolBase(
				GetCharacterConstSymbol
			);
			const auto getControlCharacter =
				skylaunch::hook::detail::ResolveSymbolBase(
					GetControlCharacterSymbol
				);
			const auto setAction = skylaunch::hook::detail::ResolveSymbolBase(
				SetActionSymbol
			);
			const auto getReactionBlowParam =
				skylaunch::hook::detail::ResolveSymbolBase(
					GetReactionBlowParamSymbol
				);
			const auto setBlow = skylaunch::hook::detail::ResolveSymbolBase(
				SetBlowSymbol
			);
			if(
				singleton == skylaunch::hook::INVALID_FUNCTION_PTR
				|| getCharacter == skylaunch::hook::INVALID_FUNCTION_PTR
				|| getControlCharacter == skylaunch::hook::INVALID_FUNCTION_PTR
				|| setAction == skylaunch::hook::INVALID_FUNCTION_PTR
				|| getReactionBlowParam
					== skylaunch::hook::INVALID_FUNCTION_PTR
				|| setBlow == skylaunch::hook::INVALID_FUNCTION_PTR
			)
				return false;

			CharacterManagerSingleton = reinterpret_cast<void**>(singleton);
			GetCharacterConst = reinterpret_cast<GetCharacterConstFn>(getCharacter);
			GetControlCharacter = reinterpret_cast<GetControlCharacterFn>(
				getControlCharacter
			);
			SetAction = reinterpret_cast<SetActionFn>(setAction);
			GetReactionBlowParam =
				reinterpret_cast<GetReactionBlowParamFn>(getReactionBlowParam);
			SetBlow = reinterpret_cast<SetBlowFn>(setBlow);
			return true;
		}

		btl::BattleCharacter* ControlledCharacter() {
			if(
				CharacterManagerSingleton == nullptr
				|| GetCharacterConst == nullptr
				|| *CharacterManagerSingleton == nullptr
			)
				return nullptr;

			// This is the authoritative live BattleCharacter while the player is
			// engaged.  The party leader handle can still point at the field-side
			// object during the transition into battle.
			if(GetControlCharacter != nullptr) {
				auto* controlled = GetControlCharacter(*CharacterManagerSingleton);
				if(controlled != nullptr)
					return controlled;
			}

			auto* leader = gf::GfGameParty::getLeader();
			if(
				leader == nullptr
				|| leader == reinterpret_cast<gf::GF_OBJ_HANDLE*>(~std::uintptr_t(0))
			)
				return nullptr;
			return GetCharacterConst(*CharacterManagerSingleton, leader);
		}

		bool ApplyFieldReaction(
			btl::BattleCharacter* character,
			const PendingReactionData& request
		) {
			auto* handle = ReadCharacterField<gf::GF_OBJ_HANDLE*>(
				character,
				0x118
			);
			if(
				handle == nullptr
				|| handle == reinterpret_cast<gf::GF_OBJ_HANDLE*>(~std::uintptr_t(0))
			)
				return false;

			auto* object = reinterpret_cast<std::uint8_t*>(
				gf::GfObjUtil::getObj(handle)
			);
			if(object == nullptr)
				return false;

			void* component = object + 0xE0;
			auto** vtable = *reinterpret_cast<void***>(component);
			if(vtable == nullptr || vtable[3] == nullptr)
				return false;

			glm::vec3 fieldPosition = static_cast<glm::vec3>(
				ReadCharacterField<mm::Vec3>(character, 0x10)
			);
			fieldPosition.y += 1.f;
			mm::Vec3 position(fieldPosition);

			FieldReactionMessage message {};
			message.position = &position;
			message.attacker = handle;
			message.reactionAndPower =
				(static_cast<std::uint32_t>(request.power) << 24)
				| (static_cast<std::uint32_t>(request.reaction) << 16);

			auto notify = reinterpret_cast<FieldReactionNotifyFn>(vtable[3]);
			notify(component, component, &message);
			return true;
		}

		void ApplyReaction(PendingReactionData request) {
			auto* character = ControlledCharacter();
			if(
				character == nullptr
				|| SetAction == nullptr
				|| GetReactionBlowParam == nullptr
				|| SetBlow == nullptr
			) {
				g_Logger->ToastWarning(
					"reactions",
					"Controlled battle character is unavailable"
				);
				return;
			}

			// Ghidra: BattleCharacter::IsBattle() returns this exact flag.
			// +0xEE0 is only the character/weapon mode and can equal 2 while
			// Rex has his weapon drawn without actually being aggroed.
			const bool isAggroed =
				(ReadCharacterField<std::uint8_t>(character, 0xEFF) & 1) != 0;
			if(!isAggroed) {
				if(!ApplyFieldReaction(character, request)) {
					g_Logger->ToastWarning(
						"reactions",
						"Controlled field character is unavailable"
					);
					return;
				}
				g_Logger->ToastInfo(
					"reactions",
					"Forced {} (power {}) through field event",
					request.name,
					request.power
				);
				return;
			}

			// SetReaction is deliberately not used here.  It rejects reactions
			// for armor, immunity, chain attacks, current-action priority and
			// several character-state flags even when its random-resistance
			// argument is false.  These controls are debug force buttons, so run
			// the same lower-level force/action sequence after those gates.
			if(request.reaction != 3) {
				float horizontal = 0.f;
				float vertical = 0.f;
				if(GetReactionBlowParam(
					character,
					request.reaction,
					request.power,
					horizontal,
					vertical
				)) {
					mm::Vec3 facing = ReadCharacterField<mm::Vec3>(
						character,
						0x60
					);
					glm::vec3 away = -static_cast<glm::vec3>(facing);
					SetBlow(
						character,
						mm::Vec3(away),
						horizontal,
						vertical,
						-1.f
					);
				}
			}

			const int action = request.reaction == 1
				? 0x11
				: (request.reaction == 3 ? 0x12 : 0x10);
			const int actionParam =
				request.power | (request.reaction << 16);
			SetAction(
				character,
				static_cast<btl::BattleCharacter::ACTION_ID>(action),
				true,
				false,
				actionParam,
				0.f
			);

			g_Logger->ToastInfo(
				"reactions",
				"Forced {} (power {}) through battle action",
				request.name,
				request.power
			);
		}

		void ReactionButton(
			const char* label,
			int reaction,
			int power,
			float width
		) {
			if(ImGui::Button(label, ImVec2(width, 0.f))) {
				PendingReaction = {
					reaction,
					power,
					label
				};
			}
		}
	} // namespace
#endif

	void ReactionControls::TopBarButton() {
#if XENOMODS_CODENAME(bf2)
		if(ImGui::MenuItem("Reactions", nullptr, ShowWindow))
			ShowWindow = !ShowWindow;
#endif
	}

	void ReactionControls::MenuWindow() {
#if XENOMODS_CODENAME(bf2)
		if(!ShowWindow)
			return;

		ImGui::SetNextWindowSize(ImVec2(360.f, 235.f), ImGuiCond_Appearing);
		if(!ImGui::Begin("Reaction Controls", &ShowWindow)) {
			ImGui::End();
			return;
		}

		ImGui::TextDisabled("Forces the reaction on the controlled Driver.");
		ImGui::SetNextItemWidth(120.f);
		int knockbackPowerIndex = SelectedKnockbackPower - 1;
		if(ImGui::Combo(
			"Knockback level",
			&knockbackPowerIndex,
			"Level 1\0Level 2\0Level 3\0Level 4\0Level 5\0Level 6\0"
		))
			SelectedKnockbackPower = knockbackPowerIndex + 1;
		ImGui::SetNextItemWidth(120.f);
		int blowdownPowerIndex = SelectedBlowdownPower - 1;
		if(ImGui::Combo(
			"Blowdown level",
			&blowdownPowerIndex,
			"Level 1\0Level 2\0Level 3\0Level 4\0Level 5\0Level 6\0"
		))
			SelectedBlowdownPower = blowdownPowerIndex + 1;

		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float width = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
		ReactionButton("Knockback", 1, SelectedKnockbackPower, width);
		ImGui::SameLine();
		ReactionButton("Blowdown", 2, SelectedBlowdownPower, width);
		ReactionButton("Break", 3, 1, width);
		ImGui::SameLine();
		ReactionButton("Topple", 4, 1, width);
		ReactionButton("Launch", 5, 1, width);
		ImGui::SameLine();
		ReactionButton("Smash", 6, 1, width);

		ImGui::End();
#endif
	}

	void ReactionControls::Initialize() {
		UpdatableModule::Initialize();
#if XENOMODS_CODENAME(bf2)
		if(!ResolveReactionFunctions()) {
			g_Logger->LogError("Reaction control symbols are unavailable");
			return;
		}
		g_Menu->RegisterTopBarCallback(&TopBarButton);
		g_Menu->RegisterRenderCallback(&MenuWindow, true);
		g_Logger->LogInfo("Reaction controls installed");
#endif
	}

	void ReactionControls::Update(fw::UpdateInfo*) {
#if XENOMODS_CODENAME(bf2)
		if(PendingReaction.reaction == 0)
			return;
		const PendingReactionData request = PendingReaction;
		PendingReaction = {};
		ApplyReaction(request);
#endif
	}

	void ReactionControls::OnSceneTransition() {
#if XENOMODS_CODENAME(bf2)
		PendingReaction = {};
#endif
	}

#if XENOMODS_CODENAME(bf2)
	XENOMODS_REGISTER_MODULE(ReactionControls);
#endif

} // namespace xenomods
