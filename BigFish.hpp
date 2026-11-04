#pragma once

#include "DrawSprites.hpp"

#include <glm/glm.hpp>

//the big fish that chases the player. all you see is the light on its lure until you get too close
struct BigFish {
	BigFish();

	enum State {
		Waiting, //hidden off screen until enter() is called
		Normal, //swims right
		Charging, //jaws showing, speeding up and opening wider
		Slamming, //jaws shutting
		Stunned, //pinned by falling rock, stops moving
	} state = Waiting;

	//left edge of the jaw pngs, at the line where the two jaws meet. the jaws turn around this point
	glm::vec2 position = glm::vec2(0.0f);
	//how wide each jaw png is drawn. the height follows the png's shape
	float size = 100.0f;
	// average swim speed, 0.9 takes about 11.1 seconds to cover 10 units, 0.8 takes about 12.5 seconds
	float speed = 0.9f;

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

	//in_reach sets off a charge, in_mouth gets caught when the jaws slam
	bool in_reach(glm::vec2 const &point) const;
	bool in_mouth(glm::vec2 const &point) const;

	//0 when the mouth is at its widest, 1 when it's shut
	float closed() const;
	//what its speed gets multiplied by while stalking. between 0 and 2, averages 1
	float stroke() const;
	//art_front is the furthest right any part of the jaws reaches from position. anything left of teeth_x is inside the mouth
	float art_front() const;
	float teeth_x() const;
	//the position.x that puts the lure's light just out of sight, off the left of the view
	float out_of_sight_x(float view_left) const;
	glm::vec2 lure_position() const;
	float lure_brightness() const;
	//0 when the jaws are hidden, 1 when they show
	float visibility() const;
	glm::vec2 shake() const;

	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;

	SpriteTexture upper_jaw;
	SpriteTexture lower_jaw;
	SpriteTexture lure_texture;

private:
	float charge_speed() const;
	//middle of the mouth's height relative to position, for a given closed()
	float mouth_offset(float shut) const;
	float lowest_mouth = 0.0f;
	float highest_mouth = 0.0f;
	float lure_top = 0.0f;
	//height of the lure's light before the drift is added
	float lure_y = 0.0f;
	float charge_time = 0.0f;
	//what closed() returns, and its value when the slam started
	float jaw = 0.0f;
	float slam_from = 0.0f;
	//0 to 1, how far the jaws have faded in
	float shown = 0.0f;
	float shake_left = 0.0f;
};
