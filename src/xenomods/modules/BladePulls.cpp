#include "BladePulls.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>
#include <nn/fs.h>
#include <skylaunch/hookng/Hooks.hpp>
#include <toml++/toml.hpp>

#include "xenomods/Logger.hpp"
#include "xenomods/NnFile.hpp"
#include "xenomods/engine/bdat/Bdat.hpp"
#include "xenomods/engine/gf/Data.hpp"
#include "xenomods/menu/Menu.hpp"

namespace xenomods {

	bool BladePulls::ShowWindow = false;

#if XENOMODS_CODENAME(bf2)
	namespace {
		enum class PullKind : std::uint8_t {
			Rare,
			Common
		};

		struct PullEntry {
			PullKind kind = PullKind::Rare;
			std::uint32_t bladeId = 0;
			std::uint8_t element = 1;
			std::uint8_t weaponType = 1;
			std::string name;
		};

		struct RareChoice {
			std::uint32_t bladeId = 0;
			std::string name;
		};

		constexpr std::uint32_t FirstCommonBlade = 0x3e9;
		constexpr std::size_t RareFlagOffset = 0x244;
		constexpr std::size_t GeneratedWeaponTypeOffset = 0x270;
		constexpr std::size_t GeneratedElementOffset = 0x278;
		constexpr std::size_t ResultBladeIdOffset = 0x32c;
		constexpr std::size_t ForcedRareBladeIdOffset = 0xa50;
		constexpr std::size_t SDataBladeIdOffset = 0x6;
		constexpr std::size_t ResultWeaponTypeOffset = 0x821;
		constexpr std::size_t ResultElementOffset = 0x823;

		constexpr std::array<const char*, 8> ElementNames {
			"Fire", "Water", "Wind", "Ice",
			"Electric", "Earth", "Light", "Dark"
		};
		constexpr std::array<const char*, 8> WeaponNames {
			"Megalances", "Greataxes", "Ether Cannons", "Bitballs",
			"Twin Rings", "Chroma Katanas", "Shield Hammers", "Knuckle Claws"
		};

		bool enabled = false;
		std::vector<PullEntry> pulls;
		std::vector<RareChoice> rareChoices;
		std::vector<std::string> presetFiles;
		std::string currentPreset = "bladePulls0.toml";
		int selectedPull = -1;
		int selectedRareChoice = 0;
		int selectedElement = 0;
		int selectedWeapon = 0;
		bool overridePendingCommit = false;
		bool hooksInstalled = false;

		std::string PresetsDirectory() {
			return fmt::format(
				XENOMODS_CONFIG_PATH "/{}/BladePulls",
				XENOMODS_CODENAME_STR
			);
		}

		std::string PresetPath() {
			return fmt::format("{}/{}", PresetsDirectory(), currentPreset);
		}

		bool IsPresetFile(const char* name) {
			if(name == nullptr)
				return false;
			const std::string value(name);
			return value.size() > 5 && value.ends_with(".toml");
		}

		bool NaturalLess(const std::string& left, const std::string& right) {
			std::size_t li = 0;
			std::size_t ri = 0;
			while(li < left.size() && ri < right.size()) {
				if(std::isdigit(static_cast<unsigned char>(left[li]))
					&& std::isdigit(static_cast<unsigned char>(right[ri]))) {
					std::size_t le = li;
					std::size_t re = ri;
					while(le < left.size() && std::isdigit(static_cast<unsigned char>(left[le])))
						le++;
					while(re < right.size() && std::isdigit(static_cast<unsigned char>(right[re])))
						re++;
					std::size_t ln = li;
					std::size_t rn = ri;
					while(ln < le && left[ln] == '0')
						ln++;
					while(rn < re && right[rn] == '0')
						rn++;
					const auto leftDigits = le - ln;
					const auto rightDigits = re - rn;
					if(leftDigits != rightDigits)
						return leftDigits < rightDigits;
					for(std::size_t digit = 0; digit < leftDigits; digit++) {
						if(left[ln + digit] != right[rn + digit])
							return left[ln + digit] < right[rn + digit];
					}
					const auto leftRun = le - li;
					const auto rightRun = re - ri;
					if(leftRun != rightRun)
						return leftRun < rightRun;
					li = le;
					ri = re;
					continue;
				}
				const auto lc = static_cast<unsigned char>(std::tolower(left[li]));
				const auto rc = static_cast<unsigned char>(std::tolower(right[ri]));
				if(lc != rc)
					return lc < rc;
				li++;
				ri++;
			}
			return left.size() == right.size() ? left < right : left.size() < right.size();
		}

