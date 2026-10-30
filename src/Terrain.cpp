#include "Terrain.h"

namespace Terrain
{
	namespace
	{
		using Settings::kKinds;

		std::vector<File>                                  files;
		std::unordered_map<const RE::TESLandTexture*, int> fileOf;     // record -> index into files
		std::vector<Covers>                                strengths;  // per file and kind, 0 when it doesn't count
		std::vector<Covers>                                materialStrengths;  // per material and kind, 0 when it doesn't count

		using M = RE::MATERIAL_ID;
		//                 leaves (x land leaf cover)  dust  rocks
		const std::vector<Material> kMaterials{
			{ "Ash covered", {}, { 0.0f, 0.7f, 0.0f } },  // Solstheim's ash layer
			{ "Rock", {}, { 0.0f, 0.0f, 0.7f } },         // natural rocks and cliffs without a layer
			{ "Ice", { M::kIce, M::kIceForm }, { 0.0f, 0.0f, 0.0f } },
			{ "Dirt", { M::kDirt }, { 0.7f, 0.8f, 0.0f } },
			{ "Mud", { M::kMud }, { 0.4f, 0.6f, 0.0f } },
			{ "Gravel", { M::kGravel }, { 0.6f, 0.5f, 0.6f } },
			{ "Sand", { M::kSand }, { 0.0f, 0.7f, 0.0f } },
			{ "Ash", { M::kAsh }, { 0.0f, 0.7f, 0.0f } },
			{ "Grass", { M::kGrass }, { 0.8f, 0.0f, 0.0f } },
			{ "Stone", { M::kStone, M::kStoneHeavy, M::kStoneBroken, M::kStoneStairs, M::kStoneAsStairs, M::kStoneStairsBroken, M::kBoulderSmall, M::kBoulderLarge },
				{ 0.6f, 0.0f, 0.0f } },
			{ "Wood", { M::kWood, M::kWoodHeavy, M::kWoodLight, M::kWoodStairs, M::kWoodAsStairs }, { 0.5f, 0.0f, 0.0f } },
		};

		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		// ---- Northern Roads

		bool northernRoads{ false };  // its plugin is loaded

		struct RoadKind
		{
			const char* name;  // the file name, or a part of it
			bool        part;
			const char* label;
			Covers      strength;  // leaves (x the leafy land around), dust, rocks
		};
		// first match wins (the winter roads before the trails); no leaves by default (they can be turned on per road)
		const std::array<RoadKind, 12> kRoadKinds{ {
			{ "_winter", true, "Winter road", { 0.0f, 0.25f, 0.0f } },
			{ "grounddirt01_trail", true, "Dirt road edge", { 0.0f, 0.7f, 0.2f } },
			{ "grounddirt01.dds", false, "Dirt road", { 0.0f, 0.8f, 0.25f } },
			{ "dirtdry.dds", false, "Dry dirt road", { 0.0f, 0.9f, 0.2f } },
			{ "groundmud01.dds", false, "Mud road", { 0.0f, 0.6f, 0.0f } },
			{ "ratwaybwall01.dds", false, "Helgen mud", { 0.0f, 0.6f, 0.0f } },
			{ "groundstone01.dds", false, "Stone road", { 0.0f, 0.2f, 0.25f } },
			{ "volcanictundra01.dds", false, "Volcanic road", { 0.0f, 0.5f, 0.4f } },
			{ "wrstonefloor01.dds", false, "Whiterun stone", { 0.0f, 0.15f, 0.0f } },
			{ "riftenplazabrick02.dds", false, "Riften brick", { 0.0f, 0.1f, 0.0f } },
			{ "dwestonefloor02.dds", false, "Dwemer stone", { 0.0f, 0.1f, 0.0f } },
			{ "glaciersnow01.dds", false, "Glacier snow", { 0.0f, 0.0f, 0.0f } },
		} };

		const RoadKind* RoadKindOf(std::string_view a_name)
		{
			for (const auto& r : kRoadKinds) {
				if (r.part ? a_name.contains(r.name) : a_name == r.name) {
					return &r;
				}
			}
			return nullptr;
		}

