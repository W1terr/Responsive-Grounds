#pragma once

// Sounds of what the ground kicks up: a soft rustle of leaves sailing past, little stones ticking on the ground.
// No plugin: the two sound descriptors are made at runtime, copies of a vanilla physics sound (its category, so the
// game's effects volume applies, and its 3D falloff) playing the mod's own files (Sound\fx\ResponsiveGrounds).
// The particles report what happens where; nearby events gather into one emitter that plays now and then, louder
// the more happened, so a run through leaves sounds like a run through leaves, not like hundreds of them.
namespace Sounds
{
	enum class Sound
	{
		kLeaves,
		kPebbles
	};

	void Init();  // after the data loaded
	void Configure(bool a_on, float a_leafVolume, float a_rockVolume);
	void Request(Sound a_sound, const RE::NiPoint3& a_position, float a_weight);  // main thread
	void Update(float a_delta, const RE::NiPoint3& a_listener);                  // main thread, every frame
	void Reset();
}
