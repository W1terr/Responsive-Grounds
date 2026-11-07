#pragma once

// Seasons of Skyrim (powerofthree's po3_SeasonsOfSkyrim.dll) repaints the landscape per season without touching the
// land data: the landscape texture records stay what the plugins painted, it swaps the textures they show (and their
// footstep materials) as the land is built. So the season's swaps are read from its own files (Data\Seasons\*_WIN.ini,
// _SPR, _SUM, _AUT: [LandTextures] "original|replacement"; MainFormSwap_WIN.ini is its generated winter one) and
// handed to Terrain, which then sorts the land by what it shows: snow in winter where autumn leaves lie the rest of
// the year. The season itself comes from the mod (SeasonsOfSkyrim.GetCurrentSeason through Papyrus, asked after a
// game load and each time you come outside, where it changes the season), the worldspaces it applies to from
// po3_SeasonsOfSkyrim.ini.
namespace Seasons
{
	enum Season : int
	{
		kNone = 0,
		kWinter,
		kSpring,
		kSummer,
		kAutumn,
		kSeasons
	};

	void Init();  // after the data loaded: is the mod there, its settings
	void Ask();   // asks the mod for the season now (any time; the answer arrives a little later)

	// main thread, every frame outside: applies a new answer, worldspace or setting to the landscape textures
	void Update(const RE::TESWorldSpace* a_worldspace, bool a_enabled);

	bool   Found();
	Season Current();   // the season the mod says it is (kNone: none, or not asked yet)
	bool   Applied();   // ... and it repaints the land here (a seasonal worldspace, support on)
	int    Swapped();   // landscape texture records repainted here now
}
