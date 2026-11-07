#pragma once

namespace Settings
{
	// what the ground kicks up
	enum Kind : std::size_t
	{
		kLeaves,
		kGreen,  // green leaves (summer forests): the same leaves, green
		kDirt,   // shown as "Dust": dirt and mud, clods and dust puffs
		kRock,   // pebbles and rock dust
		kSnow,   // snow flakes
		kKinds
	};
	inline constexpr std::array<const char*, kKinds> kKindNames{ "Leaves", "Green leaves", "Dust", "Rocks", "Snow" };

	// leaves and green leaves fly, land and get kicked up again the same way (and leave trodden patches)
	constexpr bool IsLeaves(Kind a_kind) { return a_kind == kLeaves || a_kind == kGreen; }
	// what flutters down one by one: the leaves and the snow flakes (the rest flies in arcs, or drifts as dust)
	constexpr bool Flutters(Kind a_kind) { return IsLeaves(a_kind) || a_kind == kSnow; }

	// a landscape texture's own choice in the menu (textures without one use their default, see Terrain)
	struct TextureChoice
	{
		bool  enabled{ true };
		float strength{ 1.0f };  // share of the full amount it kicks up (0..1)

		bool operator==(const TextureChoice&) const = default;
	};

	// The leaf atlases: four colors (columns) of two shapes (rows). An installer's "Mixed" choice drops a file in
	// Data\SKSE\Plugins\ResponsiveGrounds\Mix (Leaves.json: {"mix": "colors"}, GreenLeaves.json: {"mix": "shapes"}) and
	// then that kind's mix can be set in the menu: Riften's orange, yellow, red and brown leaves, or green broad leaves
	// and pine needles. Without it the colors keep their default shares and the shapes come half and half.
	enum class Mix
	{
		kNone,
		kColors,  // the four columns
		kShapes,  // the two rows
	};
	Mix MixOf(Kind a_kind);  // what the installed option of a leaf kind lets you mix
	inline constexpr std::array<float, 4> kDefaultColorMix{ 32.0f, 30.0f, 20.0f, 18.0f };
	inline constexpr std::array<float, 2> kDefaultShapeMix{ 50.0f, 50.0f };

	struct KindSettings
	{
		bool  enabled{ true };
		float amount{ 1.0f };  // amount multiplier
		int   maxParticles{ 200 };
		float size{ 1.0f };

		// by file name, lower case ("fallforestleaves01.dds"): a texture added with its amount, a default's own
		// amount, or a default taken out (enabled false); textures without an entry: their default
		std::map<std::string, TextureChoice> textures;

		bool                                 objects{ true };  // standing on objects (rocks, cliffs, streets) counts too, by their surface material
		std::map<std::string, TextureChoice> materials;        // by material name ("Stone"), like the textures

		// leaves only: how much of each color / shape new leaves get (relative weights 0..100, all 0: even)
		std::array<float, 4> colorMix{ kDefaultColorMix };
		std::array<float, 2> shapeMix{ kDefaultShapeMix };

		bool operator==(const KindSettings&) const = default;
	};

	inline const std::array<KindSettings, kKinds> kDefaultKinds{ {
		{ true, 2.2f, 200, 1.05f, {}, true, {} },  // leaves
		{ true, 1.8f, 150, 1.0f, {}, true, {} },   // green leaves
		{ true, 0.8f, 120, 0.95f, {}, true, {} },  // dust: dirt and mud (three quarters clods, a quarter dust puffs)
		{ true, 1.0f, 120, 1.0f, {}, true, {} },  // rocks (three quarters pebbles, a quarter rock dust)
		{ true, 1.6f, 250, 1.25f, {}, true, {} },  // snow flakes
	} };

	struct Values
	{
		bool                                enabled{ true };
		std::array<KindSettings, kKinds>    kinds{ kDefaultKinds };
		float                               lift{ 1.0f };        // how high everything flies
		float                               swirl{ 1.0f };       // how much the air behind you swirls and drags it along
		float                               groundTime{ 3.0f };  // seconds a landed leaf stays before it fades into the ground
		float                               startDelay{ 0.5f };  // seconds of moving before your steps start kicking things up
		bool                                puffs{ true };       // dust clouds with the dirt and rocks
		bool                                swing{ true };       // weapon swings stir up leaves and a little dust
		float                               swingStrength{ 1.0f };
		bool                                onHorse{ true };     // your horse's hooves kick things up too
		bool                                sneakQuieter{ true };  // sneaking kicks up less
		bool                                sounds{ true };        // a soft rustle of flying leaves, little stones ticking (no sound for dust and snow)
		float                               leafVolume{ 0.6f };
		float                               rockVolume{ 0.6f };

		bool  npcs{ true };          // NPCs and creatures near you kick things up too
		int   npcMax{ 6 };           // the nearest this many
		float npcRange{ 2048.0f };   // within this distance
		bool  npcCreatures{ true };  // animals and monsters too, not only people
		bool  npcAttacks{ true };    // their weapon swings stir things up like yours
		bool  shouts{ true };        // shouts (yours, NPCs', dragons') blow a cone of it away
		float shoutStrength{ 1.0f };
		bool  slams{ true };         // heavy blows that hit the ground (giant clubs, hammers) throw up a ring
		float slamStrength{ 1.0f };
		bool  explosions{ true };    // explosions near the ground (fireballs, runes, traps) blow it away in a ring
		float explosionStrength{ 1.0f };
		bool  bodies{ true };        // a body dropping to the ground (killed) throws up a ring
		float bodyStrength{ 1.0f };
		bool  wind{ true };          // strong wind (the weather's, Dynamic Wind's) lifts leaves and a little dust
		float windAmount{ 1.0f };
		bool  windFlip{ false };     // the other way round (if leaves fly against the trees' sway)

		bool northernRoads{ true };  // Northern Roads' road textures kick things up (each road kind with its own amounts)
		bool seasons{ true };        // with Seasons of Skyrim: the land counts as the season paints it (snow in winter)

		bool        debugLog{ false };
		std::string language{ "auto" };  // the menu's language: "auto" = the game's, or english, french...

		bool operator==(const Values&) const = default;
	};

	void Load();
	void Save();           // writes the file if anything changed since the last load/save
	void ResetDefaults();  // keeps nothing

	Values&     Get();
	std::mutex& Lock();  // around reads from other threads and changes from the menu

	// bumped on every change, so the other modules can notice
	std::uint32_t Revision();
	void          Touch();
}
