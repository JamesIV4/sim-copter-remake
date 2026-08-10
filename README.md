# SimCopter Remake

[![Download Latest Release](https://img.shields.io/badge/Download-Latest%20Release-brightgreen?style=for-the-badge&logo=github)](https://github.com/JamesIV4/sim-copter-remake/releases/latest)

**SimCopter Remake** is a modern, faithful re-implementation of Maxis’s classic 1996 PC flight game *SimCopter*. Built from the ground up in Unreal Engine 5, it brings back everything fans loved about taking to the skies in their favorite *SimCity 2000* cities: now running smoothly on modern PCs with high-definition visuals, widescreen support, dynamic lighting, and responsive controls. It comes packaged with classic user cities (such as Cape Wells, Tokyo, and Rio) for an immediate sandbox flight experience, alongside full support for importing any of your own custom *SimCity 2000* maps.

Whether you are rescuing stranded citizens from burning skyscrapers, airlifting boaters off sinking vessels, dousing raging city infernos with water buckets, dispersing traffic jams, or jamming out to the iconic in-game radio stations, this remake delivers the exact gameplay, physics, and nostalgia of the 1996 original without the crashes, crippling view distance, low-resolution limits, or compatibility headaches of 90s software.

## 📸 Screenshots

| Flight & City Exploration | Firefighting Operations |
| :---: | :---: |
| [![City Flight](Docs/screenshots/1.png)](Docs/screenshots/1.png) | [![Firefighting](Docs/screenshots/2.png)](Docs/screenshots/2.png) |

| Maritime Rescue Operations | Winch & Passenger Pickup |
| :---: | :---: |
| [![Sunset Rescue](Docs/screenshots/3.png)](Docs/screenshots/3.png) | [![Rescuing Survivors](Docs/screenshots/4.png)](Docs/screenshots/4.png) |

| Cockpit Camera View | Night Flight & City Skyline |
| :---: | :---: |
| [![Rescue Camera](Docs/screenshots/5.png)](Docs/screenshots/5.png) | [![Night City](Docs/screenshots/6.png)](Docs/screenshots/6.png) |

## 🚁 Recreating the Original Experience

Every core system in SimCopter Remake has been meticulously crafted to match the original 1996 release:

* **SimCity 2000 Import & Sandbox Cities:** Play through all 30 built-in career cities or load pre-packaged user cities (like Cape Wells, Egypt Falls, Tokyo, and Rio) for a flexible sandbox flight experience. You can also import any custom `.sc2` city file created in *SimCity 2000*: terrain, shorelines, roads, bridges, and buildings load directly into 3D space.
* **Authentic Flight Physics & Helicopter Fleet:** Fly the full original fleet, from the starter Schweizer 300 up through the Bell 206 JetRanger, MD 500, MBB Bo 105, Eurocopter AS365 Dauphin, and the heavy-lift Boeing CH-47 Chinook. Helicopter handling, rotor speeds, weight physics, and landing mechanics match the original design.
* **Complete Mission & Career System:** Respond to emergency dispatches across the city to earn money and points. Transport passengers between helipads, airlift medevac patients to hospitals, fight fires, break up riots with your megaphone, drop off police officers to apprehend criminals, and rescue victims stranded on roofs or out at sea.
* **Classic Cockpit & Flight Gear:** Control your searchlight, megaphone, water bucket, rescue harness, water cannon, and tear gas directly from the cockpit. The interactive instrumentation panel includes the classic radar screen, fuel gauge, altimeter, speedometer, and passenger seating grid.
* **Original Radio & Audio Experience:** Listen to all the classic in-game radio stations (Rock, Classical, Jazz, Techno, Mix) complete with retro tunes and hilarious DJ commercials while patrolling the skies.



## ✨ Modern Improvements

While preserving 100% of the original gameplay feel, the remake upgrades the experience for modern hardware with Unreal Engine 5.8:

* **Modern Graphics & Dynamic Lighting:** Enjoy full high-definition 3D graphics featuring dynamic day/night cycles, glowing city night lights, soft shadows, and realistic reflections and indirect lighting.
* **High Frame Rates & Widescreen Display:** Native 60+ FPS gameplay with full support for 1080p, 2K, 4K, and ultrawide resolutions. Say goodbye to stretched 640x480 windows or finicky patchers.
* **Modern Controls & Gamepad Support:** Fly effortlessly using keyboard/mouse setups or modern gamepads (Xbox, PlayStation, dual-stick controllers) with smooth, configurable flight inputs.
* **Rock-Solid Windows Compatibility:** Native 64-bit support ensures the game runs fast and stable on modern Windows operating systems without crash-prone emulation or complex patches.
* **Extended View Distance:** Fly high over sprawling metropolitan areas without heavy 90s distance fog cutting off your view.



## 🛠️ How It Was Made

This project was created through careful **decompilation and re-implementation**.

Rather than guessing how *SimCopter* worked or approximating its flight feel, the original 1996 game's internal rules, mathematical formulas, and asset structures were thoroughly analyzed. Every helicopter tuning parameter, city building generator, mission condition, and control rule was carefully ported step-by-step into clean modern code within Unreal Engine 5.

This means the game doesn't just look like *SimCopter*: it plays on the exact same underlying rules that made the 1996 game classic, combined with the performance and visual power of a modern game engine.
