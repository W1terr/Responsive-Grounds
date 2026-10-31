#include "Hooks.h"

#include "Particles.h"

namespace Hooks
{
	namespace
	{
		// once per frame on the main thread, while the game runs (not in paused menus)
		struct PlayerUpdate
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, a_delta);
				Particles::Update(a_delta);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// an explosion being set up (fireballs, fire and frost runes, oil, traps): once, where it goes off
		struct ExplosionInitialize
		{
			static void thunk(RE::Explosion* a_this)
			{
				func(a_this);
				if (!a_this) {
					return;
				}
				float      radius = a_this->GetExplosionRuntimeData().radius;
				const auto base = a_this->GetBaseObject();
				if (const auto explosion = base ? base->As<RE::BGSExplosion>() : nullptr; radius <= 0.0f && explosion) {
					radius = explosion->data.radius;
				}
				Particles::OnExplosion(a_this->GetPosition(), radius);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[0] };
		PlayerUpdate::func = player.write_vfunc(0xAD, PlayerUpdate::thunk);
		REL::Relocation<std::uintptr_t> explosion{ RE::VTABLE_Explosion[0] };
		ExplosionInitialize::func = explosion.write_vfunc(0xA2, ExplosionInitialize::thunk);
		logs::info("hooked the player update and explosions");
	}
}
