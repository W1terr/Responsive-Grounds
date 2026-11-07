// SKSE Menu Framework 3 API (https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API, LGPL-2.1).
// It pulls in windows.h, so only this file includes it.
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#pragma warning(push, 0)
#include "SKSEMenuFramework.h"
#pragma warning(pop)
#undef GetObject
#undef PlaySound

#include "Menu.h"

#include "Lang.h"
#include "Particles.h"
#include "Seasons.h"
#include "Settings.h"
#include "Terrain.h"

namespace Menu
{
	namespace
	{
		using namespace ImGuiMCP;
		using Lang::F;
		using Lang::T;
		using Settings::Kind;
		using Settings::kKinds;

		constexpr auto kSection = "Responsive Grounds";

		const std::array<ImVec4, kKinds> kColors{ ImVec4{ 0.95f, 0.65f, 0.25f, 1.0f }, ImVec4{ 0.55f, 0.82f, 0.35f, 1.0f },
			ImVec4{ 0.8f, 0.62f, 0.42f, 1.0f }, ImVec4{ 0.7f, 0.72f, 0.74f, 1.0f }, ImVec4{ 0.78f, 0.88f, 1.0f, 1.0f } };
		const ImVec4 kDim{ 0.6f, 0.6f, 0.6f, 1.0f };
		const ImVec4 kWarn{ 1.0f, 0.45f, 0.35f, 1.0f };

		constexpr std::array<const char*, kKinds> kAmountHint{
			"How many leaves your steps kick up. Walking always kicks up a few, running and sprinting many more.",
			"How many green leaves your steps kick up (the pine forests by default). Walking always kicks up a few, running and sprinting many more.",
			"How much dirt and mud your steps kick up: clods that bounce once and dust puffs.",
			"How much your steps kick up on rocky ground, rocks and cliffs: pebbles that bounce once and rock dust.",
			"How many snow flakes your steps kick up on snow. They flutter down slower than leaves and melt soon after landing.",
		};
		constexpr std::array<const char*, kKinds> kMostHint{
			"Leaves in the air and lying on the ground together. When all are in use, the ones that landed longest ago are reused. Lower it if you need the performance.",
			"Green leaves in the air and lying on the ground together. When all are in use, the ones that landed longest ago are reused. Lower it if you need the performance.",
			"Clods and dust puffs together (a quarter of them puffs). When all are in use, the oldest are reused. Lower it if you need the performance.",
			"Pebbles and rock dust together (a quarter of them dust). When all are in use, the oldest are reused. Lower it if you need the performance.",
			"Snow flakes in the air and on the ground together. When all are in use, the oldest are reused. Lower it if you need the performance.",
		};
		constexpr std::array<const char*, kKinds> kOnLabel{ "Leaves on", "Green leaves on", "Dust on", "Rocks on", "Snow on" };
		constexpr std::array<const char*, kKinds> kAddLabel{ "+ Leaves", "+ Green leaves", "+ Dust", "+ Rocks", "+ Snow" };
		constexpr std::array<const char*, kKinds> kOffText{ "Leaves are switched off on the Settings page.", "Green leaves are switched off on the Settings page.",
			"Dust is switched off on the Settings page.", "Rocks are switched off on the Settings page.", "Snow is switched off on the Settings page." };
		constexpr std::array<const char*, kKinds> kRemoveTip{ "Remove it from Leaves", "Remove it from Green leaves", "Remove it from Dust", "Remove it from Rocks",
			"Remove it from Snow" };
		constexpr std::array<const char*, Seasons::kSeasons> kSeasonNames{ "no season", "Winter", "Spring", "Summer", "Autumn" };

		// translated label with a fixed ImGui id (it must not change with the language)
		std::string Id(const char* a_english, const char* a_suffix = "")
		{
			return std::format("{}###{}{}", T(a_english), a_english, a_suffix);
		}

		void Hint(const char* a_english)
		{
			SameLine();
			TextDisabled("(?)");
			if (IsItemHovered()) {
				SetTooltip("%s", T(a_english));
			}
		}

		void Heading(const char* a_english)
		{
			SeparatorText(T(a_english));
		}

		void Tip(const char* a_english)
		{
			if (IsItemHovered()) {
				SetTooltip("%s", T(a_english));
			}
		}

		// widgets that change a setting bump its revision
		bool Changed(bool a_changed)
		{
			if (a_changed) {
				Settings::Touch();
			}
			return a_changed;
		}

