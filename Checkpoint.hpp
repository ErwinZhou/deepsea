#pragma once

#include "UnderwaterPhysics.hpp"

#include <optional>
#include <string>

/**
 * Level 2 Intermediate Checkpoints:
 * initial -> door -> clam -> octopus -> chest -> machine
 * Tunnel needs to be finished before the chest
 * So there are four possible routes, 6 checkpoints each, 24 possibilities
 * (1) Initial -> Tunnel -> Door -> Clam -> Octopus -> Chest -> Machine
 * (2) Initial -> Door -> Tunnel -> Clam -> Octopus -> Chest -> Machine
 * (3) Initial -> Door -> Clam -> Tunnel -> Octopus -> Chest -> Machine
 * (4) Initial -> Door -> Clam -> Octopus -> Tunnel -> Chest -> Machine
 * 
 * "Backpack" Design - The player respawn near the place of the puzzle that they just solved 
 * right now with all of the items that have not used by them for its corresponding puzzles
 */
enum class Level2Checkpoint : unsigned {
	Entrance = 0,
	Tunnel = 1,
	Door = 2,
	Clam = 3,
	Octopus = 4,
	Chest = 5,
	Machine = 6
};

//the level entrance has no puzzle, the six completed puzzles use bits 0 through 5
constexpr unsigned puzzle_flag(Level2Checkpoint point) {
	return point == Level2Checkpoint::Entrance ? 0u : 1u << (static_cast<unsigned>(point) - 1u);
}

//this state selection is based on https://docs.godotengine.org/en/stable/tutorials/io/saving_games.html#identify-persistent-objects
struct Checkpoint {
	static constexpr unsigned pit_open = 1u << 6;
	int level = 1;
	Level2Checkpoint checkpoint = Level2Checkpoint::Entrance;
	std::optional<Body2D> belly_contents{};
	unsigned puzzle_flags = 0;
	bool jelly_nearby = false;

	void finish_puzzle(Level2Checkpoint point) {
		// revisiting a puzzle keeps the current respawn point
		const unsigned flag = puzzle_flag(point);
		if ((puzzle_flags & flag) == 0 && flag != 0) {
			puzzle_flags |= flag;
			checkpoint = point;
		}
	}
};

std::string preference_path(std::string const &filename);
std::optional<Checkpoint> load_checkpoint(std::string const &filename);
bool save_checkpoint(std::string const &filename, Checkpoint const &checkpoint);
