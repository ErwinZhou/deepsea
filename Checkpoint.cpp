#include "Checkpoint.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>

/**
 * Checkpoint design
 * Platform independent saving path is based on: https://wiki.libsdl.org/SDL3/SDL_GetPrefPath
 * v1.0 - 8 rows; v1.1 - 9 rows
 * (1) `version`: version number
 * (2) `level`: selects the level, either 1 or 2
 * (3) `checkpoint`: selects the spawn within that level, 0 is the entrance
 *     level 2 also uses 1 tunnel, 2 door, 3 clam, 4 octopus, 5 chest and 6 machine
 * (4) `belly`: identifies the carried object as one of empty, rock, jelly
 * (5) `inverse_mass`: the carried body's response to forces
 * (6) `radius`: restores the size of its collision body
 * (7) `drag`: restores its resistance to motion through water
 * (8) `current_scale`: scales the effect of water currents on the body
 *     an empty belly stores zero in all four physics columns
 *
 * (9) `puzzle_flags`: records all completed puzzles on level2 using bitmasks
 * 		The idea of using bitmask is based on https://www.gamedeveloper.com/programming/bitmask-multi-state-machine
 * 		(a) (000001) 1 - tunnel (b) (000010) 2 - door (3) 4 - clam (4) 8 - octopus (5) 16 - chest (6) 32 - machine
 * (10) `jellyfish_nearby`: if the player dies with a jellyfish already in level2, the jellyfish will respawn near that player
 */

static constexpr char header[] = "version,level,checkpoint,belly,inverse_mass,radius,drag,current_scale,puzzle_flags,jelly_nearby";

// use the same item names when saving and loading
struct SavedItem {
	Body2D::Kind kind;
	const char *name;
};

static constexpr SavedItem saved_items[] = {
	{Body2D::Rock, "rock"}, {Body2D::Jelly, "jelly"}, {Body2D::Key, "key"},
	{Body2D::Pipe, "pipe"}, {Body2D::Pearl, "pearl"}, {Body2D::Keycard, "keycard"},
	{Body2D::Trash, "trash"}
};

static bool valid_belly(Body2D const &body) {
	// only save items the fish can swallow
	bool can_carry = false;
	for (const SavedItem &item : saved_items) {
		if (item.kind == body.kind) {
			can_carry = true;
			break;
		}
	}
	if (!can_carry) {
		return false;
	}

	if (!std::isfinite(body.inverse_mass) || body.inverse_mass <= 0.0f) {
		// a carried object needs a finite positive inverse mass to move
		return false;
	}

	if (!std::isfinite(body.radius) || body.radius <= 0.0f) {
		// a carried object needs a finite positive collision radius
		return false;
	}

	if (!std::isfinite(body.drag) || body.drag < 0.0f) {
		// drag must be finite and zero or greater
		return false;
	}

	if (!std::isfinite(body.current_scale) || body.current_scale < 0.0f) {
		// the saved current multiplier must be finite and zero or greater
		return false;
	}

	return true;
}

static bool valid_checkpoint(Checkpoint const &checkpoint) {
	using Point = Level2Checkpoint;
	const Point point = checkpoint.checkpoint;
	const unsigned flags = checkpoint.puzzle_flags;

	// a nearby jellyfish needs its own place
	if (checkpoint.jelly_nearby && (checkpoint.level != 2 ||
		(checkpoint.belly_contents && checkpoint.belly_contents->kind == Body2D::Jelly))) {
		return false;
	}

	if (checkpoint.level != 1 && checkpoint.level != 2) {
		// invalid if the checkpoint level is not 1 or 2, the only levels with saved progress
		return false;
	}

	if (point > Point::Machine) {
		// no checkpoint location is assigned above Machine
		return false;
	}

	// check what the fish was carrying
	if (checkpoint.belly_contents) {
		if (checkpoint.level == 1 || !valid_belly(*checkpoint.belly_contents)) {
			return false;
		}
	}

	// the entrance comes before any puzzles
	if (point == Point::Entrance) {
		return flags == 0;
	}

	if (checkpoint.level != 2) {
		// level 1 has no puzzle checkpoints
		return false;
	}

	//the save format reserves one bit for each of the six puzzles
	constexpr unsigned known_flags = 0b111111;
	if ((flags & ~known_flags) != 0) {
		// the record marks a puzzle that does not exist
		return false;
	}

	const auto done = [flags](Point puzzle) {
		return (flags & puzzle_flag(puzzle)) != 0;
	};

	//the player can only respawn at a puzzle they have already completed
	if (!done(point)) {
		// the selected puzzle is still unsolved
		return false;
	}

	//the tunnel key joins the pipe and pearl sequence at the chest
	if (done(Point::Clam) && !done(Point::Door)) {
		// the pipe must come out of the door before it can hold the clam open
		return false;
	}

	if (done(Point::Octopus) && !done(Point::Clam)) {
		// the octopus needs the pearl from the open clam
		return false;
	}

	if (done(Point::Chest) && !(done(Point::Tunnel) && done(Point::Octopus))) {
		// opening the chest requires the tunnel key and distracted tentacles
		return false;
	}

	if (done(Point::Machine) && !done(Point::Chest)) {
		// the machine needs the keycard from the chest
		return false;
	}

	return true;
}

