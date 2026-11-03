# Changelog

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
