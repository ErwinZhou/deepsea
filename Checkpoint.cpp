#include "Checkpoint.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>

static constexpr char checkpoint_header[] = "version,level,checkpoint,belly,inverse_mass,radius,drag,current_scale";

static bool valid_checkpoint(Checkpoint const &checkpoint) {
	if ((checkpoint.level != 1 && checkpoint.level != 2) || checkpoint.checkpoint != 0) {
		return false;
	}
	if (!checkpoint.belly_contents) {
		return true;
	}
	Body2D const &body = *checkpoint.belly_contents;
	return checkpoint.level == 2 && (body.kind == Body2D::Rock || body.kind == Body2D::Jelly)
		&& std::isfinite(body.inverse_mass) && body.inverse_mass > 0.0f
		&& std::isfinite(body.radius) && body.radius > 0.0f
		&& std::isfinite(body.drag) && body.drag >= 0.0f
		&& std::isfinite(body.current_scale) && body.current_scale >= 0.0f;
}

static bool read_number(std::string const &field, float &value) {
	std::istringstream input(field);
	input.imbue(std::locale::classic());
	if (!(input >> value) || !std::isfinite(value)) {
		return false;
	}
	input >> std::ws;
	return input.eof();
}

std::string checkpoint_path() {
	//this idea of saving directory and returned-pointer ownership is inspired by https://wiki.libsdl.org/SDL3/SDL_GetPrefPath
	std::unique_ptr<char, decltype(&SDL_free)> directory(SDL_GetPrefPath("DeepSea", "DeepSea"), SDL_free);
	if (!directory) {
		std::cerr << "Could not locate checkpoint directory: " << SDL_GetError() << '\n';
		return {};
	}
	return std::string(directory.get()) + "checkpoint.csv";
}

std::optional<Checkpoint> load_checkpoint(std::string const &filename) {
	//explicit utf-8 paths keep unicode save locations usable on windows
	const std::filesystem::path path(std::u8string(filename.begin(), filename.end()));
	std::ifstream input(path);
	if (!input) {
		std::error_code error;
		const bool exists = std::filesystem::exists(path, error);
		if (exists || error) {
			std::cerr << "Could not read checkpoint '" << filename << "'\n";
		}
		return std::nullopt;
	}

	std::string header;
	std::string record;
	std::string extra;
	const bool has_header = bool(std::getline(input, header));
	const bool has_record = bool(std::getline(input, record));
	const bool has_extra = bool(std::getline(input, extra));
	if (!header.empty() && header.back() == '\r') {
		header.pop_back();
	}
	if (!record.empty() && record.back() == '\r') {
		record.pop_back();
	}
	Checkpoint checkpoint;
	bool valid = has_header && has_record && !has_extra && !input.bad() && header == checkpoint_header;
	std::array<std::string, 8> fields{};
	std::istringstream row(record);
	for (std::string &field : fields) {
		if (!std::getline(row, field, ',') || field.empty()) {
			valid = false;
		}
	}
	valid = valid && row.peek() == std::char_traits<char>::eof() && record.back() != ','
		&& fields[0] == "1" && (fields[1] == "1" || fields[1] == "2") && fields[2] == "0";
	if (valid) {
		checkpoint.level = fields[1] == "2" ? 2 : 1;
		Body2D body{};
		valid = read_number(fields[4], body.inverse_mass) && read_number(fields[5], body.radius)
			&& read_number(fields[6], body.drag) && read_number(fields[7], body.current_scale);
		if (fields[3] == "empty") {
			valid = valid && body.inverse_mass == 0.0f && body.radius == 0.0f && body.drag == 0.0f && body.current_scale == 0.0f;
		} else if (fields[3] == "rock" || fields[3] == "jelly") {
			body.kind = fields[3] == "jelly" ? Body2D::Jelly : Body2D::Rock;
			body.active = false;
			checkpoint.belly_contents = body;
		} else {
			valid = false;
		}
	}
	if (!valid || !valid_checkpoint(checkpoint)) {
		std::cerr << "Could not load checkpoint '" << filename << "': invalid or unsupported save data\n";
		return std::nullopt;
	}
	return checkpoint;
}

bool save_checkpoint(std::string const &filename, Checkpoint const &checkpoint) {
	if (!valid_checkpoint(checkpoint)) {
		std::cerr << "Could not save checkpoint '" << filename << "': invalid checkpoint data\n";
		return false;
	}
	const std::string temporary = filename + ".tmp";
	std::ofstream output(std::filesystem::path(std::u8string(temporary.begin(), temporary.end())), std::ios::trunc);
	output.imbue(std::locale::classic());
	//keep enough digits to restore the carried object's exact float values
	output << std::setprecision(std::numeric_limits<float>::max_digits10);
	output << checkpoint_header << '\n' << "1," << checkpoint.level << ',' << checkpoint.checkpoint << ',';
	if (checkpoint.belly_contents) {
		Body2D const &body = *checkpoint.belly_contents;
		output << (body.kind == Body2D::Jelly ? "jelly" : "rock") << ',' << body.inverse_mass << ',' << body.radius
			<< ',' << body.drag << ',' << body.current_scale << '\n';
	} else {
		output << "empty,0,0,0,0\n";
	}
	output.close();
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
