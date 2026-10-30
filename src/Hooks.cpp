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
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[0] };
		PlayerUpdate::func = player.write_vfunc(0xAD, PlayerUpdate::thunk);
		logs::info("hooked the player update");
	}
}