		// a landscape texture record of Northern Roads: made by its plugin, or using its own landscape textures
		bool FromNorthernRoads(const RE::TESLandTexture* a_texture)
		{
			const auto file = a_texture->GetFile(0);
			if (file && Lower(file->GetFilename()) == "northern roads.esp") {
				return true;
			}
			const char* path = a_texture->textureSet ? a_texture->textureSet->textures[RE::BGSTextureSet::Texture::kDiffuse].textureName.c_str() : nullptr;
			return path && Lower(path).starts_with("cotn\\landscape");
		}

		std::string FileName(const RE::TESLandTexture* a_texture)
		{
			if (!a_texture || !a_texture->textureSet) {
				return {};
			}
			const char* path = a_texture->textureSet->textures[RE::BGSTextureSet::Texture::kDiffuse].textureName.c_str();
			if (!path) {
				return {};
			}
			std::string_view view(path);
			if (const auto slash = view.find_last_of("\\/"); slash != std::string_view::npos) {
				view.remove_prefix(slash + 1);
			}
			return Lower(view);
		}

		int FileOf(const RE::TESLandTexture* a_texture)
		{
			if (!a_texture) {
				return -1;
			}
			const auto it = fileOf.find(a_texture);
			return it != fileOf.end() ? it->second : -1;
		}

		// the land data and vertex grid position around a point: 4096 units per cell, 4 quadrants of 17 x 17
		// vertices 128 units apart (row major, rows along y)
		struct Spot
		{
			const RE::TESObjectLAND::LoadedLandData* data{ nullptr };
			int                                       quad{ 0 };
			int                                       vertex[4]{};
			float                                     weight[4]{};
		};

		bool Locate(const RE::NiPoint3& a_position, Spot& a_spot)
		{
			const auto tes = RE::TES::GetSingleton();
			const auto cell = tes ? tes->GetCell(a_position) : nullptr;
			if (!cell || !cell->IsExteriorCell()) {
				return false;
			}
			const auto land = cell->GetRuntimeData().cellLand;
			const auto coords = cell->GetCoordinates();
			if (!land || !land->loadedData || !coords) {
				return false;
			}
			const float localX = std::clamp(a_position.x - coords->cellX * 4096.0f, 0.0f, 4096.0f);
			const float localY = std::clamp(a_position.y - coords->cellY * 4096.0f, 0.0f, 4096.0f);
			const int   qx = localX >= 2048.0f ? 1 : 0;
			const int   qy = localY >= 2048.0f ? 1 : 0;
			const float fx = std::clamp((localX - qx * 2048.0f) / 128.0f, 0.0f, 16.0f);
			const float fy = std::clamp((localY - qy * 2048.0f) / 128.0f, 0.0f, 16.0f);
			const int   ix = std::min(static_cast<int>(fx), 15);
			const int   iy = std::min(static_cast<int>(fy), 15);
			const float tx = fx - ix;
			const float ty = fy - iy;
			a_spot.data = land->loadedData;
			a_spot.quad = qx + qy * 2;
			a_spot.vertex[0] = iy * 17 + ix;
			a_spot.vertex[1] = iy * 17 + ix + 1;
			a_spot.vertex[2] = (iy + 1) * 17 + ix;
			a_spot.vertex[3] = (iy + 1) * 17 + ix + 1;
			a_spot.weight[0] = (1 - tx) * (1 - ty);
			a_spot.weight[1] = tx * (1 - ty);
			a_spot.weight[2] = (1 - tx) * ty;
			a_spot.weight[3] = tx * ty;
			return true;
		}

