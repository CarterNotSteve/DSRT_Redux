## DSRT Redux
--
This is a remake of a project I did in 7th grade in python using pygame.
DSRT -- Doom Style RayTracer, Redux -- From Latin _reducire_ meaning "To lead back".
This serves primarily as a simple demonstration of the same skills -- with a twist.
Instead of remaking it in pygame, I'm remaking the DSRT using Simple Direct Medialayer V3 (SDL3) and C++.

## Core Functionality
--
To gather the surroundings, the tracer (player point with vision vector) runs a sampling raycast on the map texture at the angle to which it would appear to the player.
It then uses this to just draw a line on screen with respective distance falloff proportional to line length at position.
Hopefully, being C++ and SDL3 this time around, it does a bit better performance-wise, but I got decent results (framerate) with pygame.
Being CPU-only rendering, it should be slower than if I loaded it into GRAM and ran the traces as FLOPS on the GPU.
Developed on a Linux laptop, however, it would just be using integrated graphics nonetheless.
I will update this once it runs normally, on CPU-only mode, to add GPU contexts and such.
One goal for this project is avoiding iterative loops to detect distance (Ex. while (image[point] != Color(#000000)) {...}) for linear time (I.e. Takes as many calculations as samples).

## The Larger Meaning
--
Written using a graphics library like SDL3, these skills ought to be transferrable to other graphics libraries and APIs like raw OpenGL or Vulkan.
This shall demonstrate my usage of algorithms, memory, optimizations, etc. this time, in C++, as well as demonstrating an understanding of data and memory management.

## TODO: How the future of this project looks
--
- Adding GPU render support
- Adding images of running, static-compiled SDL3, with no reliance on directory-based dependencies (other than the map texture, as is necessary) (I.e. lacking ../assets or ../lib, etc.)
- Possible: Usage of custom SDL wrapper to avoid boilerplate as much as possible.


### Project Log
--

___________________________________________________________________________________________
- | 11/8 | Initial entry; refactored to avoid C stdio's FILE*, replaced with std::ifstream. |
____________________________________________________________________________________________________________________
- | 11/9 | Fixed FPE thrown in sanitized; Loop not detecting keypresses, nor drawing frames as player. [@ 08:01]   |
____________________________________________________________________________________________________________________
- | 11/16 | Fixed PBM loading. Drawing always finds 11 steps to wall for some reason. ???