static bool read_number(std::string const &field, float &value) {

	// read decimals the same way everywhere
	std::istringstream input(field);
	input.imbue(std::locale::classic());

	// reject missing numbers and values without limits
	if (!(input >> value) || !std::isfinite(value)) {
		return false;
	}

	// allow spaces after the number
	input >> std::ws;
	return input.eof();
}

std::string preference_path(std::string const &filename) {
	//this idea of saving directory and returned-pointer ownership is inspired by https://wiki.libsdl.org/SDL3/SDL_GetPrefPath
	std::unique_ptr<char, decltype(&SDL_free)> directory(SDL_GetPrefPath("DeepSea", "DeepSea"), SDL_free);

	// report when the save folder is unavailable
	if (!directory) {
		std::cerr << "Could not locate preferences directory: " << SDL_GetError() << '\n';
		return {};
	}

	// put the filename inside the save folder
	return std::string(directory.get()) + filename;
}

std::optional<Checkpoint> load_checkpoint(std::string const &filename) {
	//explicit utf-8 paths keep unicode save locations usable on windows
	const std::filesystem::path path(std::u8string(filename.begin(), filename.end()));
	std::ifstream input(path);

	// a first game has no save yet
	if (!input) {
		std::error_code error;
		const bool exists = std::filesystem::exists(path, error);
		if (exists || error) {
			std::cerr << "Could not read checkpoint '" << filename << "'\n";
		}
		return std::nullopt;
	}

	// read one more line to catch extras
	std::string column_names;
	std::string record;
	std::string extra;
	const bool has_header = bool(std::getline(input, column_names));
	const bool has_record = bool(std::getline(input, record));
	const bool has_extra = bool(std::getline(input, extra));

	// accept files with windows line endings
	if (!column_names.empty() && column_names.back() == '\r') {
		column_names.pop_back();
	}
	if (!record.empty() && record.back() == '\r') {
		record.pop_back();
	}

	// explain why this file cannot be loaded
	const auto reject = [&filename](const char *reason) -> std::optional<Checkpoint> {
		std::cerr << "Could not load checkpoint '" << filename << "': " << reason << '\n';
		return std::nullopt;
	};

	// expect one header and one saved checkpoint
	if (!has_header || !has_record || has_extra || input.bad()) {
		return reject("expected a header and one row");
	}

	// check the column names before reading values
	if (column_names != header) {
		return reject("unknown column names");
	}

	// read each value between the commas
	std::array<std::string, 10> fields{};
	std::istringstream row(record);
	for (std::string &field : fields) {
		if (!std::getline(row, field, ',') || field.empty()) {
			return reject("missing column value");
		}
	}

	// reject extra values and a trailing comma
	if (row.peek() != std::char_traits<char>::eof() || record.back() == ',') {
		return reject("extra column");
	}

	// match the version to its column names
	if (fields[0] != "3") {
		return reject("version disagrees with header");
	}

	// only two levels have save files
	if (fields[1] != "1" && fields[1] != "2") {
		return reject("unknown level");
	}

	// accept the entrance and six puzzle checkpoints
	if (fields[2].size() != 1 || fields[2][0] < '0' || fields[2][0] > '6') {
		return reject("unknown checkpoint");
	}

	// remember where the fish will return
	Checkpoint checkpoint;
	checkpoint.level = fields[1] == "2" ? 2 : 1;
	checkpoint.checkpoint = static_cast<Level2Checkpoint>(fields[2][0] - '0');

	// read the whole number of completed puzzle flags
	const char *end = fields[8].data() + fields[8].size();
	const std::from_chars_result parsed = std::from_chars(fields[8].data(), end, checkpoint.puzzle_flags);
	if (parsed.ec != std::errc{} || parsed.ptr != end) {
		return reject("invalid puzzle flags");
	}

	// read the carried item's size and movement
	Body2D body{};
	if (!read_number(fields[4], body.inverse_mass) || !read_number(fields[5], body.radius)
		|| !read_number(fields[6], body.drag) || !read_number(fields[7], body.current_scale)) {
		return reject("invalid carried item numbers");
	}

	// an empty mouth has no item values
	if (fields[3] == "empty") {
		if (body.inverse_mass != 0.0f || body.radius != 0.0f || body.drag != 0.0f || body.current_scale != 0.0f) {
			return reject("expected zeros for an empty mouth");
		}
	} else {
		// hide the item until the fish spits
		for (const SavedItem &item : saved_items) {
			if (fields[3] == item.name) {
				body.kind = item.kind;
				body.active = false;
				checkpoint.belly_contents = body;
				break;
			}
		}

		// reject items the fish cannot carry
		if (!checkpoint.belly_contents) {
			return reject("unknown carried item");
		}
	}

	// remember the jellyfish even after spitting it out
	if (fields[9] != "0" && fields[9] != "1") {
		return reject("invalid nearby jellyfish");
	}
	checkpoint.jelly_nearby = fields[9] == "1";

	// check the items and puzzles fit together
	if (!valid_checkpoint(checkpoint)) {
		return reject("invalid checkpoint data");
	}

	return checkpoint;
}

