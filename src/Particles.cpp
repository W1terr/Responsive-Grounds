#include "Particles.h"

#include "Settings.h"
#include "Sounds.h"
#include "Terrain.h"

namespace Particles
{
	namespace
	{
		using Vec = RE::NiPoint3;
		using Settings::Kind;
		using Settings::Flutters;
		using Settings::IsLeaves;
		using Settings::kKinds;

		constexpr auto kLeafModel = "ResponsiveGrounds\\Leaves.nif";        // Leaf0 ... Leaf7: 2 shapes x natural, golden, red, brown; GreenLeaf0 ... 7 the same green
		constexpr auto kDebrisModel = "ResponsiveGrounds\\Particles.nif";   // RockPebble0-3, DirtClod0-3, RockDust0-1, DirtDust0-3, SnowFlake0-3

		constexpr float kMaxAirTime = 9.0f;
		constexpr float kSettleTime = 0.25f;   // landing: turning flat onto the ground
		constexpr float kFadeTime = 0.8f;      // shrinking into the ground
		constexpr float kForget = 3000.0f;     // particles this far from you vanish
		constexpr float kGroundCheck = 0.12f;  // seconds between ground height checks of a flying particle
		constexpr float kMaxSpin = 24.0f;      // rad/s
		constexpr float kAppearTime = 0.18f;   // a new leaf or clod grows in instead of popping up

		// the flow of a mover's kicks: eases in after the start delay, out when it stops
		constexpr float kFlowIn = 1.1f;   // seconds (time constant)
		constexpr float kFlowOut = 0.25f;
		constexpr float kStepRelease = 0.14f;  // a footfall's kicks come out over this long (the push off)

		// trodden ground: leaves kicked from a patch leave fewer there; it fills up again slowly
		constexpr float kPatchSize = 48.0f;
		constexpr float kPatchTake = 0.12f;     // per leaf
		constexpr float kPatchRecover = 25.0f;  // seconds for what was taken to fall to a third

		// per second over full cover at a run, amount 1: leaves, dust and rock kicks (1-2 clumps + maybe a puff)
		// per kind: leaves, green leaves, dust (dirt), rocks, snow (2-3 flakes a kick)
		constexpr std::array<float, kKinds> kBaseRate{ 34.0f, 30.0f, 22.0f, 20.0f, 26.0f };
		constexpr std::array<float, kKinds> kPuffChance{ 0.0f, 0.0f, 0.35f, 0.35f, 0.0f };  // a puff with a kick

		enum class Style : std::uint8_t
		{
			kLeaf,
			kClump,
			kPuff  // a card turned to the camera, drifting with the air, growing and fading: dust
		};

		// how a puff drifts and looks
		struct PuffLook
		{
			float drag;     // per second, towards the air's speed
			float gravity;
			float alpha;    // at its peak
			float grow0;    // size at the start ...
			float grow1;    // ... and grown by this at the end
		};
		constexpr PuffLook kDustLook{ 3.0f, 10.0f, 0.45f, 0.5f, 1.5f };   // dust hangs and grows
		constexpr PuffLook kRockLook{ 3.0f, 15.0f, 0.55f, 0.5f, 1.5f };

		struct Flight
		{
			float dragFace;     // air drag across the face (per second) ...
			float dragEdge;     // ... and along it: a tilted leaf glides, an edge-on one drops
			float gravity;
			float turn;         // how hard the air turns the face into the flow (leaves: the flutter)
			float tumble;       // random turning from fast air
			float spinDamping;  // per second
		};
		constexpr Flight kLeafFlight{ 6.0f, 1.2f, 320.0f, 0.07f, 0.04f, 1.6f };  // flat leaf falls at ~55 units/s
		constexpr Flight kFlakeFlight{ 9.0f, 3.0f, 250.0f, 0.04f, 0.09f, 1.2f };  // a snow flake: ~28 units/s, tumbling
		constexpr Flight kClumpFlight{ 0.9f, 0.7f, 900.0f, 0.0f, 0.0f, 0.3f };   // heavy: arcs and drops

		// ---- small math

		struct Quat
		{
			float w{ 1.0f }, x{ 0.0f }, y{ 0.0f }, z{ 0.0f };
		};

		Quat Mul(const Quat& a, const Quat& b)
		{
			return { a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
				a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
				a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
				a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w };
		}

		Quat Normalized(const Quat& q)
		{
			const float len = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
			return len > 1e-6f ? Quat{ q.w / len, q.x / len, q.y / len, q.z / len } : Quat{};
		}

		Quat AxisAngle(const Vec& a_axis, float a_angle)
		{
			const float s = std::sin(a_angle * 0.5f);
			return { std::cos(a_angle * 0.5f), a_axis.x * s, a_axis.y * s, a_axis.z * s };
		}

		Vec Rotate(const Quat& q, const Vec& v)
		{
			const Vec u{ q.x, q.y, q.z };
			const Vec t = u.Cross(v) * 2.0f;
			return v + t * q.w + u.Cross(t);
		}

		// the shortest turn taking unit vector a to unit vector b
		Quat Between(const Vec& a, const Vec& b)
		{
			const Vec   c = a.Cross(b);
			const float d = a.Dot(b);
			if (d < -0.9999f) {
				return { 0.0f, 1.0f, 0.0f, 0.0f };
			}
			return Normalized({ 1.0f + d, c.x, c.y, c.z });
		}

		Quat Nlerp(const Quat& a, Quat b, float t)
		{
			if (a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0.0f) {
				b = { -b.w, -b.x, -b.y, -b.z };
			}
			return Normalized({ a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t });
		}

		RE::NiMatrix3 ToMatrix(const Quat& q)
		{
			RE::NiMatrix3 m;
			const float   xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
			const float   xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
			const float   wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
			m.entry[0][0] = 1 - 2 * (yy + zz);
			m.entry[0][1] = 2 * (xy - wz);
			m.entry[0][2] = 2 * (xz + wy);
			m.entry[1][0] = 2 * (xy + wz);
			m.entry[1][1] = 1 - 2 * (xx + zz);
			m.entry[1][2] = 2 * (yz - wx);
			m.entry[2][0] = 2 * (xz - wy);
			m.entry[2][1] = 2 * (yz + wx);
			m.entry[2][2] = 1 - 2 * (xx + yy);
			return m;
		}

		// a card's front (+Z) turned to the camera, rolled around the view direction
		RE::NiMatrix3 Billboard(const Vec& a_pos, const Vec& a_camera, float a_roll)
		{
			Vec z = a_camera - a_pos;
			if (z.Unitize() < 1e-3f) {
				z = { 0.0f, 0.0f, 1.0f };
			}
			Vec x = Vec{ 0.0f, 0.0f, 1.0f }.Cross(z);
			if (x.Unitize() < 1e-3f) {
				x = { 1.0f, 0.0f, 0.0f };
			}
			const Vec     y = z.Cross(x);
			const float   c = std::cos(a_roll), s = std::sin(a_roll);
			const Vec     rx = x * c + y * s;
			const Vec     ry = y * c - x * s;
			RE::NiMatrix3 m;
			m.entry[0][0] = rx.x;
			m.entry[1][0] = rx.y;
			m.entry[2][0] = rx.z;
			m.entry[0][1] = ry.x;
			m.entry[1][1] = ry.y;
			m.entry[2][1] = ry.z;
			m.entry[0][2] = z.x;
			m.entry[1][2] = z.y;
			m.entry[2][2] = z.z;
			return m;
		}