		// stores a texture's choice; one equal to its default is forgotten (the file stays short)
		void SetChoice(Settings::KindSettings& a_kind, Kind a_which, const Terrain::File& a_file, const Settings::TextureChoice& a_choice)
		{
			const bool                    standard = a_file.isDefault[a_which];
			const Settings::TextureChoice fallback{ standard, standard ? a_file.defaultStrength[a_which] : 1.0f };
			if (a_choice == fallback) {
				a_kind.textures.erase(a_file.name);
			} else {
				a_kind.textures[a_file.name] = a_choice;
			}
			Settings::Touch();
		}

		void StatusLines()
		{
			const auto st = Particles::GetStatus();
			if (!st.modelLoaded) {
				TextColored(kWarn, "%s", T("The models (meshes\\ResponsiveGrounds) didn't load: is the whole mod installed?"));
				return;
			}
			Text("%s", T("Under you:"));
			SameLine();
			if (!st.exterior) {
				TextColored(kDim, "%s", T("no landscape (interior)"));
			} else if (!st.object.empty()) {
				if (st.raised) {
					Text("%s", T("up on an object that isn't ground: nothing"));
				} else if (!st.objectTexture.empty()) {
					Text("%s", F("an object painted with {}", st.objectTexture).c_str());
				} else {
					Text("%s", F("an object, material {}", T(st.object.c_str())).c_str());
				}
				SameLine();
				TextDisabled("%s", F("(land below: {})", st.texture.empty() ? "-" : st.texture).c_str());
			} else if (st.texture.empty()) {
				TextColored(kDim, "%s", T("no landscape texture data here"));
			} else {
				Text("%s", st.texture.c_str());
				SameLine();
				TextDisabled("%s", F("({:.0f}% of the ground here)", st.textureShare * 100.0f).c_str());
			}
			for (std::size_t k = 0; k < kKinds; ++k) {
				TextColored(st.covers[k] > 0.0f ? kColors[k] : kDim, "%s: %.0f%%", T(Settings::kKindNames[k]), st.covers[k] * 100.0f);
				SameLine();
			}
			Text("%s", st.exterior && !st.grounded ? T("  (in the air or in water)") : "");
			Text("%s", F("Speed: {:.0f} units/s", st.speed).c_str());
			Text("%s", F("Flying: {} leaves, {} green leaves, {} dust, {} rocks, {} snow", st.flying[0], st.flying[1], st.flying[2], st.flying[3], st.flying[4]).c_str());
			Text("%s", F("On the ground: {} leaves, {} green leaves, {} dust, {} rocks, {} snow", st.landed[0], st.landed[1], st.landed[2], st.landed[3], st.landed[4]).c_str());
			Text("%s", F("NPCs and creatures kicking things up: {}", st.npcs).c_str());
			if (Seasons::Found()) {
				Text("%s", F("Season (Seasons of Skyrim): {}", T(kSeasonNames[Seasons::Current()])).c_str());
				SameLine();
				if (Seasons::Applied()) {
					TextDisabled("%s", F("({} landscape textures repainted here)", Seasons::Swapped()).c_str());
				} else {
					TextDisabled("%s", T("(the land as painted here)"));
				}
			}
		}

		void KindSettings(std::size_t a_kind, Settings::KindSettings& a_settings)
		{
			const auto name = Settings::kKindNames[a_kind];
			PushID(name);
			PushStyleColor(ImGuiCol_Text, kColors[a_kind]);
			Heading(name);
			PopStyleColor();
			Changed(Checkbox(Id(kOnLabel[a_kind]).c_str(), &a_settings.enabled));
			if (!a_settings.enabled) {
				BeginDisabled();
			}
			Changed(SliderFloat(Id("Amount").c_str(), &a_settings.amount, 0.1f, 3.0f, "%.2fx"));
			Hint(kAmountHint[a_kind]);
			Changed(SliderInt(Id("Most at once").c_str(), &a_settings.maxParticles, 20, 400));
			Hint(kMostHint[a_kind]);
			Changed(SliderFloat(Id("Size").c_str(), &a_settings.size, 0.5f, 2.0f, "%.2fx"));
			if (!a_settings.enabled) {
				EndDisabled();
			}
			PopID();
		}

