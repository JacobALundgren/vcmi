/*
 * CommonConstructors.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CommonConstructors.h"

#include "CGTownInstance.h"
#include "CGHeroInstance.h"
#include "CBank.h"
#include "../mapping/CMap.h"
#include "../CHeroHandler.h"
#include "../CCreatureHandler.h"
#include "JsonRandom.h"
#include "../CModHandler.h"
#include "../IGameCallback.h"

CObstacleConstructor::CObstacleConstructor()
{
}

bool CObstacleConstructor::isStaticObject()
{
	return true;
}

CTownInstanceConstructor::CTownInstanceConstructor():
	faction(nullptr)
{
}

void CTownInstanceConstructor::initTypeData(const JsonNode & input)
{
	VLC->modh->identifiers.requestIdentifier("faction", input["faction"], [&](si32 index)
	{
		faction = (*VLC->townh)[index];
	});

	filtersJson = input["filters"];
}

void CTownInstanceConstructor::afterLoadFinalization()
{
	assert(faction);
	for (auto entry : filtersJson.Struct())
	{
		filters[entry.first] = LogicalExpression<BuildingID>(entry.second, [this](const JsonNode & node)
		{
			return BuildingID(VLC->modh->identifiers.getIdentifier("building." + faction->identifier, node.Vector()[0]).get());
		});
	}
}

bool CTownInstanceConstructor::objectFilter(const CGObjectInstance * object, const ObjectTemplate & templ) const
{
	auto town = dynamic_cast<const CGTownInstance *>(object);

	auto buildTest = [&](const BuildingID & id)
	{
		return town->hasBuilt(id);
	};

	return filters.count(templ.stringID) != 0 && filters.at(templ.stringID).test(buildTest);
}

CGObjectInstance * CTownInstanceConstructor::create(const ObjectTemplate & tmpl) const
{
	CGTownInstance * obj = createTyped(tmpl);
	obj->town = faction->town;
	obj->tempOwner = PlayerColor::NEUTRAL;
	return obj;
}

void CTownInstanceConstructor::configureObject(CGObjectInstance * object, CRandomGenerator & rng) const
{
	auto templ = getOverride(object->cb->getTile(object->pos)->terType, object);
	if (templ)
		object->appearance = templ.get();
}

CHeroInstanceConstructor::CHeroInstanceConstructor()
	:heroClass(nullptr)
{

}

void CHeroInstanceConstructor::initTypeData(const JsonNode & input)
{
	VLC->modh->identifiers.requestIdentifier("heroClass", input["heroClass"],
			[&](si32 index) { heroClass = VLC->heroh->classes[index]; });

	filtersJson = input["filters"];
}

void CHeroInstanceConstructor::afterLoadFinalization()
{
	for (auto entry : filtersJson.Struct())
	{
		filters[entry.first] = LogicalExpression<HeroTypeID>(entry.second, [](const JsonNode & node)
		{
			return HeroTypeID(VLC->modh->identifiers.getIdentifier("hero", node.Vector()[0]).get());
		});
	}
}

bool CHeroInstanceConstructor::objectFilter(const CGObjectInstance * object, const ObjectTemplate & templ) const
{
	auto hero = dynamic_cast<const CGHeroInstance *>(object);

	auto heroTest = [&](const HeroTypeID & id)
	{
		return hero->type->ID == id;
	};

	if (filters.count(templ.stringID))
	{
		return filters.at(templ.stringID).test(heroTest);
	}
	return false;
}

CGObjectInstance * CHeroInstanceConstructor::create(const ObjectTemplate & tmpl) const
{
	CGHeroInstance * obj = createTyped(tmpl);
	obj->type = nullptr; //FIXME: set to valid value. somehow.
	return obj;
}

void CHeroInstanceConstructor::configureObject(CGObjectInstance * object, CRandomGenerator & rng) const
{

}

CDwellingInstanceConstructor::CDwellingInstanceConstructor()
{

}

void CDwellingInstanceConstructor::initTypeData(const JsonNode & input)
{
	const JsonVector & levels = input["creatures"].Vector();
	availableCreatures.resize(levels.size());
	for (size_t i=0; i<levels.size(); i++)
	{
		const JsonVector & creatures = levels[i].Vector();
		availableCreatures[i].resize(creatures.size());
		for (size_t j=0; j<creatures.size(); j++)
		{
			VLC->modh->identifiers.requestIdentifier("creature", creatures[j], [=] (si32 index)
			{
				availableCreatures[i][j] = VLC->creh->objects[index];
			});
		}
		assert(!availableCreatures[i].empty());
	}

	guards = input["guards"];
}

bool CDwellingInstanceConstructor::objectFilter(const CGObjectInstance *, const ObjectTemplate &) const
{
	return false;
}

CGObjectInstance * CDwellingInstanceConstructor::create(const ObjectTemplate & tmpl) const
{
	CGDwelling * obj = createTyped(tmpl);

	obj->creatures.resize(availableCreatures.size());
	for (auto & entry : availableCreatures)
	{
		for (const CCreature * cre : entry)
			obj->creatures.back().second.push_back(cre->idNumber);
	}
	return obj;
}

void CDwellingInstanceConstructor::configureObject(CGObjectInstance * object, CRandomGenerator &rng) const
{
	CGDwelling * dwelling = dynamic_cast<CGDwelling*>(object);

	dwelling->creatures.clear();
	dwelling->creatures.reserve(availableCreatures.size());

	for (auto & entry : availableCreatures)
	{
		dwelling->creatures.resize(dwelling->creatures.size() + 1);
		for (const CCreature * cre : entry)
			dwelling->creatures.back().second.push_back(cre->idNumber);
	}

	bool guarded = false; //TODO: serialize for sanity

	if (guards.getType() == JsonNode::JsonType::DATA_BOOL) //simple switch
	{
		if (guards.Bool())
		{
			guarded = true;
		}
	}
	else if (guards.getType() == JsonNode::JsonType::DATA_VECTOR) //custom guards (eg. Elemental Conflux)
	{
		for (auto & stack : JsonRandom::loadCreatures(guards, rng))
		{
			dwelling->putStack(SlotID(dwelling->stacksCount()), new CStackInstance(stack.type->idNumber, stack.count));
		}
	}
	else //default condition - creatures are of level 5 or higher
	{
		for (auto creatureEntry : availableCreatures)
		{
			if (creatureEntry.at(0)->level >= 5)
			{
				guarded = true;
				break;
			}
		}
	}

	if (guarded)
	{
		for (auto creatureEntry : availableCreatures)
		{
			const CCreature * crea = creatureEntry.at(0);
			dwelling->putStack (SlotID(dwelling->stacksCount()), new CStackInstance(crea->idNumber, crea->growth * 3));
		}
	}
}

bool CDwellingInstanceConstructor::producesCreature(const CCreature * crea) const
{
	for (auto & entry : availableCreatures)
	{
		for (const CCreature * cre : entry)
			if (crea == cre)
				return true;
	}
	return false;
}

std::vector<const CCreature *> CDwellingInstanceConstructor::getProducedCreatures() const
{
	std::vector<const CCreature *> creatures; //no idea why it's 2D, to be honest
	for (auto & entry : availableCreatures)
	{
		for (const CCreature * cre : entry)
			creatures.push_back(cre);
	}
	return creatures;
}

CBankInstanceConstructor::CBankInstanceConstructor()
	: bankResetDuration(0)
{

}

void CBankInstanceConstructor::initTypeData(const JsonNode & input)
{
	//TODO: name = input["name"].String();
	levels = input["levels"].Vector();
	bankResetDuration = input["resetDuration"].Float();
}

CGObjectInstance *CBankInstanceConstructor::create(const ObjectTemplate & tmpl) const
{
	return createTyped(tmpl);
}

BankConfig CBankInstanceConstructor::generateConfig(const JsonNode & level, CRandomGenerator & rng) const
{
	BankConfig bc;

	bc.chance = level["chance"].Float();

	bc.guards = JsonRandom::loadCreatures(level["guards"], rng);
	bc.upgradeChance = level["upgrade_chance"].Float();
	bc.combatValue = level["combat_value"].Float();

	std::vector<SpellID> spells;
	for (size_t i=0; i<6; i++)
		IObjectInterface::cb->getAllowedSpells(spells, i);

	bc.resources = Res::ResourceSet(level["reward"]["resources"]);
	bc.creatures = JsonRandom::loadCreatures(level["reward"]["creatures"], rng);
	bc.artifacts = JsonRandom::loadArtifacts(level["reward"]["artifacts"], rng);
	bc.spells    = JsonRandom::loadSpells(level["reward"]["spells"], rng, spells);

	bc.value = level["value"].Float();

	return bc;
}

void CBankInstanceConstructor::configureObject(CGObjectInstance * object, CRandomGenerator & rng) const
{
	auto bank = dynamic_cast<CBank*>(object);

	bank->resetDuration = bankResetDuration;

	si32 totalChance = 0;
	for (auto & node : levels)
		totalChance += node["chance"].Float();

	assert(totalChance != 0);

	si32 selectedChance = rng.nextInt(totalChance - 1);

	int cumulativeChance = 0;
	for (auto & node : levels)
	{
		cumulativeChance += node["chance"].Float();
		if (selectedChance < cumulativeChance)
		{
			 bank->setConfig(generateConfig(node, rng));
			 break;
		}
	}
}

CBankInfo::CBankInfo(const JsonVector & Config):
	config(Config)
{
	assert(!Config.empty());
}

static void addStackToArmy(IObjectInfo::CArmyStructure & army, const CCreature * crea, si32 amount)
{
	army.totalStrength += crea->fightValue * amount;

	bool walker = true;
	if (crea->hasBonusOfType(Bonus::SHOOTER))
	{
		army.shootersStrength += crea->fightValue * amount;
		walker = false;
	}
	if (crea->hasBonusOfType(Bonus::FLYING))
	{
		army.flyersStrength += crea->fightValue * amount;
		walker = false;
	}
	if (walker)
		army.walkersStrength += crea->fightValue * amount;
}

IObjectInfo::CArmyStructure CBankInfo::minGuards() const
{
	std::vector<IObjectInfo::CArmyStructure> armies;
	for (auto configEntry : config)
	{
		auto stacks = JsonRandom::evaluateCreatures(configEntry["guards"]);
		IObjectInfo::CArmyStructure army;
		for (auto & stack : stacks)
		{
			assert(!stack.allowedCreatures.empty());
			auto weakest = boost::range::min_element(stack.allowedCreatures, [](const CCreature * a, const CCreature * b)
			{
				return a->fightValue < b->fightValue;
			});
			addStackToArmy(army, *weakest, stack.minAmount);
		}
		armies.push_back(army);
	}
	return *boost::range::min_element(armies);
}

IObjectInfo::CArmyStructure CBankInfo::maxGuards() const
{
	std::vector<IObjectInfo::CArmyStructure> armies;
	for (auto configEntry : config)
	{
		auto stacks = JsonRandom::evaluateCreatures(configEntry["guards"]);
		IObjectInfo::CArmyStructure army;
		for (auto & stack : stacks)
		{
			assert(!stack.allowedCreatures.empty());
			auto strongest = boost::range::max_element(stack.allowedCreatures, [](const CCreature * a, const CCreature * b)
			{
				return a->fightValue < b->fightValue;
			});
			addStackToArmy(army, *strongest, stack.maxAmount);
		}
		armies.push_back(army);
	}
	return *boost::range::max_element(armies);
}

TPossibleGuards CBankInfo::getPossibleGuards() const
{
	TPossibleGuards out;

	for (const JsonNode & configEntry : config)
	{
		const JsonNode & guardsInfo = configEntry["guards"];
		auto stacks = JsonRandom::evaluateCreatures(guardsInfo);
		IObjectInfo::CArmyStructure army;


		for (auto stack : stacks)
		{
			army.totalStrength += stack.allowedCreatures.front()->AIValue * (stack.minAmount + stack.maxAmount) / 2;
			//TODO: add fields for flyers, walkers etc...
		}

		ui8 chance = configEntry["chance"].Float();
		out.push_back(std::make_pair(chance, army));
	}
	return out;
}

bool CBankInfo::givesResources() const
{
	for (const JsonNode & node : config)
		if (!node["reward"]["resources"].isNull())
			return true;
	return false;
}

bool CBankInfo::givesArtifacts() const
{
	for (const JsonNode & node : config)
		if (!node["reward"]["artifacts"].isNull())
			return true;
	return false;
}

bool CBankInfo::givesCreatures() const
{
	for (const JsonNode & node : config)
		if (!node["reward"]["creatures"].isNull())
			return true;
	return false;
}

bool CBankInfo::givesSpells() const
{
	for (const JsonNode & node : config)
		if (!node["reward"]["spells"].isNull())
			return true;
	return false;
}


std::unique_ptr<IObjectInfo> CBankInstanceConstructor::getObjectInfo(const ObjectTemplate & tmpl) const
{
	return std::unique_ptr<IObjectInfo>(new CBankInfo(levels));
}
