#pragma once

#include <string>
#include <unordered_map>

#include "common/entity_defs.hpp"

namespace net {

class EntityRegistry {
public:
    void Register(const EntityDef& def);
    void RegisterDefaults();
    bool LoadFromDirectory(const std::string& dir);
    bool LoadFile(const std::string& path);

    const EntityDef* Find(const std::string& id) const;
    const EntityDef& MustFind(const std::string& id) const;
    const CombatStats& StatsFor(const std::string& id) const;

private:
    std::unordered_map<std::string, EntityDef> defs_;
};

EntityRegistry& DefaultEntityRegistry();
bool InitializeEntityRegistry();

}  // namespace net
