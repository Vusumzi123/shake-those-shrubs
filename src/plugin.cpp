#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <unordered_set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <algorithm>
#include <windows.h>

#include "RE/T/TESDataHandler.h"
#include "RE/T/TESFlora.h"
#include "RE/T/TESModel.h"
#include "RE/T/TESObjectACTI.h"
#include "RE/T/TESObjectSTAT.h"
#include "RE/T/TESObjectTREE.h"
#include "RE/T/TESProduceForm.h"

namespace
{
	struct Config
	{
		RE::FormID targetRef = 0;                    // 0 = nearest matching shrub
		RE::FormID baseForm = 0x0009DAA2;            // TreePineShrub01Snow
		std::vector<std::string> tagEditorIDs;      // lowercase model path substrings
		bool        harvestable = true;             // treat every harvestable plant base form as a shrub
		std::vector<RE::FormID>   extraBaseForms;    // from BaseForms= and patch files
		bool        dumpBaseForms = false;          // write the built set next to the INI
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
	std::unordered_set<RE::FormID> g_shrubBases;
	std::set<RE::FormID> g_seenBases;
	RE::FormID     g_seenCell = 0;

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

	std::map<std::string, std::string> ReadValues(const char* a_path)
	{
		std::map<std::string, std::string> values;
		std::ifstream file(a_path);
		std::string   line;
		while (std::getline(file, line)) {
			if (line.size() >= 3 && line[0] == '\xEF' && line[1] == '\xBB' && line[2] == '\xBF') {
				line.erase(0, 3);
			}
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
		return values;
	}

	void AppendFormList(std::vector<RE::FormID>& a_out, std::string a_list)
	{
		while (!a_list.empty()) {
			const auto comma = a_list.find(',');
			std::string token = a_list.substr(0, comma);
			a_list = comma == std::string::npos ? "" : a_list.substr(comma + 1);
			const auto begin = token.find_first_not_of(" \t");
			const auto end = token.find_last_not_of(" \t");
			token = begin == std::string::npos ? "" : token.substr(begin, end - begin + 1);
			if (!token.empty()) {
				if (const auto id = ParseFormID(token); id != 0) {
					a_out.push_back(id);
				} else {
					SKSE::log::warn("BaseForms entry '{}' did not resolve", token);
				}
			}
		}
	}

	void LoadPatches()
	{
		namespace fs = std::filesystem;
		const char* dir = "Data/SKSE/Plugins/ShakeThoseShrubsPatches";
		std::error_code ec;
		if (!fs::is_directory(dir, ec)) {
			return;
		}
		std::size_t files = 0;
		try {
			for (const auto& entry : fs::directory_iterator(dir)) {
				std::error_code entryEc;
				std::string     ext = entry.path().extension().string();
				for (auto& c : ext) {
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				if (!entry.is_regular_file(entryEc) || entryEc || ext != ".ini") {
					continue;
				}
				const auto values = ReadValues(entry.path().string().c_str());
				const auto it = values.find("BaseForms");
				if (it != values.end()) {
					AppendFormList(g_config.extraBaseForms, it->second);
					++files;
				}
			}
		} catch (const std::exception& e) {
			SKSE::log::warn("patch scan failed: {}", e.what());
		}
		if (files > 0) {
			SKSE::log::info("read BaseForms from {} patch file(s)", files);
		}
	}

	void LoadConfig()
	{
		auto values = ReadValues("Data/SKSE/Plugins/ShakeThoseShrubs.ini");
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
		g_config.harvestable = flag("Harvestable", true);
		g_config.dumpBaseForms = flag("DumpBaseForms", false);
		AppendFormList(g_config.extraBaseForms, get("BaseForms", ""));
		const auto tagIt = values.find("TagEditorID");
		std::string tagList = tagIt == values.end() ? "Shrub,Bush,Fern,Thistle,Brush" : tagIt->second;
		while (!tagList.empty()) {
			const auto comma = tagList.find(',');
			std::string token = tagList.substr(0, comma);
			tagList = comma == std::string::npos ? "" : tagList.substr(comma + 1);
			const auto begin = token.find_first_not_of(" \t");
			const auto end = token.find_last_not_of(" \t");
			token = begin == std::string::npos ? "" : token.substr(begin, end - begin + 1);
			for (auto& c : token) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			if (!token.empty()) {
				g_config.tagEditorIDs.push_back(token);
			}
		}
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
		LoadPatches();
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

	bool MatchesTag(RE::TESForm* a_base)
	{
		if (g_config.tagEditorIDs.empty()) {
			return false;
		}
		const auto* model = a_base->As<RE::TESModel>();
		if (!model || !model->GetModel()) {
			return false;
		}
		std::string haystack{ model->GetModel() };
		for (auto& c : haystack) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		for (const auto& token : g_config.tagEditorIDs) {
			if (haystack.find(token) != std::string::npos) {
				return true;
			}
		}
		return false;
	}

	bool IsHarvestable(RE::TESForm* a_base)
	{
		if (a_base->GetFormType() == RE::FormType::Flora) {
			return true;
		}
		auto* produce = a_base->As<RE::TESProduceForm>();
		return produce && produce->produceItem;
	}

	bool IsCandidateType(RE::TESForm* a_base)
	{
		if (!a_base) {
			return false;
		}
		switch (a_base->GetFormType()) {
		case RE::FormType::Static:
		case RE::FormType::Tree:
		case RE::FormType::Flora:
		case RE::FormType::Activator:
			return true;
		default:
			return false;
		}
	}

	bool IsShrub(RE::TESForm* a_base)
	{
		return a_base && g_shrubBases.contains(a_base->GetFormID());
	}

	void BuildShrubBases()
	{
		g_shrubBases.clear();
		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			return;
		}

		const auto add = [](RE::TESForm* a_form) {
			if (!a_form) {
				return;
			}
			if (a_form->GetFormID() == g_config.baseForm || (g_config.harvestable && IsHarvestable(a_form)) || MatchesTag(a_form)) {
				g_shrubBases.insert(a_form->GetFormID());
			}
		};
		for (auto* form : handler->GetFormArray<RE::TESObjectSTAT>()) {
			add(form);
		}
		for (auto* form : handler->GetFormArray<RE::TESObjectTREE>()) {
			add(form);
		}
		for (auto* form : handler->GetFormArray<RE::TESFlora>()) {
			add(form);
		}
		for (auto* form : handler->GetFormArray<RE::TESObjectACTI>()) {
			add(form);
		}
		for (const auto id : g_config.extraBaseForms) {
			if (auto* form = RE::TESForm::LookupByID(id)) {
				g_shrubBases.insert(form->GetFormID());
			} else {
				SKSE::log::warn("BaseForms {:08X} does not resolve to a loaded form", id);
			}
		}
		if (g_config.baseForm != 0) {
			if (auto* form = RE::TESForm::LookupByID(g_config.baseForm)) {
				g_shrubBases.insert(form->GetFormID());
			} else {
				SKSE::log::warn("BaseForm {:08X} does not resolve to a loaded form", g_config.baseForm);
			}
		}
		SKSE::log::info("{} base forms match", g_shrubBases.size());

		if (!g_config.dumpBaseForms) {
			return;
		}
		std::ofstream out("Data/SKSE/Plugins/ShakeThoseShrubs_bases.txt");
		if (!out) {
			SKSE::log::warn("could not write ShakeThoseShrubs_bases.txt");
			return;
		}
		for (const auto id : g_shrubBases) {
			auto* form = RE::TESForm::LookupByID(id);
			const auto* model = form ? form->As<RE::TESModel>() : nullptr;
			const char* path = model ? model->GetModel() : nullptr;
			out << std::format("{:08X} {}\n", id, path ? path : "");
		}
		SKSE::log::info("base form dump written");
	}

	void Poll()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!cell) {
			g_sways.clear();
			return;
		}

		if (cell->GetFormID() != g_seenCell) {
			g_seenCell = cell->GetFormID();
			g_seenBases.clear();
		}

		const auto scan = g_config.radius + 128.0F;
		std::vector<RE::TESObjectREFR*> hits;
		hits.reserve(8);

		if (g_config.targetRef != 0) {
			auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(g_config.targetRef);
			if (ref && ref->Get3D() && FlatDistance(ref->GetPosition(), player->GetPosition()) <= g_config.radius) {
				hits.push_back(ref);
			}
		} else if (!g_shrubBases.empty()) {
			std::vector<std::pair<float, RE::TESObjectREFR*>> candidates;
			cell->ForEachReferenceInRange(player->GetPosition(), scan, [&](RE::TESObjectREFR* a_ref) {
				if (!a_ref || !a_ref->Get3D()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto distance = FlatDistance(a_ref->GetPosition(), player->GetPosition());
				if (distance > g_config.radius) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto* base = a_ref->GetBaseObject();
				if (!IsShrub(base)) {
					if (g_config.debug && IsCandidateType(base) && g_seenBases.size() < 64 && g_seenBases.insert(base->GetFormID()).second) {
						auto* model = base->As<RE::TESModel>();
						const char* modelPath = model ? model->GetModel() : nullptr;
						SKSE::log::info("near base {:08X} model '{}'", base->GetFormID(), modelPath ? modelPath : "");
					}
					return RE::BSContainer::ForEachResult::kContinue;
				}
				candidates.emplace_back(distance, a_ref);
				return RE::BSContainer::ForEachResult::kContinue;
			});
			std::sort(candidates.begin(), candidates.end(), [](const auto& a_left, const auto& a_right) { return a_left.first < a_right.first; });
			for (const auto& [distance, ref] : candidates) {
				if (hits.size() >= 8) {
					break;
				}
				hits.push_back(ref);
			}
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
		BuildShrubBases();
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
