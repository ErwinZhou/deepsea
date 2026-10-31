# DeepSea

A 2D underwater horror exploration game about a small fish surviving the chase of a mysterious monstrous creature and a much larger world.

![Screen Shot](docs/screenshot.png)

Team: Haoran Xie, Yuchen Zhou, Sarah Garland.

## Build and run

You need Node.js, a C++20 compiler, and the course's `nest-libs` in a folder next to this one. [NEST.md](NEST.md) has the full setup.

```sh
node Maekfile.js
./dist/game
```

On Windows, run `dist\game.exe`. Escape quits.

Built on the course's base code. See [LICENSE](LICENSE).

Credits: the fish, jaw, rock, wall, cavern background and jellyfish art in `assets/` was drawn by Sarah Garland for this game.
`cavern_rock.png` is a softened version of Sarah's cavern background.
Lighting and composition references: [The Lighting of INSIDE](https://blog.playdead.com/articles/the_lighting_of_inside/lighting_of_inside.html), [Spiral Circus interview](https://goombastomp.com/spiral-circus-interview/), and [Rendering Water Caustics](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-2-rendering-water-caustics).
The noise hash uses Chris Wellons' [lowbias32](https://github.com/skeeto/hash-prospector#discovered-hash-functions), public domain under the [Unlicense](https://github.com/skeeto/hash-prospector/blob/master/UNLICENSE).
