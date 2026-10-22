#include "DrawSprites.hpp"

#include "ColorTextureProgram.hpp"
#include "load_save_png.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstddef>
#include <vector>

SpriteTexture::SpriteTexture(std::string const &filename) {
	std::vector<glm::u8vec4> pixels;
	load_png(filename, &pixel_size, &pixels, LowerLeftOrigin);
	glGenTextures(1, &texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, pixel_size.x, pixel_size.y,
		0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
}

SpriteTexture::~SpriteTexture() {
	glDeleteTextures(1, &texture);
}

DrawSprites::DrawSprites() {
	struct Vertex {
		glm::vec2 position;
		glm::vec2 tex_coord;
	};
	Vertex const vertices[] = {
		{{-0.5f, -0.5f}, {0.0f, 0.0f}},
		{{ 0.5f, -0.5f}, {1.0f, 0.0f}},
		{{ 0.5f,  0.5f}, {1.0f, 1.0f}},
		{{-0.5f, -0.5f}, {0.0f, 0.0f}},
		{{ 0.5f,  0.5f}, {1.0f, 1.0f}},
		{{-0.5f,  0.5f}, {0.0f, 1.0f}}
	};
	glGenBuffers(1, &vertex_buffer);
	glGenVertexArrays(1, &vertex_array);
	glBindVertexArray(vertex_array);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	glVertexAttribPointer(color_texture_program->Position_vec4, 2, GL_FLOAT,
		GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, position)));
	glEnableVertexAttribArray(color_texture_program->Position_vec4);
	glVertexAttribPointer(color_texture_program->TexCoord_vec2, 2, GL_FLOAT,
		GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, tex_coord)));
	glEnableVertexAttribArray(color_texture_program->TexCoord_vec2);
	glDisableVertexAttribArray(color_texture_program->Color_vec4);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

DrawSprites::~DrawSprites() {
	glDeleteVertexArrays(1, &vertex_array);
	glDeleteBuffers(1, &vertex_buffer);
}

void DrawSprites::draw(SpriteTexture const &texture, glm::mat4 const &world_to_clip,
	glm::vec2 const &center, glm::vec2 const &size, float angle, bool flip_x,
	glm::u8vec4 const &tint) const {
	glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f));
	transform = glm::rotate(transform, angle, glm::vec3(0.0f, 0.0f, 1.0f));
	transform = glm::scale(transform, glm::vec3(flip_x ? -size.x : size.x, size.y, 1.0f));
	glm::mat4 object_to_clip = world_to_clip * transform;
	glUseProgram(color_texture_program->program);
	glUniformMatrix4fv(color_texture_program->OBJECT_TO_CLIP_mat4, 1,
		GL_FALSE, glm::value_ptr(object_to_clip));
	glm::vec4 color = glm::vec4(tint) / 255.0f;
	glVertexAttrib4fv(color_texture_program->Color_vec4, glm::value_ptr(color));
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture.texture);
	glBindVertexArray(vertex_array);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
}