		bool WriteTextFile(const std::string& path, const std::string& contents) {
			if(!NnFile::Preallocate(path, contents.size()))
				return false;
			NnFile file(path, nn::fs::OpenMode_Write);
			if(!file.Ok())
				return false;
			file.Write(contents.c_str(), contents.size());
			file.Flush();
			return true;
		}

		void RescanPresets() {
			presetFiles.clear();
			std::string ensurePath = PresetsDirectory() + "/placeholder";
			std::string_view ensurePathView = ensurePath;
			NnFile::EnsurePath(ensurePathView);
			nn::fs::DirectoryHandle directory {};
			if(R_FAILED(nn::fs::OpenDirectory(
				&directory,
				PresetsDirectory().c_str(),
				nn::fs::OpenDirectoryMode_File
			)))
				return;
			s64 count = 0;
			if(R_SUCCEEDED(nn::fs::GetDirectoryEntryCount(&count, directory)) && count > 0) {
				std::vector<nn::fs::DirectoryEntry> entries(static_cast<std::size_t>(count));
				s64 read = 0;
				if(R_SUCCEEDED(nn::fs::ReadDirectory(&read, entries.data(), directory, count))) {
					for(s64 index = 0; index < read; index++)
						if(IsPresetFile(entries[index].name))
							presetFiles.emplace_back(entries[index].name);
				}
			}
			nn::fs::CloseDirectory(directory);
			std::sort(presetFiles.begin(), presetFiles.end(), NaturalLess);
		}

		std::string PullLabel(const PullEntry& pull) {
			if(pull.kind == PullKind::Rare)
				return fmt::format("Rare: {}", pull.name.empty() ? "Unknown" : pull.name);
			const auto element = pull.element >= 1 && pull.element <= ElementNames.size()
				? ElementNames[pull.element - 1]
				: "Unknown";
			const auto weapon = pull.weaponType >= 1 && pull.weaponType <= WeaponNames.size()
				? WeaponNames[pull.weaponType - 1]
				: "Unknown";
			return fmt::format("Common: {} / {}", element, weapon);
		}

		void SavePreset() {
			toml::table root;
			root.emplace("enabled", enabled);
			toml::array queue;
			for(const auto& pull : pulls) {
				toml::table entry;
				entry.emplace("type", pull.kind == PullKind::Rare ? "rare" : "common");
				if(pull.kind == PullKind::Rare) {
					entry.emplace("blade_id", static_cast<std::int64_t>(pull.bladeId));
					entry.emplace("name", pull.name);
				} else {
					entry.emplace("element", static_cast<std::int64_t>(pull.element));
					entry.emplace("weapon_type", static_cast<std::int64_t>(pull.weaponType));
				}
				queue.emplace_back(std::move(entry));
			}
			root.emplace("pulls", std::move(queue));
			std::stringstream stream;
			stream << root;
			if(!WriteTextFile(PresetPath(), stream.str()))
				g_Logger->ToastError("blade-pulls", "Couldn't save {}", currentPreset);
			RescanPresets();
		}

		void LoadPreset() {
			pulls.clear();
			selectedPull = -1;
			const toml::parse_result result = toml::parse_file(PresetPath());
			if(!result) {
				enabled = false;
				return;
			}
			enabled = result["enabled"].value_or(false);
			if(const auto* queue = result["pulls"].as_array()) {
				for(const auto& node : *queue) {
					const auto* table = node.as_table();
					if(table == nullptr)
						continue;
					PullEntry pull;
					const std::string type = (*table)["type"].value_or(std::string("rare"));
					if(type == "common") {
						pull.kind = PullKind::Common;
						pull.element = static_cast<std::uint8_t>(
							std::clamp<std::int64_t>((*table)["element"].value_or(1), 1, 8)
						);
						pull.weaponType = static_cast<std::uint8_t>(
							std::clamp<std::int64_t>((*table)["weapon_type"].value_or(1), 1, 8)
						);
					} else {
						pull.kind = PullKind::Rare;
						pull.bladeId = static_cast<std::uint32_t>(
							std::max<std::int64_t>((*table)["blade_id"].value_or(0), 0)
						);
						pull.name = (*table)["name"].value_or(std::string("Unknown"));
					}
					pulls.push_back(std::move(pull));
				}
			}
		}

