#include "Seasons.h"

#include "Terrain.h"

namespace Seasons
{
	namespace
	{
		constexpr std::array<const char*, kSeasons> kNames{ "none", "Winter", "Spring", "Summer", "Autumn" };
		constexpr std::array<const char*, kSeasons> kSuffix{ "", "_win", "_spr", "_sum", "_aut" };
		constexpr std::array<const char*, kSeasons> kSection{ "", "winter", "spring", "summer", "autumn" };
		// the game's months in order (Calendar::GetMonth 0..11) as po3_SeasonsOfSkyrim.ini names them, and its defaults
		constexpr std::array<const char*, 12> kMonths{ "morning star", "sun's dawn", "first seed", "rain's hand", "second seed", "mid year",
			"sun's height", "last seed", "hearthfire", "frost fall", "sun's dusk", "evening star" };
		constexpr std::array<int, 12> kDefaultMonths{ kWinter, kWinter, kSpring, kSpring, kSpring, kSummer, kSummer, kSummer, kAutumn, kAutumn, kAutumn, kWinter };
		constexpr auto kDefaultWorldspaces = "Tamriel|MarkarthWorld|RiftenWorld|SolitudeWorld|WhiterunWorld|DLC1HunterHQWorld|DLC2SolstheimWorld";
		constexpr auto kGenerated = "mainformswap_win.ini";  // the winter swaps the mod generates itself

		using SwapMap = std::unordered_map<const RE::TESLandTexture*, const RE::TESLandTexture*>;

		bool                                                  found{ false };
		int                                                   seasonType{ 5 };  // 0 off, 1..4 always that season, 5 by the month
		std::array<int, 12>                                   months{ kDefaultMonths };
		std::array<std::unordered_set<std::string>, kSeasons> worldspaces;            // lower case editor ids, per season
		bool                                                  skipGenerated{ false };  // its own generated winter land swaps not used
		std::array<std::optional<SwapMap>, kSeasons>          swaps;                  // read when a season first comes

		std::atomic<int>  answer{ -1 };  // the mod's latest answer (-1: none new)
		std::atomic<bool> asking{ false };
		std::atomic<int>  season{ kNone };
		std::atomic<bool> applied{ false };
		std::atomic<int>  swapped{ 0 };

		int                      shownSeason{ -1 };  // what Terrain has now
		const RE::TESWorldSpace* shownWorld{ nullptr };
		bool                     shownEnabled{ false };

		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::string_view Trim(std::string_view a_text)
		{
			while (!a_text.empty() && std::isspace(static_cast<unsigned char>(a_text.front()))) {
				a_text.remove_prefix(1);
			}
			while (!a_text.empty() && std::isspace(static_cast<unsigned char>(a_text.back()))) {
				a_text.remove_suffix(1);
			}
			return a_text;
		}

		// a small ini reader: per lower case section its lines as (key, value) in order; a line without "=" is a key
		// with no value (the swap files have both forms)
		using Ini = std::map<std::string, std::vector<std::pair<std::string, std::string>>>;

		Ini ReadIni(const std::filesystem::path& a_path)
		{
			Ini           ini;
			std::ifstream in(a_path);
			std::string   line;
			std::string   section;
			bool          first = true;
			while (std::getline(in, line)) {
				std::string_view view(line);
				if (first && view.starts_with("\xEF\xBB\xBF")) {
					view.remove_prefix(3);
				}
				first = false;
				view = Trim(view);
				if (view.empty() || view.front() == ';' || view.front() == '#') {
					continue;
				}
				if (view.front() == '[') {
					const auto end = view.find(']');
					section = Lower(view.substr(1, end == std::string_view::npos ? std::string_view::npos : end - 1));
					continue;
				}
				const auto eq = view.find('=');
				const auto key = Trim(view.substr(0, eq));
				const auto value = eq == std::string_view::npos ? std::string_view{} : Trim(view.substr(eq + 1));
				ini[section].emplace_back(std::string(key), std::string(value));
			}
			return ini;
		}

