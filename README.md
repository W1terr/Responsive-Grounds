# Responsive Grounds

SKSE plugin for Skyrim SE / AE: the ground answers to everything that moves on it. Fallen leaves swirl up behind you in Riften's autumn forest (inspired by Ghost of Tsushima), dirt and dust fly from dirt roads and mud, pebbles and rock dust from rocky ground.

- **Steps**: things fly up with each footfall, a few when you walk, many more when you run, sprint or ride. They ease in after half a second of moving, swirl in the air you drag along, flutter or drop back down and fade away. Ground you just went over has fewer leaves left for a while.
- **Leaves** on Riften's autumn forest textures; **dust** (clods that bounce once, dust puffs) on dirt and mud; **rocks** (pebbles, rock dust) on rocky ground. Rocks, cliffs, streets and walkways count too, by their surface material.
- **Your horse, NPCs and creatures** kick things up as well (the nearest ones; giants and mammoths over a wider area).
- **Weapon swings** stir up leaves and a little dust under the blade (works with vanilla, MCO, SCAR and Precision attacks).
- **Shouts** blow everything away in front of the shouter; **heavy blows** on the ground (giant clubs, hammers) and **falling bodies** throw up a ring.
- **Wind** (optional): strong wind lifts leaves and carries them across your view; works with Dynamic Wind.
- **Northern Roads** support: its roads kick up dust and pebbles by kind of road, on their own menu page.
- Every landscape texture can be added to or taken out of each kind, with its own amount, in the menu.
- Menu in English, French, German, Italian, Spanish, Polish, Russian, Japanese and Chinese.

Settings: SKSE Menu Framework > Responsive Grounds (saved to `Documents\My Games\Skyrim Special Edition\SKSE\ResponsiveGrounds.json`).

Requires SKSE and Address Library for SKSE Plugins; SKSE Menu Framework 3 for the in-game menu. No plugin (esp) needed.

## Credits

- Leaf, dirt and rock textures made from Bethesda's own effect textures (aspen leaf, soft puffs, tumbling rocks)
- CommonLibSSE-NG, SKSE Menu Framework 3 API (LGPL-2.1), nlohmann/json

## License

GPL-3.0
