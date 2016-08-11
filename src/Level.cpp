#include <algorithm>
#include <functional>
#include <queue>
#include <utility>
#include <stack>

#include <json.hpp>

#include "Bullet.hpp"
#include "Creep.hpp"
#include "Tower.hpp"
#include "Level.hpp"

using json = nlohmann::json;

GridNavigationProvider::GridNavigationProvider(
	LevelInstance & levelInstance,
	sf::Vector2i goal)
	: levelInstance_(levelInstance)
	, goal_(goal)
{
	const auto & level = levelInstance.getLevel();
	const int32_t tableSize = level->getWidth() * level->getHeight();
	path_ = std::move(std::unique_ptr<int32_t[]>(new int32_t[tableSize]));

	// Calculate paths for the first time
	update();
}

sf::Vector2i GridNavigationProvider::getGoal() const
{
	return goal_;
}

sf::Vector2i GridNavigationProvider::getNextStep(const sf::Vector2i & point) const
{
	// TODO: Bound checking
	const int32_t fromIndex = point.y * levelInstance_.getLevel()->getWidth() + point.x;
	const int32_t toIndex = path_[fromIndex];
	const int32_t width = levelInstance_.getLevel()->getWidth();
	return{ toIndex % width, toIndex / width };
}

void GridNavigationProvider::update()
{
	// A simple BFS algorithm with articulation point finding.
	// For small maps it should suffice.
	static const int32_t EMPTY = -1, FILLED = -2;
	const int32_t width = levelInstance_.getLevel()->getWidth();
	const int32_t height = levelInstance_.getLevel()->getHeight();
	const int32_t tableSize = width * height;
	std::queue<std::pair<int32_t, int32_t>> verts;

	// Prepare tables
	std::fill(path_.get(), path_.get() + tableSize, EMPTY);
	const int32_t goalIndex = goal_.y * width + goal_.x;
	verts.emplace(goalIndex, goalIndex);

	// Reserve locations occupied by towers
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			if (levelInstance_.getTowerAt({ x, y })) {
				path_[y * width + x] = FILLED;
			}
		}
	}

	do
	{
		const int32_t current = verts.front().first;
		const int32_t previous = verts.front().second;
		verts.pop();

		path_[current] = previous;

		const int32_t x = current % width; // TODO: Optimize and move around coordinates
		const int32_t y = current / width; //       instead of performing division

		auto tryPushVertex = [&](int32_t next)
		{
			if (next != previous && path_[next] == EMPTY) {
				verts.emplace(next, current);
			}
		};

		// TODO: Abstract this pattern somewhere else?
		if (y < height - 1)
			tryPushVertex(current + width);
		if (y > 0)
			tryPushVertex(current - width);
		if (x < width - 1)
			tryPushVertex(current + 1);
		if (x > 0)
			tryPushVertex(current - 1);

	} while (!verts.empty());

	// Debug dump
	/*printf("DUMP:\n");
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			printf("%i ", path_[y * width + x]);
		}
		putchar('\n');
	}*/
}

GridTowerPlacementOracle::GridTowerPlacementOracle(LevelInstance & levelInstance)
	: levelInstance_(levelInstance)
{
	auto level = levelInstance.getLevel();
	const int32_t tableSize = level->getWidth() * level->getHeight();
	validTurretPlaces_.reset(new bool[tableSize]);
	parents_.reset(new int32_t[tableSize]);
	pre_.reset(new int32_t[tableSize]);
	low_.reset(new int32_t[tableSize]);

	update();
}

bool GridTowerPlacementOracle::canPlaceTowerHere(const sf::Vector2i & at) const
{
	const auto width = levelInstance_.getLevel()->getWidth();
	const auto height = levelInstance_.getLevel()->getHeight();

	if (at.x >= 0 && at.y >= 0 && at.x < width && at.y < height)
		return validTurretPlaces_[at.y * width + at.x];

	return false;
}

void GridTowerPlacementOracle::render(sf::RenderTarget & target)
{
	const int32_t width = levelInstance_.getLevel()->getWidth();
	const int32_t height = levelInstance_.getLevel()->getHeight();

	sf::RectangleShape rs;
	rs.setSize({ 1.f, 1.f });
	rs.setOrigin({ 0.5f, 0.5f });
	rs.setFillColor(sf::Color(0, 0, 255, 127));

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			if (validTurretPlaces_[y * width + x]) {
				rs.setPosition((float)x, (float)y);
				target.draw(rs);
			}
		}
	}
}

