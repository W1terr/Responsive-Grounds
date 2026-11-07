#include "Hooks.h"
#include "Lang.h"
#include "Menu.h"
#include "Particles.h"
#include "Seasons.h"
#include "Settings.h"
#include "Sounds.h"
#include "Terrain.h"

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Terrain::Init();
			Terrain::Apply();
			Seasons::Init();
			Particles::Init();
			Sounds::Init();
			Lang::Set(Settings::Get().language);
			Menu::Register();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			Particles::Reset();
			break;
		case SKSE::MessagingInterface::kNewGame:
			Particles::Reset();
			Seasons::Ask();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			Seasons::Ask();
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	logs::info("Responsive Grounds loading");
	Settings::Load();
	Hooks::Install();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
