#include "Sounds.h"

namespace Sounds
{
	namespace
	{
		using Vec = RE::NiPoint3;

		constexpr RE::FormID kTemplate = 0x3F3A1;  // PHYGenericGrassLightLSD: physics sound category, small 3D falloff
		constexpr std::size_t kSoundCount = 2;

		struct SoundInfo
		{
			const char* name;
			const char* files;      // Sound\fx\ResponsiveGrounds\<files>01.wav ...
			int         count;
			float       gather;     // events within this many units share an emitter
			float       threshold;  // events an emitter needs before it plays
			float       interval;   // seconds between its sounds (at least)
			float       perSecond;  // all of this kind together, at most
		};
		constexpr std::array<SoundInfo, kSoundCount> kSounds{ {
			{ "leaves", "Leaves", 5, 260.0f, 2.0f, 0.4f, 4.0f },
			{ "pebbles", "Pebbles", 8, 160.0f, 1.0f, 0.12f, 8.0f },
		} };
		constexpr float kHearing = 2000.0f;  // farther away: not played at all
		constexpr std::size_t kMaxEmitters = 16;

		struct Emitter
		{
			Sound sound;
			Vec   position;
			float energy{ 0.0f };
			float nextAt{ 0.0f };
			float lastAt{ 0.0f };
		};

		std::array<RE::BSISoundDescriptor*, kSoundCount> descriptors{};
		std::vector<Emitter>                        emitters;
		std::array<float, kSoundCount>                   tokens{};
		float                                       clock{ 0.0f };
		bool                                        on{ true };
		std::array<float, kSoundCount>                   volume{ 0.6f, 0.6f };

		std::mt19937 rng{ std::random_device{}() };
		float        Random(float a_min, float a_max) { return std::uniform_real_distribution<float>(a_min, a_max)(rng); }

		bool Same(const RE::BSResource::ID& a_a, const RE::BSResource::ID& a_b)
		{
			return a_a.file == a_b.file && a_a.dir == a_b.dir && std::memcmp(a_a.ext, a_b.ext, sizeof(a_a.ext)) == 0;
		}

		// How the game names a sound file in a resource id: the template's files were loaded from "fx\phy\...", so
		// try the prefixes the loader may have put before them and keep the one that gives the same id.
		std::string Prefix(const RE::BGSStandardSoundDef* a_template)
		{
			static constexpr std::array<const char*, 2> kFiles{ "fx\\phy\\generic\\grass\\light\\l\\PHY_Grass_Light_L_02.wav", "fx\\phy\\generic\\grass\\light\\l\\PHY_Grass_Light_L_01.wav" };
			static constexpr std::array<const char*, 4> kPrefixes{ "Sound\\", "Data\\Sound\\", "", "Data\\" };
			for (const auto& known : a_template->soundFiles) {
				for (const auto prefix : kPrefixes) {
					for (const auto file : kFiles) {
						RE::BSResource::ID id;
						id.GenerateFromPath(std::format("{}{}", prefix, file).c_str());
						if (Same(id, known)) {
							logs::info("sounds: the game names its sound files '{}...'", prefix);
							return prefix;
						}
					}
				}
			}
			logs::warn("sounds: couldn't tell how the game names sound files (trying 'Sound\\...')");
			return "Sound\\";
		}

		// a copy of the template's descriptor playing our files
		RE::BGSStandardSoundDef* MakeDefinition(const RE::BGSStandardSoundDef* a_template, const SoundInfo& a_kind, const std::string& a_prefix)
		{
			const auto definition = static_cast<RE::BGSStandardSoundDef*>(RE::malloc(sizeof(RE::BGSStandardSoundDef)));
			if (!definition) {
				return nullptr;
			}
			std::memcpy(static_cast<void*>(definition), a_template, sizeof(RE::BGSStandardSoundDef));
			std::construct_at(&definition->soundFiles);  // its own list (the template keeps its files)
			definition->conditions = nullptr;
			definition->alternateSoundFormID = 0;
			definition->soundCharacteristics.frequencyVariance = std::max<std::uint8_t>(definition->soundCharacteristics.frequencyVariance, 8);
			for (int i = 1; i <= a_kind.count; ++i) {
				RE::BSResource::ID id;
				id.GenerateFromPath(std::format("{}fx\\ResponsiveGrounds\\{}{:02}.wav", a_prefix, a_kind.files, i).c_str());
				definition->soundFiles.push_back(id);
			}
			return definition;
		}

