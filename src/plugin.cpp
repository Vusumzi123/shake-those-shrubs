#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <windows.h>

namespace
{
	struct Config
	{
		RE::FormID targetRef = 0;                    // 0 = nearest pine of BaseForm
		RE::FormID baseForm = 0x0009DAA2;            // TreePineShrub01Snow
		RE::FormID sound = 0x00000DD0;               // ITMGenericUpLoFi
		float      radius = 90.0F;                   // horizontal game units
		float      yawDegrees = 4.0F;
		float      pitchDegrees = 3.0F;
		float      speed = 2.5F;                     // wobble, Hz
		float      spring = 2.2F;                    // decay per second
		float      leanFraction = 0.5F;              // share of the angle held while touching
		float      leanRate = 8.0F;                  // how fast the lean eases in/out
		float      minImpact = 0.3F;                 // strength at zero speed
		float      maxImpact = 1.0F;                 // strength at full speed
		float      impactSpeed = 350.0F;             // player speed giving full gain
		float      velocityInfluence = 1.0F;        // 0 = velocity ignored, 1 = full velocity scaling
		float      retriggerCooldown = 0.5F;        // seconds between repeats while still touched
		float      moveThreshold = 20.0F;           // speed below which a held contact does not repeat
		float      soundVolume = 1.0F;               // multiplier on the velocity-scaled volume
		bool       soundEnabled = true;
		bool       debug = false;
	};

	struct Sway
	{
		RE::FormID    refID = 0;
		RE::NiMatrix3 original;
		float         wobbleTime = 0.0F;
		float         lean = 0.0F;
		bool          captured = false;
		bool          touching = false;
		bool          wasTouching = false;
		float         gain = 1.0F;
		float         cooldown = 0.0F;
	};

	std::vector<Sway> g_sways;

	Config     g_config;
	Sway       g_sway;
	float      g_pollAccum = 0.0F;
	float      g_debugAccum = 0.0F;

	RE::FormID ParseFormID(std::string_view a_value)
	{
		const auto bar = a_value.find('|');
		if (bar != std::string_view::npos) {
			const std::string plugin{ a_value.substr(0, bar) };
			std::string       local{ a_value.substr(bar + 1) };
			const auto        id = std::strtoul(local.c_str(), nullptr, 16);
			auto*             handler = RE::TESDataHandler::GetSingleton();
			if (!handler) {
				return 0;
			}
			const auto* form = handler->LookupForm(static_cast<RE::FormID>(id), std::string_view{ plugin });
			return form ? form->GetFormID() : 0;
		}
		return static_cast<RE::FormID>(std::strtoul(std::string(a_value).c_str(), nullptr, 16));
	}