		const std::string* Value(const Ini& a_ini, const char* a_section, std::string_view a_key)
		{
			const auto it = a_ini.find(a_section);
			if (it == a_ini.end()) {
				return nullptr;
			}
			for (const auto& [key, value] : it->second) {
				if (Lower(key) == a_key) {
					return &value;
				}
			}
			return nullptr;
		}

		bool IsTrue(const std::string* a_value, bool a_default)
		{
			if (!a_value) {
				return a_default;
			}
			const auto value = Lower(*a_value);
			return value == "true" || value == "1";
		}

		// "0xAE4~Skyrim.esm" (a form id in its plugin) or an editor id
		const RE::TESLandTexture* Resolve(std::string_view a_text)
		{
			a_text = Trim(a_text);
			if (a_text.empty()) {
				return nullptr;
			}
			if (const auto tilde = a_text.find('~'); tilde != std::string_view::npos) {
				const std::string id(Trim(a_text.substr(0, tilde)));
				const auto        plugin = Trim(a_text.substr(tilde + 1));
				const auto        dataHandler = RE::TESDataHandler::GetSingleton();
				char*             end = nullptr;
				const auto        local = std::strtoul(id.c_str(), &end, 16);
				if (!dataHandler || end == id.c_str()) {
					return nullptr;
				}
				return dataHandler->LookupForm<RE::TESLandTexture>(static_cast<RE::FormID>(local & 0xFFFFFF), plugin);
			}
			return RE::TESForm::LookupByEditorID<RE::TESLandTexture>(a_text);
		}

		// every [LandTextures] swap of a season's files; its own generated winter file first, the others (mods'
		// files, alphabetical) after it and over it (a mod keeping a texture as it is: "x|x")
		SwapMap ReadSwaps(Season a_season)
		{
			SwapMap                            result;
			std::vector<std::filesystem::path> files;
			std::error_code                    error;
			for (const auto& entry : std::filesystem::directory_iterator("Data/Seasons", error)) {
				const auto name = Lower(entry.path().filename().string());
				if (entry.is_regular_file(error) && name.ends_with(std::string(kSuffix[a_season]) + ".ini")) {
					files.push_back(entry.path());
				}
			}
			std::ranges::sort(files, [](const auto& a_a, const auto& a_b) {
				const auto a = Lower(a_a.filename().string());
				const auto b = Lower(a_b.filename().string());
				return (a == kGenerated) != (b == kGenerated) ? a == kGenerated : a < b;
			});
			int         unknown = 0;
			std::string used;
			for (const auto& path : files) {
				const auto name = path.filename().string();
				if (Lower(name) == kGenerated && skipGenerated) {
					continue;
				}
				const auto ini = ReadIni(path);
				const auto section = ini.find("landtextures");
				if (section == ini.end() || section->second.empty()) {
					continue;
				}
				int count = 0;
				for (const auto& [key, value] : section->second) {
					const std::string_view text = value.empty() ? std::string_view(key) : std::string_view(value);
					const auto             bar = text.find('|');
					if (bar == std::string_view::npos) {
						continue;
					}
					const auto from = Resolve(text.substr(0, bar));
					const auto to = Resolve(text.substr(bar + 1));
					if (!from || !to) {
						++unknown;
						continue;
					}
					result[from] = to;
					++count;
				}
				used += std::format("{}{} ({})", used.empty() ? "" : ", ", name, count);
			}
			std::erase_if(result, [](const auto& a_swap) { return a_swap.first == a_swap.second; });
			logs::info("Seasons of Skyrim: {} repaints {} landscape texture records (files: {}{})", kNames[a_season], result.size(), used.empty() ? "none" : used,
				unknown ? std::format("; {} entries name records that aren't loaded", unknown) : "");
			return result;
		}

