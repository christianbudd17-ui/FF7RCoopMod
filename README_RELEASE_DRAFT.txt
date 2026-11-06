FINAL FANTASY VII REMAKE INTERGRADE - LOCAL CO-OP MOD
=====================================================
Version: 1.0 beta                   Game: FINAL FANTASY VII REMAKE INTERGRADE, Steam (PC)
Created by Christian Budd


Welcome to the Final Fantasy VII Remake Co-op mod!  

***This version is a beta test version!  While testing has been done to eliminate obvious bugs, there is a very real possibility that you will run into some during your playthrough!  Please save often.  If you notice bugs or oddities, please send the log (see TROUBLESHOOTING) and try to reproduce it so it can be fixed!


Have fun!


WHAT YOU NEED
-------------
- FINAL FANTASY VII REMAKE INTERGRADE, Steam version. (The Epic version is not supported yet.)
- One controller per extra player. Almost any gamepad works: Xbox-type pads, PlayStation
  DualShock 4 / DualSense (USB or Bluetooth), and generic DirectInput pads. Steam Input is
  NOT needed - the mod reads every controller itself.
- Player 1 can play on keyboard and mouse - BUT a controller is recommended for the best experience.
- Linux / Steam Deck (Proton): NOT tested. It may work with the Steam launch option
  WINEDLLOVERRIDES="xinput1_3=n,b" %command% - controller detection is the most likely problem.
  If you try it, please send your log (see TROUBLESHOOTING).
- Nothing else: the settings app is a normal Windows program (FF7RCoopMod_Settings.exe), no Python needed.




INSTALL
-------
1. Open the game's folder:
     ...\steamapps\common\FINAL FANTASY VII REMAKE\End\Binaries\Win64
   (the folder with ff7remake_.exe in it).