		void CreatePreset() {
			RescanPresets();
			int suffix = 0;
			while(std::find(
				presetFiles.begin(), presetFiles.end(),
				fmt::format("bladePulls{}.toml", suffix)
			) != presetFiles.end())
				suffix++;
			currentPreset = fmt::format("bladePulls{}.toml", suffix);
			enabled = false;
			pulls.clear();
			selectedPull = -1;
			SavePreset();
		}

		void RefreshRareChoices() {
			rareChoices.clear();
			auto* sheet = Bdat::getFP("BLD_RareList");
			if(sheet == nullptr)
				return;

			std::set<std::uint32_t> seen;
			const auto first = Bdat::getIdTop(sheet);
			const auto last = Bdat::getIdEnd(sheet);
			for(unsigned int row = first; row < last; row++) {
				const auto bladeId = static_cast<std::uint32_t>(
					Bdat::getValCheck(sheet, "Blade", row, Bdat::ValueType::kUInt16)
				);
				if(bladeId == 0 || !seen.insert(bladeId).second)
					continue;
				const char* nativeName = gf::GfDataBlade::getName(bladeId);
				rareChoices.push_back({
					bladeId,
					nativeName != nullptr && nativeName[0] != '\0'
						? nativeName
						: fmt::format("Blade {}", bladeId)
				});
			}
			std::sort(
				rareChoices.begin(), rareChoices.end(),
				[](const RareChoice& left, const RareChoice& right) {
					return NaturalLess(left.name, right.name);
				}
			);
			selectedRareChoice = std::clamp(
				selectedRareChoice,
				0,
				std::max(0, static_cast<int>(rareChoices.size()) - 1)
			);
		}

		bool IsRareBladeId(std::uint32_t bladeId) {
			return std::any_of(
				rareChoices.begin(), rareChoices.end(),
				[bladeId](const RareChoice& choice) {
					return choice.bladeId == bladeId;
				}
			);
		}

		template<typename T>
		T ReadField(const void* object, std::size_t offset) {
			T value {};
			std::memcpy(
				&value,
				reinterpret_cast<const std::uint8_t*>(object) + offset,
				sizeof(T)
			);
			return value;
		}

		template<typename T>
		void WriteField(void* object, std::size_t offset, T value) {
			std::memcpy(
				reinterpret_cast<std::uint8_t*>(object) + offset,
				&value,
				sizeof(T)
			);
		}

		std::size_t queueCursor = 0;

		PullEntry* ActivePull() {
			if(!enabled || queueCursor >= pulls.size())
				return nullptr;
			return &pulls[queueCursor];
		}

		void ResetQueueCursor() {
			queueCursor = 0;
			overridePendingCommit = false;
		}

		void CommitPull(PullKind kind) {
			auto* pull = ActivePull();
			if(!overridePendingCommit || pull == nullptr || pull->kind != kind)
				return;
			overridePendingCommit = false;
			queueCursor++;
			if(queueCursor >= pulls.size()) {
				enabled = false;
				SavePreset();
			}
		}

		struct SetupSDataBladeHook : skylaunch::hook::Trampoline<SetupSDataBladeHook> {
			static void Hook(void* result, void* info) {
				auto* pull = ActivePull();
				if(pull == nullptr) {
					Orig(result, info);
					return;
				}

				if(pull->kind == PullKind::Rare) {
					WriteField<std::uint8_t>(info, RareFlagOffset, 1);
					// +0x32c belongs to the generated/common result path.  Native
					// rare creation leaves it clear and resolves the selected
					// BLD_RareList entry into +0xa50.  Populating both fields makes
					// setupSDataBlade prefer the wrong path and produces an empty
					// result card.
					WriteField<std::uint32_t>(info, ResultBladeIdOffset, 0);
					WriteField<std::uint32_t>(info, ForcedRareBladeIdOffset, pull->bladeId);
				} else {
					WriteField<std::uint8_t>(info, RareFlagOffset, 0);
					WriteField<std::uint32_t>(info, ForcedRareBladeIdOffset, 0);
					const auto generatedId = ReadField<std::uint32_t>(info, ResultBladeIdOffset);
					if(generatedId == 0 || IsRareBladeId(generatedId))
						WriteField<std::uint32_t>(info, ResultBladeIdOffset, FirstCommonBlade);
					WriteField<std::uint32_t>(
						info, GeneratedWeaponTypeOffset,
						static_cast<std::uint32_t>(pull->weaponType)
					);
					WriteField<std::uint32_t>(
						info, GeneratedElementOffset,
						static_cast<std::uint32_t>(pull->element)
					);
				}

				Orig(result, info);
				if(pull->kind == PullKind::Rare) {
					const auto resultBladeId = ReadField<std::uint16_t>(result, SDataBladeIdOffset);
					if(resultBladeId != pull->bladeId) {
						g_Logger->LogError(
							"Blade Pulls: native rare result mismatch (wanted {}, got {})",
							pull->bladeId, resultBladeId
						);
						overridePendingCommit = false;
						return;
					}
				}
				if(pull->kind == PullKind::Common) {
					WriteField<std::uint8_t>(result, ResultWeaponTypeOffset, pull->weaponType);
					WriteField<std::uint8_t>(result, ResultElementOffset, pull->element);
				}
				overridePendingCommit = true;
			}
		};

