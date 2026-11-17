# Changelog

## 3.1

New
- Ash: Solstheim's volcanic ash kicks up ash flakes, the snow flakes in the ash's grey (they flutter down the same way and settle soon after landing). Ash covered rocks and objects too, and the wind lifts them. Own section on the Settings page and an Ash category on the Textures page.
- Dragons: a dragon landing throws up a big ring of whatever lies around it, and taking off another. Its wing beats blow the ground below while it hovers, lands or takes off, and gliding low it stirs up a strip behind it. A bite blows a blast from its jaws along the ground, its tail and wings sweeping the ground throw everything up, and its tail smashing down throws up a ring. Works from the dragons' own animation events (landing, takeoff, bite, tail and wing attacks, wing flaps) and their flight state. Dragons count up to 6000 units away, besides the NPC limit, and a dragon killed in the air is watched until it crashes. Own section on the Settings page (on/off, strength).
- Rain: while it rains the ground is wet and no dust or rock dust flies up (clods, pebbles and everything else still do). Always on.

Other
- Ash covered objects and the Ash collision material give ash flakes instead of dust.
- Shouts, explosions, heavy blows and falling bodies no longer throw leaves and dust up from the bottom of lakes and rivers.
- Menu texts in all 9 languages.

## 3.0

New
- Seasons of Skyrim support: the land counts as the season paints it. In winter the snowy land kicks up snow where autumn leaves, pine needles, dust and pebbles fly the rest of the year; other seasons follow the swaps that mods add for them. The season comes from Seasons of Skyrim itself (also when a mod sets it), only in the worldspaces it repaints. Can be switched off (Settings page).
- Green leaves: a third installer choice, Mixed (broad green leaves and pine needles together, about half of each).
- Leaf mix page (only with a Mixed choice in the installer): set how much of each the ground kicks up, Riften's orange, yellow, red and brown leaves, or green broad leaves and pine needles. Leaves already out change as they are kicked up again.

Skyrim SE and VR
- Skyrim VR: the player update and explosion hooks use VR's own virtual function slots (they hooked the wrong functions there before).
- Skyrim VR: weapon swings stir things up by your real arm movements while your hands are out (VR has no attack animations to watch).
- Checked for Skyrim SE 1.5.97 and VR 1.4.15: every game function the mod calls has its address in their address libraries.

Other
- Menu: a "Seasons of Skyrim" section and the season in the status (all 9 languages).

## 2.1

New
- FOMOD installer with a choice of leaves, each with a preview picture:
  - Riften's autumn leaves: orange, yellow, red, or mixed (the 2.0 look).
  - Green leaves: broad (the 2.0 look) or pine needles (single long needles, a better match for pine and spruce forests).

Other
- Green leaves have their own normal map (textures\ResponsiveGrounds\GreenLeaf_n.dds).
- All leaf textures are BC7.

## 2.0

New
- Green leaves: a category of their own (textures, amounts, object materials), on the pine forest textures by default.
- Snow: snow flakes kicked up on snow textures and snowy rocks and cliffs; they flutter down and melt soon after landing.
- Sounds: a soft rustle of leaves flying up and sailing past, little stones ticking as pebbles land (own volumes, the game's effects volume applies; no plugin needed).
- Explosions near the ground (fireballs, runes, oil, traps) blow everything away in a ring (own strength slider).
- Leaves and snow flakes float on water and drift, then sink; nothing is kicked up from under water.

Better ground detection
- Objects painted with landscape textures (stream beds, ground pieces, cliffs with a landscape texture swap) kick up what those textures give on the land.
- Mountains give rocks (they use the landscape's collision layer and were taken for the land under them); dirt cliffs give dust, mountain and rock slabs pebbles, by their texture names.
- Standing up on things that aren't ground (log piles, buildings, docks, lumber mills, carts) kicks up nothing.
- Wind and shock waves don't throw the land's leaves onto rocks and roofs.

Other
- Particles a bit darker; smaller (BC7) textures.
- Menu: new texts in all 9 languages; the status shows the landscape texture painted on an object, or that you are up on something that isn't ground.

## 1.0

First release.