void GridTowerPlacementOracle::update()
{
	// The algorithm marks articulation points, creep sources and the goal
	// as unsuitable to place a tower on. Every other point is marked as
	// suitable. It uses program-stack based dfs, so on big maps there
	// might be a stack overflow.
	// TODO: This algorithm marks invalid points too eagerly, as some
	// articulation points are still valid turret placement points
	// (e.g. dead-end corridors leading neither to a source nor the goal).
	// TODO: Mark creep sources
	static const int32_t EMPTY = -1;
	static const int32_t ROOT = -2;
	const int32_t width = levelInstance_.getLevel()->getWidth();
	const int32_t height = levelInstance_.getLevel()->getHeight();
	const int32_t tableSize = width * height;
	int32_t preCounter = 0;
	
	// First, calculate low and pre numbers
	std::fill(parents_.get(), parents_.get() + tableSize, EMPTY);

	std::function<void(int32_t, int32_t, int32_t, int32_t)> dfs =
		[&](int32_t current, int32_t x, int32_t y, int32_t parent) {

		int32_t minimum = preCounter++;
		pre_[current] = minimum;
		parents_[current] = parent;

		auto processChild = [&](int32_t child, int32_t nx, int32_t ny) {
			if (child != parent && !levelInstance_.getTowerAt(nx, ny)) {
				int32_t v;
				if (parents_[child] == EMPTY) {
					dfs(child, nx, ny, current);
					v = low_[child];
				}
				else
					v = pre_[child];
				minimum = std::min(minimum, v);
			}

		};

		if (y < height - 1)
			processChild(current + width, x, y + 1);
		if (y > 0)
			processChild(current - width, x, y - 1);
		if (x < width - 1)
			processChild(current + 1, x + 1, y);
		if (x > 0)
			processChild(current - 1, x - 1, y);

		low_[current] = minimum;
	};

	const auto goal = levelInstance_.getLevel()->getGoal();
	const auto goalIndex = goal.y * width + goal.x;
	dfs(goalIndex, goal.x, goal.y, ROOT);

	// Now see which vertices are articulation points.
	// Note we don't process root differently, as it never is a valid place
	// for a turret
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const int32_t current = y * width + x;

			if (levelInstance_.getTowerAt({ x, y })) {
				validTurretPlaces_[current] = false;
				continue;
			}

			auto checkChild = [&](int32_t child) {
				if (parents_[child] == current) {
					const auto low = low_[child];
					const auto pre = pre_[current];
					if (low >= pre)
						validTurretPlaces_[current] = false;
				}
			};

			validTurretPlaces_[current] = true;

			if (y < height - 1)
				checkChild(current + width);
			if (y > 0)
				checkChild(current - width);
			if (x < width - 1)
				checkChild(current + 1);
			if (x > 0)
				checkChild(current - 1);

		}
	}

	validTurretPlaces_[goalIndex] = false;
}

NavigationProvider<sf::Vector2i> & LevelInstance::getGoalNavigationProvider()
{
	return gridNavigation_;
}

std::shared_ptr<Tower> LevelInstance::getTowerAt(sf::Vector2i position)
{
	// TODO: Bounds checking maybe?
	return towerMap_[position.y * level_->getWidth() + position.x];
}

InvasionManager::InvasionManager(const json & data)
{
	// We are interested in the "waves" table
	for (const auto & wave : data["waves"]) {
		const int64_t startFrame = wave["start-frame"];

		// Calculate spawn moments for the given description
		for (const auto & creepDesc : wave["creeps"]) {
			std::vector<int64_t> moments;
			const std::string type = creepDesc["type"];
			const int32_t hp = creepDesc["hp"];
			const int32_t bounty = creepDesc["bounty"];
			const sf::Vector2i spawnAt = {
				creepDesc["spawn-at"][0], creepDesc["spawn-at"][1]
			};

			auto addInfo = [&](int64_t waveTime) {
				invasionPlan_.push_back({
					startFrame + waveTime,
					type, spawnAt, hp, bounty
				});
			};

			const auto & spawnTime = creepDesc["spawn-time"];
			if (spawnTime.is_object()) {
				// We have a range of spawn moments
				const int64_t start = spawnTime["start"];
				const int64_t interval = spawnTime["interval"];
				const int64_t count = spawnTime["count"];
				for (int64_t i = 0; i < count; ++i)
					addInfo(start + i * interval);
			}
			else if (spawnTime.is_array()) {
				for (const auto & time : spawnTime)
					addInfo((int64_t)time);
			}
			else if (spawnTime.is_number_integer()) {
				addInfo((int64_t)spawnTime);
			}
			else {
				throw std::runtime_error("Invalid type of spawnTime");
			}
		}
	}

	std::sort(invasionPlan_.begin(), invasionPlan_.end());
}

