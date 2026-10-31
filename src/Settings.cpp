#include "Settings.h"

#include <nlohmann/json.hpp>

namespace Settings
{
	namespace
	{
		using json = nlohmann::json;

		constexpr int kVersion = 1;

		constexpr std::array<const char*, kKinds> kKeys{ "leaves", "greenLeaves", "dust", "rocks", "snow" };

		Values                     values;
		Values                     saved;
		std::mutex                 lock;
		std::atomic<std::uint32_t> revision{ 1 };

		// next to the plugin; under Mod Organizer 2 the file lands in its overwrite folder (no mod ships it, so updating
		// the mod never replaces it)
		std::filesystem::path Path() { return "Data/SKSE/Plugins/ResponsiveGrounds.json"; }

		template <class T>
		void Read(const json& a_obj, const char* a_key, T& a_value)
		{
			if (const auto it = a_obj.find(a_key); it != a_obj.end()) {
				try {
					a_value = it->get<T>();
				} catch (const std::exception& e) {
					logs::warn("settings: '{}' ignored ({})", a_key, e.what());
				}
			}
		}

		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		// texture choices by file name (lower case), material choices by material name
		void ReadChoices(const json& a_obj, const char* a_key, std::map<std::string, TextureChoice>& a_out, bool a_lower)
		{
			const auto it = a_obj.find(a_key);
			if (it == a_obj.end() || !it->is_object()) {
				return;
			}
			for (const auto& [name, entry] : it->items()) {
				if (!entry.is_object()) {
					continue;
				}
				TextureChoice choice;
				Read(entry, "enabled", choice.enabled);
				Read(entry, "strength", choice.strength);
				a_out[a_lower ? Lower(name) : name] = choice;
			}
		}

		json WriteChoices(const std::map<std::string, TextureChoice>& a_choices)
		{
			json out = json::object();
			for (const auto& [name, choice] : a_choices) {
				out[name] = { { "enabled", choice.enabled }, { "strength", choice.strength } };
			}
			return out;
		}

		void ReadKind(const json& a_obj, KindSettings& a_kind)
		{
			Read(a_obj, "enabled", a_kind.enabled);
			Read(a_obj, "amount", a_kind.amount);
			Read(a_obj, "maxParticles", a_kind.maxParticles);
			Read(a_obj, "size", a_kind.size);
			ReadChoices(a_obj, "textures", a_kind.textures, true);
			Read(a_obj, "objects", a_kind.objects);
			ReadChoices(a_obj, "materials", a_kind.materials, false);
		}

		json WriteKind(const KindSettings& a_kind)
		{
			return {
				{ "enabled", a_kind.enabled },
				{ "amount", a_kind.amount },
				{ "maxParticles", a_kind.maxParticles },
				{ "size", a_kind.size },
				{ "textures", WriteChoices(a_kind.textures) },
				{ "objects", a_kind.objects },
				{ "materials", WriteChoices(a_kind.materials) },
			};
		}

		void Clamp(Values& a_values)
		{
			for (auto& kind : a_values.kinds) {
				kind.amount = std::clamp(kind.amount, 0.0f, 5.0f);
				kind.maxParticles = std::clamp(kind.maxParticles, 10, 500);
				kind.size = std::clamp(kind.size, 0.25f, 4.0f);
				for (auto* list : { &kind.textures, &kind.materials }) {
					for (auto& [name, choice] : *list) {
						choice.strength = std::clamp(choice.strength, 0.0f, 1.0f);
					}
				}
			}
			a_values.lift = std::clamp(a_values.lift, 0.0f, 4.0f);
			a_values.swirl = std::clamp(a_values.swirl, 0.0f, 4.0f);
			a_values.groundTime = std::clamp(a_values.groundTime, 0.0f, 60.0f);
			a_values.startDelay = std::clamp(a_values.startDelay, 0.0f, 10.0f);
			a_values.npcMax = std::clamp(a_values.npcMax, 0, 30);
			a_values.npcRange = std::clamp(a_values.npcRange, 256.0f, 8192.0f);
			a_values.shoutStrength = std::clamp(a_values.shoutStrength, 0.0f, 4.0f);
			a_values.slamStrength = std::clamp(a_values.slamStrength, 0.0f, 4.0f);
			a_values.bodyStrength = std::clamp(a_values.bodyStrength, 0.0f, 4.0f);
			a_values.explosionStrength = std::clamp(a_values.explosionStrength, 0.0f, 4.0f);
			a_values.windAmount = std::clamp(a_values.windAmount, 0.0f, 5.0f);
			a_values.swingStrength = std::clamp(a_values.swingStrength, 0.0f, 4.0f);
			a_values.leafVolume = std::clamp(a_values.leafVolume, 0.0f, 1.0f);
			a_values.rockVolume = std::clamp(a_values.rockVolume, 0.0f, 1.0f);
		}
	}