		// calls a_fn(texture, share) for the base texture and each layer of one land vertex; the base gets what the
		// layers leave (like the engine's own GetLandTexture), all scaled to a_scale in total
		template <class F>
		void ForEachTexture(const RE::TESObjectLAND::LoadedLandData& a_data, int a_quad, int a_vertex, float a_scale, F&& a_fn)
		{
			if (a_scale <= 0.0f) {
				return;
			}
			float layers[6]{};
			float sum = 0.0f;
			for (int l = 0; l < 6; ++l) {
				if (a_data.quadTextures[a_quad][l]) {
					layers[l] = static_cast<std::uint8_t>(a_data.percents[a_quad][a_vertex][l]) / 255.0f;
					sum += layers[l];
				}
			}
			const float base = std::max(0.0f, 1.0f - sum);
			const float total = base + sum;
			if (total <= 0.0f) {
				return;
			}
			const float norm = a_scale / total;
			if (base > 0.0f) {
				a_fn(a_data.defQuadTextures[a_quad], base * norm);
			}
			for (int l = 0; l < 6; ++l) {
				if (layers[l] > 0.0f) {
					a_fn(a_data.quadTextures[a_quad][l], layers[l] * norm);
				}
			}
		}
	}

	float DefaultStrength(Kind a_kind, std::string_view a_fileName)
	{
		const auto name = Lower(a_fileName);
		switch (a_kind) {
		case Kind::kLeaves:
			if (!name.contains("fallforest") || name.contains("road")) {
				return 0.0f;
			}
			if (name.contains("leaves") || name.contains("leaf")) {
				return 1.0f;
			}
			if (name.contains("grass")) {
				return 0.6f;
			}
			if (name.contains("dirt")) {
				return 0.5f;
			}
			if (name.contains("rock")) {
				return 0.35f;
			}
			return 0.6f;
		case Kind::kDirt:
			if ((!name.contains("dirt") && !name.contains("mud")) || name.contains("snow")) {
				return 0.0f;
			}
			if (name.contains("fallforest")) {
				return 0.3f;
			}
			if (name.contains("mud")) {
				return 0.6f;
			}
			return 0.8f;
		case Kind::kRock:
			if ((!name.contains("rock") && !name.contains("gravel")) || name.contains("snow")) {
				return 0.0f;
			}
			if (name.contains("gravel")) {
				return 0.8f;
			}
			if (name.contains("fallforest")) {
				return 0.4f;  // Riften's leafy rock ground: leaves too
			}
			if (name.contains("moss")) {
				return 0.5f;
			}
			if (name.contains("tundra") && !name.contains("volcanic")) {
				return 0.6f;
			}
			if (name.contains("ash")) {
				return 0.6f;
			}
			return 0.75f;
		default:
			return 0.0f;
		}
	}

	Settings::TextureChoice Choice(const Settings::KindSettings& a_kind, Kind a_which, const File& a_file)
	{
		if (const auto it = a_kind.textures.find(a_file.name); it != a_kind.textures.end()) {
			return it->second;
		}
		const bool standard = a_file.isDefault[a_which];
		return { standard, standard ? a_file.defaultStrength[a_which] : 1.0f };
	}

	bool Active(const Settings::KindSettings& a_kind, Kind a_which, const File& a_file)
	{
		const auto choice = Choice(a_kind, a_which, a_file);
		return a_kind.enabled && choice.enabled && choice.strength > 0.0f;
	}

