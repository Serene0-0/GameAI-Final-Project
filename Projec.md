Project Proposal
Project Name: The Killing Hour
Team Members: Shiyu Chen, Shanshan Ye
 
Overview:
The Killing Hour is a shooter survival game where a single human player must survive against a squad of AI hunters in a restricted environment. The AI hunters communicate, divide search responsibilities, coordinate and shoot to eliminate the player. The player spawns in a map with multiple obstacles and line-of-sight blockers and must survive for a set amount of time. 
The player’s primary goal is survival by hiding and shooting down the AI hunters. Shooting is an optional tool available to the player: opening fire can elimate a hunter, but it also produces sound that immediately alerts the entire squad.
 
Implementation steps:
Settings: 
Create a Map: A single-floor maps with multiple obstacles and line-of-sight blockers designed to support the zone-based search system.

  Behavior Tree
Search Branch: The hunter moves through its assigned zone in search mode, covering the full map. If all zones are cleared with no contact, zones are reassigned to different member in AI squad and restart the search.

Investigate Branch: Triggered by a heard sound or a teammate's broadcast. The hunter moves toward the reported position cautiously, prepared to shoot.
Hunt mode: if find the player.
AI Hunter squad: 
1.    Perception & Information Sharing:
Each hunter is equipped with a vision cone and a hearing radius. When a hunter spots or hears the player, it immediately broadcasts the player's last known position to all teammates via a shared Blackboard.

-> change: Each hunter is equipped with a vision cone and a hearing radius. These perception channels allow the squad to detect the player directly through sight or indirectly through gunshot sounds.

2.    Shared Blackboard
All AI hunters read from and write to a single shared Blackboard that tracks:
 The player's last known position
Which map zones have already been searched (i.e., color highlighted)
 Whether the squad is in Search mode or Investigate mode. In Search mode, AI does not know the player’s position at all. In Investigate mode, AI chase based on the last heared shoot sound

-> focused on: squad coordination: how different hunter types react to the gunshot sound.

Hunter A(Chaser): see the player
Hunter B(Interceptor): read the position from blackboard, and intercept the player on the anticipated escape route
Hunter C(Ranged Shooter): find a long-range firing place to shoot player with clear LOS, and also applies pressure to limit the player’s movement.

   Shooting System (AI & Player)

For the AI:
Hunters only fire when they have a clear line of sight to the player and are within effective range.

Before firing, a short aim time (e.g., 0.4 seconds) is applied to prevent instant-kill behavior and give the player a reaction time.

For the Player:
The player can shoot at any time
Firing a shot produces sound that the AI Perception system detects, immediately triggering an Investigate or Hunt response to all nearby hunters.


AI Hunter Role Distribution: 

In Investigate Mode, when Hunt mode was triggered, AI hunters will be assigned two roles: 

Chaser: The hunter with the shortest path to the player pursues directly. (shooter?
(Ambition) Interceptors: They predict the player's likely escape direction based on current velocity and nearby exit paths, then navigate to intercept positions rather than chasing from behind.
Algorithm choose: 
Path Distance: A* or UE’s pathFinding
Predict potential intercept point: use velocity

Responsibility:

Shanshan Ye: Map Settings, Behavior Tree
Shiyu Chen: AI Hunter Role Distribution

Together: Test sharedboard info and overall testing