bool save_checkpoint(std::string const &filename, Checkpoint const &checkpoint) {
	// check the save before opening a file
	if (!valid_checkpoint(checkpoint)) {
		std::cerr << "Could not save checkpoint '" << filename << "': invalid checkpoint data\n";
		return false;
	}

	// write a separate file until saving finishes
	const std::string temporary = filename + ".tmp";
	std::ofstream output(std::filesystem::path(std::u8string(temporary.begin(), temporary.end())), std::ios::trunc);
	output.imbue(std::locale::classic());

	//keep enough digits to restore the carried object's exact float values
	output << std::setprecision(std::numeric_limits<float>::max_digits10);
	output << header << '\n' << "3," << checkpoint.level << ',' << static_cast<unsigned>(checkpoint.checkpoint) << ',';

	// keep the item for the next life
	if (checkpoint.belly_contents) {
		Body2D const &body = *checkpoint.belly_contents;
		for (const SavedItem &item : saved_items) {
			if (item.kind == body.kind) {
				output << item.name;
				break;
			}
		}

		// keep the size and movement with the item
		output << ',' << body.inverse_mass << ',' << body.radius
			<< ',' << body.drag << ',' << body.current_scale;
	} else {
		output << "empty,0,0,0,0";
	}

	// finish the row with all completed puzzles
	output << ',' << checkpoint.puzzle_flags << ',' << checkpoint.jelly_nearby << '\n';
	output.close();

	// discard the new file if writing failed
	if (!output) {
		std::cerr << "Could not write checkpoint '" << filename << "'\n";
		SDL_RemovePath(temporary.c_str());
		return false;
	}

	//replace the previous save only after the complete record has been closed successfully
	//temporary-file replacement follows https://wiki.libsdl.org/SDL3/SDL_CopyFile
	if (!SDL_RenamePath(temporary.c_str(), filename.c_str())) {
		std::cerr << "Could not replace checkpoint '" << filename << "': " << SDL_GetError() << '\n';
		SDL_RemovePath(temporary.c_str());
		return false;
	}

	return true;
}