		void __stdcall RenderSettings()
		{
			std::scoped_lock guard(Settings::Lock());
			auto&            s = Settings::Get();
			const auto       seconds = std::format("%.1f {}", T("s"));

			Changed(Checkbox(Id("Enable the effect").c_str(), &s.enabled));
			Hint("Leaves, dust and rocks fly up from the ground when you, NPCs and creatures walk, run or ride over it.");
			if (!s.enabled) {
				BeginDisabled();
			}

			for (std::size_t k = 0; k < kKinds; ++k) {
				KindSettings(k, s.kinds[k]);
				if (k == Kind::kGreen) {
					Changed(SliderFloat(Id("Time on the ground").c_str(), &s.groundTime, 0.0f, 15.0f, seconds.c_str()));
					Hint("How long a landed leaf stays before it fades into the ground. Walking through it again kicks it back up.");
				}
			}
			Changed(Checkbox(Id("Dust clouds").c_str(), &s.puffs));
			Hint("Soft puffs of dust and rock dust with the clods and pebbles. Off: only clods and pebbles.");

			Heading("Everything");
			Changed(SliderFloat(Id("Start after").c_str(), &s.startDelay, 0.0f, 3.0f, seconds.c_str()));
			Hint("How long you move before your steps start kicking things up (short stops don't start it over). Landing from a jump and weapon swings don't wait.");
			Changed(SliderFloat(Id("How high it flies").c_str(), &s.lift, 0.0f, 2.5f, "%.2fx"));
			Changed(SliderFloat(Id("Swirl").c_str(), &s.swirl, 0.0f, 2.5f, "%.2fx"));
			Hint("How strongly the air behind you lifts things, circles them and throws them outward.");
			Changed(Checkbox(Id("Weapon swings stir things up").c_str(), &s.swing));
			Hint("The air of a fast swing throws up leaves from the ground under the blade (more the lower it passes), a little dust and rock dust, and pushes what is already flying. Works with vanilla, MCO and SCAR attacks and with Precision: it follows your weapon's real motion.");
			if (!s.swing) {
				BeginDisabled();
			}
			Changed(SliderFloat(Id("Swing strength").c_str(), &s.swingStrength, 0.1f, 3.0f, "%.2fx"));
			if (!s.swing) {
				EndDisabled();
			}
			Changed(Checkbox(Id("Your horse kicks things up too").c_str(), &s.onHorse));
			Changed(Checkbox(Id("Sneaking kicks up less").c_str(), &s.sneakQuieter));

			Heading("Sounds");
			Changed(Checkbox(Id("Sounds on").c_str(), &s.sounds));
			Hint("A soft rustle of leaves flying up and sailing past, and little stones ticking as pebbles land. Dust and snow are quiet. Uses the game's sound effects volume.");
			if (!s.sounds) {
				BeginDisabled();
			}
			Changed(SliderFloat(Id("Leaves volume").c_str(), &s.leafVolume, 0.0f, 1.0f, "%.2f"));
			Changed(SliderFloat(Id("Pebbles volume").c_str(), &s.rockVolume, 0.0f, 1.0f, "%.2f"));
			if (!s.sounds) {
				EndDisabled();
			}

			Heading("NPCs and creatures");
			Changed(Checkbox(Id("NPCs and creatures kick things up too").c_str(), &s.npcs));
			Hint("Their steps, landings and wakes work like yours (big ones like giants and mammoths kick up more, over a wider area).");
			if (!s.npcs) {
				BeginDisabled();
			}
			Changed(SliderInt(Id("Most at once", "npcs").c_str(), &s.npcMax, 1, 20));
			Hint("Only the nearest ones count. Fewer is lighter on performance.");
			const auto units = std::format("%.0f {}", T("units"));
			Changed(SliderFloat(Id("Range").c_str(), &s.npcRange, 512.0f, 6144.0f, units.c_str()));
			Changed(Checkbox(Id("Creatures too").c_str(), &s.npcCreatures));
			Hint("Animals and monsters, not only people.");
			Changed(Checkbox(Id("Their attacks stir things up").c_str(), &s.npcAttacks));
			Hint("Their weapon swings work like yours (needs Weapon swings on).");
			if (!s.npcs) {
				EndDisabled();
			}

			Heading("Shouts and heavy blows");
			Changed(Checkbox(Id("Shouts blow it away").c_str(), &s.shouts));
			Hint("A shout (yours, an NPC's, a dragon's on the ground) sends a blast along the ground in front of the shouter: everything there flies away from it.");
			if (!s.shouts) {
				BeginDisabled();
			}
			Changed(SliderFloat(Id("Shout strength").c_str(), &s.shoutStrength, 0.2f, 3.0f, "%.2fx"));
			if (!s.shouts) {
				EndDisabled();
			}
			Changed(Checkbox(Id("Heavy blows on the ground").c_str(), &s.slams));
			Hint("A weapon hitting the ground hard (a giant's club, a hammer power attack) throws up a ring of whatever lies around the impact.");
			if (!s.slams) {
				BeginDisabled();
			}
			Changed(SliderFloat(Id("Blow strength").c_str(), &s.slamStrength, 0.2f, 3.0f, "%.2fx"));
			if (!s.slams) {
				EndDisabled();
			}
			Changed(Checkbox(Id("Explosions blow it away").c_str(), &s.explosions));
			Hint("A fireball or any other explosion near the ground (runes, oil, traps) throws up a ring of whatever lies around it and blows away what is in the air.");
			if (!s.explosions) {
				BeginDisabled();
			}
			Changed(SliderFloat(Id("Explosion strength").c_str(), &s.explosionStrength, 0.2f, 3.0f, "%.2fx"));
			if (!s.explosions) {
				EndDisabled();
			}
			Changed(Checkbox(Id("Falling bodies").c_str(), &s.bodies));
			Hint("A body dropping to the ground (someone just killed) throws up a ring of whatever lies there, as big as the body.");
			if (!s.bodies) {
				BeginDisabled();
			}
			Changed(SliderFloat(Id("Body strength").c_str(), &s.bodyStrength, 0.2f, 3.0f, "%.2fx"));
			if (!s.bodies) {
				EndDisabled();
			}

			Heading("Wind");
			Changed(Checkbox(Id("Wind lifts leaves").c_str(), &s.wind));
			Hint("Strong wind (the weather's, or what Dynamic Wind makes) picks up leaves from leafy ground around you and carries them, lifts a little dust, and blows everything in the air harder. Gusts come and go.");
			if (!s.wind) {
				BeginDisabled();
			}
			Changed(SliderFloat(Id("Wind amount").c_str(), &s.windAmount, 0.1f, 3.0f, "%.2fx"));
			Changed(Checkbox(Id("Flip wind direction").c_str(), &s.windFlip));
			Hint("If the leaves fly against the way the trees and grass bend.");
			if (!s.wind) {
				EndDisabled();
			}

			Heading("Seasons of Skyrim");
			if (Seasons::Found()) {
				Changed(Checkbox(Id("Seasons of Skyrim support").c_str(), &s.seasons));
				Hint("The land counts as the season paints it: in winter the snowy land kicks up snow instead of leaves and dust, and a mod's autumn or spring textures what they give. Off: the textures the land is painted with all year.");
			} else {
				TextColored(kDim, "%s", T("Seasons of Skyrim isn't installed: the land counts as it is painted."));
			}

			if (!s.enabled) {
				EndDisabled();
			}

			Heading("Status");
			StatusLines();
			if (Button(Id("Test: throw some of everything around me").c_str())) {
				Particles::RequestTest();
			}
			Hint("Throws a ring of each around you on any ground once you close the menu, to see how they look.");

			Heading("Other");
			Changed(Checkbox(Id("Debug log").c_str(), &s.debugLog));
			Hint("Writes the ground under you, your speed and the counts to ResponsiveGrounds.log every 2 seconds, and each shout, heavy blow and falling body.");
			if (Button(Id("Reset to defaults").c_str())) {
				const auto language = s.language;
				Settings::ResetDefaults();
				s.language = language;  // the language stays
			}
			Hint("Every setting, including the texture choices.");
		}