		void Play(std::size_t a_kind, const Vec& a_position, float a_volume)
		{
			const auto audio = RE::BSAudioManager::GetSingleton();
			if (!audio || !descriptors[a_kind]) {
				return;
			}
			RE::BSSoundHandle handle;
			if (!audio->GetSoundHandle(handle, descriptors[a_kind]) || !handle.IsValid()) {
				return;
			}
			handle.SetPosition(a_position);
			handle.SetVolume(a_volume);
			handle.Play();
		}
	}

	void Init()
	{
		const auto source = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(kTemplate);
		const auto templ = source ? skyrim_cast<RE::BGSStandardSoundDef*>(source->soundDescriptor) : nullptr;
		if (!templ) {
			logs::error("sounds: the vanilla sound to copy ({:08X}) isn't there: no sounds", kTemplate);
			return;
		}
		const auto prefix = Prefix(templ);
		const auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSSoundDescriptorForm>();
		for (std::size_t k = 0; k < kSoundCount; ++k) {
			const auto definition = MakeDefinition(templ, kSounds[k], prefix);
			if (!definition) {
				continue;
			}
			// a form of its own around it, like the game's own sounds (else the descriptor alone)
			const auto form = factory ? factory->Create() : nullptr;
			if (form) {
				form->soundDescriptor = definition;
				descriptors[k] = form;
			} else {
				descriptors[k] = definition;
			}
			logs::info("sounds: {} ready ({} files{})", kSounds[k].name, kSounds[k].count, form ? "" : ", no form");
		}
	}

	void Configure(bool a_on, float a_leafVolume, float a_rockVolume)
	{
		on = a_on;
		volume = { a_leafVolume, a_rockVolume };
		if (!on) {
			emitters.clear();
		}
	}

	void Request(Sound a_sound, const RE::NiPoint3& a_position, float a_weight)
	{
		const auto k = static_cast<std::size_t>(a_sound);
		if (!on || volume[k] <= 0.0f || !descriptors[k]) {
			return;
		}
		const float gather = kSounds[k].gather;
		for (auto& e : emitters) {
			if (e.sound == a_sound && (e.position - a_position).SqrLength() < gather * gather) {
				e.position += (a_position - e.position) * 0.3f;  // follows where things happen
				e.energy += a_weight;
				e.lastAt = clock;
				return;
			}
		}
		if (emitters.size() < kMaxEmitters) {
			emitters.push_back({ a_sound, a_position, a_weight, clock, clock });
		}
	}

	void Update(float a_delta, const RE::NiPoint3& a_listener)
	{
		clock += a_delta;
		for (std::size_t k = 0; k < kSoundCount; ++k) {
			tokens[k] = std::min(tokens[k] + kSounds[k].perSecond * a_delta, 2.0f);
		}
		if (emitters.empty()) {
			return;
		}
		const float fade = std::exp(-a_delta / 0.8f);  // old events count less and less
		for (auto& e : emitters) {
			const auto  k = static_cast<std::size_t>(e.sound);
			const auto& info = kSounds[k];
			e.energy *= fade;
			if (clock < e.nextAt || e.energy < info.threshold || tokens[k] < 1.0f) {
				continue;
			}
			if ((e.position - a_listener).SqrLength() < kHearing * kHearing) {
				const float loud = std::clamp(0.35f + 0.2f * std::sqrt(e.energy), 0.35f, 1.0f);
				Play(k, e.position, std::min(volume[k] * loud, 1.0f));
				tokens[k] -= 1.0f;
			}
			e.energy = 0.0f;
			e.nextAt = clock + info.interval * Random(0.8f, 1.3f);
		}
		std::erase_if(emitters, [](const Emitter& a_e) {
			return clock - a_e.lastAt > 1.5f && a_e.energy < kSounds[static_cast<std::size_t>(a_e.sound)].threshold;
		});
	}

	void Reset()
	{
		emitters.clear();
	}
}