	void LoadConfig()
	{
		std::map<std::string, std::string> values;
		std::ifstream file("Data/SKSE/Plugins/ShakeThoseShrubs.ini");
		std::string   line;
		while (std::getline(file, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			const auto start = line.find_first_not_of(" \t");
			if (start == std::string::npos || line[start] == ';' || line[start] == '#' || line[start] == '[') {
				continue;
			}
			const auto eq = line.find('=', start);
			if (eq == std::string::npos) {
				continue;
			}
			auto key = line.substr(start, eq - start);
			auto value = line.substr(eq + 1);
			while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
				key.pop_back();
			}
			const auto valueStart = value.find_first_not_of(" \t");
			value = valueStart == std::string::npos ? "" : value.substr(valueStart);
			const auto valueEnd = value.find_last_not_of(" \t");
			if (valueEnd != std::string::npos) {
				value = value.substr(0, valueEnd + 1);
			}
			values[key] = value;
		}

		if (values.empty()) {
			SKSE::log::warn("ShakeThoseShrubs.ini missing or empty, using defaults");
		}

		const auto get = [&](const char* a_key, const char* a_default) {
			const auto it = values.find(a_key);
			return it == values.end() || it->second.empty() ? std::string{ a_default } : it->second;
		};
		const auto flag = [&](const char* a_key, bool a_default) {
			auto raw = get(a_key, a_default ? "1" : "0");
			for (auto& c : raw) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return raw != "0" && raw != "false" && raw != "no" && raw != "off";
		};
		const auto number = [&](const char* a_key, float a_default, float a_low, float a_high) {
			const auto raw = std::strtof(get(a_key, std::to_string(a_default).c_str()).c_str(), nullptr);
			return raw < a_low ? a_low : (raw > a_high ? a_high : raw);
		};

		g_config.targetRef = ParseFormID(get("TargetRef", "0"));
		g_config.baseForm = ParseFormID(get("BaseForm", "0x0009DAA2"));
		g_config.sound = ParseFormID(get("Sound", "0x00000DD0"));
		g_config.radius = number("Radius", 90.0F, 10.0F, 400.0F);
		g_config.yawDegrees = number("YawDegrees", 4.0F, 0.0F, 25.0F);
		g_config.pitchDegrees = number("PitchDegrees", 3.0F, 0.0F, 25.0F);
		g_config.speed = number("Speed", 2.5F, 0.3F, 10.0F);
		g_config.spring = number("Spring", 2.2F, 0.3F, 10.0F);
		g_config.leanFraction = number("LeanFraction", 0.5F, 0.0F, 1.0F);
		g_config.leanRate = number("LeanRate", 8.0F, 1.0F, 30.0F);
		g_config.minImpact = number("MinImpact", 0.3F, 0.0F, 2.0F);
		g_config.maxImpact = number("MaxImpact", 1.0F, 0.0F, 2.0F);
		g_config.impactSpeed = number("ImpactSpeed", 350.0F, 50.0F, 900.0F);
		g_config.velocityInfluence = number("VelocityInfluence", 1.0F, 0.0F, 1.0F);
		g_config.retriggerCooldown = number("RetriggerCooldown", 0.5F, 0.1F, 5.0F);
		g_config.moveThreshold = number("MoveThreshold", 20.0F, 0.0F, 1000.0F);
		g_config.soundVolume = number("SoundVolume", 1.0F, 0.0F, 1.0F);
		g_config.soundEnabled = flag("SoundEnabled", true);
		g_config.debug = flag("Debug", false);
	}

	float FlatDistance(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b)
	{
		const auto dx = a_a.x - a_b.x;
		const auto dy = a_a.y - a_b.y;
		return std::sqrt(dx * dx + dy * dy);
	}

	void PlayImpact(float a_gain)
	{
		if (!g_config.soundEnabled || g_config.sound == 0) {
			return;
		}
		auto* manager = RE::BSAudioManager::GetSingleton();
		if (!manager) {
			return;
		}
		const auto volume = (std::min)(1.0F, (std::max)(0.0F, a_gain * g_config.soundVolume));

		auto* form = RE::TESForm::LookupByID(g_config.sound);
		auto* descriptor = form ? form->As<RE::BGSSoundDescriptorForm>() : nullptr;
		RE::BSSoundHandle handle{};
		if (!descriptor || !manager->GetSoundHandle(handle, descriptor)) {
			manager->Play(g_config.sound);
			SKSE::log::info("sound fallback, vol {:.2f}", volume);
			return;
		}
		handle.SetVolume(volume);
		handle.Play();
		SKSE::log::info("sound vol {:.2f}", volume);
	}

