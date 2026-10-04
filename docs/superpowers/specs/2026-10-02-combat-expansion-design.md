# Combat expansion design

The user's eight requested changes extend the existing UE 5.8 live arena. Keep the minimalist Canvas presentation, five-minute rounds, red/blue/gray caps of 2000/2000/1000 and bounded work at 5000 viewers. User has authorized implementation; routine implementation decisions proceed in the current workspace without another approval gate.

## Camera and feedback

Right mouse releases follow. Wheel changes zoom while retaining the current follow target; while locked, map dragging, minimap and WASD do not move the camera away from it. Another ranked player or body can become the follow target. Free-camera wheel still anchors the mouse world point. The user clarified that the followed player's marker is a small gold downward-pointing arrow centered above the name, with visible spacing; replace the former gold horizontal line. Draw short animated projectile trails, muzzle flashes, impact sparks, recoil and hit flash instead of one long static trace. Reuse capped simulation/display pools and cull offscreen effects.

## Round awards

The user clarified that MVP is the highest-scoring eligible viewer on the winning team, while FMVP is the highest-scoring eligible viewer on the losing team. Within either team, break equal individual scores by kills, then smaller stable player ID. Host and gray participants are ineligible; an empty eligible team has no recipient. Keep the existing team-victory tie-break: equal total scores compare team kills, then alternate red/blue by round number, so the normal round flow still chooses a winner. If winnerTeam is neither red nor blue, the defensive award branch grants neither award. Do not introduce a new draw rule or use a global highest score/previous contribution formula to choose these awards.

## Social events and weapons

The plugin exposes normalized Like and Share events, event-ID deduplication, a positive delta like count and local simulation controls. One like restores 5% of current maximum HP for an existing living fighter, capped at maximum; no resurrection or implicit admission. Sharing grants a shotgun for the current round, retained across death and reset to a pistol next round. Repeated shares do not refill ammunition or bypass cooldown. A host always retains its rifle. The official SDK remains unavailable; do not claim production event delivery.

Shotgun: 3–8 random pellets per shot, 8 base damage per pellet, 2-shell magazine, 5-second base reload, 2-second interval. Rifle: 5 damage, 30 rounds, 0.2-second interval, 3-second reload. Existing spectator shape bonuses apply to spectator weapons. The host has no spectator shape passives.

## Host and defense

User confirmed automatic movement and battle; host chooses a team. One host is an additional slot outside the 5000 viewers, rendered as a five-point star with 2.5 times normal size and 50% movement speed. Circular collision envelope represents the star. HP 1000, defense 500. It cannot collect points, enter rankings or receive MVP/FMVP; host kills drop stolen points for viewers. It obeys its team's base/respawn rules. Host identity comes from the local host control, never an untrusted viewer comment.

Apply shape defense before armor. With defense available, split incoming damage 50/50: half to life, defense cost is floor(other half × 0.35). Thus unmodified damage 100 reduces life by 50 and defense by 17. Clamp defense at zero; if defense is insufficient, the unabsorbed fraction returns to life. Likes heal life only. Defense is restored to the host's starting value upon revival.

## Evolution

At elapsed 60, 120, 180 and 240 seconds, spawn five packs at different random map locations. Packs are square with a lightning symbol. Only living viewer geometries may consume them; gray is eligible, the host is not. Evolution doubles current/max life, grants 100 defense, lasts 40 seconds and ends immediately on death. Repeated pickup refreshes duration, not the HP multiplier. Expiry restores ordinary max life while preserving HP fraction and removes evolution defense. Every ten seconds while evolved, emit six evenly spaced traveling sword waves. Each wave pierces hostile units once and travels five ordinary diameters (220 world units), base damage 60. Expiry wins a tie with the fourth pulse. Use fixed limits and reset all transient state between rounds.

## Central trapezoid BOSS

Spawn once per round at 30 seconds: center, HP 3000, five times ordinary size, prominent health bar under the round title. The user clarified that the BOSS never moves horizontally: its world position stays at the spawn center in every attack phase, and jumping changes visual height only before landing at that same center. Fire ground radius 300 world units slows players inside by 20% and applies one damage per second. BOSS targets any living geometry, never bases.

- Cannon: one projectile after a five-second interval, diameter 0.8 ordinary body, range 4000, damage 100.
- Laser: five seconds of tracking warning, then direction locks; a single forward beam starts at the BOSS center and ends at the first map boundary in that direction. Warning and active beam never extend behind the BOSS, and units whose centers are behind its firing direction take no laser damage. Width 1.2 ordinary diameter, three seconds, 50 damage per second.
- Slam: jump vertically in place and land at the spawn center, expanding shockwave to fire-ground radius × 1.5, damage 45 and knockback once per target, then rest two seconds.

After each attack completes, randomly choose one of the three. At HP <=600, rage changes body and attack effects to red and animates the health bar: double attack damage, cannon interval 2.5 seconds, laser charge 3 seconds, no slam rest. Fire-ground environmental damage remains the specified one per second.

The red/blue side delivering the killing blow receives 60 seconds of +20% earned score and +20% outgoing damage. User confirmed: immediately double the surviving base's current life permanently, expanding its max HP if necessary; never revive a destroyed base. Gray last hits grant no red/blue buff. Host last hits reward its team but never the host personally. Score transfers remain conserved rather than multiplying stolen player points. Fractional score bonuses accumulate so repeated 2-point pickups actually benefit from 20%.

## Verification

Native tests cover exact timers, damage/armor/overkill, weapon timing and random bounds, evolution duration/death/refresh, host score exclusion and capacity, every BOSS attack and rage, reward ownership, no base damage and round reset. UE tests cover normalized events reaching the actual GameMode and changed HUD gestures. Build Editor and Game, inspect real rendering at full/compact sizes and all important combat states, and repeat 5000-participant sampling with new systems active. Report OS-input and real-SDK boundaries honestly.