void InvasionManager::spawn(std::shared_ptr<LevelInstance> levelInstance, int64_t moment)
{
	// Damn you, c++
	creationInfo_t dummy;
	dummy.moment = moment;

	const auto its = std::equal_range(invasionPlan_.begin(), invasionPlan_.end(), dummy);
	for (auto it = its.first; it != its.second; ++it) {
		levelInstance->createCreepAt(it->creepName, it->life, it->bounty, it->position);
	}
}

bool InvasionManager::invasionEnded(int64_t moment) const
{
	return invasionPlan_.empty() || (invasionPlan_.back().moment > moment);
}

Level::Level(std::istream & source)
{
	json levelDescription;
	source >> levelDescription;

	width_ = levelDescription["grid-size"][0];
	height_ = levelDescription["grid-size"][1];
	goal_ = { levelDescription["goal"][0], levelDescription["goal"][1] };
	startingMoney_ = levelDescription["starting-money"];

	invasionManager_.reset(new InvasionManager(levelDescription));
}

LevelInstance::LevelInstance(std::shared_ptr<Level> level)
	: level_(level)
	, towerMap_(new std::shared_ptr<Tower>[level->getWidth() * level->getHeight()])
	, gridNavigation_(*this, level->getGoal())
	, gridTowerPlacement_(*this)
	, currentFrame_(0)
	, money_(level->getStartingMoney())
{}

bool LevelInstance::createTowerAt(const std::string & name, sf::Vector2i position)
{
	// TODO: Bounds checking maybe?
	const auto typeInfo = TowerFactory::getTowerTypeInfo(name);
	if (typeInfo.cost > money_)
		return false;

	auto tower = typeInfo.construct({ (float)position.x, (float)position.y });

	towers_.push_back(tower);
	towerMap_[position.y * level_->getWidth() + position.x] = tower;

	gridNavigation_.update();
	gridTowerPlacement_.update();
	money_ -= typeInfo.cost;

	return true;
}

void LevelInstance::createCreepAt(const std::string & name, int32_t life, int32_t bounty, sf::Vector2i position)
{
	creeps_.push_back(CreepFactory().createCreep(name, life, bounty, position));
}

template<typename Entity>
static void removeDeadEntities(std::vector<std::shared_ptr<Entity>> & entities)
{
	auto remover = [&](const std::shared_ptr<Entity> & entity)
	{
		return !entity->isAlive();
	};

	auto it = std::remove_if(entities.begin(), entities.end(), remover);
	entities.resize(std::distance(entities.begin(), it));
}

void LevelInstance::update()
{
	level_->getInvasionManager().spawn(shared_from_this(), currentFrame_);

	CreepVectorQueryService queryService(creeps_);

	for (auto & tower : towers_) {
		BulletFactory factory(bullets_, tower->getPosition());
		tower->update(factory, queryService);
	}

	for (auto & bullet : bullets_)
		bullet->update();
	removeDeadEntities(bullets_);

	for (auto & creep : creeps_) {
		creep->update(gridNavigation_);
		if (!creep->isAlive())
			money_ += creep->getBounty();
	}
	removeDeadEntities(creeps_);

	++currentFrame_;
}

void LevelInstance::render(sf::RenderTarget & target)
{
	// TODO: Make all renderables derived from one base class
	for (auto & tower : towers_)
		tower->render(target);
	for (auto & bullet : bullets_)
		bullet->render(target);
	for (auto & creep : creeps_)
		creep->render(target);
	gridTowerPlacement_.render(target);
}