		// the season by the game's month, the way the mod picks it (when it can't be asked)
		int ByMonth()
		{
			if (seasonType >= kWinter && seasonType <= kAutumn) {
				return seasonType;
			}
			if (seasonType != 5) {
				return kNone;
			}
			const auto calendar = RE::Calendar::GetSingleton();
			const auto month = calendar ? calendar->GetMonth() : 0;
			return months[std::min<std::uint32_t>(month, 11)];
		}

		class Callback : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			void operator()(RE::BSScript::Variable a_result) override
			{
				answer = a_result.IsInt() ? std::clamp(a_result.GetSInt(), 0, kSeasons - 1) : ByMonth();
				asking = false;
			}
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
		};
	}

	void Init()
	{
		found = REX::W32::GetModuleHandleW(L"po3_SeasonsOfSkyrim.dll") != nullptr;
		if (!found) {
			logs::info("Seasons of Skyrim: not loaded");
			return;
		}
		const auto ini = ReadIni("Data/SKSE/Plugins/po3_SeasonsOfSkyrim.ini");
		if (const auto type = Value(ini, "settings", "season type")) {
			seasonType = std::atoi(type->c_str());
		}
		for (std::size_t m = 0; m < kMonths.size(); ++m) {
			if (const auto value = Value(ini, "settings", kMonths[m])) {
				months[m] = std::clamp(std::atoi(value->c_str()), 0, kSeasons - 1);
			}
		}
		for (int s = kWinter; s < kSeasons; ++s) {
			const auto       value = Value(ini, kSection[s], "worldspaces");
			std::string_view list = value ? std::string_view(*value) : std::string_view(kDefaultWorldspaces);
			while (!list.empty()) {
				const auto bar = list.find('|');
				if (const auto name = Trim(list.substr(0, bar)); !name.empty()) {
					worldspaces[s].insert(Lower(name));
				}
				list = bar == std::string_view::npos ? std::string_view{} : list.substr(bar + 1);
			}
		}
		skipGenerated = IsTrue(Value(ini, "winter", "ignore auto generated win formswap"), false) || IsTrue(Value(ini, "winter", "skip land textures"), false);
		logs::info("Seasons of Skyrim: found (season type {}, its generated winter land swaps {})", seasonType, skipGenerated ? "skipped" : "used");
	}

	void Ask()
	{
		if (!found || asking.exchange(true)) {
			return;
		}
		const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new Callback());
		if (!vm || !vm->DispatchStaticCall("SeasonsOfSkyrim", "GetCurrentSeason", RE::MakeFunctionArguments(), callback)) {
			answer = ByMonth();
			asking = false;
		}
	}

	void Update(const RE::TESWorldSpace* a_worldspace, bool a_enabled)
	{
		if (!found) {
			return;
		}
		if (const int a = answer.exchange(-1); a >= 0 && a != season) {
			season = a;
			logs::info("Seasons of Skyrim: the season is {}", kNames[a]);
		}
		const int now = season;
		if (now == shownSeason && a_worldspace == shownWorld && a_enabled == shownEnabled) {
			return;
		}
		shownSeason = now;
		shownWorld = a_worldspace;
		shownEnabled = a_enabled;
		const char* id = a_worldspace ? a_worldspace->GetFormEditorID() : nullptr;
		const bool  apply = a_enabled && now > kNone && id && worldspaces[now].contains(Lower(id));
		const SwapMap* map = nullptr;
		if (apply) {
			if (!swaps[now]) {
				swaps[now] = ReadSwaps(static_cast<Season>(now));
			}
			map = &*swaps[now];
		}
		Terrain::SetSeasonSwaps(map);
		applied = apply;
		swapped = map ? static_cast<int>(map->size()) : 0;
		logs::info("Seasons of Skyrim: {} in {}: {}", kNames[now], id && *id ? id : "this worldspace",
			apply ? std::format("{} landscape texture records repainted", map->size()) : (a_enabled ? "the land as painted" : "support off"));
	}

	bool Found() { return found; }

	Season Current() { return static_cast<Season>(season.load()); }

	bool Applied() { return applied; }

	int Swapped() { return swapped; }
}
