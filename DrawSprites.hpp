#pragma once

#include "GL.hpp"

#include <glm/glm.hpp>
#include <string>

struct SpriteTexture {
	explicit SpriteTexture(std::string const &filename);
	~SpriteTexture();
	SpriteTexture(SpriteTexture const &) = delete;
	SpriteTexture &operator=(SpriteTexture const &) = delete;

	GLuint texture = 0;
	glm::uvec2 pixel_size = glm::uvec2(0);
};

// four sandbox sprites only need a shared quad
struct DrawSprites {
	DrawSprites();
	~DrawSprites();
	DrawSprites(DrawSprites const &) = delete;
	DrawSprites &operator=(DrawSprites const &) = delete;

	void draw(SpriteTexture const &texture, glm::mat4 const &world_to_clip,
		glm::vec2 const &center, glm::vec2 const &size, float angle = 0.0f,
		bool flip_x = false, glm::u8vec4 const &tint = glm::u8vec4(255)) const;

private:
	GLuint vertex_buffer = 0;
	GLuint vertex_array = 0;
};
