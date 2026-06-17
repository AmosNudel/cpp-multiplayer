#include "common/entity_registry.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace net {
namespace {

EntityDef EntityDefFromJson(const nlohmann::json& json) {
    EntityDef def;
    def.id = json.at("id").get<std::string>();
    def.displayName = json.value("display_name", def.id);
    def.stats.maxHp = json.value("max_hp", 100);
    def.stats.maxShield = json.value("max_shield", 0);
    def.stats.attackDamage = json.value("attack_damage", 10);
    def.stats.critChancePercent = json.value("crit_chance_percent", 0);
    def.stats.critDamageMultiplier = json.value("crit_damage_multiplier", 2);
    def.stats.attackCooldownTicks = json.value("attack_cooldown_ticks", 12);
    def.stats.hitStunTicks = json.value("hit_stun_ticks", 8);
    def.stats.shieldRegenPerTick = json.value("shield_regen_per_tick", 0);
    def.spriteHeight = json.value("sprite_height", 96.0f);
    return def;
}

}  // namespace

void EntityRegistry::Register(const EntityDef& def) {
    defs_[def.id] = def;
}

void EntityRegistry::RegisterDefaults() {
    if (Find(kPlayerEntityId) == nullptr) {
        EntityDef player;
        player.id = kPlayerEntityId;
        player.displayName = "Player";
        player.stats.maxHp = 100;
        player.stats.maxShield = 50;
        player.stats.attackDamage = 10;
        player.stats.critChancePercent = 25;
        player.stats.critDamageMultiplier = 2;
        player.stats.attackCooldownTicks = 12;
        player.stats.hitStunTicks = 8;
        player.stats.shieldRegenPerTick = 1;
        player.spriteHeight = 96.0f;
        Register(player);
    }

    if (Find(kGoblinEntityId) == nullptr) {
        EntityDef goblin;
        goblin.id = kGoblinEntityId;
        goblin.displayName = "Goblin";
        goblin.stats.maxHp = 130;
        goblin.stats.attackDamage = 12;
        goblin.stats.critDamageMultiplier = 2;
        goblin.stats.attackCooldownTicks = 14;
        goblin.stats.hitStunTicks = 6;
        goblin.spriteHeight = 128.0f;
        Register(goblin);
    }
}

bool EntityRegistry::LoadFromDirectory(const std::string& dir) {
    namespace fs = std::filesystem;

    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return false;
    }

    bool loadedAny = false;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        if (LoadFile(entry.path().string())) {
            loadedAny = true;
        }
    }

    return loadedAny;
}

bool EntityRegistry::LoadFile(const std::string& path) {
    try {
        std::ifstream input(path);
        if (!input.is_open()) {
            return false;
        }

        nlohmann::json json;
        input >> json;
        const EntityDef def = EntityDefFromJson(json);
        Register(def);
        std::cout << "[entities] loaded " << def.id << " from " << path << "\n";
        return true;
    } catch (const std::exception& error) {
        std::cerr << "[entities] failed to load " << path << ": " << error.what() << "\n";
        return false;
    }
}

const EntityDef* EntityRegistry::Find(const std::string& id) const {
    const auto it = defs_.find(id);
    if (it == defs_.end()) {
        return nullptr;
    }
    return &it->second;
}

const EntityDef& EntityRegistry::MustFind(const std::string& id) const {
    const EntityDef* def = Find(id);
    if (def == nullptr) {
        throw std::runtime_error("Unknown entity definition: " + id);
    }
    return *def;
}

const CombatStats& EntityRegistry::StatsFor(const std::string& id) const {
    const EntityDef* def = Find(id);
    if (def != nullptr) {
        return def->stats;
    }

    const EntityDef* fallback = Find(kGoblinEntityId);
    if (fallback != nullptr) {
        return fallback->stats;
    }

    throw std::runtime_error("Entity registry has no stats for: " + id);
}

EntityRegistry& DefaultEntityRegistry() {
    static EntityRegistry registry;
    static bool defaultsRegistered = false;
    if (!defaultsRegistered) {
        registry.RegisterDefaults();
        defaultsRegistered = true;
    }
    return registry;
}

bool InitializeEntityRegistry() {
    static bool filesLoaded = false;
    if (filesLoaded) {
        return true;
    }

    EntityRegistry& registry = DefaultEntityRegistry();

    static constexpr const char* kSearchPaths[] = {
        "assets/entities",
        "../assets/entities",
    };

    for (const char* path : kSearchPaths) {
        registry.LoadFromDirectory(path);
    }

    filesLoaded = true;
    return true;
}

}  // namespace net
