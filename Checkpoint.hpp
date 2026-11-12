#pragma once

#include "UnderwaterPhysics.hpp"

#include <optional>
#include <string>

//this state selection is based on https://docs.godotengine.org/en/stable/tutorials/io/saving_games.html#identify-persistent-objects
struct Checkpoint {
	int level = 1;
	//zero identifies the level's entrance
	int checkpoint = 0;
	std::optional<Body2D> belly_contents;
};

std::string checkpoint_path();
std::optional<Checkpoint> load_checkpoint(std::string const &filename);
bool save_checkpoint(std::string const &filename, Checkpoint const &checkpoint);