	void Load()
	{
		std::scoped_lock guard(lock);
		const auto       path = Path();
		std::ifstream    file(path);
		if (!file) {
			logs::info("no settings file at {}, using defaults", path.string());
			saved = values;
			return;
		}
		try {
			const auto root = json::parse(file);
			Read(root, "enabled", values.enabled);
			for (std::size_t k = 0; k < kKinds; ++k) {
				if (const auto it = root.find(kKeys[k]); it != root.end() && it->is_object()) {
					ReadKind(*it, values.kinds[k]);
				}
			}
			Read(root, "lift", values.lift);
			Read(root, "swirl", values.swirl);
			Read(root, "groundTime", values.groundTime);
			Read(root, "startDelay", values.startDelay);
			Read(root, "puffs", values.puffs);
			Read(root, "swing", values.swing);
			Read(root, "swingStrength", values.swingStrength);
			Read(root, "onHorse", values.onHorse);
			Read(root, "sneakQuieter", values.sneakQuieter);
			Read(root, "sounds", values.sounds);
			Read(root, "leafVolume", values.leafVolume);
			Read(root, "rockVolume", values.rockVolume);
			Read(root, "npcs", values.npcs);
			Read(root, "npcMax", values.npcMax);
			Read(root, "npcRange", values.npcRange);
			Read(root, "npcCreatures", values.npcCreatures);
			Read(root, "npcAttacks", values.npcAttacks);
			Read(root, "shouts", values.shouts);
			Read(root, "shoutStrength", values.shoutStrength);
			Read(root, "slams", values.slams);
			Read(root, "slamStrength", values.slamStrength);
			Read(root, "explosions", values.explosions);
			Read(root, "explosionStrength", values.explosionStrength);
			Read(root, "bodies", values.bodies);
			Read(root, "bodyStrength", values.bodyStrength);
			Read(root, "wind", values.wind);
			Read(root, "windAmount", values.windAmount);
			Read(root, "windFlip", values.windFlip);
			Read(root, "northernRoads", values.northernRoads);
			Read(root, "debugLog", values.debugLog);
			Read(root, "language", values.language);
			Clamp(values);
			logs::info("settings loaded from {}", path.string());
		} catch (const std::exception& e) {
			logs::error("settings file {} is broken ({}), using defaults", path.string(), e.what());
			values = {};
		}
		saved = values;
	}

	void Save()
	{
		if (values == saved) {
			return;
		}
		json root{
			{ "version", kVersion },
			{ "enabled", values.enabled },
			{ "lift", values.lift },
			{ "swirl", values.swirl },
			{ "groundTime", values.groundTime },
			{ "startDelay", values.startDelay },
			{ "puffs", values.puffs },
			{ "swing", values.swing },
			{ "swingStrength", values.swingStrength },
			{ "onHorse", values.onHorse },
			{ "sneakQuieter", values.sneakQuieter },
			{ "sounds", values.sounds },
			{ "leafVolume", values.leafVolume },
			{ "rockVolume", values.rockVolume },
			{ "npcs", values.npcs },
			{ "npcMax", values.npcMax },
			{ "npcRange", values.npcRange },
			{ "npcCreatures", values.npcCreatures },
			{ "npcAttacks", values.npcAttacks },
			{ "shouts", values.shouts },
			{ "shoutStrength", values.shoutStrength },
			{ "slams", values.slams },
			{ "slamStrength", values.slamStrength },
			{ "explosions", values.explosions },
			{ "explosionStrength", values.explosionStrength },
			{ "bodies", values.bodies },
			{ "bodyStrength", values.bodyStrength },
			{ "wind", values.wind },
			{ "windAmount", values.windAmount },
			{ "windFlip", values.windFlip },
			{ "northernRoads", values.northernRoads },
			{ "debugLog", values.debugLog },
			{ "language", values.language },
		};
		for (std::size_t k = 0; k < kKinds; ++k) {
			root[kKeys[k]] = WriteKind(values.kinds[k]);
		}
		const auto      path = Path();
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		std::ofstream file(path, std::ios::trunc);
		if (!file) {
			logs::error("can't write {}", path.string());
			return;
		}
		file << root.dump(2);
		saved = values;
		logs::info("settings saved");
	}

	void ResetDefaults()
	{
		values = {};
		Touch();
	}

	Values&     Get() { return values; }
	std::mutex& Lock() { return lock; }

	std::uint32_t Revision() { return revision.load(); }
	void          Touch() { ++revision; }
}