		struct AddRareBladeHook : skylaunch::hook::Trampoline<AddRareBladeHook> {
			static std::uint32_t Hook(void* result) {
				const auto bladeId = Orig(result);
				if(bladeId != 0)
					CommitPull(PullKind::Rare);
				return bladeId;
			}
		};

		struct AddCommonBladeHook : skylaunch::hook::Trampoline<AddCommonBladeHook> {
			static std::uint32_t Hook(void* result, std::uint32_t driverId) {
				const auto bladeId = Orig(result, driverId);
				if(bladeId != 0)
					CommitPull(PullKind::Common);
				return bladeId;
			}
		};

		void AddRarePull() {
			if(rareChoices.empty())
				return;
			const auto& choice = rareChoices[selectedRareChoice];
			pulls.push_back({PullKind::Rare, choice.bladeId, 1, 1, choice.name});
			ResetQueueCursor();
			SavePreset();
		}

		void AddCommonPull() {
			PullEntry pull;
			pull.kind = PullKind::Common;
			pull.element = static_cast<std::uint8_t>(selectedElement + 1);
			pull.weaponType = static_cast<std::uint8_t>(selectedWeapon + 1);
			pulls.push_back(std::move(pull));
			ResetQueueCursor();
			SavePreset();
		}

		void DrawPresetRow() {
			ImGui::SetNextItemWidth(210.f);
			if(ImGui::BeginCombo("##blade-pull-preset", currentPreset.c_str())) {
				for(const auto& file : presetFiles) {
					const bool selected = file == currentPreset;
					if(ImGui::Selectable(file.c_str(), selected)) {
						currentPreset = file;
						LoadPreset();
						ResetQueueCursor();
					}
					if(selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if(ImGui::Button("Reload")) {
				RescanPresets();
				LoadPreset();
				ResetQueueCursor();
			}
			ImGui::SameLine();
			if(ImGui::Button("+ New"))
				CreatePreset();
		}

		void DrawRareEditor() {
			const char* preview = rareChoices.empty()
				? "No rare Blades available"
				: rareChoices[selectedRareChoice].name.c_str();
			ImGui::SetNextItemWidth(290.f);
			if(ImGui::BeginCombo("##rare-blade", preview)) {
				for(int index = 0; index < static_cast<int>(rareChoices.size()); index++) {
					const bool selected = index == selectedRareChoice;
					if(ImGui::Selectable(rareChoices[index].name.c_str(), selected))
						selectedRareChoice = index;
					if(selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if(ImGui::Button("Add"))
				AddRarePull();
		}

		void DrawCommonEditor() {
			ImGui::TextUnformatted("Element");
			ImGui::SameLine(78.f);
			ImGui::SetNextItemWidth(125.f);
			if(ImGui::BeginCombo("##common-element", ElementNames[selectedElement])) {
				for(int index = 0; index < static_cast<int>(ElementNames.size()); index++) {
					const bool selected = index == selectedElement;
					if(ImGui::Selectable(ElementNames[index], selected))
						selectedElement = index;
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::TextUnformatted("Weapon");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(150.f);
			if(ImGui::BeginCombo("##common-weapon", WeaponNames[selectedWeapon])) {
				for(int index = 0; index < static_cast<int>(WeaponNames.size()); index++) {
					const bool selected = index == selectedWeapon;
					if(ImGui::Selectable(WeaponNames[index], selected))
						selectedWeapon = index;
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if(ImGui::Button("Add"))
				AddCommonPull();
		}

		void DrawPullList() {
			ImGui::SeparatorText("Pull order");
			if(ImGui::BeginChild("##blade-pull-list", ImVec2(0.f, 180.f), true)) {
				for(int index = 0; index < static_cast<int>(pulls.size()); index++) {
					const auto label = fmt::format("{}: {}", index + 1, PullLabel(pulls[index]));
					if(ImGui::Selectable(label.c_str(), selectedPull == index))
						selectedPull = index;
				}
			}
			ImGui::EndChild();

			const bool valid = selectedPull >= 0 && selectedPull < static_cast<int>(pulls.size());
			ImGui::BeginDisabled(!valid);
			if(ImGui::SmallButton("Up") && selectedPull > 0) {
				std::swap(pulls[selectedPull], pulls[selectedPull - 1]);
				selectedPull--;
				ResetQueueCursor();
				SavePreset();
			}
			ImGui::SameLine();
			if(ImGui::SmallButton("Down") && selectedPull + 1 < static_cast<int>(pulls.size())) {
				std::swap(pulls[selectedPull], pulls[selectedPull + 1]);
				selectedPull++;
				ResetQueueCursor();
				SavePreset();
			}
			ImGui::SameLine();
			if(ImGui::SmallButton("Duplicate") && valid) {
				pulls.insert(pulls.begin() + selectedPull + 1, pulls[selectedPull]);
				selectedPull++;
				ResetQueueCursor();
				SavePreset();
			}
			ImGui::SameLine();
			if(ImGui::SmallButton("Delete") && valid) {
				pulls.erase(pulls.begin() + selectedPull);
				selectedPull = std::min(selectedPull, static_cast<int>(pulls.size()) - 1);
				ResetQueueCursor();
				SavePreset();
			}
			ImGui::EndDisabled();
		}
	}
#endif

	void BladePulls::TopBarButton() {
#if XENOMODS_CODENAME(bf2)
		if(ImGui::MenuItem("Blade Pulls"))
			ShowWindow = true;
#endif
	}

	void BladePulls::MenuWindow() {
#if XENOMODS_CODENAME(bf2)
		if(!ShowWindow)
			return;
		ImGui::SetNextWindowSize(ImVec2(445.f, 390.f), ImGuiCond_FirstUseEver);
		if(!ImGui::Begin("Blade Pulls", &ShowWindow)) {
			ImGui::End();
			return;
		}
		if(rareChoices.empty())
			RefreshRareChoices();

		DrawPresetRow();
		const bool previousEnabled = enabled;
		if(ImGui::Checkbox("Enable", &enabled)) {
			if(enabled && !previousEnabled)
				ResetQueueCursor();
			SavePreset();
		}
		if(enabled && !pulls.empty()) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "Ready");
		}

		if(ImGui::BeginTabBar("##blade-pull-kind")) {
			if(ImGui::BeginTabItem("Rare")) {
				DrawRareEditor();
				ImGui::EndTabItem();
			}
			if(ImGui::BeginTabItem("Common")) {
				DrawCommonEditor();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		DrawPullList();
		ImGui::End();
#endif
	}

	void BladePulls::Initialize() {
		UpdatableModule::Initialize();
#if XENOMODS_CODENAME(bf2)
		RescanPresets();
		if(presetFiles.empty())
			CreatePreset();
		else {
			currentPreset = presetFiles.front();
			LoadPreset();
		}
		ResetQueueCursor();

		SetupSDataBladeHook::HookAt(
			"_ZN2gf13GfBladeCreate15setupSDataBladeERNS_10SDataBladeERKNS_17GfBladeCreateInfoE"
		);
		AddRareBladeHook::HookAt(
			"_ZN2gf10GfGameUtil16execAddRareBladeERKNS_10SDataBladeE"
		);
		AddCommonBladeHook::HookAt(
			"_ZN2gf10GfGameUtil18execAddCommonBladeERNS_10SDataBladeEj"
		);
		hooksInstalled = true;

		if(auto misc = g_Menu->FindSection("misc"); misc != nullptr)
			misc->RegisterRenderCallback(&TopBarButton);
		g_Menu->RegisterRenderCallback(&MenuWindow, true);
		g_Logger->LogInfo("Blade pull controls installed");
#endif
	}

	void BladePulls::Update(fw::UpdateInfo*) {
#if XENOMODS_CODENAME(bf2)
		(void)hooksInstalled;
#endif
	}

	void BladePulls::OnSceneTransition() {
#if XENOMODS_CODENAME(bf2)
		overridePendingCommit = false;
#endif
	}

#if XENOMODS_CODENAME(bf2)
	XENOMODS_REGISTER_MODULE(BladePulls);
#endif

} // namespace xenomods
