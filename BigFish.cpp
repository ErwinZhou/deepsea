//the vertex buffer code here is based on DrawLines.cpp from the base code.
#include "BigFish.hpp"

#include "ColorTextureProgram.hpp"
#include "GL.hpp"
#include "Load.hpp"
#include "data_path.hpp"
#include "gl_errors.hpp"
#include "load_save_png.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

static std::mt19937 rng{std::random_device{}()};

//how far the jaws open, as a fraction of the jaw width
static constexpr float MaxGap = 0.6f;
//how far in front of the jaws it can suck things in, in jaw widths
static constexpr float SuckReach = 1.0f;

struct Vertex {
	glm::vec2 Position;
	glm::u8vec4 Color;
	glm::vec2 TexCoord;
};

static GLuint vertex_buffer = 0;
static GLuint vertex_buffer_for_color_texture_program = 0;
static GLuint jaw_tex = 0;
static glm::uvec2 jaw_size = glm::uvec2(0);

static Load< void > setup_big_fish(LoadTagDefault, [](){
	glGenBuffers(1, &vertex_buffer);

	glGenVertexArrays(1, &vertex_buffer_for_color_texture_program);
	glBindVertexArray(vertex_buffer_for_color_texture_program);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
	glVertexAttribPointer(color_texture_program->Position_vec4, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, Position));
	glEnableVertexAttribArray(color_texture_program->Position_vec4);
	glVertexAttribPointer(color_texture_program->Color_vec4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, Color));
	glEnableVertexAttribArray(color_texture_program->Color_vec4);
	glVertexAttribPointer(color_texture_program->TexCoord_vec2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, TexCoord));
	glEnableVertexAttribArray(color_texture_program->TexCoord_vec2);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	//this png is the top jaw. the bottom jaw is the same png flipped
	std::vector< glm::u8vec4 > data;
	load_png(data_path("sprites/big-fish-jaw.png"), &jaw_size, &data, LowerLeftOrigin);

	glGenTextures(1, &jaw_tex);
	glBindTexture(GL_TEXTURE_2D, jaw_tex);
	//based on https://registry.khronos.org/OpenGL-Refpages/gl4/html/glTexImage2D.xhtml
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, jaw_size.x, jaw_size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	GL_ERRORS();
});

void BigFish::reset(glm::vec2 const &start) {
	state = Waiting;
	position = start;
	mouth_phase = 0.0f;
	//based on https://en.cppreference.com/w/cpp/numeric/random/uniform_real_distribution
	timer = std::uniform_real_distribution< float >(MinDelay, MaxDelay)(rng);
}

void BigFish::update(float elapsed) {
	if (state == Waiting) {
		timer -= elapsed;
		if (timer <= 0.0f) state = Normal;
		return;
	}

	mouth_phase += elapsed / MouthPeriod;
	mouth_phase -= std::floor(mouth_phase);

	if (state == Normal) {
		position.x += speed * elapsed;
	} else {
		//rocked or stunned, so stay still until the timer runs out
		timer -= elapsed;
		if (timer <= 0.0f) state = Normal;
	}
}

void BigFish::rock() {
	if (state == Waiting) return;
	state = Rocked;
	timer = RockedTime;
}

void BigFish::stun() {
	if (state == Waiting) return;
	state = Stunned;
	timer = StunnedTime;
}

float BigFish::gap() const {
	float open = 0.5f - 0.5f * std::cos(mouth_phase * 2.0f * float(M_PI));
	return open * MaxGap * size;
}

bool BigFish::can_suck(glm::vec2 const &p) const {
	if (state != Normal) return false;
	glm::vec2 d = p - position;
	if (d.x < -0.5f * size || d.x > (0.5f + SuckReach) * size) return false;
	return std::abs(d.y) < 0.5f * gap();
}

void BigFish::draw(glm::mat4 const &world_to_clip) const {
	if (state == Waiting) return;

	//draw it darker while it's stopped
	glm::u8vec4 color = (state == Normal ? glm::u8vec4(0xff) : glm::u8vec4(0x77, 0x77, 0x77, 0xff));
	float w = 0.5f * size;
	float h = size * float(jaw_size.y) / float(jaw_size.x);
	float y = 0.5f * gap();

	std::vector< Vertex > attribs;
	auto quad = [&](float bottom, float top, float tex_bottom, float tex_top) {
		Vertex a{glm::vec2(position.x - w, bottom), color, glm::vec2(0.0f, tex_bottom)};
		Vertex b{glm::vec2(position.x + w, bottom), color, glm::vec2(1.0f, tex_bottom)};
		Vertex c{glm::vec2(position.x + w, top), color, glm::vec2(1.0f, tex_top)};
		Vertex d{glm::vec2(position.x - w, top), color, glm::vec2(0.0f, tex_top)};
		attribs.insert(attribs.end(), {a, b, c, a, c, d});
	};
	quad(position.y + y, position.y + y + h, 0.0f, 1.0f);
	quad(position.y - y - h, position.y - y, 1.0f, 0.0f);

	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, attribs.size() * sizeof(attribs[0]), attribs.data(), GL_STREAM_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(color_texture_program->program);
	glUniformMatrix4fv(color_texture_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(world_to_clip));
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, jaw_tex);
	glBindVertexArray(vertex_buffer_for_color_texture_program);

	glDrawArrays(GL_TRIANGLES, 0, GLsizei(attribs.size()));

	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);

	GL_ERRORS();
}