		float Smooth(float a_edge0, float a_edge1, float a_x)
		{
			const float t = std::clamp((a_x - a_edge0) / (a_edge1 - a_edge0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		Vec Flat(const Vec& v) { return { v.x, v.y, 0.0f }; }

		const Vec kUp{ 0.0f, 0.0f, 1.0f };

		// ---- random

		std::mt19937 rng{ std::random_device{}() };

		float Random(float a_min, float a_max) { return std::uniform_real_distribution<float>(a_min, a_max)(rng); }
		float Gauss(float a_sigma) { return std::normal_distribution<float>(0.0f, a_sigma)(rng); }

		Vec RandomUnit()
		{
			Vec v{ Gauss(1.0f), Gauss(1.0f), Gauss(1.0f) };
			return v.Unitize() > 1e-4f ? v : kUp;
		}

		Quat RandomRotation() { return Normalized({ Gauss(1.0f), Gauss(1.0f), Gauss(1.0f), Gauss(1.0f) }); }

		// ---- particles

		enum class Phase : std::uint8_t
		{
			kFree,
			kAir,
			kSettle,
			kGround,
			kFade
		};

		struct Particle
		{
			RE::NiPointer<RE::NiAVObject>  node;
			RE::BSLightingShaderProperty*  shader{ nullptr };  // puffs: its own material, for fading (owned by node)
			Phase                          phase{ Phase::kFree };
			Vec                            pos;
			Vec                            vel;
			Vec                            spin;  // world angular velocity, rad/s
			Quat                           rot;
			Quat                           settleFrom;
			Quat                           restRot;
			float                          restZ{ 0.0f };
			float                          settleFromZ{ 0.0f };
			float                          ground{ 0.0f };
			float                          groundTimer{ 0.0f };
			float                          timer{ 0.0f };    // time in the current phase
			float                          restFor{ 0.0f };  // seconds on the ground before fading
			float                          size{ 1.0f };
			float                          scale{ 1.0f };    // size, growing (puffs) or shrinking (fading)
			float                          seed[3]{};
			float                          swirlSign{ 1.0f };
			float                          life{ 1.0f };     // puffs
			float                          roll{ 0.0f };
			float                          rollSpeed{ 0.0f };
			float                          alpha{ 1.0f };
			float                          appear{ 1.0f };  // 0 just made .. 1 full size
			bool                           bounced{ false };
			bool                           water{ false };  // its ground is a water surface: it floats there
			float                          rayAt{ 0.0f };   // when its next ground ray is due at the latest
		};

		struct Pool
		{
			const char*                                name;
			Kind                                       kind;
			Style                                      style;
			std::vector<RE::NiPointer<RE::NiAVObject>> templates;
			std::vector<Particle>                      items;
			std::size_t                                capacity{ 0 };
			std::size_t                                active{ 0 };  // particles in use
			std::size_t                                used{ 0 };    // slots below this may be in use (free slots are taken lowest first)
			bool                                       reported{ false };
		};

		enum PoolIndex : std::size_t
		{
			kLeafPool,
			kGreenPool,
			kDirtClods,
			kDirtDust,
			kRockPebbles,
			kRockDust,
			kSnowFlakes,
			kPools
		};

		std::array<Pool, kPools> pools{ {
			{ "leaves", Kind::kLeaves, Style::kLeaf },
			{ "green leaves", Kind::kGreen, Style::kLeaf },
			{ "dirt clods", Kind::kDirt, Style::kClump },
			{ "dirt dust", Kind::kDirt, Style::kPuff },
			{ "rock pebbles", Kind::kRock, Style::kClump },
			{ "rock dust", Kind::kRock, Style::kPuff },
			{ "snow flakes", Kind::kSnow, Style::kLeaf },  // fly and land like little leaves, melt soon
		} };

		const PuffLook& LookOf(const Pool& a_pool)
		{
			return a_pool.kind == Kind::kRock ? kRockLook : kDustLook;
		}

		constexpr std::array<PoolIndex, kKinds> kMainPool{ kLeafPool, kGreenPool, kDirtClods, kRockPebbles, kSnowFlakes };
		constexpr std::array<PoolIndex, kKinds> kPuffPool{ kPools, kPools, kDirtDust, kRockDust, kPools };

		std::vector<RE::NiPointer<RE::NiNode>> models;     // the loaded models, kept so their shapes keep their parent
		RE::NiPointer<RE::NiNode>              container;  // our node in the scene, parent of every particle

		// ---- weapon swings (data)

		// Animation events start a swing window: vanilla "weaponSwing" / "weaponLeftSwing", and "preHitFrame" /
		// "HitFrame" (any case), which MCO / SCAR attacks send even when they leave out weaponSwing. Precision only
		// replaces the hit detection, the events stay. How strong the gust is comes from the weapon's own motion.
		constexpr float kSwingWindow = 0.6f;      // seconds after an event the blades are watched
		constexpr float kSwingMinSpeed = 250.0f;  // tip speed (units/s) where the air starts to move
		constexpr float kSwingFullSpeed = 850.0f;
		constexpr float kSwingReach = 35.0f;      // how far from the blade the air pushes

		struct Blade
		{
			bool  valid{ false };
			Vec   base;  // the hand (weapon node)
			Vec   tip;   // the far end of what it holds (the hand for fists)
			Vec   baseVel;
			Vec   tipVel;
			float strength{ 0.0f };  // 0 slow .. 1 full speed (times the menu's strength)
		};

		// ---- what holds an actor up (data)

		struct SupportInfo
		{
			bool        object{ false };        // the support body belongs to an object (a reference): a rock, a cliff, a street piece
			bool        layerTerrain{ false };  // its collision layer says landscape
			int         layer{ 0 };             // for the log
			int         material{ -1 };         // an object: Terrain::Materials() index (-1: none of them)
			std::string model;                  // the object's model, for the log
			std::string layerColor;             // its painted on layer's color, for the log
			bool        groundLike{ false };    // ground to walk on at any height (landscape pieces, rocks, cliffs, streets), not a thing standing on it

			// landscape textures on the object (Terrain file index, share of the object), shapes whose texture names
			// tell what they are (Terrain::Materials() index, share: a dirt cliff's face, a mountain's slabs), and the
			// share of the rest (that goes by the collision material under your feet)
			std::vector<std::pair<int, float>> textures;
			std::vector<std::pair<int, float>> named;
			float                              otherShare{ 1.0f };
			std::string                        texture;  // the biggest of them, for the status
		};

		struct SupportCache
		{
			const RE::hkpRigidBody* body{ nullptr };
			RE::MATERIAL_ID         material{ RE::MATERIAL_ID::kNone };
			SupportInfo             info;
		};

		// ---- movers: you and the NPCs and creatures near you whose steps, attacks and wake move things

		struct Mover
		{
			RE::FormID  formID{ 0 };
			bool        player{ false };
			const void* watchedRoot{ nullptr };  // the 3D its animation event sink was added for
			RE::Actor*  actor{ nullptr };        // this frame's (looked up once)

			// motion this frame
			bool  hasLast{ false };
			Vec   lastPos;
			Vec   velocity;  // smoothed
			float rawSpeed{ 0.0f };
			Vec   pos;  // its feet (or your horse's)
			Vec   dir{ 0.0f, 1.0f, 0.0f };  // horizontal direction of travel
			Vec   side{ 1.0f, 0.0f, 0.0f };  // to the right of it
			float speed{ 0.0f };
			float kick{ 0.0f };  // 0 standing .. ~1 running .. 1.8 galloping: how hard the ground gets kicked
			float kickSmooth{ 0.0f };  // the same, following changes of pace softly
			float flow{ 0.0f };        // 0 .. 1: kicks easing in and out
			float stepPhase{ 0.0f };   // 0 .. 1 to its next footfall
			float foot{ 1.0f };        // the side of the foot landing (+1 right, -1 left)
			float wanderSeed{ 0.0f };  // its own slow ups and downs of how much a step kicks up
			bool  mounted{ false };
			float footprint{ 1.0f };  // how far apart its feet land (a person 1, a horse ~2.6, a giant ~3)
			bool  awake{ false };     // moving, swinging or just landed this frame: worked out in full

			// the ground
			bool            onLand{ false };
			bool            onObject{ false };
			bool            raised{ false };  // up on an object that isn't ground (a log pile): kicks up nothing
			bool            supported{ false };
			bool            swimming{ false };
			bool            canKick{ false };
			bool            kicking{ false };
			float           groundZ{ 0.0f };
			int             materialIndex{ -1 };
			Terrain::Covers covers{};
			Terrain::Covers objectCovers{};
			SupportCache    support;

			// timers and amounts owed
			float                     airborneFor{ 0.0f };
			float                     kickingFor{ 0.0f };     // how long its steps have been on kickable ground
			float                     notKickingFor{ 0.0f };
			std::array<float, kKinds> emitDebt{};
			std::array<float, kKinds> stepLeft{};  // what the last footfall still has to kick up
			std::array<float, kKinds> stepRate{};  // per second while it does
			std::array<float, kKinds> swingDebt{};

			// attacks
			std::array<Blade, 2> blades{};  // right (WEAPON node), left (SHIELD node)
			float                swingUntil{ -100.0f };
			float                slamReady{ 0.0f };
			float                shoutReady{ 0.0f };
			float                hitFrameAt{ -100.0f };  // its attack's hit frame (a big creature's blow lands then)
		};

		std::vector<std::unique_ptr<Mover>> movers;          // [0]: you
		Mover*                              mover{ nullptr };  // whose kick is being made right now
		float                               selectTimer{ 0.0f };

		// ---- shock waves: shouts (a cone) and heavy blows on the ground (a ring)

		struct Wave
		{
			Vec                       origin;  // on the ground
			Vec                       dir;     // a cone's direction (horizontal)
			float                     cosHalf{ -1.0f };  // a cone's half angle (cosine); -1: all around
			float                     speed{ 1000.0f };
			float                     reach{ 500.0f };
			float                     front{ 0.0f };
			float                     prevFront{ 0.0f };
			float                     strength{ 1.0f };
			std::array<float, kKinds> debt{};
		};
		std::vector<Wave> waves;

		Settings::Values tuning;  // this frame's copy (without the texture lists' use)
		std::uint32_t    settingsRevision{ 0 };
		float            clock{ 0.0f };
		float            statusTimer{ 0.0f };
		float            logTimer{ 0.0f };
		bool             wasExterior{ false };
		bool             lastOnLand{ false };
		Vec              wind;
		Vec              cameraPos;
		Vec              cameraFwd{ 0.0f, 1.0f, 0.0f };  // horizontal
		std::string      objectName;         // what you stand on, for the status: a material ...
		std::string      objectTexture;      // ... or the landscape texture painted on it
		bool             raised{ false };    // up on something that isn't ground

		std::atomic<bool> testRequested{ false };

		// animation events and spell casts arrive on other threads: kept here until the next frame
		std::mutex              signalLock;
		std::vector<RE::FormID> swingSignals;
		std::vector<RE::FormID> shoutSignals;
		std::vector<RE::FormID> hitSignals;
		std::vector<RE::FormID> deathSignals;

		struct Blast
		{
			Vec   at;
			float radius;
		};
		std::vector<Blast> blastSignals;  // explosions from the hook (any thread) ...
		std::vector<Blast> blasts;        // ... and the ones this frame still has to send out

		// bodies of the ones just killed, watched until they hit the ground
		struct Corpse
		{
			RE::FormID formID{ 0 };
			float      until{ 0.0f };
			bool       hasLast{ false };
			float      lastZ{ 0.0f };
			float      fallSpeed{ 0.0f };  // smoothed downward speed
			float      readyAt{ 0.0f };
			int        impacts{ 0 };
		};
		std::vector<Corpse> corpses;

		// the wind: the weather's (Dynamic Wind sets it), gusting
		float windStrength{ 0.0f };  // 0..1
		Vec   windDir{ 0.0f, 1.0f, 0.0f };
		float windGust{ 1.0f };
		std::array<float, kKinds> windDebt{};
		float                     WindLift();  // how much the wind lifts this frame (below)

		std::mutex status_lock;
		Status     status;

		bool Active(const Particle& a_p) { return a_p.phase != Phase::kFree; }

		// what the menu's status shows, counted while moving the particles
		std::array<int, kKinds> flyingCount{};
		std::array<int, kKinds> landedCount{};

		void Hide(Pool& a_pool, Particle& a_p)
		{
			if (a_p.phase != Phase::kFree && a_pool.active > 0) {
				--a_pool.active;
			}
			a_p.phase = Phase::kFree;
			// out of the scene: the frame's scene update only walks the particles that are shown
			if (a_p.node && a_p.node->parent) {
				a_p.node->parent->DetachChild(a_p.node.get());
			}
		}

		std::size_t ActiveTotal()
		{
			std::size_t total = 0;
			for (const auto& pool : pools) {
				total += pool.active;
			}
			return total;
		}

		// which shape a new leaf gets: columns natural, golden, red, brown (green, yellow green, dark green, olive); rows
		// two leaf shapes
		std::size_t PickTemplate(const Pool& a_pool)
		{
			const auto count = a_pool.templates.size();
			if (a_pool.style != Style::kLeaf || count != 8) {
				return std::uniform_int_distribution<std::size_t>(0, count - 1)(rng);
			}
			static constexpr std::array<float, 4> kWeights{ 0.32f, 0.30f, 0.20f, 0.18f };
			float       r = Random(0.0f, 1.0f);
			std::size_t column = 0;
			while (column < 3 && r > kWeights[column]) {
				r -= kWeights[column];
				++column;
			}
			return column + (Random(0.0f, 1.0f) < 0.5f ? 0 : 4);
		}

		RE::BSLightingShaderProperty* LightingOf(RE::NiAVObject* a_object)
		{
			const auto geometry = a_object ? netimmerse_cast<RE::BSGeometry*>(a_object) : nullptr;
			const auto property = geometry ? geometry->GetGeometryRuntimeData().shaderProperty.get() : nullptr;
			return property ? netimmerse_cast<RE::BSLightingShaderProperty*>(property) : nullptr;
		}

		bool MakeNode(Pool& a_pool, Particle& a_p)
		{
			if (a_p.node) {
				return true;
			}
			if (a_pool.templates.empty() || !container) {
				return false;
			}
			const auto& source = a_pool.templates[PickTemplate(a_pool)];
			const auto  clone = source ? source->Clone() : nullptr;
			const auto  object = clone ? netimmerse_cast<RE::NiAVObject*>(clone) : nullptr;
			if (!object) {
				return false;
			}
			a_p.node.reset(object);  // joins the scene when shown
			if (a_pool.style == Style::kPuff) {
				// a material of its own, so each puff fades by itself
				const auto mine = LightingOf(object);
				const auto theirs = LightingOf(source.get());
				if (mine && theirs && mine != theirs && theirs->material) {
					mine->SetMaterial(theirs->material, true);
					if (mine->material && mine->material != theirs->material) {
						a_p.shader = mine;
					}
				}
				if (!a_pool.reported) {
					a_pool.reported = true;
					logs::info("{}: {}", a_pool.name,
						a_p.shader             ? "each puff has its own material (fades out)" :
						mine && mine == theirs ? "copies share the shader property: puffs shrink instead of fading" :
												 "no own material: puffs shrink instead of fading");
				}
			}
			return true;
		}

		// a free slot, else the one that has waited longest (landed, or the oldest puff)
		Particle* Acquire(Pool& a_pool)
		{
			const auto count = std::min(a_pool.items.size(), a_pool.capacity);
			Particle*  oldest = nullptr;
			float      oldestAge = -1.0f;
			for (std::size_t i = 0; i < count; ++i) {
				auto& p = a_pool.items[i];
				if (p.phase == Phase::kFree) {
					return MakeNode(a_pool, p) ? &p : nullptr;
				}
				float age = -1.0f;
				if (a_pool.style == Style::kPuff) {
					age = p.timer / std::max(p.life, 0.1f);
				} else if (p.phase == Phase::kGround || p.phase == Phase::kFade) {
					age = p.timer;
				}
				if (age > oldestAge) {
					oldestAge = age;
					oldest = &p;
				}
			}
			return oldestAge >= 0.0f ? oldest : nullptr;
		}

		// the water surface over a point, if there is water above a_above (rivers, lakes, the sea)
		bool WaterAt(const Vec& a_pos, float a_above, float& a_water)
		{
			const auto tes = RE::TES::GetSingleton();
			const auto cell = tes ? tes->GetCell(a_pos) : nullptr;
			float      height = 0.0f;
			if (!cell || !cell->GetWaterHeight(a_pos, height) || height <= a_above || height > a_pos.z + 400.0f) {
				return false;
			}
			a_water = height;
			return true;
		}

		float LandHeight(const Vec& a_pos, float a_fallback)
		{
			float      z = a_fallback;
			const auto tes = RE::TES::GetSingleton();
			if (tes && tes->GetLandHeight(a_pos, z)) {
				return z;
			}
			return a_fallback;
		}

		Terrain::Covers CoverAt(const Vec& a_at) { return mover->onObject ? mover->objectCovers : Terrain::Cover(a_at); }

		// The real surface under a point from the live collision (rocks, roots, roads, the landscape): a ray straight
		// down that passes through the body of whoever kicks (you, your horse, the NPC). Without a hit (nothing loaded
		// there), the land's height.
		RE::bhkWorld* havokWorld{ nullptr };  // this frame's
		std::uint32_t rayFilter{ 0 };         // the current mover's
		std::uint32_t yourRayFilter{ 0 };     // yours, or your horse's while you ride (falling particles, wind, waves)

		// a ray filter that passes through an actor's own body, against what blocks sight (statics, the landscape)
		std::uint32_t RayFilterOf(RE::Actor* a_actor)
		{
			RE::CFilter filter{};
			a_actor->GetCollisionFilterInfo(filter);
			return (filter.filter & 0xFFFF0000) | static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);
		}

		struct Surface
		{
			bool  hit{ false };
			float z{ 0.0f };
			Vec   normal{ 0.0f, 0.0f, 1.0f };
		};

		Surface CastDown(const Vec& a_from, float a_length)
		{
			Surface result;
			if (!havokWorld) {
				return result;
			}
			const float     scale = RE::bhkWorld::GetWorldScale();
			const Vec       to = a_from - Vec{ 0.0f, 0.0f, a_length };
			RE::bhkPickData pick{};
			pick.rayInput.from = RE::hkVector4(a_from.x * scale, a_from.y * scale, a_from.z * scale, 0.0f);
			pick.rayInput.to = RE::hkVector4(to.x * scale, to.y * scale, to.z * scale, 0.0f);
			pick.rayInput.filterInfo.filter = rayFilter;
			if (havokWorld->PickObject(pick) && pick.rayOutput.HasHit()) {
				result.hit = true;
				result.z = a_from.z - a_length * pick.rayOutput.hitFraction;
				alignas(16) float n[4];
				_mm_store_ps(n, pick.rayOutput.normal.quad);
				Vec normal{ n[0], n[1], n[2] };
				if (normal.Unitize() > 0.5f && normal.z > 0.2f) {
					result.normal = normal;
				}
			}
			return result;
		}

		// how high to start a kick at a spot around the current mover
		float SurfaceAt(const Vec& a_at)
		{
			// on a horse from below your feet in the stirrups (the horse's own body is passed through)
			const float above = mover->mounted ? 50.0f : 40.0f * mover->footprint;
			const auto  hit = CastDown({ a_at.x, a_at.y, mover->pos.z + above }, above + 100.0f * mover->footprint);
			return hit.hit ? hit.z : LandHeight(a_at, mover->pos.z);
		}

		// the ground at a spot anywhere (shock waves): the land, or what lies on it
		float GroundAt(const Vec& a_at, float a_fallback)
		{
			const float land = LandHeight(a_at, a_fallback);
			const auto  hit = CastDown({ a_at.x, a_at.y, land + 150.0f }, 300.0f);
			return hit.hit ? hit.z : land;
		}

		// the ground under a falling particle
		float GroundUnder(const Particle& a_p)
		{
			const auto hit = CastDown(a_p.pos + Vec{ 0.0f, 0.0f, 15.0f }, 400.0f);
			return hit.hit ? hit.z : LandHeight(a_p.pos, a_p.ground);
		}

		void Seed(Particle& a_p)
		{
			for (auto& s : a_p.seed) {
				s = Random(0.0f, 6.2832f);
			}
		}

		void Show(Pool& a_pool, Particle& a_p, float a_sideSign)
		{
			if (a_p.phase == Phase::kFree) {
				++a_pool.active;
				a_pool.used = std::max(a_pool.used, static_cast<std::size_t>(&a_p - a_pool.items.data()) + 1);
				if (!a_p.node->parent && container) {
					container->AttachChild(a_p.node.get(), true);
				}
			}
			a_p.swirlSign = a_sideSign != 0.0f ? a_sideSign : (Random(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f);
			a_p.phase = Phase::kAir;
			a_p.timer = 0.0f;
			a_p.groundTimer = 0.0f;
			a_p.rayAt = 0.0f;
			a_p.bounced = false;
			a_p.water = false;
			a_p.alpha = 1.0f;
			a_p.appear = 1.0f;
			a_p.node->SetAppCulled(false);
		}

		// sends a leaf (new or lying on the ground) up into the air
		void LaunchLeaf(Pool& a_pool, Particle& a_p, float a_kick, float a_sideSign, bool a_fresh)
		{
			const float kick = std::max(a_kick, 0.15f);
			const float lift = (70.0f + 140.0f * kick) * Random(0.55f, 1.25f) * tuning.lift;
			const float forward = mover->speed * Random(0.1f, 0.5f);
			const float sideways = a_sideSign * Random(8.0f, 40.0f) * (0.4f + kick);
			a_p.vel = mover->dir * forward + mover->side * sideways + kUp * lift + Vec{ Gauss(12.0f), Gauss(12.0f), 0.0f };
			a_p.spin = RandomUnit() * (Random(3.0f, 11.0f) * (0.5f + kick));
			if (a_fresh) {
				// a leaf that lay on the ground: about flat, any heading, either side up
				Quat r = AxisAngle(kUp, Random(0.0f, 6.2832f));
				if (Random(0.0f, 1.0f) < 0.35f) {
					r = Mul(r, AxisAngle({ 0.0f, 1.0f, 0.0f }, 3.1416f));
				}
				const float tiltAngle = Random(0.0f, 6.2832f);
				a_p.rot = Normalized(Mul(AxisAngle({ std::cos(tiltAngle), std::sin(tiltAngle), 0.0f }, Gauss(0.35f)), r));
				a_p.size = (a_pool.kind == Kind::kSnow ? Random(0.5f, 1.4f) : Random(0.8f, 1.25f)) * tuning.kinds[a_pool.kind].size;
				Seed(a_p);
			}
			a_p.scale = a_p.size;
			a_p.pos.z = std::max(a_p.pos.z, a_p.ground + 0.5f);
			Show(a_pool, a_p, a_sideSign);
			if (a_fresh) {
				a_p.appear = 0.0f;
			}
			if (IsLeaves(a_pool.kind)) {
				Sounds::Request(Sounds::Sound::kLeaves, a_p.pos, a_fresh ? 1.0f : 0.6f);
			}
		}

		// a dirt clod or rock pebble flicked up by the foot: carried forward with it, thrown up and a little sideways
		void LaunchClump(Pool& a_pool, Particle& a_p, Kind a_kind, float a_kick, float a_sideSign)
		{
			const float kick = std::max(a_kick, 0.15f);
			a_p.vel = mover->dir * (mover->speed * Random(0.15f, 0.75f)) + mover->side * ((a_sideSign * Random(5.0f, 25.0f) + Gauss(10.0f)) * (0.5f + kick)) +
			          kUp * ((60.0f + 190.0f * kick) * Random(0.5f, 1.2f) * tuning.lift);
			a_p.spin = RandomUnit() * Random(6.0f, 22.0f);
			a_p.rot = RandomRotation();
			a_p.size = Random(0.6f, 1.4f) * tuning.kinds[a_kind].size;
			a_p.scale = a_p.size;
			a_p.pos.z = a_p.ground + 1.0f;
			Seed(a_p);
			Show(a_pool, a_p, a_sideSign);
			a_p.appear = 0.0f;
		}

		// a puff of dust rising from the kick
		void LaunchPuff(Pool& a_pool, Particle& a_p, Kind a_kind, float a_kick)
		{
			const float kick = std::max(a_kick, 0.15f);
			a_p.pos.z = a_p.ground + Random(2.0f, 5.0f);
			a_p.vel = mover->dir * (mover->speed * Random(0.1f, 0.35f)) + mover->side * Gauss(12.0f) + kUp * (Random(10.0f, 40.0f) * (0.5f + kick) * tuning.lift);
			a_p.life = Random(1.0f, 2.0f);
			a_p.size = Random(0.7f, 1.2f) * (0.7f + 0.3f * std::min(kick, 1.5f)) * tuning.kinds[a_kind].size;
			a_p.scale = a_p.size * 0.5f;
			a_p.roll = Random(0.0f, 6.2832f);
			a_p.rollSpeed = Random(-0.6f, 0.6f);
			Seed(a_p);
			Show(a_pool, a_p, 0.0f);
			a_p.alpha = 0.0f;
		}

		// ---- trodden ground

		struct Patch
		{
			float taken{ 0.0f };
			float at{ 0.0f };
		};
		std::unordered_map<std::int64_t, Patch> patches;
		float                                   patchPrune{ 0.0f };

		std::int64_t PatchKey(const Vec& a_at)
		{
			const auto x = static_cast<std::int64_t>(std::floor(a_at.x / kPatchSize));
			const auto y = static_cast<std::int64_t>(std::floor(a_at.y / kPatchSize));
			return (x << 32) ^ (y & 0xFFFFFFFF);
		}

		float Taken(const Patch& a_p) { return a_p.taken * std::exp(-(clock - a_p.at) / kPatchRecover); }

		// how many leaves a patch still has (1 untouched)
		float LeavesLeft(const Vec& a_at)
		{
			const auto it = patches.find(PatchKey(a_at));
			return it == patches.end() ? 1.0f : 1.0f / (1.0f + Taken(it->second));
		}

		void TakeLeaf(const Vec& a_at)
		{
			auto& patch = patches[PatchKey(a_at)];
			patch.taken = Taken(patch) + kPatchTake;
			patch.at = clock;
		}

		void PrunePatches(float a_dt)
		{
			if ((patchPrune -= a_dt) > 0.0f) {
				return;
			}
			patchPrune = 3.0f;
			std::erase_if(patches, [](const auto& a_entry) { return Taken(a_entry.second) < 0.03f; });
		}

		// one kick of a kind from a spot around the current mover's feet, if the ground there has it
		void Kick(Kind a_kind, const Vec& a_at, float a_kick, float a_sideSign, bool a_anyGround)
		{
			if (!a_anyGround && Random(0.0f, 1.0f) >= CoverAt(a_at)[a_kind] * (IsLeaves(a_kind) ? LeavesLeft(a_at) : 1.0f)) {
				return;
			}
			const float ground = SurfaceAt(a_at);
			if (float water = 0.0f; WaterAt(a_at, ground + 2.0f, water)) {
				return;  // under water: nothing flies up
			}
			if (IsLeaves(a_kind) && !a_anyGround) {
				TakeLeaf(a_at);
			}
			const auto  mainIndex = kMainPool[a_kind];
			const int   count = a_kind == Kind::kSnow ? (Random(0.0f, 1.0f) < 0.5f ? 3 : 2) : IsLeaves(a_kind) ? 1 : (Random(0.0f, 1.0f) < 0.4f ? 2 : 1);
			const float spread = a_kind == Kind::kSnow ? 4.0f : 2.0f;
			for (int i = 0; i < count; ++i) {
				auto&      main = pools[mainIndex];
				const auto p = Acquire(main);
				if (!p || !p->node) {
					break;
				}
				p->pos = { a_at.x + Gauss(spread), a_at.y + Gauss(spread), ground + 0.5f };
				p->ground = ground;
				if (Flutters(a_kind)) {
					LaunchLeaf(main, *p, a_kick, a_sideSign, true);
				} else {
					LaunchClump(main, *p, a_kind, a_kick, a_sideSign);
				}
			}
			if (!Flutters(a_kind) && tuning.puffs && Random(0.0f, 1.0f) < kPuffChance[a_kind]) {
				auto&      puffs = pools[kPuffPool[a_kind]];
				const auto p = Acquire(puffs);
				if (p && p->node) {
					p->pos = a_at;
					p->ground = ground;
					LaunchPuff(puffs, *p, a_kind, a_kick);
				}
			}
		}

		// a spot where the current mover's landing foot pushes off: mostly a little ahead of it, sometimes flicked out
		// to its side, never right under its legs
		void SpawnStep(Kind a_kind, float a_kick, float a_foot)
		{
			const float fp = mover->footprint;
			float       along = 0.0f;
			float       across = 0.0f;
			if (Random(0.0f, 1.0f) < 0.7f) {
				along = Random(4.0f, 24.0f) * fp;
				across = a_foot * Random(3.0f, 13.0f) * fp + Gauss(3.0f * fp);
			} else {
				along = Random(-6.0f, 12.0f) * fp;
				across = a_foot * Random(10.0f, 24.0f) * fp;
			}
			Kick(a_kind, mover->pos + mover->dir * along + mover->side * across, a_kick, across >= 0.0f ? 1.0f : -1.0f, false);
		}

		// a burst all around the mover (landing from a jump or a fall, the menu's test)
		void Burst(Kind a_kind, int a_count, bool a_anyGround)
		{
			for (int i = 0; i < a_count; ++i) {
				const float angle = Random(0.0f, 6.2832f);
				const float radius = Random(4.0f, 20.0f * mover->footprint);
				const Vec   out{ std::cos(angle), std::sin(angle), 0.0f };
				Kick(a_kind, mover->pos + out * radius, 0.7f, 0.0f, a_anyGround);
			}
		}

		// something of a kind thrown from a spot with a given push (shock waves): leaves and clumps, or a puff
		void Throw(Kind a_kind, const Vec& a_at, float a_ground, const Vec& a_push, float a_lift)
		{
			const auto mainIndex = kMainPool[a_kind];
			const bool hasPuff = tuning.puffs && kPuffPool[a_kind] != kPools;
			const bool puff = hasPuff && Random(0.0f, 1.0f) < 0.45f;
			if (!puff) {
				auto&      pool = pools[mainIndex];
				const auto p = Acquire(pool);
				if (!p || !p->node) {
					return;
				}
				p->pos = { a_at.x, a_at.y, a_ground + 0.5f };
				p->ground = a_ground;
				if (Flutters(a_kind)) {
					LaunchLeaf(pool, *p, 0.6f, 0.0f, true);
				} else {
					LaunchClump(pool, *p, a_kind, 0.6f, 0.0f);
				}
				p->vel = a_push * Random(0.6f, 1.1f) + kUp * (a_lift * Random(0.6f, 1.2f) * tuning.lift) + Vec{ Gauss(20.0f), Gauss(20.0f), 0.0f };
			} else {
				auto&      pool = pools[kPuffPool[a_kind]];
				const auto p = Acquire(pool);
				if (!p || !p->node) {
					return;
				}
				p->pos = { a_at.x, a_at.y, a_ground };
				p->ground = a_ground;
				LaunchPuff(pool, *p, a_kind, 1.0f);
				p->vel = a_push * 0.35f + kUp * Random(20.0f, 60.0f);
				p->size *= 1.4f;
			}
		}

		// one mover's wake at a particle: the air its body drags along, rising in the middle, swirling behind it
		void AddWake(const Mover& a_m, const Particle& a_p, Vec& a_air)
		{
			const float s = a_m.speed;
			if (s < 5.0f) {
				return;
			}
			const Vec   r = a_p.pos - a_m.pos;
			const Vec   flat = Flat(r);
			const float length = (80.0f + 0.35f * s) * a_m.footprint;  // how far back the wake reaches
			if (flat.SqrLength() > (6.0f * length + 100.0f) * (6.0f * length + 100.0f)) {
				return;
			}
			const float along = flat.Dot(a_m.dir);
			const float behind = -along;
			Vec         lateral = flat - a_m.dir * along;
			const float sideDist = lateral.Length();
			const Vec   sideDir = sideDist > 0.01f ? lateral * (1.0f / sideDist) : a_m.side;
			const float height = std::max(r.z, 0.0f);

			const float sigma = (12.0f + 0.3f * std::max(behind, 0.0f)) * a_m.footprint;  // and how wide it is (widening behind)
			const float w = std::exp(-std::max(behind, 0.0f) / length) * Smooth(-20.0f * a_m.footprint, 4.0f, behind) *
			                std::exp(-(sideDist * sideDist) / (2.0f * sigma * sigma)) * std::exp(-height / ((60.0f + 0.1f * s) * a_m.footprint));
			if (w < 0.002f) {
				return;
			}
			// the air its body drags along
			a_air += a_m.dir * (s * 0.75f * w);
			// rising in the middle of the wake, sinking at its edges (the pair of eddies behind a moving body)
			const float q = sideDist / sigma;
			a_air.z += s * 0.5f * w * (1.0f - 1.6f * q * q) * tuning.swirl;
			// flung outward once up
			a_air += sideDir * (s * 0.22f * w * std::clamp(height / (30.0f * a_m.footprint), 0.0f, 1.5f) * tuning.swirl);
			// circling a point trailing behind it
			const Vec   center = a_m.pos - a_m.dir * ((25.0f + 0.06f * s) * a_m.footprint);
			const Vec   around = Flat(a_p.pos - center);
			const float aroundDist = around.Length();
			if (aroundDist > 1.0f) {
				const Vec tangent{ -around.y / aroundDist * a_p.swirlSign, around.x / aroundDist * a_p.swirlSign, 0.0f };
				a_air += tangent * (s * 0.3f * w * tuning.swirl);
			}
		}

		// the air at a point: the weather's wind, gusts, and the wakes of everyone moving near it
		Vec Air(const Particle& a_p)
		{
			Vec air = wind;
			air += Vec{ std::sin(clock * 1.3f + a_p.seed[0]), std::sin(clock * 1.7f + a_p.seed[1]), 0.4f * std::sin(clock * 2.3f + a_p.seed[2]) } * 22.0f;
			if (tuning.wind && windStrength > 0.08f) {
				// turbulence: up and down drafts and sideways eddies, stronger the stronger the wind
				const Vec   across{ -windDir.y, windDir.x, 0.0f };
				const float w = windStrength * windGust;
				air.z += w * 110.0f * (0.6f * std::sin(clock * 2.1f + a_p.seed[0] * 3.0f) + 0.4f * std::sin(clock * 0.9f + a_p.seed[1] * 2.0f)) + w * 25.0f;
				air += across * (w * 60.0f * std::sin(clock * 1.3f + a_p.seed[2] * 2.0f));
			}
			for (const auto& m : movers) {
				if (m->awake) {
					AddWake(*m, a_p, air);
				}
			}
			return air;
		}

		// turned flat onto the ground (along the slope), keeping its heading and the side that faced up
		void Land(const Pool& a_pool, Particle& a_p)
		{
			const Vec  normal = Rotate(a_p.rot, kUp);
			const bool faceDown = normal.z < 0.0f;
			Vec        along = Flat(Rotate(a_p.rot, { 0.0f, 1.0f, 0.0f }));
			if (along.Unitize() < 1e-3f) {
				along = { 0.0f, 1.0f, 0.0f };
			}
			Quat rest = AxisAngle(kUp, std::atan2(-along.x, along.y));
			if (faceDown) {
				rest = Mul(rest, AxisAngle({ 0.0f, 1.0f, 0.0f }, 3.1416f));
			}
			const auto  surface = a_p.water ? Surface{} : CastDown(a_p.pos + Vec{ 0.0f, 0.0f, 15.0f }, 60.0f);
			const float h0 = a_p.water ? a_p.ground : surface.hit ? surface.z : LandHeight(a_p.pos, a_p.ground);
			Vec         slope = a_p.water ? kUp : surface.normal;
			if (!surface.hit && !a_p.water) {
				const float hx = LandHeight(a_p.pos + Vec{ 4.0f, 0.0f, 0.0f }, h0);
				const float hy = LandHeight(a_p.pos + Vec{ 0.0f, 4.0f, 0.0f }, h0);
				slope = { -(hx - h0) / 4.0f, -(hy - h0) / 4.0f, 1.0f };
				slope.Unitize();
			}
			a_p.restRot = Normalized(Mul(Between(kUp, slope), rest));
			// leaves are cupped (face down they stand on the rim), clumps domed (face up they stand on the rim)
			const bool  leaf = a_pool.style == Style::kLeaf;
			const float rim = a_p.water ? 0.25f : a_pool.kind == Kind::kSnow ? 0.2f : leaf ? (faceDown ? 1.0f * a_p.size : 0.35f) : (faceDown ? 0.15f : 0.55f * a_p.size);
			a_p.restZ = h0 + rim;
			a_p.ground = h0;
			a_p.settleFrom = a_p.rot;
			a_p.settleFromZ = a_p.pos.z;
			a_p.vel = {};
			a_p.spin = {};
			a_p.phase = Phase::kSettle;
			a_p.timer = 0.0f;
			switch (a_pool.kind) {
			case Kind::kLeaves:
			case Kind::kGreen:
				a_p.restFor = tuning.groundTime * (a_p.water ? Random(1.5f, 2.5f) : Random(0.7f, 1.3f));  // floating a while
				break;
			case Kind::kSnow:
				a_p.restFor = Random(0.3f, 0.9f);  // melts into the snow
				break;
			default:
				a_p.restFor = Random(1.0f, 2.5f);
				break;
			}
		}

		void Fly(Pool& a_pool, Particle& a_p, const Flight& a_flight, float a_dt)
		{
			const Vec   normal = Rotate(a_p.rot, kUp);
			const Vec   rel = Air(a_p) - a_p.vel;
			const float relLen = rel.Length();

			// drag: strong across the face, weak along it (a tilted leaf glides sideways, an edge-on one drops)
			const Vec across = normal * rel.Dot(normal);
			Vec       accel = across * a_flight.dragFace + (rel - across) * a_flight.dragEdge;
			accel.z -= a_flight.gravity;
			a_p.vel += accel * a_dt;
			a_p.pos += a_p.vel * a_dt;

			// the air turns the face into the flow (it overshoots and rocks: the flutter), fast air tumbles it
			if (relLen > 1.0f && (a_flight.turn > 0.0f || a_flight.tumble > 0.0f)) {
				const Vec   dir = rel * (1.0f / relLen);
				const float sign = normal.Dot(dir) >= 0.0f ? 1.0f : -1.0f;
				a_p.spin += (normal * sign).Cross(dir) * (a_flight.turn * std::min(relLen, 300.0f) * a_dt * 10.0f);
				a_p.spin += RandomUnit() * (std::min(relLen, 400.0f) * a_flight.tumble * a_dt * 10.0f);
			}
			a_p.spin *= std::exp(-a_flight.spinDamping * a_dt);
			if (const float spin = a_p.spin.Length(); spin > kMaxSpin) {
				a_p.spin *= kMaxSpin / spin;
			}
			if (const float spin = a_p.spin.Length(); spin > 1e-4f) {
				a_p.rot = Normalized(Mul(AxisAngle(a_p.spin * (1.0f / spin), spin * a_dt), a_p.rot));
			}

			a_p.groundTimer -= a_dt;
			if (a_p.groundTimer <= 0.0f && a_p.vel.z <= 0.0f) {
				a_p.groundTimer = kGroundCheck;
				// the land's height is cheap; the real surface (rocks, roots, roads) needs a ray: only close to a
				// surface, and now and then high up (a cliff top below)
				const float land = LandHeight(a_p.pos, a_p.ground);
				if (a_p.pos.z - std::max(land, a_p.ground) < 120.0f || clock >= a_p.rayAt) {
					a_p.ground = GroundUnder(a_p);
					a_p.rayAt = clock + 0.6f;
				} else {
					a_p.ground = land;
				}
				// leaves (and snow flakes) come down on the water and float there
				a_p.water = false;
				if (float water = 0.0f; Flutters(a_pool.kind) && a_p.pos.z - a_p.ground < 400.0f && WaterAt(a_p.pos, a_p.ground, water)) {
					a_p.ground = water;
					a_p.water = true;
				}
			}
			if (a_p.pos.z <= a_p.ground + 0.55f && a_p.vel.z <= 0.0f) {
				if (a_pool.style == Style::kClump && !a_p.bounced && a_p.vel.z < -150.0f) {
					// a clod or pebble bounces once
					a_p.bounced = true;
					if (a_pool.kind == Kind::kRock) {
						Sounds::Request(Sounds::Sound::kPebbles, a_p.pos, 1.0f);
					}
					a_p.pos.z = a_p.ground + 0.6f;
					a_p.vel = { a_p.vel.x * 0.5f, a_p.vel.y * 0.5f, -a_p.vel.z * 0.3f };
					a_p.spin *= 0.6f;
				} else {
					if (a_pool.style == Style::kClump && a_pool.kind == Kind::kRock) {
						Sounds::Request(Sounds::Sound::kPebbles, a_p.pos, 0.6f);
					}
					Land(a_pool, a_p);
				}
			} else if (a_p.timer > kMaxAirTime) {
				a_p.phase = Phase::kFade;
				a_p.timer = 0.0f;
			}
		}

		void Drift(Pool& a_pool, Particle& a_p, float a_dt)
		{
			const auto& look = LookOf(a_pool);
			a_p.vel += (Air(a_p) - a_p.vel) * std::min(1.0f, look.drag * a_dt);
			a_p.vel.z -= look.gravity * a_dt;
			a_p.pos += a_p.vel * a_dt;
			a_p.groundTimer -= a_dt;
			if (a_p.groundTimer <= 0.0f) {
				a_p.ground = LandHeight(a_p.pos, a_p.ground);
				a_p.groundTimer = kGroundCheck;
			}
			const float t = std::min(a_p.timer / a_p.life, 1.0f);
			if (a_p.pos.z < a_p.ground + 1.5f) {
				a_p.pos.z = a_p.ground + 1.5f;
				a_p.vel.z = std::max(a_p.vel.z, 0.0f);
			}
			a_p.alpha = look.alpha * Smooth(0.0f, 0.12f, a_p.timer) * std::pow(1.0f - t, 1.5f);
			a_p.scale = a_p.size * (look.grow0 + look.grow1 * std::sqrt(t));
			if (!a_p.shader) {
				a_p.scale *= std::min(1.0f, (1.0f - t) * 3.0f);  // can't fade: shrinks away at the end
			}
			a_p.roll += a_p.rollSpeed * a_dt;
			if (t >= 1.0f) {
				Hide(a_pool, a_p);
			}
		}

		void Step(Pool& a_pool, Particle& a_p, float a_dt)
		{
			a_p.timer += a_dt;
			switch (a_p.phase) {
			case Phase::kAir:
				if (a_pool.style == Style::kPuff) {
					Drift(a_pool, a_p, a_dt);
				} else {
					Fly(a_pool, a_p, a_pool.kind == Kind::kSnow ? kFlakeFlight : a_pool.style == Style::kLeaf ? kLeafFlight : kClumpFlight, a_dt);
				}
				break;
			case Phase::kSettle:
				{
					const float t = Smooth(0.0f, 1.0f, a_p.timer / kSettleTime);
					a_p.rot = Nlerp(a_p.settleFrom, a_p.restRot, t);
					a_p.pos.z = a_p.settleFromZ + (a_p.restZ - a_p.settleFromZ) * t;
					if (a_p.timer >= kSettleTime) {
						a_p.rot = a_p.restRot;
						a_p.phase = Phase::kGround;
						a_p.timer = 0.0f;
					}
					break;
				}
			case Phase::kGround:
				{
					// floating on water: bobbing, drifting with the wind and the water, turning slowly
					if (a_p.water) {
						const Vec drift{ std::sin(clock * 0.31f + a_p.seed[0] * 4.0f), std::cos(clock * 0.27f + a_p.seed[1] * 4.0f), 0.0f };
						a_p.pos += (Flat(wind) * 0.06f + drift * 4.0f) * a_dt;
						a_p.pos.z = a_p.restZ + 0.3f * std::sin(clock * 1.7f + a_p.seed[2] * 6.0f);
						a_p.rot = Normalized(Mul(AxisAngle(kUp, 0.25f * a_p.swirlSign * a_dt), a_p.rot));
						if (a_p.timer >= a_p.restFor) {
							a_p.phase = Phase::kFade;  // soaks and sinks
							a_p.timer = 0.0f;
						}
						break;
					}
					// a leaf walked through again: kicked back up
					if (a_pool.style == Style::kLeaf) {
						for (const auto& m : movers) {
							const float reach = 16.0f * m->footprint;
							if (m->kicking && Flat(a_p.pos - m->pos).SqrLength() < reach * reach && Random(0.0f, 1.0f) < a_dt * 8.0f) {
								mover = m.get();
								const float sideSign = Flat(a_p.pos - m->pos).Dot(m->side) >= 0.0f ? 1.0f : -1.0f;
								LaunchLeaf(a_pool, a_p, m->kick, sideSign, false);
								return;
							}
						}
					}
					// a strong gust picks it up again
					if (a_pool.style == Style::kLeaf && Random(0.0f, 1.0f) < WindLift() * 0.6f * a_dt) {
						mover = movers.front().get();
						LaunchLeaf(a_pool, a_p, 0.4f, 0.0f, false);
						a_p.vel = wind * Random(0.4f, 0.8f) + kUp * Random(60.0f, 160.0f);
						break;
					}
					if (a_p.timer >= a_p.restFor) {
						a_p.phase = Phase::kFade;
						a_p.timer = 0.0f;
					}
					break;
				}
			case Phase::kFade:
				{
					const float t = std::min(a_p.timer / kFadeTime, 1.0f);
					a_p.scale = a_p.size * (1.0f - t);
					a_p.pos.z -= 1.5f * a_dt;
					if (t >= 1.0f) {
						Hide(a_pool, a_p);
					}
					break;
				}
			default:
				break;
			}
		}

		void ReadTuning()
		{
			std::scoped_lock guard(Settings::Lock());
			const auto&      s = Settings::Get();
			tuning.enabled = s.enabled;
			for (std::size_t k = 0; k < kKinds; ++k) {
				auto& kind = tuning.kinds[k];
				kind.enabled = s.kinds[k].enabled;
				kind.amount = s.kinds[k].amount;
				kind.maxParticles = s.kinds[k].maxParticles;
				kind.size = s.kinds[k].size;
			}
			tuning.lift = s.lift;
			tuning.swirl = s.swirl;
			tuning.groundTime = s.groundTime;
			tuning.startDelay = s.startDelay;
			tuning.puffs = s.puffs;
			tuning.swing = s.swing;
			tuning.swingStrength = s.swingStrength;
			tuning.onHorse = s.onHorse;
			tuning.sneakQuieter = s.sneakQuieter;
			tuning.npcs = s.npcs;
			tuning.npcMax = s.npcMax;
			tuning.npcRange = s.npcRange;
			tuning.npcCreatures = s.npcCreatures;
			tuning.npcAttacks = s.npcAttacks;
			tuning.shouts = s.shouts;
			tuning.shoutStrength = s.shoutStrength;
			tuning.slams = s.slams;
			tuning.slamStrength = s.slamStrength;
			tuning.bodies = s.bodies;
			tuning.bodyStrength = s.bodyStrength;
			tuning.explosions = s.explosions;
			tuning.explosionStrength = s.explosionStrength;
			tuning.wind = s.wind;
			tuning.windAmount = s.windAmount;
			tuning.windFlip = s.windFlip;
			tuning.debugLog = s.debugLog;
			Sounds::Configure(s.enabled && s.sounds, s.leafVolume, s.rockVolume);

			// leaves (both kinds) and snow get their whole limit; dust and rocks split theirs: three quarters clumps, a quarter puffs
			for (std::size_t k = 0; k < kKinds; ++k) {
				const auto max = static_cast<std::size_t>(tuning.kinds[k].enabled ? tuning.kinds[k].maxParticles : 0);
				if (Flutters(static_cast<Kind>(k))) {
					pools[kMainPool[k]].capacity = max;
				} else {
					pools[kMainPool[k]].capacity = max * 3 / 4;
					pools[kPuffPool[k]].capacity = tuning.puffs ? max - max * 3 / 4 : 0;
				}
			}
		}

		// The particles hang from the node your character's 3D hangs from (the cell's), which the game always draws
		// and lights and which doesn't move with you: hanging under the skeleton itself, the skeleton's own update
		// came after ours and everything slid and turned with you for a frame. The container undoes the node's
		// transform, so positions stay in world space.
		bool AttachContainer(RE::PlayerCharacter* a_player)
		{
			const auto root = a_player->Get3D(false);
			auto       parent = root ? root->parent : nullptr;
			if (!parent) {
				const auto camera = RE::PlayerCamera::GetSingleton();
				const auto shown = a_player->Get3D(camera && camera->IsInFirstPerson());
				parent = shown ? shown->AsNode() : nullptr;  // no parent (yet): the skeleton as before
			}
			if (!parent || !container) {
				return false;
			}
			if (container->parent != parent) {
				if (container->parent) {
					container->parent->DetachChild(container.get());
				}
				parent->AttachChild(container.get(), true);
				logs::info("particle node attached to '{}' (under {}), at {:.0f}, {:.0f}, {:.0f}", parent->name.c_str(), parent->parent ? parent->parent->name.c_str() : "nothing",
					parent->world.translate.x, parent->world.translate.y, parent->world.translate.z);
			}
			container->local = parent->world.Invert();
			return true;
		}

		void DetachContainer()
		{
			if (container && container->parent) {
				container->parent->DetachChild(container.get());
			}
		}

		// Always written (not only with the debug log) when a line's state changes: each line at most once a second,
		// 300 lines a session. The text is only put together when it gets written.
		enum DiagnoseLine : std::size_t
		{
			kLineAttach,
			kLineGround,
			kLineCover,
			kLineNpcs,
			kLineWind,
			kLines
		};

		template <class F>
		void Diagnose(DiagnoseLine a_line, std::uint32_t a_state, F&& a_text)
		{
			static std::array<std::uint32_t, kLines> last{ ~0u, ~0u, ~0u, ~0u, ~0u };
			static std::array<float, kLines>         when{ -100.0f, -100.0f, -100.0f, -100.0f, -100.0f };
			static int                               lines = 0;
			if ((a_line != kLineAttach && !tuning.debugLog) || last[a_line] == a_state || lines >= 300 || clock - when[a_line] < 1.0f) {
				return;
			}
			last[a_line] = a_state;
			when[a_line] = clock;
			++lines;
			logs::info("{}", a_text());
		}

		// The menu's status and the debug log line, twice a second. The counts come from the last pass over the
		// particles; a_covers null: not measured this frame (standing still), the probe's are used.
		void PublishStatus(float a_dt, bool a_exterior, bool a_grounded, const Terrain::Covers* a_covers)
		{
			statusTimer -= a_dt;
			logTimer -= a_dt;
			if (statusTimer > 0.0f) {
				return;
			}
			statusTimer = 0.5f;
			const auto& you = *movers.front();
			Status      next;
			next.modelLoaded = !pools[kLeafPool].templates.empty();
			next.exterior = a_exterior;
			next.grounded = a_grounded;
			next.speed = you.speed;
			next.flying = flyingCount;
			next.landed = landedCount;
			next.object = you.onObject ? objectName : "";
			next.objectTexture = you.onObject ? objectTexture : "";
			next.raised = you.onObject && raised;
			next.npcs = static_cast<int>(movers.size()) - 1;
			if (a_covers) {
				next.covers = *a_covers;
			}
			if (a_exterior) {
				const auto probe = Terrain::At(you.pos);
				next.texture = probe.dominant;
				next.textureShare = probe.dominantShare;
				if (!a_covers) {
					next.covers = probe.covers;
				}
			}
			if (tuning.debugLog && logTimer <= 0.0f) {
				logTimer = 2.0f;
				logs::info("status: on land {}, ground '{}' {:.0f}%, cover leaves {:.2f} green {:.2f} dust {:.2f} rocks {:.2f} snow {:.2f}, speed {:.0f}, kick {:.2f}{}, NPCs {}, waves {}, flying {}/{}/{}/{}/{}, landed {}/{}/{}/{}/{}",
					a_grounded, next.texture, next.textureShare * 100.0f, next.covers[0], next.covers[1], next.covers[2], next.covers[3], next.covers[4], you.speed, you.kick,
					you.mounted ? " (horse)" : "", next.npcs, waves.size(), next.flying[0], next.flying[1], next.flying[2], next.flying[3], next.flying[4], next.landed[0],
					next.landed[1], next.landed[2], next.landed[3], next.landed[4]);
			}
			std::scoped_lock guard(status_lock);
			status = std::move(next);
		}

		// ---- animation events and spell casts

		bool SameText(std::string_view a_a, std::string_view a_b)
		{
			return a_a.size() == a_b.size() && std::ranges::equal(a_a, a_b, [](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
			});
		}

		class EventSink :
			public RE::BSTEventSink<RE::BSAnimationGraphEvent>,
			public RE::BSTEventSink<RE::TESSpellCastEvent>,
			public RE::BSTEventSink<RE::TESDeathEvent>
		{
		public:
			static EventSink* GetSingleton()
			{
				static EventSink sink;
				return &sink;
			}

			// attacks of whoever the event comes from (you and the NPCs watched); some shout behaviors announce the
			// shout here too
			RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
			{
				if (!a_event || !a_event->tag.c_str() || !a_event->holder) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const std::string_view tag(a_event->tag.c_str());
				const bool             swing = SameText(tag, "weaponSwing") || SameText(tag, "weaponLeftSwing") || SameText(tag, "preHitFrame") || SameText(tag, "HitFrame");
				const bool             shout = SameText(tag, "Voice_SpellFire_Event") || SameText(tag, "ShoutRelease") || SameText(tag, "shoutStart");
				if (swing || shout) {
					std::scoped_lock guard(signalLock);
					(swing ? swingSignals : shoutSignals).push_back(a_event->holder->GetFormID());
					if (SameText(tag, "HitFrame")) {
						hitSignals.push_back(a_event->holder->GetFormID());
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			// someone starting to die: their body gets watched until it hits the ground
			RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
			{
				if (a_event && a_event->actorDying && !a_event->dead) {
					std::scoped_lock guard(signalLock);
					deathSignals.push_back(a_event->actorDying->GetFormID());
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			// shouts: any voice power cast (yours, NPCs', dragons')
			RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event, RE::BSTEventSource<RE::TESSpellCastEvent>*) override
			{
				if (!a_event || !a_event->object) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto spell = RE::TESForm::LookupByID<RE::SpellItem>(a_event->spell);
				if (spell && spell->GetSpellType() == RE::MagicSystem::SpellType::kVoicePower) {
					std::scoped_lock guard(signalLock);
					shoutSignals.push_back(a_event->object->GetFormID());
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// a new 3D gets new animation graphs: the sink is added again
		void Watch(Mover& a_mover, RE::Actor* a_actor)
		{
			const void* root = a_actor->Get3D(false);
			if (root == a_mover.watchedRoot) {
				return;
			}
			a_actor->RemoveAnimationGraphEventSink(EventSink::GetSingleton());
			if (!a_mover.player) {
				a_mover.footprint = std::clamp(a_actor->GetHeight() / 128.0f, 0.6f, 3.5f);  // its size: measured once per 3D
			}
			if (root && a_actor->AddAnimationGraphEventSink(EventSink::GetSingleton())) {
				a_mover.watchedRoot = root;
				if (a_mover.player) {
					logs::info("watching your attacks");
				}
			} else {
				a_mover.watchedRoot = nullptr;
			}
		}

		void Unwatch(Mover& a_mover)
		{
			if (const auto actor = RE::TESForm::LookupByID<RE::Actor>(a_mover.formID)) {
				actor->RemoveAnimationGraphEventSink(EventSink::GetSingleton());
			}
			a_mover.watchedRoot = nullptr;
		}

		Mover* FindMover(RE::FormID a_formID)
		{
			for (const auto& m : movers) {
				if (m->formID == a_formID) {
					return m.get();
				}
			}
			return nullptr;
		}

		// ---- weapon swings

		// where a weapon node's held model ends: its bound's far side, seen from the hand (bigger for big actors)
		bool MeasureBlade(RE::NiAVObject* a_root, const char* a_node, float a_footprint, Vec& a_base, Vec& a_tip)
		{
			auto node = a_root->GetObjectByName(a_node);
			if (!node) {
				return false;
			}
			a_base = node->world.translate;
			a_tip = a_base;
			const auto& bound = node->worldBound;
			Vec         out = bound.center - a_base;
			const float length = out.Length();
			if (bound.radius > 2.0f && length > 1.0f && length < 200.0f * a_footprint) {
				a_tip = bound.center + out * (std::min(bound.radius, 120.0f * a_footprint) / length);
			}
			return true;
		}

		Vec ClosestOnSegment(const Vec& a_a, const Vec& a_b, const Vec& a_p, float& a_t)
		{
			const Vec   ab = a_b - a_a;
			const float len2 = ab.SqrLength();
			a_t = len2 > 1e-4f ? std::clamp((a_p - a_a).Dot(ab) / len2, 0.0f, 1.0f) : 0.0f;
			return a_a + ab * a_t;
		}

		// a leaf or a puff of dust or rock dust thrown up from the ground under the current mover's blade
		void SwingKick(Kind a_kind, const Vec& a_at, const Vec& a_bladeVel)
		{
			if (Random(0.0f, 1.0f) >= CoverAt(a_at)[a_kind]) {
				return;
			}
			const float ground = SurfaceAt(a_at);
			const float speed = a_bladeVel.Length();
			Vec         push = Flat(a_bladeVel);
			if (const float flat = push.Length(); flat > 450.0f) {
				push *= 450.0f / flat;
			}
			if (Flutters(a_kind)) {
				auto&      pool = pools[kMainPool[a_kind]];
				const auto p = Acquire(pool);
				if (!p || !p->node) {
					return;
				}
				p->pos = { a_at.x, a_at.y, ground + 0.5f };
				p->ground = ground;
				LaunchLeaf(pool, *p, 0.5f, Random(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f, true);
				p->vel = push * Random(0.25f, 0.5f) + kUp * ((80.0f + 0.12f * std::min(speed, 1500.0f)) * Random(0.6f, 1.2f) * tuning.lift) +
				         Vec{ Gauss(15.0f), Gauss(15.0f), 0.0f };
			} else if (tuning.puffs) {
				auto&      pool = pools[kPuffPool[a_kind]];
				const auto p = Acquire(pool);
				if (!p || !p->node) {
					return;
				}
				p->pos = a_at;
				p->ground = ground;
				LaunchPuff(pool, *p, a_kind, 0.4f);
				p->vel = push * 0.15f + kUp * Random(10.0f, 30.0f);
			}
		}

		void StartWave(const Vec& a_origin, const Vec& a_dir, float a_cosHalf, float a_speed, float a_reach, float a_strength)
		{
			if (a_strength <= 0.0f || waves.size() >= 12) {
				return;
			}
			Wave wave;
			wave.origin = a_origin;
			wave.dir = a_dir;
			wave.cosHalf = a_cosHalf;
			wave.speed = a_speed;
			wave.reach = a_reach;
			wave.strength = a_strength;
			waves.push_back(wave);
		}

		// an explosion near the ground: a fast ring as wide as the blast blows everything away from it
		void Explode(const Blast& a_blast, const Vec& a_you)
		{
			if (!tuning.explosions || tuning.explosionStrength <= 0.0f || (a_blast.at - a_you).Length() > 5000.0f) {
				return;
			}
			const float radius = a_blast.radius > 0.0f ? a_blast.radius : 150.0f;
			const float ground = GroundAt(a_blast.at, LandHeight(a_blast.at, a_blast.at.z - 1000.0f));
			if (a_blast.at.z - ground > std::max(0.8f * radius, 120.0f)) {
				return;  // high in the air: the blast doesn't reach the ground
			}
			const float reach = std::clamp(radius * 1.4f, 160.0f, 1000.0f);
			const float strength = std::clamp(0.5f + radius / 350.0f, 0.5f, 1.6f) * tuning.explosionStrength;
			StartWave({ a_blast.at.x, a_blast.at.y, ground }, {}, -1.0f, 900.0f, reach, strength);
			if (tuning.debugLog) {
				logs::info("an explosion (radius {:.0f}) at {:.0f}, {:.0f}, {:.0f}: a ring of strength {:.2f}, {:.0f} units wide", radius, a_blast.at.x, a_blast.at.y,
					a_blast.at.z, strength, 2.0f * reach);
			}
		}

		void Slam(Mover& a_m, const Vec& a_at, float a_strength, const char* a_how)
		{
			const float fp = a_m.footprint;
			StartWave(a_at, {}, -1.0f, 650.0f + 250.0f * fp, (110.0f + 90.0f * a_strength) * fp, a_strength);
			a_m.slamReady = clock + 0.6f;
			if (tuning.debugLog) {
				logs::info("{} hit the ground ({}): a ring of strength {:.2f}, {:.0f} units wide", a_m.player ? std::string("you") : std::format("{:08X}", a_m.formID), a_how,
					a_strength, 2.0f * (110.0f + 90.0f * a_strength) * fp);
			}
		}

		// The mover's blades this frame (during a swing window). Air pushed by a fast blade throws up leaves (and a
		// little dust and rock dust, no clumps) from the ground under it, more the lower it passes; a
		// blade that hits the ground hard (a giant's club, a hammer) sends a ring of it out.
		void Swing(Mover& a_m, RE::Actor* a_actor, float a_dt)
		{
			const bool allowed = tuning.swing && (a_m.player || tuning.npcAttacks);
			const auto camera = a_m.player ? RE::PlayerCamera::GetSingleton() : nullptr;
			const auto root = a_actor->Get3D(camera && camera->IsInFirstPerson());
			if (!root || clock > a_m.swingUntil || !allowed) {
				for (auto& b : a_m.blades) {
					b.valid = false;
					b.strength = 0.0f;
				}
				a_m.swingDebt = {};
				return;
			}
			static constexpr std::array<std::array<const char*, 2>, 2> kNodes{ { { "WEAPON", "NPC R Hand [RHnd]" }, { "SHIELD", "NPC L Hand [LHnd]" } } };
			const float fp = a_m.footprint;
			for (std::size_t h = 0; h < a_m.blades.size(); ++h) {
				auto& b = a_m.blades[h];
				Vec   base, tip;
				if (!MeasureBlade(root, kNodes[h][0], fp, base, tip) && !MeasureBlade(root, kNodes[h][1], fp, base, tip)) {
					b.valid = false;
					b.strength = 0.0f;
					continue;
				}
				const bool had = b.valid;
				if (had) {
					b.baseVel = (base - b.base) * (1.0f / a_dt);
					b.tipVel = (tip - b.tip) * (1.0f / a_dt);
				}
				b.base = base;
				b.tip = tip;
				b.valid = true;
				const float speed = had ? b.tipVel.Length() : 0.0f;
				b.strength = had && speed < 5000.0f * fp ? std::clamp((speed - kSwingMinSpeed) / (kSwingFullSpeed - kSwingMinSpeed), 0.0f, 1.5f) * tuning.swingStrength : 0.0f;
				// a big creature's hit frame with its weapon (or fist) down low: the club landed (giants)
				if (tuning.slams && fp >= 1.8f && clock - a_m.hitFrameAt < 0.25f && clock > a_m.slamReady && a_m.canKick) {
					const float ground = a_m.onObject ? a_m.groundZ : LandHeight(tip, a_m.groundZ);
					if (tip.z - ground < 60.0f * fp) {
						Slam(a_m, { tip.x, tip.y, ground }, 0.9f * std::sqrt(fp) * tuning.slamStrength, "a hit frame");
						a_m.hitFrameAt = -100.0f;
					}
				}
				if (b.strength <= 0.0f || !a_m.canKick) {
					continue;
				}
				// a heavy blow hitting the ground: a ring of everything around the impact
				if (tuning.slams && b.tipVel.z < -350.0f && clock > a_m.slamReady) {
					const float ground = LandHeight(tip, a_m.groundZ);
					const float hitZ = a_m.onObject ? a_m.groundZ : ground;
					if (tip.z - hitZ < 18.0f * fp) {
						const float strength = std::clamp((-b.tipVel.z - 350.0f) / 900.0f, 0.3f, 1.5f) * std::sqrt(fp) * tuning.slamStrength;
						Slam(a_m, { tip.x, tip.y, hitZ }, strength, "a blow");
					}
				}
				// the ground under four points along the blade; lower passes stir up more
				for (int i = 1; i <= 4; ++i) {
					const float t = i / 4.0f;
					const Vec   at = base + (tip - base) * t;
					const Vec   vel = b.baseVel + (b.tipVel - b.baseVel) * t;
					const float height = std::clamp(1.0f - (at.z - a_m.groundZ - 30.0f * fp) / (120.0f * fp), 0.15f, 1.0f);
					for (std::size_t k = 0; k < kKinds; ++k) {
						if (!tuning.kinds[k].enabled) {
							continue;
						}
						const float rate = (Flutters(static_cast<Kind>(k)) ? 45.0f : 6.0f) * tuning.kinds[k].amount * b.strength * height / 4.0f;
						a_m.swingDebt[k] += rate * a_dt;
						while (a_m.swingDebt[k] >= 1.0f) {
							a_m.swingDebt[k] -= 1.0f;
							SwingKick(static_cast<Kind>(k), Vec{ at.x + Gauss(8.0f * fp), at.y + Gauss(8.0f * fp), at.z }, vel);
						}
					}
				}
			}
		}

		bool Swinging(const Mover& a_m)
		{
			return a_m.blades[0].strength > 0.0f || a_m.blades[1].strength > 0.0f;
		}

		// what a moving blade does to a particle near it: flying ones get pushed along, landed leaves kicked up
		void SwingPush(const Mover& a_m, Pool& a_pool, Particle& a_p, float a_dt)
		{
			const float reach = kSwingReach * a_m.footprint;
			for (const auto& b : a_m.blades) {
				if (!b.valid || b.strength <= 0.0f) {
					continue;
				}
				float t = 0.0f;
				if (a_p.phase == Phase::kAir) {
					const Vec   closest = ClosestOnSegment(b.base, b.tip, a_p.pos, t);
					const float d = (a_p.pos - closest).Length();
					if (d >= reach) {
						continue;
					}
					const float f = (1.0f - d / reach) * (1.0f - d / reach) * std::min(b.strength, 1.0f);
					const Vec   air = (b.baseVel + (b.tipVel - b.baseVel) * t) * 0.6f;
					a_p.vel += (air - a_p.vel) * std::min(1.0f, 8.0f * a_dt * f);
					if (a_pool.style == Style::kLeaf) {
						a_p.spin += RandomUnit() * (f * 20.0f * a_dt * 10.0f);
					}
				} else if (a_pool.style == Style::kLeaf && (a_p.phase == Phase::kGround || a_p.phase == Phase::kSettle)) {
					// the blade's shadow on the ground, weaker the higher it passes
					const Vec   flatBase{ b.base.x, b.base.y, a_p.pos.z };
					const Vec   flatTip{ b.tip.x, b.tip.y, a_p.pos.z };
					const Vec   closest = ClosestOnSegment(flatBase, flatTip, a_p.pos, t);
					const float d = (a_p.pos - closest).Length();
					if (d >= reach) {
						continue;
					}
					const Vec   at = b.base + (b.tip - b.base) * t;
					const float height = std::clamp(1.0f - (at.z - a_m.groundZ - 30.0f * a_m.footprint) / (120.0f * a_m.footprint), 0.15f, 1.0f);
					const float f = (1.0f - d / reach) * height * std::min(b.strength, 1.0f);
					if (Random(0.0f, 1.0f) < f * a_dt * 12.0f) {
						const Vec vel = b.baseVel + (b.tipVel - b.baseVel) * t;
						LaunchLeaf(a_pool, a_p, 0.5f, Random(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f, false);
						Vec push = Flat(vel);
						if (const float flat = push.Length(); flat > 450.0f) {
							push *= 450.0f / flat;
						}
						a_p.vel = push * Random(0.25f, 0.5f) + kUp * ((80.0f + 0.1f * std::min(vel.Length(), 1500.0f)) * Random(0.6f, 1.2f) * tuning.lift);
						return;
					}
				}
			}
		}

		// ---- shock waves

		// where a particle is in a wave: inside the band its front swept this frame (and inside its cone)
		bool InWave(const Wave& a_w, const Vec& a_pos, Vec& a_out, float& a_dist)
		{
			Vec flat = Flat(a_pos - a_w.origin);
			a_dist = flat.Length();
			if (a_dist <= a_w.prevFront || a_dist > a_w.front + 1.0f) {
				return false;
			}
			a_out = a_dist > 0.5f ? flat * (1.0f / a_dist) : (a_w.cosHalf > -1.0f ? a_w.dir : Vec{ 1.0f, 0.0f, 0.0f });
			return a_w.cosHalf <= -1.0f || a_out.Dot(a_w.dir) >= a_w.cosHalf;
		}

		// the front passing: flying things blown outward, landed leaves thrown up again
		void WavePush(Pool& a_pool, Particle& a_p)
		{
			for (const auto& w : waves) {
				Vec   out;
				float dist = 0.0f;
				if (!InWave(w, a_p.pos, out, dist)) {
					continue;
				}
				const float f = w.strength * (1.0f - 0.6f * dist / w.reach);
				if (a_p.phase == Phase::kAir) {
					const float heavy = a_pool.style == Style::kClump ? 0.5f : 1.0f;
					a_p.vel += out * (Random(250.0f, 450.0f) * f * heavy) + kUp * (Random(80.0f, 200.0f) * f * heavy);
					if (a_pool.style == Style::kLeaf) {
						a_p.spin += RandomUnit() * (12.0f * f);
					}
				} else if (a_pool.style == Style::kLeaf && (a_p.phase == Phase::kGround || a_p.phase == Phase::kSettle)) {
					LaunchLeaf(a_pool, a_p, 0.8f, 0.0f, false);
					a_p.vel = out * (Random(250.0f, 450.0f) * f) + kUp * (Random(120.0f, 260.0f) * f * tuning.lift);
				}
			}
		}

		// the fronts move on; the ground they sweep throws up what lies on it
		void AdvanceWaves(float a_dt)
		{
			mover = movers.front().get();  // the launches below set their own pushes
			for (auto& w : waves) {
				w.prevFront = w.front;
				w.front = std::min(w.front + w.speed * a_dt, w.reach);
				const float span = w.cosHalf <= -1.0f ? 6.2832f : 2.0f * std::acos(std::clamp(w.cosHalf, -1.0f, 1.0f));
				const float area = span * 0.5f * (w.front * w.front - w.prevFront * w.prevFront);  // the band swept this frame
				const float fade = 1.0f - 0.6f * w.front / w.reach;
				for (std::size_t k = 0; k < kKinds; ++k) {
					if (!tuning.kinds[k].enabled) {
						continue;
					}
					w.debt[k] += area / 1400.0f * w.strength * fade * tuning.kinds[k].amount * (Flutters(static_cast<Kind>(k)) ? 1.0f : 0.6f);
					int count = std::min(static_cast<int>(w.debt[k]), 20);
					w.debt[k] -= static_cast<float>(count);
					w.debt[k] = std::min(w.debt[k], 3.0f);
					while (count-- > 0) {
						const float r = std::sqrt(Random(w.prevFront * w.prevFront, std::max(w.front * w.front, w.prevFront * w.prevFront + 1.0f)));
						const float base = w.cosHalf <= -1.0f ? 0.0f : std::atan2(w.dir.y, w.dir.x);
						const float angle = base + Random(-0.5f, 0.5f) * span;
						const Vec   out{ std::cos(angle), std::sin(angle), 0.0f };
						const Vec   at = w.origin + out * r;
						if (Random(0.0f, 1.0f) >= Terrain::Cover(at)[k]) {
							continue;
						}
						const float ground = GroundAt(at, w.origin.z);
						if (ground - LandHeight(at, ground) > 40.0f) {
							continue;  // a rock, a roof, a walkway: not the land whose cover this is
						}
						const float f = w.strength * (1.0f - 0.6f * r / w.reach);
						Throw(static_cast<Kind>(k), at, ground, out * (Random(200.0f, 420.0f) * f), (Flutters(static_cast<Kind>(k)) ? 220.0f : 160.0f) * f);
					}
				}
			}
			std::erase_if(waves, [](const Wave& a_w) { return a_w.front >= a_w.reach; });
		}

		// a shout: a cone of it blown away in front of the shouter, along the ground
		void Shout(RE::Actor* a_actor)
		{
			if (!tuning.shouts || !a_actor || !a_actor->Get3D()) {
				return;
			}
			const auto player = RE::PlayerCharacter::GetSingleton();
			const Vec  pos = a_actor->GetPosition();
			if (!player || (pos - player->GetPosition()).Length() > 5000.0f) {
				return;
			}
			auto       m = FindMover(a_actor->GetFormID());
			if (m && clock < m->shoutReady) {
				return;  // the cast and the animation event of one shout
			}
			const float ground = LandHeight(pos, pos.z);
			if (pos.z - ground > 600.0f) {
				return;  // a dragon shouting from high up: nothing reaches the ground
			}
			const float heading = a_actor->GetAngleZ();
			const Vec   dir{ std::sin(heading), std::cos(heading), 0.0f };
			const float size = std::clamp(a_actor->GetHeight() / 128.0f, 0.6f, 4.0f);
			StartWave({ pos.x + dir.x * 30.0f * size, pos.y + dir.y * 30.0f * size, ground }, dir, std::cos(0.45f), 1800.0f, 1300.0f * std::sqrt(size) * std::max(tuning.shoutStrength, 0.3f),
				1.2f * tuning.shoutStrength);
			if (m) {
				m->shoutReady = clock + 1.0f;
			}
			if (tuning.debugLog) {
				logs::info("{} shouted: a blast of air along the ground", a_actor->IsPlayerRef() ? std::string("you") : std::format("{:08X}", a_actor->GetFormID()));
			}
		}

		// ---- what holds a mover up

		// ground you walk on at any height: natural rock, cliffs, mountains and landscape pieces; not trees, logs,
		// stumps and plants, nor buildings, docks, bridges, clutter and furniture: up on those nothing is kicked up
		// (at the land's level they still count)
		bool GroundLike(std::string_view a_model)
		{
			if (a_model.contains("\\trees\\") || a_model.contains("\\plants\\") || a_model.contains("log") || a_model.contains("stump")) {
				return false;
			}
			return a_model.starts_with("landscape\\") || a_model.contains("rock") || a_model.contains("cliff") || a_model.contains("mountain") ||
			       a_model.contains("boulder");
		}

		// what a shape's own (not landscape) texture says it is: a dirt cliff's face, a mountain's or rock's slabs, snow
		int NamedMaterial(std::string_view a_name)
		{
			if (a_name.contains("snow") && !a_name.contains("ice")) {
				return Terrain::MaterialIndex("Snow covered");
			}
			if (a_name.contains("dirt") || a_name.contains("mud")) {
				return Terrain::MaterialIndex("Dirt");
			}
			if (a_name.contains("mountain") || a_name.contains("rock") || a_name.contains("cliff") || a_name.contains("slab") || a_name.contains("boulder")) {
				return Terrain::MaterialIndex("Rock");
			}
			return -1;
		}

		// The landscape textures painted on an object (its shapes' diffuse textures after texture swaps), each by the
		// share of the object it covers (by bound size), and the shapes whose own texture names tell what they are
		void ObjectTextures(RE::TESObjectREFR* a_ref, SupportInfo& a_info)
		{
			const auto root = a_ref ? a_ref->Get3D() : nullptr;
			if (!root) {
				return;
			}
			float total = 0.0f;
			float best = 0.0f;
			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) {
				const auto property = a_geometry->GetGeometryRuntimeData().shaderProperty.get();
				const auto lighting = property ? netimmerse_cast<RE::BSLightingShaderProperty*>(property) : nullptr;
				const auto material = lighting ? static_cast<RE::BSLightingShaderMaterialBase*>(lighting->material) : nullptr;
				if (!material || !material->textureSet) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				const float weight = a_geometry->worldBound.radius * a_geometry->worldBound.radius;
				total += weight;
				const char* path = material->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
				if (!path) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				std::string name(path);
				if (const auto slash = name.find_last_of("\\/"); slash != std::string::npos) {
					name.erase(0, slash + 1);
				}
				std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				const int file = Terrain::FileIndex(name);
				if (file < 0) {
					if (const int named = NamedMaterial(name); named >= 0) {
						auto it = std::ranges::find(a_info.named, named, &std::pair<int, float>::first);
						if (it == a_info.named.end()) {
							a_info.named.emplace_back(named, 0.0f);
							it = a_info.named.end() - 1;
						}
						it->second += weight;
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				auto it = std::ranges::find(a_info.textures, file, &std::pair<int, float>::first);
				if (it == a_info.textures.end()) {
					a_info.textures.emplace_back(file, 0.0f);
					it = a_info.textures.end() - 1;
				}
				it->second += weight;
				if (it->second > best) {
					best = it->second;
					a_info.texture = name;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (total <= 0.0f) {
				a_info.textures.clear();
				a_info.named.clear();
				return;
			}
			float matched = 0.0f;
			for (auto* list : { &a_info.textures, &a_info.named }) {
				for (auto& [index, share] : *list) {
					share /= total;
					matched += share;
				}
			}
			a_info.otherShare = std::max(0.0f, 1.0f - matched);
		}

		bool Natural(std::string_view a_model)
		{
			return !a_model.contains("architecture") &&
			       (a_model.starts_with("landscape\\") || a_model.contains("\\rocks\\") || a_model.contains("rock") || a_model.contains("cliff") ||
					   a_model.contains("mountain") || a_model.contains("boulder"));
		}

		// The body it stands on: an object when the physics leads to a reference (else the landscape, or some body
		// without one), sorted by its painted on layer (a static's directional material, told apart by its color:
		// bluish white snow, neutral grey ash), a snowy model, natural rock, or its collision material. Only worked
		// out again when the body or the material changes.
		SupportInfo Support(SupportCache& a_cache, const RE::bhkCharacterController* a_controller)
		{
			const auto body = a_controller->supportBody.get();
			if (body == a_cache.body && a_controller->surfaceMaterial == a_cache.material) {
				return a_cache.info;
			}
			a_cache.body = body;
			a_cache.material = a_controller->surfaceMaterial;
			SupportInfo info;
			const auto  collidable = body ? body->GetCollidable() : nullptr;
			if (!collidable) {
				return a_cache.info = info;
			}
			const auto layer = collidable->GetCollisionLayer();
			info.layer = static_cast<int>(std::to_underlying(layer));
			info.layerTerrain = layer == RE::COL_LAYER::kTerrain || layer == RE::COL_LAYER::kGround;
			// mountains use the terrain layer too: only the missing reference tells the landscape
			const auto ref = RE::TESHavokUtilities::FindCollidableRef(*collidable);
			const auto base = ref ? ref->GetBaseObject() : nullptr;
			info.object = base != nullptr;
			if (!info.object) {
				info.material = Terrain::MaterialOf(a_controller->surfaceMaterial);
				return a_cache.info = info;
			}
			if (const auto model = base ? base->As<RE::TESModel>() : nullptr; model && model->GetModel()) {
				info.model = model->GetModel();
				std::ranges::transform(info.model, info.model.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (info.model.starts_with("meshes\\")) {
					info.model.erase(0, 7);
				}
			}
			ObjectTextures(ref, info);
			info.groundLike = info.otherShare < 0.7f || GroundLike(info.model);
			// a painted on layer (snow, ash) or a snowy model lies over the shapes' own textures
			const auto layered = [&](int a_material) {
				for (const auto& [index, share] : info.named) {
					info.otherShare += share;
				}
				info.named.clear();
				info.material = a_material;
				return info;
			};
			if (const auto stat = base ? base->As<RE::TESObjectSTAT>() : nullptr; stat && stat->data.materialObj) {
				const auto& d = stat->data.materialObj->directionalData;
				const auto& c = d.singlePassColor;
				const float low = std::min({ c.red, c.green, c.blue });
				const float high = std::max({ c.red, c.green, c.blue });
				info.layerColor = std::format("{:.2f} {:.2f} {:.2f}{}", c.red, c.green, c.blue, d.singlePass ? "" : " (not single pass)");
				if (d.singlePass && low >= 0.33f && c.blue >= c.red) {
					return a_cache.info = layered(Terrain::MaterialIndex("Snow covered"));
				}
				if (d.singlePass && high - low < 0.05f && low >= 0.2f && high <= 0.36f) {
					return a_cache.info = layered(Terrain::MaterialIndex("Ash covered"));
				}
			}
			if (info.model.contains("snow") && !info.model.contains("ice") && !info.model.contains("glacier")) {
				return a_cache.info = layered(Terrain::MaterialIndex("Snow covered"));  // snow in the model's own textures
			}
			info.material = Terrain::MaterialOf(a_controller->surfaceMaterial);
			if (info.material == Terrain::MaterialIndex("Stone") && Natural(info.model)) {
				info.material = Terrain::MaterialIndex("Rock");
			}
			return a_cache.info = info;
		}

		// ---- bodies dropping

		void WatchDeath(RE::FormID a_formID)
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			const auto actor = RE::TESForm::LookupByID<RE::Actor>(a_formID);
			if (!tuning.bodies || !player || !actor || !actor->Get3D() || (actor->GetPosition() - player->GetPosition()).Length() > std::max(tuning.npcRange, 2048.0f) ||
				std::ranges::any_of(corpses, [a_formID](const Corpse& a_c) { return a_c.formID == a_formID; })) {
				return;
			}
			Corpse corpse;
			corpse.formID = a_formID;
			corpse.until = clock + 6.0f;
			corpses.push_back(corpse);
		}

		// a falling body that stops: it hit the ground (a ring of what lies there, as big as the body)
		void Bodies(float a_dt)
		{
			for (auto& c : corpses) {
				const auto actor = RE::TESForm::LookupByID<RE::Actor>(c.formID);
				const auto root = actor ? actor->Get3D(false) : nullptr;
				if (!root) {
					c.until = 0.0f;
					continue;
				}
				const auto& bound = root->worldBound;
				const Vec   center = bound.radius > 1.0f ? bound.center : actor->GetPosition();
				if (!c.hasLast) {
					c.hasLast = true;
					c.lastZ = center.z;
					continue;
				}
				const float vz = (center.z - c.lastZ) / a_dt;
				c.lastZ = center.z;
				const float wasFalling = c.fallSpeed;
				c.fallSpeed += (-vz - c.fallSpeed) * std::min(1.0f, a_dt / 0.06f);
				if (wasFalling > 120.0f && -vz < 40.0f && clock > c.readyAt && c.impacts < 2) {
					const float fp = std::clamp(actor->GetHeight() / 128.0f, 0.6f, 3.5f);
					const float land = LandHeight(center, center.z - 30.0f * fp);
					const float ground = center.z - land < 60.0f * fp ? land : center.z - 15.0f * fp;
					const float strength = std::clamp(wasFalling / 450.0f, 0.35f, 1.2f) * std::sqrt(fp) * tuning.bodyStrength * (c.impacts == 0 ? 1.0f : 0.5f);
					StartWave({ center.x, center.y, ground }, {}, -1.0f, 400.0f + 200.0f * fp, (45.0f + 35.0f * strength) * fp + bound.radius * 0.3f, strength);
					if (tuning.debugLog) {
						logs::info("a body ({:08X}) hit the ground: a ring of strength {:.2f}", c.formID, strength);
					}
					++c.impacts;
					c.readyAt = clock + 0.35f;
				}
			}
			std::erase_if(corpses, [](const Corpse& a_c) { return clock > a_c.until; });
		}

		// ---- the wind

		// the weather's wind (Dynamic Wind changes it), with gusts; shows in Air() and lifts things off the ground
		void ReadWind()
		{
			const auto sky = RE::Sky::GetSingleton();
			if (!sky) {
				windStrength = 0.0f;
				return;
			}
			windStrength = std::clamp(sky->windSpeed, 0.0f, 1.0f);
			float angle = sky->windAngle;
			if (std::abs(angle) > 6.3f) {
				angle *= 3.14159f / 180.0f;  // in degrees
			}
			windDir = { std::sin(angle), std::cos(angle), 0.0f };
			if (tuning.windFlip) {
				windDir = windDir * -1.0f;
			}
			windGust = std::max(0.2f, 0.75f + 0.35f * std::sin(clock * 0.7f) + 0.2f * std::sin(clock * 1.9f + 1.0f));
			const float speed = tuning.wind ? (150.0f + 250.0f * windStrength) * windGust * windStrength : 70.0f * windStrength;
			wind = windDir * speed;
			Diagnose(kLineWind, static_cast<std::uint32_t>(std::round(windStrength * 20.0f)) | static_cast<std::uint32_t>(std::round(sky->windAngle)) << 8,
				[&] { return std::format("wind: speed {:.2f}, angle {:.2f} (blowing towards {:.2f}, {:.2f})", sky->windSpeed, sky->windAngle, windDir.x, windDir.y); });
		}

		// how much the wind lifts this frame (0 below a light breeze, 1 at a good wind)
		float WindLift()
		{
			return tuning.wind ? std::clamp((windStrength - 0.08f) / 0.55f, 0.0f, 1.4f) * windGust * tuning.windAmount : 0.0f;
		}

		// Wind picks up leaves (and a little dust) where you can see it: mostly in front of the
		// camera, some upwind of you; half the leaves come already flying, as if torn off the trees above.
		void WindLifts(const Vec& a_you, float a_dt)
		{
			const float lift = WindLift();
			if (lift <= 0.0f) {
				windDebt = {};
				return;
			}
			mover = movers.front().get();
			static constexpr std::array<float, kKinds> kRate{ 26.0f, 22.0f, 4.0f, 0.0f, 18.0f };
			const float speed = wind.Length();
			const float view = std::atan2(cameraFwd.y, cameraFwd.x);
			for (std::size_t k = 0; k < kKinds; ++k) {
				if (!tuning.kinds[k].enabled || kRate[k] <= 0.0f) {
					continue;
				}
				windDebt[k] += kRate[k] * lift * tuning.kinds[k].amount * a_dt;
				int count = std::min(static_cast<int>(windDebt[k]), 6);
				windDebt[k] = std::min(windDebt[k] - static_cast<float>(count), 3.0f);
				while (count-- > 0) {
					Vec at;
					if (Random(0.0f, 1.0f) < 0.65f) {
						const float angle = view + Random(-1.0f, 1.0f);
						at = cameraPos + Vec{ std::cos(angle), std::sin(angle), 0.0f } * Random(120.0f, 750.0f);
					} else {
						const float angle = Random(0.0f, 6.2832f);
						at = a_you - windDir * 400.0f + Vec{ std::cos(angle), std::sin(angle), 0.0f } * (std::sqrt(Random(0.0f, 1.0f)) * 700.0f);
					}
					if (Random(0.0f, 1.0f) >= Terrain::Cover(at)[k]) {
						continue;
					}
					const float ground = GroundAt(at, a_you.z);
					if (ground - LandHeight(at, ground) > 40.0f) {
						continue;  // up on a rock or a roof: not the land whose cover this is
					}
					if (Flutters(static_cast<Kind>(k))) {
						auto& leaves = pools[kMainPool[k]];
						if (leaves.active * 4 >= leaves.capacity * 3) {
							continue;  // the last quarter stays for your steps
						}
						if (IsLeaves(static_cast<Kind>(k)) && Random(0.0f, 1.0f) < 0.5f) {
							// torn off the trees: already flying, drifting down across the view
							const auto p = Acquire(leaves);
							if (!p || !p->node) {
								continue;
							}
							p->pos = { at.x, at.y, ground + Random(120.0f, 450.0f) };
							p->ground = ground;
							LaunchLeaf(leaves, *p, 0.5f, 0.0f, true);
							p->vel = windDir * (speed * Random(0.6f, 1.0f)) + kUp * Random(-20.0f, 40.0f);
						} else {
							Throw(static_cast<Kind>(k), at, ground, windDir * (speed * Random(0.5f, 0.9f)), Random(120.0f, 260.0f));
						}
					} else if (tuning.puffs && kPuffPool[k] != kPools) {
						auto&      pool = pools[kPuffPool[k]];
						const auto p = Acquire(pool);
						if (!p || !p->node) {
							continue;
						}
						p->pos = { at.x, at.y, ground };
						p->ground = ground;
						LaunchPuff(pool, *p, static_cast<Kind>(k), 0.6f);
						p->vel = windDir * (speed * Random(0.5f, 0.9f)) + kUp * Random(5.0f, 25.0f);
						p->life *= 1.5f;
					}
				}
			}
		}

		// ---- choosing the NPCs

		// the nearest NPCs (and creatures) around you, every half second; the ones staying keep their state
		void SelectNpcs(RE::PlayerCharacter* a_player, RE::Actor* a_playerMount, float a_dt)
		{
			selectTimer -= a_dt;
			if (selectTimer > 0.0f) {
				return;
			}
			selectTimer = 0.5f;
			std::vector<std::pair<float, RE::FormID>> nearby;
			const auto                                lists = RE::ProcessLists::GetSingleton();
			if (tuning.npcs && tuning.npcMax > 0 && lists) {
				const Vec   you = a_player->GetPosition();
				const float range2 = tuning.npcRange * tuning.npcRange;
				for (const auto& handle : lists->highActorHandles) {
					const auto actor = handle.get();
					if (!actor || actor.get() == a_player || actor.get() == a_playerMount || actor->IsDead() || actor->IsDisabled() || !actor->Get3D() ||
						actor->IsOnMount()) {
						continue;
					}
					const float d2 = (actor->GetPosition() - you).SqrLength();
					if (d2 > range2) {
						continue;
					}
					if (!tuning.npcCreatures) {
						const auto race = actor->GetRace();
						if (!race || !race->HasKeywordString("ActorTypeNPC")) {
							continue;
						}
					}
					nearby.emplace_back(d2, actor->GetFormID());
				}
				std::ranges::sort(nearby);
				if (nearby.size() > static_cast<std::size_t>(tuning.npcMax)) {
					nearby.resize(static_cast<std::size_t>(tuning.npcMax));
				}
			}
			// drop the ones no longer chosen (you stay first)
			for (auto it = movers.begin() + 1; it != movers.end();) {
				const auto id = (*it)->formID;
				if (std::ranges::none_of(nearby, [id](const auto& a_n) { return a_n.second == id; })) {
					Unwatch(**it);
					it = movers.erase(it);
				} else {
					++it;
				}
			}
			for (const auto& [d2, id] : nearby) {
				if (!FindMover(id)) {
					auto m = std::make_unique<Mover>();
					m->formID = id;
					m->wanderSeed = Random(0.0f, 6.2832f);
					movers.push_back(std::move(m));
				}
			}
			Diagnose(kLineNpcs, static_cast<std::uint32_t>(movers.size()), [] { return std::format("NPCs and creatures kicking things up: {}", movers.size() - 1); });
		}

		// ---- one mover's frame

		// how it moves this frame; false: gone or teleported
		void Track(Mover& a_m, RE::Actor* a_body, float a_dt)
		{
			const Vec pos = a_body->GetPosition();
			if (!a_m.hasLast || (pos - a_m.lastPos).Length() > 400.0f * a_m.footprint) {
				a_m.hasLast = true;
				a_m.lastPos = pos;
				a_m.velocity = {};
				a_m.kickingFor = 0.0f;
			}
			const Vec raw = (pos - a_m.lastPos) * (1.0f / a_dt);
			a_m.lastPos = pos;
			a_m.rawSpeed = raw.Length();
			a_m.velocity += (raw - a_m.velocity) * (1.0f - std::exp(-a_dt / 0.12f));
			a_m.pos = pos;
			a_m.speed = Flat(a_m.velocity).Length();
		}

		// steps, landing and attacks of one mover this frame
		void Kicks(Mover& a_m, RE::Actor* a_body, RE::Actor* a_actor, float a_dt)
		{
			mover = &a_m;
			rayFilter = a_m.player ? yourRayFilter : RayFilterOf(a_body);  // its kicks start under its own body
			if (a_m.speed > 1.0f) {
				a_m.dir = Flat(a_m.velocity) * (1.0f / a_m.speed);
			} else {
				const float heading = a_body->GetAngleZ();
				a_m.dir = { std::sin(heading), std::cos(heading), 0.0f };
			}
			a_m.side = { a_m.dir.y, -a_m.dir.x, 0.0f };
			// a big body's steps kick as hard at its slower pace
			a_m.kick = std::clamp((a_m.speed / std::sqrt(a_m.footprint) - 25.0f) / 300.0f, 0.0f, 1.8f);
			if (!a_m.mounted && tuning.sneakQuieter && a_actor->IsSneaking()) {
				a_m.kick *= 0.4f;
			}

			// on the landscape, or standing on an object (rock, cliff, street, walkway: known by the collision material
			// footstep sounds use); not jumping, falling or swimming
			const Vec   pos = a_m.pos;
			const float land = LandHeight(pos, pos.z - 1000.0f);
			a_m.swimming = a_body->AsActorState()->IsSwimming() || a_body->IsInWater();
			const auto controller = a_body->GetCharController();
			a_m.supported = controller && controller->context.currentState == RE::hkpCharacterStateType::kOnGround;
			const auto support = a_m.supported && controller ? Support(a_m.support, controller) : SupportInfo{};
			// an object when the physics says so (a reference holds it up), else the landscape when it is right under it
			const bool nearLand = pos.z - land < 18.0f * a_m.footprint && pos.z - land > -40.0f;
			a_m.onLand = !a_m.swimming && !support.object && (nearLand || support.layerTerrain);
			a_m.onObject = !a_m.onLand && a_m.supported && !a_m.swimming;
			a_m.materialIndex = a_m.onObject ? support.material : -1;
			a_m.objectCovers = {};
			// up on a thing that isn't ground (a log pile, a cart, a table): nothing to kick up there
			a_m.raised = a_m.onObject && !support.groundLike && pos.z - land > 30.0f * a_m.footprint;
			if (a_m.onObject && !a_m.raised) {
				for (const auto& [file, share] : support.textures) {
					const auto cover = Terrain::FileCover(file);
					for (std::size_t k = 0; k < kKinds; ++k) {
						a_m.objectCovers[k] += share * cover[k];
					}
				}
				for (const auto& [named, share] : support.named) {
					const auto cover = Terrain::ObjectCover(named, pos);
					for (std::size_t k = 0; k < kKinds; ++k) {
						a_m.objectCovers[k] += share * cover[k];
					}
				}
				if (support.otherShare > 0.01f) {
					const auto cover = Terrain::ObjectCover(a_m.materialIndex, pos);
					for (std::size_t k = 0; k < kKinds; ++k) {
						a_m.objectCovers[k] += support.otherShare * cover[k];
					}
				}
			}
			a_m.groundZ = a_m.onLand ? land : pos.z;
			a_m.canKick = (a_m.onLand || a_m.onObject) && (!a_m.mounted || tuning.onHorse);
			const bool moving = a_m.canKick && a_m.kick > 0.0f;
			// kicking only after moving for the start delay (stops shorter than 0.3 s don't start it over)
			if (moving) {
				a_m.kickingFor += a_dt;
				a_m.notKickingFor = 0.0f;
			} else if ((a_m.notKickingFor += a_dt) > 0.3f) {
				a_m.kickingFor = 0.0f;
			}
			a_m.kicking = moving && a_m.kickingFor >= tuning.startDelay;
			a_m.covers = a_m.onLand ? Terrain::Cover(pos) : a_m.objectCovers;

			if (a_m.player) {
				lastOnLand = a_m.onLand || a_m.onObject;
				objectName = !a_m.onObject ? "" : a_m.materialIndex >= 0 ? Terrain::Materials()[a_m.materialIndex].name : "other";
				objectTexture = a_m.onObject && support.otherShare < 0.5f ? support.texture : "";
				raised = a_m.raised;
				const auto& covers = a_m.covers;
				const auto  material = controller ? controller->surfaceMaterial : RE::MATERIAL_ID::kNone;
				Diagnose(kLineGround,
					(land > pos.z - 999.0f ? 1u : 0u) | (a_m.onLand ? 2u : 0u) | (a_m.swimming ? 4u : 0u) | (a_m.mounted ? 8u : 0u) | (a_m.onObject ? 16u : 0u) |
						(a_m.raised ? 32u : 0u) | (static_cast<std::uint32_t>(a_m.materialIndex + 1) << 8),
					[&] {
						return std::format("ground: land found {}, on the landscape {}, on an object {}{}, swimming {}, mounted {}{}, support layer {}, gives leaves {:.0f}% green leaves {:.0f}% dust {:.0f}% rocks {:.0f}% snow {:.0f}%",
							land > pos.z - 999.0f, a_m.onLand, a_m.onObject,
							a_m.onObject ? std::format(" ({}{}{}{}{})", a_m.materialIndex >= 0 ? Terrain::Materials()[a_m.materialIndex].name : std::format("material {}", std::to_underlying(material)),
											   support.model.empty() ? "" : std::format(", {}", support.model),
											   support.layerColor.empty() ? "" : std::format(", painted layer {}", support.layerColor),
											   support.textures.empty() && support.named.empty() ? "" :
																									  std::format(", textures tell {:.0f}% (landscape: {})", (1.0f - support.otherShare) * 100.0f,
																										  support.texture.empty() ? "none" : support.texture),
											   a_m.raised ? ", raised: not ground" : support.groundLike ? ", ground" : "") :
										   "",
							a_m.swimming, a_m.mounted, a_m.onLand ? "" : std::format(" ({:.0f} units above the land)", pos.z - land), support.layer, covers[0] * 100.0f,
							covers[1] * 100.0f, covers[2] * 100.0f, covers[3] * 100.0f, covers[4] * 100.0f);
					});
				const auto tens = [](float a_cover) { return static_cast<std::uint32_t>(std::round(a_cover * 10.0f)); };
				Diagnose(kLineCover, tens(covers[0]) | tens(covers[1]) << 4 | tens(covers[2]) << 8 | tens(covers[3]) << 12 | tens(covers[4]) << 16, [&] {
					return std::format("under you: leaves about {}0%, green leaves {}0%, dust {}0%, rocks {}0%, snow {}0%", tens(covers[0]), tens(covers[1]), tens(covers[2]),
						tens(covers[3]), tens(covers[4]));
				});

				if (testRequested.exchange(false)) {
					Burst(Kind::kLeaves, 30, true);
					Burst(Kind::kGreen, 20, true);
					Burst(Kind::kDirt, 16, true);
					Burst(Kind::kRock, 16, true);
					Burst(Kind::kSnow, 14, true);
					logs::info("test: threw leaves, green leaves, dust, rocks and snow around you at {:.0f}, {:.0f}, {:.0f}", pos.x, pos.y, pos.z);
				}
			}

			// Its steps: the kicks come with each footfall (left, right), not as a steady stream. They ease in after the
			// start delay and out when it stops, follow changes of pace softly, and some stretches and steps kick up
			// more than others.
			a_m.flow += ((a_m.kicking ? 1.0f : 0.0f) - a_m.flow) * (1.0f - std::exp(-a_dt / (a_m.kicking ? kFlowIn : kFlowOut)));
			a_m.kickSmooth += (a_m.kick - a_m.kickSmooth) * (1.0f - std::exp(-a_dt / 0.35f));
			const float cadence = (1.5f + std::min(a_m.speed, 700.0f) / 400.0f) / std::sqrt(a_m.footprint) * (a_m.mounted ? 2.0f : 1.0f);  // footfalls per second
			a_m.stepPhase += cadence * a_dt;
			const bool footfall = a_m.stepPhase >= 1.0f;
			if (footfall) {
				a_m.stepPhase -= std::floor(a_m.stepPhase);
				a_m.foot = -a_m.foot;
			}
			const float wander = std::clamp(0.75f + 0.3f * std::sin(clock * 0.83f + a_m.wanderSeed) + 0.2f * std::sin(clock * 2.1f + 2.0f * a_m.wanderSeed), 0.25f, 1.25f);
			for (std::size_t k = 0; k < kKinds; ++k) {
				const auto kind = static_cast<Kind>(k);
				if (!moving || a_m.flow < 0.01f || !tuning.kinds[k].enabled || a_m.covers[k] <= 0.0f) {  // none under its feet: no spots to try
					if (a_m.flow < 0.01f || !moving) {
						a_m.emitDebt[k] = 0.0f;
						a_m.stepLeft[k] = 0.0f;
					}
					continue;
				}
				if (footfall) {
					const float rate = tuning.kinds[k].amount * kBaseRate[k] * std::pow(a_m.kickSmooth, 1.15f) * (a_m.mounted ? 1.6f : 1.0f) * std::sqrt(a_m.footprint);
					a_m.stepLeft[k] = std::min(a_m.stepLeft[k], 1.0f) + rate / cadence * a_m.flow * wander * Random(0.5f, 1.5f);
					a_m.stepRate[k] = a_m.stepLeft[k] / kStepRelease;
				}
				const float give = std::min(a_m.stepLeft[k], a_m.stepRate[k] * a_dt);
				a_m.stepLeft[k] -= give;
				a_m.emitDebt[k] += give;
				int count = std::min(static_cast<int>(a_m.emitDebt[k]), 12);
				a_m.emitDebt[k] = std::min(a_m.emitDebt[k] - static_cast<float>(count), 2.0f);
				while (count-- > 0) {
					SpawnStep(kind, a_m.kickSmooth, a_m.foot);
				}
			}

			// landing from a jump or a fall throws up a ring
			if (!a_m.onLand && !a_m.supported && !a_m.swimming) {
				a_m.airborneFor += a_dt;
			} else {
				if (a_m.canKick && a_m.airborneFor > 0.35f) {
					for (std::size_t k = 0; k < kKinds; ++k) {
						if (tuning.kinds[k].enabled && a_m.covers[k] > 0.05f) {
							const float count = (Flutters(static_cast<Kind>(k)) ? 10.0f : 8.0f) * (a_m.mounted ? 1.8f : 1.0f) * a_m.footprint;
							Burst(static_cast<Kind>(k), static_cast<int>(std::round(tuning.kinds[k].amount * count * a_m.covers[k])), false);
						}
					}
				}
				a_m.airborneFor = 0.0f;
			}

			Swing(a_m, a_actor, a_dt);
		}

		bool LoadModel(const char* a_path)
		{
			RE::NiPointer<RE::NiNode> model;
			if (RE::BSModelDB::Demand(a_path, model, RE::BSModelDB::DBTraits::ArgsType{}) != RE::BSResource::ErrorCode::kNone || !model) {
				logs::error("couldn't load meshes\\{} (is the whole mod installed?)", a_path);
				return false;
			}
			models.push_back(model);
			for (const auto& child : model->GetChildren()) {
				if (!child) {
					continue;
				}
				const std::string_view name(child->name.c_str());
				PoolIndex              pool = kPools;
				if (name.starts_with("Leaf")) {
					pool = kLeafPool;
				} else if (name.starts_with("GreenLeaf")) {
					pool = kGreenPool;
				} else if (name.starts_with("RockPebble")) {
					pool = kRockPebbles;
				} else if (name.starts_with("RockDust")) {
					pool = kRockDust;
				} else if (name.starts_with("DirtClod")) {
					pool = kDirtClods;
				} else if (name.starts_with("DirtDust")) {
					pool = kDirtDust;
				} else if (name.starts_with("SnowFlake")) {
					pool = kSnowFlakes;
				}
				if (pool < kPools) {
					pools[pool].templates.emplace_back(child);
				}
			}
			return true;
		}

		void ClearMovers()
		{
			for (auto it = movers.begin() + 1; it != movers.end(); ++it) {
				Unwatch(**it);
			}
			movers.resize(1);
			auto& you = *movers.front();
			you.hasLast = false;
			you.airborneFor = 0.0f;
			you.kickingFor = 0.0f;
			you.emitDebt = {};
			you.stepLeft = {};
			you.flow = 0.0f;
			you.kickSmooth = 0.0f;
			you.swingDebt = {};
			you.blades = {};
		}
	}

	void Init()
	{
		LoadModel(kLeafModel);
		LoadModel(kDebrisModel);
		std::string counts;
		for (auto& pool : pools) {
			pool.items.resize(500);
			counts += std::format("{}{} {}", counts.empty() ? "" : ", ", pool.templates.size(), pool.name);
		}
		container.reset(RE::NiNode::Create(0));
		auto you = std::make_unique<Mover>();
		you->player = true;
		you->formID = 0x14;
		movers.push_back(std::move(you));
		if (const auto events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESSpellCastEvent>(EventSink::GetSingleton());
			events->AddEventSink<RE::TESDeathEvent>(EventSink::GetSingleton());
		}
		logs::info("models loaded: {}", counts);
	}

	void Reset()
	{
		for (auto& pool : pools) {
			const auto end = std::min(pool.used, pool.items.size());
			for (std::size_t i = 0; i < end; ++i) {
				if (Active(pool.items[i])) {
					Hide(pool, pool.items[i]);
				}
			}
			pool.active = 0;
			pool.used = 0;
		}
		flyingCount = {};
		landedCount = {};
		Sounds::Reset();
		waves.clear();
		corpses.clear();
		patches.clear();
		if (!movers.empty()) {
			ClearMovers();
		}
		std::scoped_lock guard(signalLock);
		swingSignals.clear();
		shoutSignals.clear();
		hitSignals.clear();
		deathSignals.clear();
		blastSignals.clear();
		blasts.clear();
	}

	void Update(float a_delta)
	{
		if (!container || movers.empty()) {
			return;
		}
		const float dt = std::clamp(a_delta, 0.0f, 0.05f);
		if (dt <= 0.0f) {
			return;
		}
		clock += dt;
		// the settings only change in the menu
		if (const auto revision = Settings::Revision(); revision != settingsRevision) {
			settingsRevision = revision;
			ReadTuning();
			Terrain::Apply();
		}

		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto cell = player ? player->GetParentCell() : nullptr;
		const bool exterior = cell && cell->IsExteriorCell() && player->Get3D();
		if (!tuning.enabled || !exterior) {
			if (wasExterior || ActiveTotal() > 0 || movers.size() > 1) {
				Reset();
				DetachContainer();
			}
			wasExterior = false;
			if (!exterior) {
				PublishStatus(dt, false, false, nullptr);
			}
			return;
		}
		wasExterior = true;

		// you, or the horse you ride; the NPCs near you
		auto&                    you = *movers.front();
		RE::NiPointer<RE::Actor> mount;
		const bool               mounted = player->IsOnMount() && player->GetMount(mount) && mount;
		RE::Actor*               body = mounted ? mount.get() : player;
		const Vec                before = you.lastPos;
		const bool               hadLast = you.hasLast;
		you.mounted = mounted;
		you.footprint = mounted ? 2.6f : 1.0f;
		Watch(you, player);
		Track(you, body, dt);
		if (hadLast && (you.pos - before).Length() > 400.0f) {
			Reset();  // fast travel, a door, a teleport
			return;
		}
		SelectNpcs(player, mounted ? mount.get() : nullptr, dt);

		// animation events and casts since the last frame
		std::vector<RE::FormID> swings, shouts, hits, deaths;
		{
			std::scoped_lock guard(signalLock);
			swings.swap(swingSignals);
			shouts.swap(shoutSignals);
			hits.swap(hitSignals);
			deaths.swap(deathSignals);
			blasts.insert(blasts.end(), blastSignals.begin(), blastSignals.end());
			blastSignals.clear();
		}
		for (const auto id : hits) {
			if (const auto m = FindMover(id)) {
				m->hitFrameAt = clock;
			}
		}
		for (const auto id : deaths) {
			WatchDeath(id);
		}
		for (const auto id : swings) {
			if (const auto m = FindMover(id)) {
				if (clock > m->swingUntil) {
					m->blades = {};  // a new swing: no motion from the last one
				}
				m->swingUntil = clock + kSwingWindow;
			}
		}
		for (const auto id : shouts) {
			Shout(RE::TESForm::LookupByID<RE::Actor>(id));
		}

		// the NPCs' motion (cheap); who needs the full work this frame
		bool anyAwake = false;
		for (auto& m : movers) {
			RE::Actor* actor = m->player ? player : RE::TESForm::LookupByID<RE::Actor>(m->formID);
			m->actor = actor && actor->Get3D() ? actor : nullptr;
			if (!m->actor) {
				m->awake = false;
				continue;
			}
			if (!m->player) {
				Watch(*m, actor);
				m->mounted = false;
				Track(*m, actor, dt);
			}
			m->awake = m->rawSpeed >= 3.0f || m->speed >= 3.0f || clock <= m->swingUntil || m->airborneFor > 0.0f;
			anyAwake = anyAwake || m->awake;
		}

		ReadWind();

		// everyone standing still with nothing in the air, no wind: nothing else to do (no ground lookups, no scene updates)
		if (ActiveTotal() == 0 && !anyAwake && waves.empty() && corpses.empty() && blasts.empty() && WindLift() <= 0.0f && !testRequested) {
			you.kick = 0.0f;
			you.emitDebt = {};
			you.stepLeft = {};
			you.flow = 0.0f;
			you.kickSmooth = 0.0f;
			you.kickingFor = 0.0f;  // standing still: the start delay begins again
			flyingCount = {};
			landedCount = {};
			PublishStatus(dt, true, lastOnLand, nullptr);
			return;
		}

		if (!AttachContainer(player)) {
			Diagnose(kLineAttach, 1, [] { return std::string("no player skeleton to hang the particles on yet"); });
			return;
		}

		if (const auto camera = RE::Main::WorldRootCamera()) {
			cameraPos = camera->world.translate;
			Vec forward{ camera->world.rotate.entry[0][0], camera->world.rotate.entry[1][0], 0.0f };  // a camera looks along its X
			if (forward.Unitize() > 0.1f) {
				cameraFwd = forward;
			}
		}
		// rays for the real ground: through your own body (your horse's while you ride: from its back they hit it)
		havokWorld = cell->GetbhkWorld();
		yourRayFilter = RayFilterOf(body);
		rayFilter = yourRayFilter;

		// everyone's steps, landings and attacks
		for (auto& m : movers) {
			if (!m->awake && !(m->player && testRequested)) {
				m->kicking = false;
				m->kickingFor = 0.0f;
				m->flow = 0.0f;
				m->kickSmooth = 0.0f;
				m->stepLeft = {};
				continue;
			}
			if (!m->actor) {
				continue;
			}
			Kicks(*m, m->player ? body : m->actor, m->actor, dt);
		}
		rayFilter = yourRayFilter;
		Bodies(dt);
		PrunePatches(dt);
		WindLifts(you.pos, dt);
		for (const auto& b : blasts) {
			Explode(b, you.pos);
		}
		blasts.clear();
		AdvanceWaves(dt);
		Sounds::Update(dt, cameraPos);

		bool anySwinging = false;
		for (const auto& m : movers) {
			anySwinging = anySwinging || (m->awake && Swinging(*m));
		}

		// move everything (only the slots that can be in use); past the limit or far away goes
		flyingCount = {};
		landedCount = {};
		bool any = false;
		for (auto& pool : pools) {
			if (pool.active == 0) {
				pool.used = 0;
				continue;
			}
			std::size_t last = 0;
			const auto  end = std::min(pool.used, pool.items.size());
			for (std::size_t i = 0; i < end; ++i) {
				auto& p = pool.items[i];
				if (!Active(p)) {
					continue;
				}
				if (i >= pool.capacity || Flat(p.pos - you.pos).SqrLength() > kForget * kForget) {
					Hide(pool, p);
					continue;
				}
				if (anySwinging && pool.style != Style::kClump) {
					for (const auto& m : movers) {
						if (m->awake && Swinging(*m)) {
							SwingPush(*m, pool, p, dt);
						}
					}
				}
				if (!waves.empty()) {
					WavePush(pool, p);
				}
				Step(pool, p, dt);
				if (!Active(p)) {
					continue;
				}
				last = i + 1;
				if (p.phase == Phase::kAir) {
					++flyingCount[pool.kind];
				} else {
					++landedCount[pool.kind];
				}
				auto& local = p.node->local;
				local.translate = p.pos;
				local.rotate = pool.style == Style::kPuff ? Billboard(p.pos, cameraPos, p.roll) : ToMatrix(p.rot);
				if (p.appear < 1.0f) {
					p.appear = std::min(1.0f, p.appear + dt / kAppearTime);
				}
				local.scale = p.appear < 1.0f ? p.scale * (0.15f + 0.85f * Smooth(0.0f, 1.0f, p.appear)) : p.scale;
				if (p.shader && p.shader->material) {
					static_cast<RE::BSLightingShaderMaterialBase*>(p.shader->material)->materialAlpha = p.alpha;
				}
				any = true;
			}
			pool.used = last;
		}
		if (any) {
			RE::NiUpdateData update{};
			container->Update(update);
		}

		PublishStatus(dt, true, you.onLand || you.onObject, &you.covers);
	}

	void OnExplosion(const RE::NiPoint3& a_position, float a_radius)
	{
		std::scoped_lock guard(signalLock);
		if (blastSignals.size() < 16) {
			blastSignals.push_back({ a_position, a_radius });
		}
	}

	void RequestTest() { testRequested = true; }

	Status GetStatus()
	{
		std::scoped_lock guard(status_lock);
		return status;
	}
}