2. Copy these files into it:
     xinput1_3.dll                  - the mod
     FF7RCoopMod_Settings.exe       - the settings app (optional)
     FF7RCoopMod_Profiles (folder)  - the ready-made difficulty profiles for the settings app (optional)
   Windows may warn about the settings app the first time ("Windows protected your PC" - it isn't signed): click
   "More info" > "Run anyway". (FF7RCoopMod_Settings.pyw is the same app as a Python script, for anyone who prefers it.)
3. Start the game. The mod writes its settings file (FF7RCoopMod.ini) and a log
   (FF7RCoopMod_Coop.log) into the same folder the first time.


The mod never changes the game's own files; it only runs while the game runs.
UNINSTALL: delete xinput1_3.dll (and FF7RCoopMod.ini / FF7RCoopMod_Coop.log if you like).




CONTROLLERS
---------------------------------
- When one controller is plugged in, that controller defaults to player 1's (the game plays exactly like normal).  If playing with one person on keyboard and mouse, and the other on controller, then keyboard must be selected in the settings app (or manually in the .ini file) for player 1, and the controller set for player 2.
- More controllers -> Additional eligible controllers are automatically set for player 2 and player 3 in order of detection.
- Devices that aren't gamepads are never picked for a player - for example racing wheels, pedals and flight sticks. They can
  stay plugged in.


To manually set which player uses which controller…
      -  use the settings app (Controller lists on the Player tabs),
OR 
* edit [Input2] in FF7RCoopMod.ini:
     Seat1=Auto        player 1 (Keyboard / Auto / a controller model, e.g. 046D:C21D)
     Seat2=Auto        player 2 (None = no player 2)
     Seat3=Auto        player 3 (None = no player 3)

NOTE: A controller is remembered by the exact device, so unplugging and re-plugging never moves a controller to another player. Controller changes take effect the next time the game starts.


Generic controllers with an unusual button layout: the settings app's Controller maps tab lets you
press each button once to map it (saved per controller model).


IMPORTANT: the mod keeps the game reading controllers the "XInput" way. The controller choice in the game's own Options menu therefore has no effect while the mod is installed - pick
controllers in the mod's settings instead.




JOINING AND LEAVING
-------------------
Keyboard Commands
- F10 = player 2 joins / leaves
- Ctrl+F10 = player 3 joins / leaves
Controller commands 
-  Hold Start on player 2’s or player 3’s controller for 1 second (this button can be changed in the settings app)


Other notes
- A player whose controller is switched off or unplugged hands their character to the AI after
  3 seconds and gets it back when the controller returns.
- If their controllers are connected, players 2 and 3 are already joined when the game starts, and a controller
  connected later joins automatically (settings app: "Join P2 / P3 when the game starts" - Auto / Off).
- Players 2 and 3 take party members 2 and 3 (in the party order). If a character leaves the
  party in the story, that player waits until someone is in that slot again.
- To switch party members, Player 2 (or 3) can press the up or down directional pad (this is how the standard game commands a character switch in battle, but in the mod this works in the field as well).  If Player 2 and Player 3 want to swap characters, then one player should first leave (ex: F10 for player 2) and the other person then swap characters. The player who left can then rejoin as the other character.




CO-OP MOD CONTROL KEYS (all can be changed in the settings)
Note: The mod's keys only work in normal play (walking around or fighting), never in menus.
---------------------------------------------------
  F5          show / hide the list of co-op keys and buttons in game
  F6          reload the settings file while playing
  F7          Toggle split screen: always on / battle only / off
  Ctrl+F7     Toggle between split screen layouts
  F8          Co-op camera on / off (one shared camera that keeps everyone in view - can be used in the field as well!)
  F9          player 1 takes the lead
  Ctrl+F9     player 2 takes the lead
  Shift+F9    player 3 takes the lead
  F10 / Ctrl+F10   player 2 / player 3 joins or leaves










PLAYERS 2 AND 3 (default buttons, Xbox names; change them in the settings)
--------------------------------------------------------------------------
Left stick        move                       
Right stick    camera (in their window)
  X                 attack                     
  Y              unique ability
  B                 dodge                      
  A                 open the command menu  
RB             guard
hold LB           shortcut palette: then X / Y / B / A use shortcuts 1-4     
R3             lock on (flick the right stick to switch target)
LT / RT           command an AI teammate (opens that teammate's page)
D-pad up / down   switch to another AI-controlled party member (cannot switch to a human controlled character)


In the command menu: 
D-pad to move
A to confirm
B to go back.




THE LEAD, HOT SWAP AND THE KEYBOARD
-----------------------------------
- The "leader" is the character the game follows: the one who opens chests, talks to people
  and walks the story forward. Player 1 starts as the leader.
- F9 / Ctrl+F9 / Shift+F9 hand the lead to player 1 / 2 / 3: that person takes the leader's
  character and the person who was leading takes theirs. Everyone keeps their own controller.
  Only the keyboard can do this (player 1's decision).
- THE KEYBOARD ALWAYS CONTROLS THE LEADER. If player 1 plays on the keyboard only and hands the lead to someone else, player 1's new character is played by the AI until player 1 takes the lead back. To avoid this, hot swap is blocked by default while player 1 uses the keyboard (settings app: "No hot swap when P1 uses the keyboard"); the game shows "Hot swap disallowed - Player 1's primary input is the keyboard".
- If player 1's character is knocked out:
    - with an AI-played teammate still standing, the game switches player 1 to that teammate
      (like the normal game);
    - with no AI teammate left, player 1's character stays the leader (camera will remain on their knocked out character) and players 2 and 3 keep playing their own characters. 




BATTLES
-------
- Split screen (Default configuration is “Battle Only” - can be changed by pressing F7): Players 2
  and 3 get their own window - the right half of the screen by default (Ctrl+F7 toggles different split screen configurations - right / left /  bottom / top).  Player 3 is always below player 2. If only one of them is playing, their window takes the whole half. Each window has its own camera (right stick), and players 2 and 3 see their own battle HUD in it.
- Own command menus: every player opens their own command menu in their own part of the screen, and all can navigate their menu at the same time as everyone else. Each menu stays locked to its player. Players 2 and 3's closed command bar (ability / Attack / Commands) shows their own character.
- By default, while any command menu is open, time slows down like in the normal game; Menu slowdown amounts can be customized per player.  If multiple player menus are open at the same time, the game will slow down to the slowest open menu speed (ex:  P1 slowdown is 25%, P2 is 50%, P3 is 75% - if P1 and P3 are open at the same time, slowdown speed will be 75%)
- Screen darkening in command menus: with the split screen on, only the view of the player whose menu is open goes darker - each player can turn this off for their own view (settings app, Balance tab, next to their menu slowdown). With one shared screen (split screen off, co-op camera) the whole screen darkens while anyone's menu is open, like the normal game - one switch for everyone.
- Items are shared, like in the normal game. Once someone picks an item it counts as used for the others until their action happens, so two players can't both pick the last Potion - this is the default game behavior.
- Target markers: players 2 and 3 get their own reticle on their target, in their color
  (default: player 2 green, player 3 orange.  Colors can be changed between Green / Orange / Yellow / Purple / Pink / Red in the settings - blue is player 1's, and players 2 and 3 can't pick the same color). Arrows at the screen edge point to players who are off screen and correspond with their reticle color.
- Players 2 and 3 see the game's own target name, health bar, stagger bar and damage numbers in their window.
- Health / ATB panel: Players can choose to have one HUD element with all available characters in it (the way the default game HUD is set up)


OR


 one panel per player screen, with three different configurations…
* Only their own character
* Their own character + AI teammates
* Whole party (as the default game would portray, but one on each player screen)


The menus are scaled in size for each window (half / third / quarter of the screen).


- Near death: with the split screen on, each player's view gets its own red edge when THEIR
  character is low on health (instead of the game's red filter over the whole screen).
- Button prompts: each player sees their own controller's button icons (PlayStation or Xbox,
  picked automatically - ButtonIcons per player).
- Talking scenes and cinematic shots use the game's normal camera; the split screen steps aside and comes back afterwards.
  (Grabs and other special enemy attacks in a battle keep the split screen.)
- Battle messages (enemy defeated, Gil / EXP / items gained) sit at the right edge of the screen, in every layout
  (settings app: Split screen tab > Battle messages to move them).
- Players 2 and 3 can use limit breaks and summons from their own command menus.




DIFFICULTY
----------
Out of the box the mod uses Co-op Balanced, which depends on how many PEOPLE are playing:
- One person (alone, or with AI teammates): the game's normal difficulty - exactly like the vanilla game.
- Two or three people: a little tougher enemies (more health, slower to stagger, a bit more damage) and a slightly
  slower ATB (90%), to make up for more people attacking. It aims for about the same challenge as a single-player run.

The settings app's Balance tab has four presets:
Vanilla - The normal game for any number of players.
Co-op Balanced - The mod's default (see above).
Co-op Hard - Higher setting for those looking for a bit more challenge.
Co-op Extreme - Highest preset for veteran players.


- Difficulty drop-down (Balance tab):
    * "Dynamic difficulty based on human player count" (recommended, the default): separate settings for a one / two /
      three character party, picked by how many PEOPLE play (with AI teammates instead, the set for the number of people
      playing is used). Every ready-made preset uses this.
    * "Same difficulty no matter how many humans are playing": one set of values for any number of players.
- Each set has its own enemy values, ATB speed per player and command-menu slowdown / darkening per player.


Some balancing options are: enemy health, damage, how often enemies attack, how fast they get staggered, and who they go after (stay on a player they hit, turn on a player who hits them hard, go after a weakened player). 


- Enemy settings never go DOWN during a battle: a player leaving, a controller dropping out, or pressing F6 mid-fight -
  the new values apply when the battle is over. They do go UP mid-battle when more people's characters join the fight
  (a player joins, or the story adds a character someone plays).


- Enemies spread their attention over all players, not just the leader.


- Players 2 and 3 fill ATB and stagger enemies at the same rates as player 1.  ATB speed and command-menu slowdown are set per player and per party size (Co-op Balanced: 90% ATB with two people, 85% with three; player 1 alone has the normal speed unless you lower it).




CO-OP CAMERA
------------
F8 turns the co-op camera on / off (a short note on screen says which): This is one shared camera that keeps every player in view (it only stops at walls and scenery, not at characters or props). It steps aside while the split screen
is on, in cutscenes and in talking scenes. Its framing presets are on the settings app's Co-op
camera tab.




MINI-GAMES AND SPECIAL SECTIONS
-------------------------------
Mini-games, vehicle sections and scenes where the game takes control of a character are played by the leader (the game only listens to the leader's controller there). If players 2 / 3 get in the way in such a section, they can leave with F10 / Ctrl+F10 (or their join button) and rejoin afterwards. Players 2 and 3 should have no control during mini-games - if they do, that's a bug: please report it (see TROUBLESHOOTING).


SETTINGS
--------
Run FF7RCoopMod_Settings.exe (in the game folder, next to the mod). 
Changes marked * need a game restart to take effect; everything else applies after F6 in game (with the exception of enemy settings, which cannot take place during a battle and refresh upon completion of a battle in progress).


NOTE: If not using the settings app, then editing can be done in the FF7RCoopMod.ini with any text editor - each line has a short explanation above it.


- Profiles: save all your settings under a name and load them again (your controller choices
  are never part of a profile). Ready-made profiles: the four difficulty levels.
- Split screen tab: window cameras, picture quality (and "Dynamic quality": the windows get
  plainer by themselves when the frame rate drops below a target), panel / menu sizes.
- Troubleshooting tab: an on-screen clock and frame-rate counter, split-screen windows for AI-played
  characters (testing), and "Allow Thirds on a normal monitor" (three narrow columns on a 16:9 screen).
- Split screen tab, "Experimental" box: "Multiple monitors / ultrawide" (see MULTIPLE MONITORS AND ULTRAWIDE below).




MULTIPLE MONITORS AND ULTRAWIDE (EXPERIMENTAL)
----------------------------------------------
Settings app > Split screen > Experimental: tick "Multiple monitors / ultrawide". Set the game's own display mode to
Borderless window. Monitors must sit side by side and have the same height.
- Several monitors: the game window is stretched over all of them - one player per monitor. "Player 1's monitor" picks
  which one (Main = your Windows main display); players 2 and 3 get the others from left to right. With two monitors and
  three players, players 2 and 3 share the second monitor ("P2 / P3 on one monitor": Columns = side by side, Rows = one
  above the other). Each player's HUD (command menu, health panel, enemy bars, reticle) shows on their own monitor.
- Menus and the pause screen move to player 1's monitor (the others go black for a moment - a short flash when a menu
  opens or closes). Cutscenes, loading screens and walking around without the split screen show on player 1's monitor
  with the others black (no resolution change, no pause).
- One ultrawide monitor (wider than 2.2:1, e.g. 21:9 or 32:9): the game runs at the monitor's full resolution (the mod
  sets it - the game's own list has no ultrawide sizes) and the split screen is side by side: player 1 on the left,
  halves with two players, thirds with three. Menus show 16:9 in the middle of the screen.
- Tested on two 1080p monitors (also as one 3840 x 1080 "wide screen" - the "Test: monitors as one wide screen" box). NOT
  tested: three monitors, a real ultrawide monitor, monitors of different sizes / refresh rates (the whole picture runs
  at one frame rate).




KNOWN ISSUES
------------
- After the tutorial fight where Barret joins, the game may say "Player 2 isn't available in this
  section". Open and close the main menu once to fix it.
- A DualSense on Bluetooth with a low battery can lag (late or missing button presses). Charge
  it or use a USB cable.
- Thirds on a normal 16:9 monitor (Troubleshooting tab) makes very narrow columns - it's meant for wide screens.
- Multiple monitors / ultrawide (experimental): the map can reach into player 2's part in the wide-screen thirds layout;
  on a real ultrawide monitor the mouse may not line up with menu buttons (controller / keyboard are fine). The summon
  gauge shows with player 1's health panel only.
- Don't run UE4SS or other mods that also use xinput1_3.dll at the same time.
- Failed Experiment fight (chapter 17): the story needs Tifa as the leader for her jump over the gate. When player 2 / 3
  plays Tifa, the mod hands her the lead for that moment by itself - walk her down the stairs to the gate. Tifa's
  player needs a controller. If player 1 plays on the keyboard, player 1's character stands still for those few
  seconds - keep off the keyboard until the jump cutscene starts. If it gets stuck, have player 2 leave (F10) for
  the jump.
- The leader's rumble only works on an Xbox-type (XInput) controller.




TROUBLESHOOTING
---------------
- Nothing happens / no co-op: if the game's version is different from the one the mod was made for (usually after a game update), a small window says "Co-op is switched off for this session" and the game runs normally without co-op. If only some parts of the mod couldn't start, a message on screen says so a few seconds into the game. Either way, FF7RCoopMod_Coop.log in the game folder has the details (lines starting with "HOOK FAILED").
- A controller drives the wrong player: set Seat1-3 in the settings and restart the game.
- Reporting a problem: play until the problem happens (note the time if you can - the settings app's Troubleshooting tab can show a clock), then send FF7RCoopMod_Coop.log from the game folder. The beta always writes the full log. When it grows past 40 MB during a session (or is over 20 MB when the game starts) it is renamed to FF7RCoopMod_Coop_previous.log and a new one is started - if the problem happened a long time ago, send that file too.
- What the log contains: the mod's settings, the names / IDs of your controllers, which player plays which character, what the mod did in the game (with the PC clock time), and any errors. It never records which keys you type (only the co-op hotkeys F5 - F10 when used, and players 2 / 3's controller buttons in their menus), and the mod sends nothing over the internet - the log only leaves your PC if you send it. (It also lists Windows' device paths of your controllers.)




CREDITS
-------
Christian Budd - creator, design and testing


LICENSE AND SOURCE CODE
-----------------------
The mod is released under the MIT License (LICENSE.txt). The full source code is a separate download:
FF7RCoopMod_1.0_beta_source.zip (build instructions in its README_SOURCE.txt).
This mod is a fan project, not affiliated with or endorsed by Square Enix. No game files or assets are included.