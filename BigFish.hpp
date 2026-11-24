#pragma once

#include "DrawSprites.hpp"
#include "Sound.hpp"

#include <glm/glm.hpp>

//chases the player and catches it when the jaws close
struct BigFish {
	BigFish();

	enum State {
		Waiting, //hidden off screen until enter() is called
		Normal, //approaches at a steady swimming pace
		Charging, //speeding up and opening wider
		Slamming, //jaws shutting
		Stunned, //pinned by falling rock, stops moving
		Held, //jaws held open by the octopus
	} state = Waiting;

	//pivot at the back of the mouth, shared by both jaws
	glm::vec2 position = glm::vec2(0.0f);
	//how wide each jaw png is drawn. the height follows the png's shape
	float size = 105.0f;

	//timer counts down while stunned or slamming. base_y is the height it drifts up and down around
	float timer = 0.0f;
	float time = 0.0f;
	float base_y = 0.0f;
	//where the small fish is, set each frame
	glm::vec2 prey = glm::vec2(0.0f);
	//true for the one update where the jaws slam shut
	bool slammed = false;

	//reset puts it at start, hidden until enter() is called. reset_for_world hides it off the world's left edge
	void reset(glm::vec2 const &start);
	void reset_for_world(glm::vec2 const &bounds_min, glm::vec2 const &bounds_max);
	void enter(float x);
	void update(float elapsed);
	void stun(float seconds);
	void charge();
	//true while the rock can pin it
	bool swimming() const;

	//check whether the player is close enough to trigger a charge or be caught
	bool in_reach(glm::vec2 const &point) const;
	bool in_mouth(glm::vec2 const &point) const;
	glm::vec2 escape_target() const;

	//0 when the mouth is at its widest, 1 when it's shut
	float closed() const;
	//front of the artwork for offscreen placement, and the teeth for catching the player
	float art_front() const;
	float teeth_x() const;
	//world position of the mouth center, including the head's turn
	glm::vec2 mouth_position() const;
	//the position.x that puts its head just off the left of the view
	float out_of_sight_x(float view_left) const;
	glm::vec2 shake() const;

	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;
	//its shadow passes behind the cave, head first to the left. hinge is where the jaws meet
	void draw_far(DrawSprites const &sprites, glm::mat4 const &world_to_clip, glm::vec2 const &hinge) const;
	glm::vec2 far_mouth(glm::vec2 const &hinge) const;

	SpriteTexture upper_jaw;
	SpriteTexture lower_jaw;
	SpriteTexture passage_light;

	void enter_from_right(float view_right, float mouth_y);
	static Sound::Sample const &arrival_sound();

	//both levels use the same chase sounds
	static std::shared_ptr<Sound::PlayingSample> play_presence(glm::vec3 const &mouth);
	static std::shared_ptr<Sound::PlayingSample> play_charge_roar(glm::vec3 const &mouth);
	static constexpr float presence_reach = 20.0f;
	static constexpr float charge_start_volume = 0.45f;
	static constexpr float charge_fade_in = 3.0f;

	static constexpr float arrival_volume = 1.2f;
	static constexpr float arrival_reach = 90.0f;
	static constexpr float arrival_shake_size = 0.08f;

	//keep the jaws open while the octopus holds them
	void hold_open();
	void hold_open(float passage_y);
	//attachment point along either jaw
	glm::vec2 tentacle_point(bool upper, float along) const;
private:
	void update_turn(float elapsed);
	glm::vec2 local_point(glm::vec2 const &point) const;
	float angle = 0.0f;
	float turn_speed = 0.0f;
	float held_mouth_x = 0.0f;
	float charge_speed() const;
	//upper and lower bite limits at the teeth
	glm::vec2 mouth_edges(float shut) const;
	//find the mouth center to align it with the middle of the corridor
	float mouth_offset(float shut) const;
	float charge_time = 0.0f;
	//what closed() returns, and its value when the slam started
	float jaw = 0.0f;
	float slam_from = 0.0f;
	float shake_left = 0.0f;

	//swims right in level 1 and left in level 2
	float direction = 1.0f;

	//faster approach during the final chase
	static constexpr float finale_speed_scale = 4.0f;

	//use the normal pace until the chase starts
	float swim_speed_scale = 1.0f;

};
