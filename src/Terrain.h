#pragma once

#include "Settings.h"

// Which landscape textures kick up leaves, dirt and rocks, and how much of each the ground has at a position. The
// weights come from the land's own texture layers (the per vertex opacities the landscape shader blends), so they
// follow the texture actually painted there, whatever the weather, the season mod or the terrain mesh.
namespace Terrain
{
	using Kind = Settings::Kind;
	using Covers = std::array<float, Settings::kKinds>;

	struct File
	{
		std::string                          name;      // e.g. "fallforestleaves01.dds", lower case
		int                                  records{ 0 };  // landscape texture records using it
		std::array<bool, Settings::kKinds>   isDefault{};   // one of a kind's defaults
		std::array<float, Settings::kKinds>  defaultStrength{};
		bool                                 road{ false };  // a Northern Roads road texture (its own page, its own defaults)
		const char*                          roadLabel{ "" };  // what kind of road
	};

	struct Probe
	{
		bool        land{ false };  // there is loaded land here
		Covers      covers{};       // 0..1 per kind: texture shares x their strength
		std::string dominant;       // the texture with the most weight here (any texture)
		float       dominantShare{ 0.0f };
	};

	// Defaults (file names, case ignored):
	//   leaves: "fallforest" (Riften's autumn forest), not the roads; leaves 1, grass 0.6, dirt 0.5, rocks 0.35
	//   dust:   "dirt" or "mud" without "snow"; Riften's leafy dirt 0.3, mud 0.6, else 0.8
	//   rocks:  "rock" or "gravel" without "snow"; gravel 0.8, Riften's leafy rocks 0.4, mossy 0.5, tundra 0.6,
	//           ash 0.6, else 0.75
	float DefaultStrength(Kind a_kind, std::string_view a_fileName);  // 0: not a default

	// Northern Roads (Northern Roads.esp, its landscape textures under cotn\landscape and a few it borrows): each road
	// texture sorted into a kind of road with defaults of its own. No leaves on roads by default; turned on for a road,
	// only where the land around it has leaves (the most leaf cover of the land 256 units to the sides).
	//   dust / rocks: dirt road 0.8 / 0.25; its trail edges 0.7 / 0.2; dry dirt 0.9 / 0.2; mud road 0.6 / 0; Helgen mud
	//   0.6 / 0; stone road 0.2 / 0.25; volcanic road 0.5 / 0.4; Whiterun stone 0.15 / 0; Riften brick 0.1 / 0; Dwemer
	//   stone 0.1 / 0; winter roads 0.25 / 0; glacier snow nothing
	bool NorthernRoadsFound();

	// the choice that applies to a file: the menu's own one, else its default
	Settings::TextureChoice Choice(const Settings::KindSettings& a_kind, Kind a_which, const File& a_file);
	bool                    Active(const Settings::KindSettings& a_kind, Kind a_which, const File& a_file);

	// Objects (rocks, cliffs, streets, walkways): what you stand on is sorted into one of these. First its painted
	// on layer (a static's directional material: Solstheim's ash; snow covered ones give nothing), then natural
	// rock (stone collision on a model under landscape\), else its collision material (the one footstep sounds
	// use). Dust and rocks come from the entry itself; leaves need leafy land around too (the most
	// leaf cover of the landscape at your spot and 192 units around it).
	struct Material
	{
		const char*                         name;
		std::vector<RE::MATERIAL_ID>        ids;
		std::array<float, Settings::kKinds> defaultStrength;  // 0: off by default
	};

	const std::vector<Material>& Materials();
	int                          MaterialOf(RE::MATERIAL_ID a_id);       // index into Materials(), -1 when none
	int                          MaterialIndex(std::string_view a_name);  // by name, -1 when none

	Settings::TextureChoice MaterialChoice(const Settings::KindSettings& a_kind, Kind a_which, const Material& a_material);

	// what standing on an object sorted into that entry gives here
	Covers ObjectCover(int a_material, const RE::NiPoint3& a_position);

	void Init();   // after the data loaded: reads every landscape texture record
	void Apply();  // rebuilds the per texture strengths from the settings (main thread)

	const std::vector<File>& Files();  // every landscape texture file, by name

	Covers Cover(const RE::NiPoint3& a_position);  // fast, main thread
	Probe  At(const RE::NiPoint3& a_position);     // with the dominant texture, for the status line
}