	void Poll()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!cell) {
			g_sways.clear();
			return;
		}

		const auto scan = g_config.radius + 128.0F;
		std::vector<RE::TESObjectREFR*> hits;
		hits.reserve(8);

		if (g_config.targetRef != 0) {
			auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(g_config.targetRef);
			if (ref && ref->Get3D() && FlatDistance(ref->GetPosition(), player->GetPosition()) <= g_config.radius) {
				hits.push_back(ref);
			}
		} else if (g_config.baseForm != 0) {
			cell->ForEachReferenceInRange(player->GetPosition(), scan, [&](RE::TESObjectREFR* a_ref) {
				if (!a_ref || !a_ref->Get3D() || !a_ref->GetBaseObject() || a_ref->GetBaseObject()->GetFormID() != g_config.baseForm) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				if (FlatDistance(a_ref->GetPosition(), player->GetPosition()) > g_config.radius) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				if (hits.size() < 8) {
					hits.push_back(a_ref);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		for (auto& sway : g_sways) {
			sway.wasTouching = sway.touching;
			sway.touching = false;
		}

		RE::NiPoint3 velocity;
		player->GetLinearVelocity(velocity);
		const auto speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);

		for (auto* ref : hits) {
			const auto id = ref->GetFormID();
			const auto distance = FlatDistance(ref->GetPosition(), player->GetPosition());
			const auto t = (std::min)(1.0F, (std::max)(0.0F, speed / g_config.impactSpeed));
			const auto scaled = g_config.minImpact + (g_config.maxImpact - g_config.minImpact) * t;
			const auto gain = g_config.maxImpact - g_config.velocityInfluence * (g_config.maxImpact - scaled);

			auto it = std::find_if(g_sways.begin(), g_sways.end(), [id](const Sway& a_sway) { return a_sway.refID == id; });
			if (it == g_sways.end()) {
				g_sways.push_back(Sway{ .refID = id, .gain = gain });
				it = g_sways.end() - 1;
				it->cooldown = g_config.retriggerCooldown;
				SKSE::log::info("enter ref {:08X} flat {:.0f} speed {:.0f} gain {:.2f}", id, distance, speed, gain);
				PlayImpact(gain);
			} else if (!it->wasTouching) {
				it->gain = gain;
				it->wobbleTime = 0.0F;
				it->cooldown = g_config.retriggerCooldown;
				SKSE::log::info("re-enter ref {:08X} speed {:.0f} gain {:.2f}", id, speed, gain);
				PlayImpact(gain);
			} else if (speed >= g_config.moveThreshold && it->cooldown <= 0.0F) {
				it->gain = gain;
				it->wobbleTime = 0.0F;
				it->cooldown = g_config.retriggerCooldown;
				SKSE::log::info("retrigger ref {:08X} speed {:.0f} gain {:.2f}", id, speed, gain);
				PlayImpact(gain);
			}

			it->touching = true;
		}
	}

	void UpdateSways(float a_delta)
	{
		for (auto it = g_sways.begin(); it != g_sways.end();) {
			auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(it->refID);
			auto* node = ref ? ref->Get3D() : nullptr;
			if (!node) {
				it = g_sways.erase(it);
				continue;
			}
			if (it->cooldown > 0.0F) {
				it->cooldown = (std::max)(0.0F, it->cooldown - a_delta);
			}

			if (!it->captured) {
				it->original = node->local.rotate;
				it->captured = true;
			}

			const auto target = it->touching ? 1.0F : 0.0F;
			const auto ease = (std::min)(1.0F, a_delta * g_config.leanRate);
			it->lean += (target - it->lean) * ease;
			it->wobbleTime += a_delta;

			const auto wobble = it->touching ?
			                        std::exp(-g_config.spring * it->wobbleTime) * std::sin(it->wobbleTime * 6.283185F * g_config.speed) :
			                        0.0F;
			const auto amount = (it->lean * g_config.leanFraction + wobble) * it->gain;

			RE::NiMatrix3 yaw;
			yaw.MakeZRotation(g_config.yawDegrees * 0.017453292F * amount);
			RE::NiMatrix3 pitch;
			pitch.MakeXRotation(g_config.pitchDegrees * 0.017453292F * amount);
			node->local.rotate = it->original * yaw * pitch;

			RE::NiUpdateData data{};
			node->UpdateTransformAndBounds(data);

			if (!it->touching && it->lean < 0.01F) {
				node->local.rotate = it->original;
				node->UpdateTransformAndBounds(data);
				it = g_sways.erase(it);
				continue;
			}
			++it;
		}
	}

	void OnFrame()
	{
		const auto* timer = RE::BSTimer::GetSingleton();
		if (!timer) {
			return;
		}
		const auto delta = timer->delta;

		g_pollAccum += delta;
		if (g_pollAccum >= 0.1F) {
			g_pollAccum = 0.0F;
			Poll();
		}

		if (!g_sways.empty()) {
			UpdateSways(delta);
		}

		if (g_config.debug) {
			g_debugAccum += delta;
			if (g_debugAccum >= 1.0F) {
				g_debugAccum = 0.0F;
				auto* player = RE::PlayerCharacter::GetSingleton();
				RE::NiPoint3 velocity;
				if (player) {
					player->GetLinearVelocity(velocity);
				}
				SKSE::log::info("debug: speed {:.0f} active {}", std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y), g_sways.size());
			}
		}
	}

	class FrameSink final : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const*, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			OnFrame();
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	FrameSink g_frameSink;

	namespace menu
	{
		using SliderFloatFn = bool (*)(const char*, float*, float, float, const char*, int);
		using CheckboxFn = bool (*)(const char*, bool*);
		using AddSectionItemFn = void (*)(const char*, void (*)());

		SliderFloatFn g_slider = nullptr;
		CheckboxFn    g_checkbox = nullptr;

		void WINAPI Render()
		{
			if (!g_slider) {
				return;
			}
			g_slider("Yaw (degrees)", &g_config.yawDegrees, 0.0F, 25.0F, "%.1f", 0);
			g_slider("Pitch (degrees)", &g_config.pitchDegrees, 0.0F, 25.0F, "%.1f", 0);
			g_slider("Speed (Hz)", &g_config.speed, 0.3F, 10.0F, "%.2f", 0);
			g_slider("Spring (decay)", &g_config.spring, 0.3F, 10.0F, "%.2f", 0);
			g_slider("Lean while touching", &g_config.leanFraction, 0.0F, 1.0F, "%.2f", 0);
			g_slider("Lean rate", &g_config.leanRate, 1.0F, 30.0F, "%.0f", 0);
			g_slider("Velocity impact", &g_config.velocityInfluence, 0.0F, 1.0F, "%.2f", 0);
			g_slider("Strength at zero speed", &g_config.minImpact, 0.0F, 2.0F, "%.2f", 0);
			g_slider("Strength at full speed", &g_config.maxImpact, 0.0F, 2.0F, "%.2f", 0);
			g_slider("Repeat cooldown", &g_config.retriggerCooldown, 0.1F, 5.0F, "%.2f", 0);
			g_slider("Move threshold", &g_config.moveThreshold, 0.0F, 1000.0F, "%.0f", 0);
			g_slider("Speed for full shake", &g_config.impactSpeed, 50.0F, 900.0F, "%.0f", 0);
			g_slider("Sound volume", &g_config.soundVolume, 0.0F, 1.0F, "%.2f", 0);
			if (g_checkbox) {
				g_checkbox("Play sound", &g_config.soundEnabled);
			}
			g_slider("Radius", &g_config.radius, 10.0F, 400.0F, "%.0f", 0);
		}

		void Init()
		{
			auto* module = ::GetModuleHandleA("SKSEMenuFramework.dll");
			if (!module) {
				SKSE::log::warn("SKSE Menu Framework not loaded, sliders unavailable");
				return;
			}

			const auto addItem = reinterpret_cast<AddSectionItemFn>(::GetProcAddress(module, "AddSectionItem"));
			g_slider = reinterpret_cast<SliderFloatFn>(::GetProcAddress(module, "igSliderFloat"));
			g_checkbox = reinterpret_cast<CheckboxFn>(::GetProcAddress(module, "igCheckbox"));
			if (!addItem || !g_slider) {
				SKSE::log::warn("SKSE Menu Framework API not found");
				return;
			}
			addItem("Shake Those Shrubs/Settings", &Render);
			SKSE::log::info("page registered: Shake Those Shrubs/Settings");
		}
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type != SKSE::MessagingInterface::kDataLoaded) {
			return;
		}

		LoadConfig();
		SKSE::log::info("Shake Those Shrubs ready: base {:08X}, ref {:08X}, sound {:08X}, radius {:.0f}",
			g_config.baseForm, g_config.targetRef, g_config.sound, g_config.radius);

		if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
			input->AddEventSink(&g_frameSink);
		}
		menu::Init();
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
