# Video editor - built for automation and my workflow

## Broad strcuture
### Libraries
GUI: IMGUI (imnodes for nodes)
audio: aubio
Rendering: Vulkan

### Overall software design
#### Priorities
Img+Text+Audio all have equal weight here.  Can select, cut, etc. by conditions on any of these equally or manually. Have different modes like blender. Everything is parametric and effects rather than destructive manipulation.
#### Operations
Make user operations recorded for undo/redo - means everything is inversible?
#### Customizable
Editing source code, and to be extensible and usable with python/lua, easily setup in nodes.
### Example built-in tools
transcription, auto cutting by conditions (image in part of screen, silence for a certain duration, cut out certain words, ...), 
