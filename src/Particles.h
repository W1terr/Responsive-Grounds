#pragma once

#include "Terrain.h"

// What the ground kicks up: a pool of small lit meshes in the world per kind (leaves, green leaves, dirt, rocks, snow),
// kicked up around the feet of you (or your horse) and the NPCs and creatures near you while they move over matching
// ground, by weapon swings, shouts, heavy blows on the ground, falling bodies and the wind, and carried by the air
// bodies drag along and the swirl behind them. Leaves (both kinds) flutter down, settle and fade into the ground; snow
// flakes flutter down slower and melt soon after; dirt clods and rock pebbles fly in arcs and drop (bouncing once);
// dust and rock dust puffs drift in the wake, grow and fade out.
namespace Particles
{
	struct Status
	{
		bool                                modelLoaded{ false };
		bool                                exterior{ false };
		bool                                grounded{ false };  // standing/moving on the landscape
		Terrain::Covers                     covers{};           // under you, 0..1 per kind (texture shares x strength)
		float                               speed{ 0.0f };      // units per second
		std::array<int, Settings::kKinds>   flying{};
		std::array<int, Settings::kKinds>   landed{};
		std::string                         texture;            // the texture under you
		std::string                         object;             // standing on an object: its surface material ("Stone", "other")
		std::string                         objectTexture;      // ... or the landscape texture painted on it
		bool                                raised{ false };    // up on an object that isn't ground (a log pile): nothing
		float                               textureShare{ 0.0f };
		int                                 npcs{ 0 };          // NPCs and creatures kicking things up
	};

	void Init();                 // after the data loaded: loads the models
	void Update(float a_delta);  // every frame, main thread
	void Reset();                // everything gone (game load, interiors, fast travel)
	void RequestTest();          // throws some of each kind around you on any ground (next frame the game runs)
	void OnExplosion(const RE::NiPoint3& a_position, float a_radius);  // an explosion went off (any thread)

	Status GetStatus();
}