	void Init()
	{
		files.clear();
		fileOf.clear();
		const auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return;
		}
		std::unordered_map<std::string, int>                           index;
		std::vector<std::pair<const RE::TESLandTexture*, std::string>> records;
		std::unordered_set<std::string>                                roads;
		for (const auto texture : dataHandler->GetFormArray<RE::TESLandTexture>()) {
			auto name = FileName(texture);
			if (name.empty()) {
				continue;
			}
			if (FromNorthernRoads(texture)) {
				roads.insert(name);
			}
			if (const auto [it, added] = index.try_emplace(name, 0); added) {
				File f{ name };
				for (std::size_t k = 0; k < kKinds; ++k) {
					f.defaultStrength[k] = DefaultStrength(static_cast<Kind>(k), name);
					f.isDefault[k] = f.defaultStrength[k] > 0.0f;
				}
				files.push_back(std::move(f));
			}
			records.emplace_back(texture, std::move(name));
		}
		// Northern Roads' road textures: their own defaults
		northernRoads = !roads.empty();
		for (auto& f : files) {
			if (!roads.contains(f.name)) {
				continue;
			}
			f.road = true;
			const auto kind = RoadKindOf(f.name);
			f.roadLabel = kind ? kind->label : "Other road";
			for (std::size_t k = 0; k < kKinds; ++k) {
				if (kind) {
					f.defaultStrength[k] = kind->strength[k];
				}
				f.isDefault[k] = f.defaultStrength[k] > 0.0f;
			}
		}
		std::ranges::sort(files, [](const File& a_a, const File& a_b) { return a_a.name < a_b.name; });
		for (int i = 0; i < static_cast<int>(files.size()); ++i) {
			index[files[i].name] = i;
		}
		for (const auto& [texture, name] : records) {
			const int i = index[name];
			++files[i].records;
			fileOf.emplace(texture, i);
		}
		strengths.assign(files.size(), Covers{});

