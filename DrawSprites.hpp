#pragma once

#include "GL.hpp"
#include "Water.hpp"

#include <glm/glm.hpp>
#include <string>

struct SpriteTexture {
	explicit SpriteTexture(std::string const &filename);
	explicit SpriteTexture(glm::u8vec4 const &color);
	~SpriteTexture();
	SpriteTexture(SpriteTexture const &) = delete;
	SpriteTexture &operator=(SpriteTexture const &) = delete;

	GLuint texture = 0;
	glm::uvec2 pixel_size = glm::uvec2(0);

private:
	void upload(glm::u8vec4 const *pixels);
};

// every sprite is the same square, stretched and turned
struct DrawSprites {
	DrawSprites();
	~DrawSprites();
	DrawSprites(DrawSprites const &) = delete;
	DrawSprites &operator=(DrawSprites const &) = delete;

	enum Material { Emissive, Surface, Background, Stone, Predator, Player, Cracked, FarTail, Pillar };

	// fade blends distant sprites into the water
	void draw(SpriteTexture const &texture, glm::mat4 const &world_to_clip,
		glm::vec2 const &center, glm::vec2 const &size, float angle = 0.0f,
		bool flip_x = false, glm::u8vec4 const &tint = glm::u8vec4(255), float fade = 0.0f, Material material = Surface,
		glm::vec4 const &uv_rect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)) const;
	// call once a frame before drawing, so sprites match the water around them
	void set_look(Water::Look const &look) const;
	// the jellyfish light makes cracks visible
	void set_jelly_light(glm::vec3 const &light) const;

private:
	GLuint vertex_buffer = 0;
	GLuint vertex_array = 0;
	GLuint program = 0;
	GLint OBJECT_TO_CLIP_mat4 = -1;
	GLint TINT_vec4 = -1;
	GLint FADE_float = -1;
	GLint MATERIAL_int = -1;
	GLint TEX_RECT_vec4 = -1;
	GLint JELLY_LIGHT_vec3 = -1;
	Water::Uniforms look_uniforms;
};