		// stores a material's choice; one equal to its default is forgotten
		void SetMaterialChoice(Settings::KindSettings& a_kind, Kind a_which, const Terrain::Material& a_material, const Settings::TextureChoice& a_choice)
		{
			const float                   standard = a_material.defaultStrength[a_which];
			const Settings::TextureChoice fallback{ standard > 0.0f, standard > 0.0f ? standard : 1.0f };
			if (a_choice == fallback) {
				a_kind.materials.erase(a_material.name);
			} else {
				a_kind.materials[a_material.name] = a_choice;
			}
			Settings::Touch();
		}

		void ObjectMaterials(Kind a_which, Settings::KindSettings& a_kind)
		{
			Changed(Checkbox(Id("On objects too").c_str(), &a_kind.objects));
			Hint(a_which == Kind::kLeaves ?
					 "Standing on an object instead of the landscape: it kicks up leaves by its surface material (the one footstep sounds use), where the land around it has leaves (Riften's streets, rocks in the autumn forest)." :
				 a_which == Kind::kGreen ?
					 "Standing on an object instead of the landscape: it kicks up green leaves by its surface material (the one footstep sounds use), where the land around it has green leaves (rocks and paths in the pine forests)." :
					 "Standing on an object instead of the landscape: it kicks this up by its surface material (the one footstep sounds use), like rock cliffs or dirt paths.");
			if (!a_kind.objects) {
				BeginDisabled();
			}
			if (BeginTable("materials", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
				TableSetupColumn(T("Material"), 0, 3.0f);
				TableSetupColumn(T("Amount"), 0, 2.0f);
				TableHeadersRow();
				for (const auto& m : Terrain::Materials()) {
					auto choice = Terrain::MaterialChoice(a_kind, a_which, m);
					PushID(m.name);
					TableNextRow();
					TableNextColumn();
					if (Checkbox(Id(m.name).c_str(), &choice.enabled)) {
						SetMaterialChoice(a_kind, a_which, m, choice);
					}
					TableNextColumn();
					if (choice.enabled) {
						float percent = choice.strength * 100.0f;
						SetNextItemWidth(-1.0f);
						if (SliderFloat("##strength", &percent, 5.0f, 100.0f, "%.0f%%")) {
							choice.strength = std::clamp(percent / 100.0f, 0.05f, 1.0f);
							SetMaterialChoice(a_kind, a_which, m, choice);
						}
					} else {
						TextDisabled("-");
					}
					PopID();
				}
				EndTable();
			}
			if (Button(Id("Default materials").c_str())) {
				a_kind.materials.clear();
				Settings::Touch();
			}
			if (!a_kind.objects) {
				EndDisabled();
			}
		}

		// a Northern Roads road texture (by file name)
		bool IsRoad(const std::string& a_name)
		{
			const auto& files = Terrain::Files();
			const auto  it = std::ranges::find(files, a_name, &Terrain::File::name);
			return it != files.end() && it->road;
		}

		bool Matches(const std::string& a_name, const char* a_filter)
		{
			if (!a_filter[0]) {
				return true;
			}
			std::string needle(a_filter);
			std::ranges::transform(needle, needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_name.contains(needle);
		}

		// adds a texture to a category (a default gets its default amount back, another one the full amount)
		void AddTexture(Settings::KindSettings& a_kind, Kind a_which, const Terrain::File& a_file)
		{
			Settings::TextureChoice choice{ true, a_file.isDefault[a_which] ? a_file.defaultStrength[a_which] : 1.0f };
			SetChoice(a_kind, a_which, a_file, choice);
		}

		// the category's textures: amount, records, remove
		void TextureList(Settings::KindSettings& a_kind, Kind a_which)
		{
			int  shown = 0;
			bool table = BeginTable("textures", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp);
			if (table) {
				TableSetupColumn(T("Texture"), 0, 3.0f);
				TableSetupColumn(T("Amount"), 0, 2.0f);
				TableSetupColumn(T("Records"), 0, 0.6f);
				TableSetupColumn("", 0, 0.3f);
				TableHeadersRow();
			}
			for (const auto& f : Terrain::Files()) {
				auto choice = Terrain::Choice(a_kind, a_which, f);
				if (!choice.enabled || !table || f.road) {
					continue;
				}
				++shown;
				PushID(f.name.c_str());
				TableNextRow();
				TableNextColumn();
				Text("%s", f.name.c_str());
				if (f.isDefault[a_which]) {
					SameLine();
					TextDisabled("%s", T("(default)"));
				}
				TableNextColumn();
				float percent = choice.strength * 100.0f;
				SetNextItemWidth(-1.0f);
				if (SliderFloat("##strength", &percent, 5.0f, 100.0f, "%.0f%%")) {
					choice.strength = std::clamp(percent / 100.0f, 0.05f, 1.0f);
					SetChoice(a_kind, a_which, f, choice);
				}
				TableNextColumn();
				Text("%d", f.records);
				TableNextColumn();
				if (SmallButton("x")) {
					choice.enabled = false;
					SetChoice(a_kind, a_which, f, choice);
				}
				Tip(kRemoveTip[a_which]);
				PopID();
			}
			if (table) {
				EndTable();
			}
			if (shown == 0) {
				TextColored(kDim, "%s", T("No textures: add some with the + button."));
			}
		}

		// the + button: every landscape texture not in the category yet, searchable
		void AddButton(Settings::KindSettings& a_kind, Kind a_which)
		{
			static std::array<char[64], kKinds> addFilter{};
			auto&                               search = addFilter[a_which];
			if (Button(Id("+ Add texture").c_str())) {
				search[0] = 0;
				OpenPopup("add");
			}
			Tip("Pick a landscape texture that kicks this up too");
			if (BeginPopup("add")) {
				SetNextItemWidth(320.0f);
				if (IsWindowAppearing()) {
					SetKeyboardFocusHere();
				}
				InputTextWithHint("##search", T("Search textures"), search, sizeof(search));
				if (BeginChild("list", ImVec2(420.0f, 320.0f))) {
					for (const auto& f : Terrain::Files()) {
						if (f.road || Terrain::Choice(a_kind, a_which, f).enabled || !Matches(f.name, search)) {
							continue;
						}
						const auto label = std::format("{}  ({}){}###{}", f.name, F(f.records == 1 ? "{} record" : "{} records", f.records),
							f.isDefault[a_which] ? std::format("  {}", T("default")) : "", f.name);
						if (Selectable(label.c_str())) {
							AddTexture(a_kind, a_which, f);
							CloseCurrentPopup();
						}
					}
				}
				EndChild();
				EndPopup();
			}
		}

		void Category(Kind a_which, Settings::KindSettings& a_kind)
		{
			PushID(static_cast<int>(a_which));
			PushStyleColor(ImGuiCol_Text, kColors[a_which]);
			const bool open = CollapsingHeader(Id(Settings::kKindNames[a_which]).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
			PopStyleColor();
			if (open) {
				if (!a_kind.enabled) {
					TextColored(kDim, "%s", T(kOffText[a_which]));
				}
				TextureList(a_kind, a_which);
				AddButton(a_kind, a_which);
				SameLine();
				if (Button(Id("Defaults").c_str())) {
					std::erase_if(a_kind.textures, [](const auto& a_entry) { return !IsRoad(a_entry.first); });
					Settings::Touch();
				}
				Tip("Back to the default textures at their default amounts (Northern Roads' roads keep theirs)");
				if (TreeNode(Id("Objects (rocks, cliffs, streets, walkways)").c_str())) {
					ObjectMaterials(a_which, a_kind);
					TreePop();
				}
				Spacing();
			}
			PopID();
		}

		void __stdcall RenderTextures()
		{
			const auto st = Particles::GetStatus();

			std::scoped_lock guard(Settings::Lock());
			auto&            s = Settings::Get();

			TextWrapped("%s", T("Which landscape textures kick up leaves, dust and rocks. The amount says how much of the full effect a texture gives (sparse leaves on grass or dirt: less). Objects (rocks, cliffs, streets) go by their surface material, in each category's Objects list."));
			if (Terrain::NorthernRoadsFound()) {
				TextColored(kDim, "%s", T("Northern Roads' road textures are on their own page."));
			}

			// the texture you stand on, to add it right away
			if (!st.texture.empty()) {
				const auto& files = Terrain::Files();
				const auto  it = std::ranges::find(files, st.texture, &Terrain::File::name);
				if (it != files.end() && it->road) {
					Text("%s", F("Under you: {} (a Northern Roads road: see its page)", it->name).c_str());
				} else if (it != files.end()) {
					Text("%s", F("Under you: {}", it->name).c_str());
					for (std::size_t k = 0; k < kKinds; ++k) {
						const auto which = static_cast<Kind>(k);
						SameLine();
						PushID(static_cast<int>(k));
						if (Terrain::Choice(s.kinds[k], which, *it).enabled) {
							TextColored(kColors[k], "[%s]", T(Settings::kKindNames[k]));
						} else if (SmallButton(Id(kAddLabel[k]).c_str())) {
							AddTexture(s.kinds[k], which, *it);
						}
						PopID();
					}
				}
			}
			Separator();

			for (std::size_t k = 0; k < kKinds; ++k) {
				Category(static_cast<Kind>(k), s.kinds[k]);
			}
		}

		// ---- Northern Roads

		// one road texture's amount of a kind: 0 takes it out
		void SetRoadAmount(Settings::KindSettings& a_kind, Kind a_which, const Terrain::File& a_file, float a_percent)
		{
			if (a_percent < 1.0f) {
				SetChoice(a_kind, a_which, a_file, { false, a_file.isDefault[a_which] ? a_file.defaultStrength[a_which] : 1.0f });
			} else {
				SetChoice(a_kind, a_which, a_file, { true, std::clamp(a_percent / 100.0f, 0.01f, 1.0f) });
			}
		}

		void __stdcall RenderRoads()
		{
			const auto st = Particles::GetStatus();

			std::scoped_lock guard(Settings::Lock());
			auto&            s = Settings::Get();

			if (!Terrain::NorthernRoadsFound()) {
				TextColored(kWarn, "%s", T("Northern Roads isn't loaded (no Northern Roads.esp): nothing to set here."));
				return;
			}
			Changed(Checkbox(Id("Northern Roads support").c_str(), &s.northernRoads));
			Hint("Its roads kick things up by kind of road: dirt roads dust and a few pebbles, mud roads clods, stone roads a little grit. Off: its roads kick up nothing.");
			TextWrapped("%s", T("Leaves on a road only fly where the land beside it has leaves (forest roads in the Rift), as much as the leafiest land 256 units to the sides. 0% takes a kind off a road."));
			if (!s.northernRoads) {
				BeginDisabled();
			}

			const auto& files = Terrain::Files();
			const auto  under = std::ranges::find(files, st.texture, &Terrain::File::name);
			if (under != files.end() && under->road) {
				Text("%s", F("Under you: {} ({})", T(under->roadLabel), under->name).c_str());
			} else {
				TextColored(kDim, "%s", T("Under you: not a Northern Roads road"));
			}
			Separator();

			const std::string off = T("off");
			if (BeginTable("roads", static_cast<int>(kKinds) + 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
				TableSetupColumn(T("Road"), 0, 2.6f);
				for (std::size_t k = 0; k < kKinds; ++k) {
					TableSetupColumn(T(Settings::kKindNames[k]), 0, 1.4f);
				}
				TableSetupColumn(T("Records"), 0, 0.5f);
				TableHeadersRow();
				for (const auto& f : files) {
					if (!f.road) {
						continue;
					}
					PushID(f.name.c_str());
					TableNextRow();
					TableNextColumn();
					Text("%s", T(f.roadLabel));
					if (IsItemHovered()) {
						SetTooltip("%s", f.name.c_str());
					}
					SameLine();
					TextDisabled("%s", f.name.c_str());
					for (std::size_t k = 0; k < kKinds; ++k) {
						const auto which = static_cast<Kind>(k);
						const auto choice = Terrain::Choice(s.kinds[k], which, f);
						TableNextColumn();
						PushID(static_cast<int>(k));
						float percent = choice.enabled ? choice.strength * 100.0f : 0.0f;
						SetNextItemWidth(-1.0f);
						PushStyleColor(ImGuiCol_Text, percent > 0.0f ? kColors[k] : kDim);
						if (SliderFloat("##amount", &percent, 0.0f, 100.0f, percent > 0.0f ? "%.0f%%" : off.c_str())) {
							SetRoadAmount(s.kinds[k], which, f, percent);
						}
						PopStyleColor();
						PopID();
					}
					TableNextColumn();
					Text("%d", f.records);
					PopID();
				}
				EndTable();
			}
			if (Button(Id("Defaults").c_str())) {
				for (auto& kind : s.kinds) {
					std::erase_if(kind.textures, [](const auto& a_entry) { return IsRoad(a_entry.first); });
				}
				Settings::Touch();
			}
			Tip("Every road back to its default amounts");
			if (!s.northernRoads) {
				EndDisabled();
			}
		}

		// ---- the leaf mix (an installer "Mixed" choice)

		bool AnyMix() { return Settings::MixOf(Kind::kLeaves) != Settings::Mix::kNone || Settings::MixOf(Kind::kGreen) != Settings::Mix::kNone; }

		// one kind's mix: a slider per color or shape with the share of the leaves it gets
		template <std::size_t N>
		void MixSliders(Kind a_kind, const char* a_heading, std::array<float, N>& a_weights, const std::array<float, N>& a_defaults,
			const std::array<const char*, N>& a_names, const std::array<ImVec4, N>& a_colors)
		{
			PushID(a_heading);
			PushStyleColor(ImGuiCol_Text, kColors[a_kind]);
			Heading(a_heading);
			PopStyleColor();
			float total = 0.0f;
			for (const auto w : a_weights) {
				total += w;
			}
			for (std::size_t i = 0; i < N; ++i) {
				PushID(static_cast<int>(i));
				SetNextItemWidth(GetContentRegionAvail().x * 0.45f);
				PushStyleColor(ImGuiCol_Text, a_colors[i]);
				Changed(SliderFloat(Id(a_names[i]).c_str(), &a_weights[i], 0.0f, 100.0f, "%.0f"));
				PopStyleColor();
				SameLine();
				const float share = total > 0.0f ? a_weights[i] / total * 100.0f : 100.0f / N;
				TextDisabled("%s", F("{:.0f}% of them", share).c_str());
				PopID();
			}
			if (Button(Id("Defaults").c_str())) {
				a_weights = a_defaults;
				Settings::Touch();
			}
			PopID();
		}

		void __stdcall RenderMix()
		{
			std::scoped_lock guard(Settings::Lock());
			auto&            s = Settings::Get();
			TextWrapped("%s", T("You chose a mixed set of leaves in the installer: set how much of each the ground kicks up. Leaves that are already out change as they get kicked up again. All at 0: an even mix."));
			if (Settings::MixOf(Kind::kLeaves) == Settings::Mix::kColors) {
				static constexpr std::array<const char*, 4> kNames{ "Orange", "Yellow", "Red", "Brown" };
				static const std::array<ImVec4, 4>          kTint{ ImVec4{ 0.95f, 0.6f, 0.2f, 1.0f }, ImVec4{ 0.98f, 0.85f, 0.3f, 1.0f },
					ImVec4{ 0.9f, 0.35f, 0.25f, 1.0f }, ImVec4{ 0.7f, 0.55f, 0.4f, 1.0f } };
				MixSliders(Kind::kLeaves, "Riften leaves (Mixed)", s.kinds[Kind::kLeaves].colorMix, Settings::kDefaultColorMix, kNames, kTint);
			}
			if (Settings::MixOf(Kind::kGreen) == Settings::Mix::kShapes) {
				static constexpr std::array<const char*, 2> kNames{ "Broad leaves", "Pine needles" };
				static const std::array<ImVec4, 2>          kTint{ ImVec4{ 0.55f, 0.82f, 0.35f, 1.0f }, ImVec4{ 0.45f, 0.7f, 0.55f, 1.0f } };
				MixSliders(Kind::kGreen, "Green leaves (Mixed)", s.kinds[Kind::kGreen].shapeMix, Settings::kDefaultShapeMix, kNames, kTint);
			}
		}

		// ---- the language

		void __stdcall RenderLanguages();

		// the sidebar pages: English name (the translation key), page, name it has in the framework now
		struct Page
		{
			const char*                              english;
			SKSEMenuFramework::Model::RenderFunction render;
			std::string                              shown;
			bool (*wanted)(){ nullptr };  // only added when this says so
		};
		std::array pages{
			Page{ "Settings", RenderSettings, {} },
			Page{ "Textures", RenderTextures, {} },
			Page{ "Northern Roads", RenderRoads, {} },
			Page{ "Leaf mix", RenderMix, {}, AnyMix },
			Page{ "Language", RenderLanguages, {} },
		};

		// exported by SKSE Menu Framework 3.8+, not in its header: renames a page at runtime
		using RenameSectionFunction = bool (*)(const char* a_path, const char* a_newName);

		RenameSectionFunction RenameSection()
		{
			static const auto func = SKSEMenuFramework::Model::Internal::GetFunction<RenameSectionFunction>("RenameSection");
			return func;
		}

		void RenamePages()
		{
			if (!RenameSection()) {
				return;
			}
			for (auto& page : pages) {
				const std::string wanted = T(page.english);
				if (!page.shown.empty() && wanted != page.shown && RenameSection()(std::format("{}/{}", kSection, page.shown).c_str(), wanted.c_str())) {
					page.shown = wanted;
				}
			}
		}

		void __stdcall RenderLanguages()
		{
			std::scoped_lock guard(Settings::Lock());
			auto&            s = Settings::Get();

			Heading("Language");
			bool changed = false;
			if (RadioButton(Id("Same as the game").c_str(), s.language == "auto")) {
				s.language = "auto";
				changed = true;
			}
			for (const auto& language : Lang::All()) {
				if (RadioButton(language.name, s.language == language.id)) {
					s.language = language.id;
					changed = true;
				}
			}
			if (changed) {
				Settings::Touch();
				Lang::Set(s.language);
				RenamePages();
			}
			Spacing();
			if (!RenameSection()) {
				TextWrapped("%s", T("The names of the pages on the left change after you restart the game."));
			}
			// without the right glyphs the framework draws "?" for every letter, so this is also shown in English
			if (const auto glyphs = Lang::Current().glyphs) {
				constexpr auto kGlyphHint = "If you see ? instead of letters: in SKSE Menu Framework open Options > Open Settings and set Character Glyphs to {}.";
				Spacing();
				TextWrapped("%s", F(kGlyphHint, glyphs).c_str());
				TextDisabled("%s", std::vformat(kGlyphHint, std::make_format_args(glyphs)).c_str());
			}
		}

		void __stdcall OnFrameworkEvent(SKSEMenuFramework::Model::EventType a_type)
		{
			if (a_type == SKSEMenuFramework::Model::EventType::kCloseMenu) {
				std::scoped_lock guard(Settings::Lock());
				Settings::Save();
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled() || !GetMenuFrameworkModule()) {
			logs::info("SKSE Menu Framework is not installed: no settings menu (the settings file still works)");
			return;
		}
		SKSEMenuFramework::SetSection(kSection);
		for (auto& page : pages) {
			if (page.wanted && !page.wanted()) {
				continue;
			}
			page.shown = T(page.english);
			SKSEMenuFramework::AddSectionItem(page.shown, page.render);
		}
		SKSEMenuFramework::AddEvent(OnFrameworkEvent, 0.0f);
		logs::info("SKSE Menu Framework {} found, pages added", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