		logs::info("landscape textures: {} records, {} files", records.size(), files.size());
		if (northernRoads) {
			std::string list;
			for (const auto& f : files) {
				if (f.road) {
					list += std::format("{}{} ({})", list.empty() ? "" : ", ", f.name, f.roadLabel);
				}
			}
			logs::info("  Northern Roads: {} road textures: {}", roads.size(), list);
		} else {
			logs::info("  Northern Roads: not loaded");
		}
		for (std::size_t k = 0; k < kKinds; ++k) {
			std::string list;
			for (const auto& f : files) {
				if (f.isDefault[k] && !f.road) {
					list += std::format("{}{} ({:.0f}%)", list.empty() ? "" : ", ", f.name, f.defaultStrength[k] * 100.0f);
				}
			}
			logs::info("  {} by default: {}", Settings::kKindNames[k], list.empty() ? "none found" : list);
		}
	}

	void Apply()
	{
		std::scoped_lock guard(Settings::Lock());
		const auto&      s = Settings::Get();
		strengths.assign(files.size(), Covers{});
		for (std::size_t i = 0; i < files.size(); ++i) {
			if (files[i].road && !s.northernRoads) {
				continue;  // Northern Roads support off: its roads kick up nothing
			}
			for (std::size_t k = 0; k < kKinds; ++k) {
				const auto which = static_cast<Kind>(k);
				if (Active(s.kinds[k], which, files[i])) {
					strengths[i][k] = Choice(s.kinds[k], which, files[i]).strength;
				}
			}
		}
		materialStrengths.assign(kMaterials.size(), Covers{});
		for (std::size_t m = 0; m < kMaterials.size(); ++m) {
			for (std::size_t k = 0; k < kKinds; ++k) {
				const auto& kind = s.kinds[k];
				const auto  choice = MaterialChoice(kind, static_cast<Kind>(k), kMaterials[m]);
				if (kind.enabled && kind.objects && choice.enabled) {
					materialStrengths[m][k] = choice.strength;
				}
			}
		}
	}

	const std::vector<File>& Files() { return files; }

	const std::vector<Material>& Materials() { return kMaterials; }

	int MaterialOf(RE::MATERIAL_ID a_id)
	{
		for (int i = 0; i < static_cast<int>(kMaterials.size()); ++i) {
			if (std::ranges::find(kMaterials[i].ids, a_id) != kMaterials[i].ids.end()) {
				return i;
			}
		}
		return -1;
	}

	int MaterialIndex(std::string_view a_name)
	{
		for (int i = 0; i < static_cast<int>(kMaterials.size()); ++i) {
			if (a_name == kMaterials[i].name) {
				return i;
			}
		}
		return -1;
	}

	Settings::TextureChoice MaterialChoice(const Settings::KindSettings& a_kind, Kind a_which, const Material& a_material)
	{
		if (const auto it = a_kind.materials.find(a_material.name); it != a_kind.materials.end()) {
			return it->second;
		}
		const float standard = a_material.defaultStrength[a_which];
		return { standard > 0.0f, standard > 0.0f ? standard : 1.0f };
	}

	namespace
	{
		// the covers at a point; road leaves are left out of them and summed into a_roadLeaves (when given)
		Covers CoverRaw(const RE::NiPoint3& a_position, float* a_roadLeaves)
		{
			Covers total{};
			Spot   spot;
			if (!Locate(a_position, spot)) {
				return total;
			}
			for (int v = 0; v < 4; ++v) {
				ForEachTexture(*spot.data, spot.quad, spot.vertex[v], spot.weight[v], [&](const RE::TESLandTexture* a_texture, float a_share) {
					const int f = FileOf(a_texture);
					if (f < 0) {
						return;
					}
					const bool road = files[f].road;
					for (std::size_t k = 0; k < kKinds; ++k) {
						if (road && k == Kind::kLeaves) {
							if (a_roadLeaves) {
								*a_roadLeaves += a_share * strengths[f][k];
							}
						} else {
							total[k] += a_share * strengths[f][k];
						}
					}
				});
			}
			return total;
		}

		// how leafy the land beside a road is (not counting the roads)
		float LeavesAround(const RE::NiPoint3& a_position)
		{
			float leaves = 0.0f;
			for (const auto& [dx, dy] : { std::pair{ 256.0f, 0.0f }, { -256.0f, 0.0f }, { 0.0f, 256.0f }, { 0.0f, -256.0f } }) {
				leaves = std::max(leaves, CoverRaw({ a_position.x + dx, a_position.y + dy, a_position.z }, nullptr)[Kind::kLeaves]);
			}
			return std::min(leaves, 1.0f);
		}
	}

	bool NorthernRoadsFound() { return northernRoads; }

	Covers ObjectCover(int a_material, const RE::NiPoint3& a_position)
	{
		Covers    result{};
		const int m = a_material;
		if (m < 0 || m >= static_cast<int>(materialStrengths.size())) {
			return result;
		}
		result = materialStrengths[m];
		if (result[Kind::kLeaves] > 0.0f) {
			float leaves = Cover(a_position)[Kind::kLeaves];
			for (const auto& [dx, dy] : { std::pair{ 192.0f, 0.0f }, { -192.0f, 0.0f }, { 0.0f, 192.0f }, { 0.0f, -192.0f } }) {
				leaves = std::max(leaves, Cover({ a_position.x + dx, a_position.y + dy, a_position.z })[Kind::kLeaves]);
			}
			result[Kind::kLeaves] *= leaves;
		}
		return result;
	}

	Covers Cover(const RE::NiPoint3& a_position)
	{
		float roadLeaves = 0.0f;
		Covers total = CoverRaw(a_position, &roadLeaves);
		if (roadLeaves > 0.0f) {
			total[Kind::kLeaves] += roadLeaves * LeavesAround(a_position);
		}
		for (auto& c : total) {
			c = std::clamp(c, 0.0f, 1.0f);
		}
		return total;
	}

	Probe At(const RE::NiPoint3& a_position)
	{
		Probe result;
		Spot  spot;
		if (!Locate(a_position, spot)) {
			return result;
		}
		result.land = true;
		std::vector<std::pair<int, float>> shares;
		for (int v = 0; v < 4; ++v) {
			ForEachTexture(*spot.data, spot.quad, spot.vertex[v], spot.weight[v], [&](const RE::TESLandTexture* a_texture, float a_share) {
				const int f = FileOf(a_texture);
				if (f < 0) {
					return;
				}
				for (auto& [file, share] : shares) {
					if (file == f) {
						share += a_share;
						return;
					}
				}
				shares.emplace_back(f, a_share);
			});
		}
		result.covers = Cover(a_position);
		for (const auto& [file, share] : shares) {
			if (share > result.dominantShare) {
				result.dominantShare = share;
				result.dominant = files[file].name;
			}
		}
		return result;
	}
}
