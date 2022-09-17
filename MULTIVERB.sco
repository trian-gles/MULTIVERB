rtsetparams(44100, 8)
load("NOISE")
load("NPAN")
load("./libMULTIVERB.so")

bus_config("NOISE", "aux 0 out")
bus_config("NPAN", "aux 0 in", "aux 1-8 out")
bus_config("MULTIVERB", "aux 1-8 in", "out 0-7")


sin45 = 0.70710678

NPANspeakers("polar",
       45, 1,   // front left
      -45, 1,   // front right
       90, 1,   // side left
      -90, 1,   // side right
      135, 1,   // rear left
     -135, 1,   // rear right rear
        0, 1,   // front center
      180, 1)   // rear center

dur = 10
amp = 1000
freq = 440

env = maketable("line", 1000, 0,0, 1,1, 49,1, 50,0)
NOISE(0, dur, amp*env)


angle = maketable("line", "nonorm", 1000, 0,0, 1,360 * 8)

dist = 1

NPAN(0, 0, dur, 1, "polar", angle, dist)

roomsize = 0.9
predelay = .03
ringdur = 3
damp = 70
dry = 0
wet = 100
   
   
MULTIVERB(0, 0, dur, 1, roomsize, predelay, ringdur, damp, dry, wet)