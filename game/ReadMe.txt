DUNGEON KEEPER (TM) 2
README v1.7
Bullfrog Productions Ltd.

COPYRIGHT 1999, ELECTRONIC ARTS LTD.
Unauthorised copying, reproduction, rental, public performance or broadcast of
this game is a violation of applicable laws.

Windows is a trademark or registered trademark of Microsoft Corporation in the
U.S. and/or other countries.  All rights reserved.


========================================

TABLE OF CONTENTS

========================================

1. INTRODUCTION
2. VERSION 1.7
3. VIDEO PROBLEMS
4. SOUND PROBLEMS
5. ADVANCED FEATURES
6. GAME AMENDMENTS
7. DUNGEON KEEPER 2 ON-LINE
8. FULL KEY LIST
9. Dungeon Keeper 2 Update 1.7

=======================================

1. INTRODUCTION
This file contains minor amendments and updates intended to
supplement existing reference material. For more information on playing DK2,
refer to the DK2 game manual provided. For help on installation and
troubleshooting, consult the DK2 Reference Card.

=======================================

2. VERSION 1.7
This is the most recent version of Dungeon Keeper and has many changes, fixes
and tweaks over the original version. For a full list of updates in this version,
please consult the Update.txt file on "C:\Program Files\Bullfrog\Dungeon Keeper 2\".

=======================================

3. VIDEO PROBLEMS
i. Monitor Flickering or Switching Off / Power-Saving
You may experience a blank or flickering display with some monitors when a game
begins. If this occurs, try changing the Resolution to 640 x 480 in the Graphics
Options.

ii. Brightness
If the screen is too dark on your monitor, try adjusting the Gamma bar in the
Graphics Options.

If you are still experiencing a dark picture, it may be that your video card has
been configured with low gamma correction. Gamma Correction can be adjusted in
Display Properties, which you can access via the Windows Control Panel (normally
found by opening the Windows Start Menu and looking on the Settings sub-menu).

iii. Graphics Corruption
If you experience unusual or severe colouring of the display, or possibly a
black screen, you might try disabling gamma correction altogether. To do this,
use the -disablegamma command-line option as described in 5.ii below.

NB Some cards will not support DK2's gamma correction. If you do not have a
Gamma Correction bar in your Graphics Options, the Increase and Decrease Gamma
keys will serve no function in the game.

=======================================

4. SOUND PROBLEMS
i. No Speech Messages
If you fail to hear dungeon message speech while playing the game, it may be a
problem with your Speaker Configuration. Try setting this to Stereo in the Sound
Options.

ii. Choosing The Language For DK2 (not available on English version)
If you wish to install Dungeon Keeper 2 in a language other than English, place
Disc 2 in the CD drive first to be given a choice of languages.

In addition, you will be automatically prompted for Disc 2 at installation if
the version of Windows detected on your machine is not running in English.

iii. Specific Card Problems
You may experience difficulties running Dungeon Keeper 2 in conjunction with a 
Creative Labs Sound Blaster Live! sound card. This is a potential driver 
problem, so consult your dealer or the Creative Labs web site for the latest 
driver updates.

=======================================

5. ADVANCED FEATURES
If your 3D card can support them, DK2 features some advanced 32-bit options to
improve the visual quality of the game. On most current 3D hardware there is a
performance loss in using these options, though this is card-dependent.

WARNING 1. The following command-line options are unsupported features and not
covered by any technical support agreement or warranty. The user applies these
options at their own risk, and with no guarantee of effectiveness. Electronic
Arts is under no obligation to provide any technical or customer support in the
event of problems arising from the use of these options.

WARNING 2 (for citizens of Planet Earth). In plainer language, we thought you
might like to see some of the neat extras we've been working on, but these are
NOT official features of DK2 -- so PLEASE don't phone up our tech support if you
can't get them to work. They won't be able to help you.

i. Adding Command-line options
To make use of the array of advanced hardware and software options in Dungeon
Keeper 2, you will need to apply command-line options. To do this, simply:

(a) Open the Windows Start Menu and select RUN to open a dialogue box.

(b) Type in the name and location of the DKII.exe program file, or use the
browser to locate it, or use right-click on the program file to discover this
information in its Properties.

Simpler still, if you followed the installer's default options regarding where
to install the game, then you can either copy the following exactly, or use cut
and paste to place it in the RUN windows dialogue box:

"C:\Program Files\Bullfrog\Dungeon Keeper 2\DKII.exe"

(c) Add the option to the end of the command-line, after the quotes, always
leaving a space before each command-line option. For example:

"C:\Program Files\Bullfrog\Dungeon Keeper 2\DKII.exe" -softwarefilter -32bitdisplay

Alternatively, you can create a shortcut to DKII.exe, open the shortcut's
Properties and add the options to the command-line in the Shortcut dialogue box.

ii. Command-line Option List

-disablegamma
If the screen is tinted and the gamma slider causes colour corruption, try
adding this command-line option. The problem is mostly restricted to a small
number of laptop video cards.

-nointmouse
This option disables the frame-rate independent mouse pointer.

-softwarefilter
This switches on bilinear filtering in the software version. A 300 MHz or higher
processor is recommended.

-32bitdisplay
This forces the game to run in 32-bit colour, rather than 16-bit colour. Using
this option will increase the game's performance running in software mode on
most systems by approximately 5%.

-32bitzbuffer
This tells the game engine to use a 32-bit Z-buffer instead of the default
16-bit Z-buffer. If this feature is not available then the game will find the
highest depth Z-buffer available and use that (usually 24- or 16-bit if 32-bit
is not available).

-32bittextures
This command-line option instructs the game engine to use 32-bit textures, if
your card is able to support this feature.

-32biteverything
This shortcut enables all three 32-bit options simultaneously.

NB the -softwarefilter and -32bitdisplay options can be used in conjunction to
give the highest possible image quality when running in software mode.

=======================================

6. GAME AMENDMENTS
i. Query Tool
The Query Tool provides detailed information on creatures only, not on rooms or
items. Its only other use is to reveal a Trap's effective range: just hold the
'?' cursor over the trap to highlight it, rather than clicking on it, to see a
burst of particles radiating outwards. The distance covered by the particles
visibly demonstrates both the Trap's range and the obstacles obscuring its line
of sight.

ii. Attractants -- The Rogue
Early versions of the DK2 "How To Attract Your Creatures" Card incorrectly state
that the Rogue is attracted by a Lair and a Treasury. You will actually need to
build a Lair and a Casino to entice the services of the Rogue.

Similarly, section 9.1.10 of the DK2 game manual also claims that a Treasury is
an attractant for the Rogue. The Treasury does no act as an attractant for any
creature.

iii. Attractants -- The Dark Angel
To attract a Dark Angel through the Portal, you will need to build a Temple that
boasts an idol of the Hand at the centre of its Sacrificial Pool. This requires
a Temple of at least a 5x5 square in size. Each Hand idol can support up to two
Dark Angels. Since each Temple may only have one idol to worship, the secret to
recruiting more Dark Angels is thus to build additional, separate, 5x5 Temples.

iv. Mana Limits
The maximum Mana any player can have is 200,000 points. No matter how much
territory the player owns.  

v. End of Level Sequence
A short cut-scene appears on the completion of each level of the Campaign Mode
to announce your glorious success. To end the scene and proceed with the game,
hit the SPACE BAR.

vi. Ally Window
You can make alliances during the course of a multi-player game by calling up
the Ally Window: just press A or use the Allies icon on the Control Panel.

To propose an alliance, click on the triangle next to your intended ally's name
to make the offer. The recipient will get a message to inform them of your
diplomacy. They can choose to ignore you, or accept by clicking on the other
triangle to form a solid square. This seals the deal.

Once the alliance has been forged, you may freely transfer your own resources to
them. Since you may be embroiled in more than one allegiance, you will need to
click on the ally's name to highlight it in order to confirm that they are the
Keeper with whom you are about to share your goods.

To break the alliance, simply click on the triangle again to withdraw from the
deal. Your erstwhile ally will be alerted to the change by a message. No more
resources may be shared once one side has withdrawn.

vii. Creature Efficiency
Left to their own devices, creatures naturally perform their chores at
approximately 80% efficiency. There are two ways for the impatient Keeper to
make them work even harder:

(a) Player Assignment
Deliberately assigning creatures to specific tasks raises their efficiency by
10%. They know you are watching their efforts, and try harder to please you. To
assign a creature, simply pick it up and drop it into the workplace. For
example, drop a Warlock in a Library, or place an Imp next to a wall that needs
reinforcing, and their efforts will immediately intensify.

(b) Slapping
A snappy right-clicking with the Hand of Evil will remind your minions of their
lowly status in the Dungeon hierarchy. Slapping a creature once adds 10% to its
efficiency. The effect is not cumulative, so further slaps will only serve to
injure and annoy.

Both of these 10% increases can be applied simultaneously, boosting the
beleaguered creature to 100% efficiency. You can also check the message bar to
discover a creature's precise efficiency, although the change in the text colour
provides speedier identification of micro-managed minions.

Note, also, that some experienced creatures have the ability to cast the 'Haste'
spell independent of the player's actions. The effect of this spell is equivalent
to a 10% boost, so it's possible to have assigned, slapped and magically enhanced
creatures working at 110% efficiency.

viii. Playing on in My Pet Dungeon
This mode gives you the chance to play on indefinitely, should you wish, so that
you may plan, build and tyrannise a truly megalithic dungeon at your own pace.
Upon completion of the level objectives, the game will ask if you wish to
continue. Declining the offer takes you to the Debriefing Screen, where you may
review your achievements and total score. If you agree to play on, be aware that
you will only be able to quit via the Options Screen.

In either eventuality, your progress through the levels of My Pet Dungeon will
be saved. Victory on one level makes the next level available to play.

ix. Hero Level Selector
This device is available in My Pet Dungeon's Hero Toolbox and can be used to increase or
reduce the experience level of heroic invaders. Left-click on the dial to increase the
level, or right-click to decrease it.

=======================================

7. DUNGEON KEEPER 2 ON-LINE
A link to the DK2 web site may be opened very quickly through the Windows Start
Menu. Go to the Program Files sub-directory, then Bullfrog, then Dungeon Keeper
this will connect you to WWW.Dungeonkeper.com. (This function is no longer available.)

2. Check this site regularly for news, assistance, downloads, updates, gaming
advice and links to other EA services. 

=======================================

8. FULL KEY LIST

Possessed Creature Mode (1st Person Mode)

Function                                   Key or Combination
Toggle Sniper Mode                         INSERT
Pick Lock/Disarm/Jailbreak                 NUM 0
Fire                                       SPACE
Creep                                      RCTRL
Run                                        RSHIFT
Melee Attack Select                        1
Creature Spell Select 1                    2
Creature Spell Select 2                    3
Creature Spell Select 3                    4
Creature Primary Ability                   5
Creature Secondary Ability                 6
Select Ally For Group                      7
Remove From Group                          CTRL + G

Keeper Mode (overview)
Scroll Up                                  UP Arrow
Scroll Down                                DOWN Arrow
Scroll Left                                LEFT Arrow
Scroll Right                               RIGHT Arrow
Zoom In                                    HOME
Zoom Out                                   END
Rotate View Left                           DELETE
Rotate View Right                          PG DN
Zoom Key                                   CTRL + UP/DOWN Arrow
Speed Scroll Key                           SHIFT + UP/DOWN/LEFT/RIGHT Arrow
Rotate Key                                 CTRL + LEFT/RIGHT Arrow
Pause And Options                          ESC
Take Screen Shot                           PRT SCRN
Broadcast Message To All                   TAB
Send Message To All Allies                 ALT + A
Send Message To Ally 1                     ALT + 1
Send Message To Ally 2                     ALT + 2
Send Message To Ally 3                     ALT + 3
Send Message To Ally 4                     ALT + 4
Ally Window                                A
Toggle Control Panel                       G
Toggle Player Information                  I
Toggle Full Screen Map                     M
Zoom To Dungeon Heart                      H
Zoom To Next Fight                         F
Zoom To Portal                             P
Camera Mouse Rotate                        X
Camera Mouse Zoom                          Z
Increase Gamma                             SHIFT + .
Decrease Gamma                             SHIFT + ,
Increase Ambient Light                     +
Decrease Ambient Light                     -

Preset Camera Views:
Camera Isometric                           F1
Camera Top Down                            F2
Camera Oblique                             F3
User Camera 1                              F4
User Camera 2                              F5
User Camera 3                              F6

Free Camera / Set User Camera Controls:
Roll Right                                 CTRL + DELETE
Pitch Down                                 CTRL + END
Pitch Up                                   CTRL + HOME
Roll Left                                  CTRL + INSERT
Yaw Right                                  CTRL + PG DN
Yaw Left                                   CTRL + PG UP

Hand Of Evil Modifiers
When picking creatures from the Creature Panel:
Pick Highest Level Creature                Hold CTRL + .
Pick Lowest Level Creature                 Hold CTRL + ,

Quick Load                                 CTRL + L
Quick Save                                 CTRL + S
Restart Level                              CTRL + R

=======================================

9. Dungeon Keeper 2 Update 1.7
===========================

WE CANNOT GUARANTEE THAT SAVEGAMES FROM EARLIER VERSIONS OF DK2 WILL WORK WITH THIS VERSION
OF DUNGEON KEEPER 2. 

This version adds the Maiden and the Jack-In-The-Box Trap to Dungeon Keeper 2. The Maiden is
an awesome creature that spits webs and slays enemies. The Maiden can be used in Skirmish
and Multiplayer levels. The Jack-In-The-Box Trap literally scares the life out of Imps and
also damages enemy Dungeon Hearts.

This version has a fix for users of Environment Mapped Bump Mapping 'EMBM' - please do not
confuse this with other types of bump mapping that are not supported. Currently, the Matrox
G400 is the only card we are aware of that fully supports 'EMBM'. Please ensure you are
using the latest Matrox drivers (Point your browser to 'www.matrox.com') if you wish to try
this feature.  Use the following commandlines '-enablebumpmapping' and '-enablebumpluminance'
to activate bump mapping on your G400. Please note 'EMBM' Bump Mapping is an unsupported
feature and, as such, cannot be discussed by our technical support staff. 

USERS OF OTHER CARDS PLEASE NOTE YOU MAY EXPERIENCE CRASHES IF YOU TRY TO RUN 'EMBM'
BUMPMAPPING ON YOUR MACHINE. (For further information on commandlines please consult the
ReadMe.txt)


Installing this update on top of an earlier version of Dungeon Keeper 2 will mean that the
Uninstall function will not clear out all DK2 files. After using the Uninstall option, you
can safely delete the remaining DK2 files manually. If you have installed to the default
path these files will be located at "C:\Program Files\Bullfrog\Dungeon Keeper 2\"

Gameplay tweaks and additions in this Update 
============================================
* Added a new creature - 'The Maiden'.
* Added the 'Jack-In-The-Box' Trap.
* Added 'Frosty's Realm' a new 2 player map. Multiplayer and Skirmish 4 player map.
* Added 'Frosty's Lair' a new 3 player map. Multiplayer and Skirmish.
* Added 'Frosty's Castle' a new four player map. Multiplayer and Skirmish.
* Added 'Mercenary Portals' these Portals allow you to get hero creatures in the levels that
  support them (Frosty's Castle, Frosty's Lair and Frosty's Castle).
* Added 'EMBM' Bump Mapping for G400's. This effect has been added onto Lava and Water in
  the game.
* Added 'My Pet Dungeon 7' to the update.
* Added Windows 2000 support to DK2.
* Altered Balance of Elite Creatures so that the Elites are now much tougher.
* Altered AI on some skirmish maps to improve Keepers abilities.
* Fixed - Elite creatures are now not turned into their normal counterparts after
  saving/loading.
* Fixed - You now do not become stuck if you press ESCAPE in first person.
* Fixed - Wizards do not fall into the Temple.
* Imps now disappear when their Mana reaches zero.
* Imps now perform better on Skirmish maps.
* Shrinking Princes problem on Level 19 fixed.
* Creatures in the combat pit now get hungry.
* Sniveller is not the only rating in multiplayer.
* It is now possible to have spaces in a savename when saving at the end of a level.
* You can now slap Horny again to cancel him once cast.

These are just some of the changes we have made in our efforts to support the people who
play Dungeon Keeper 2.

Below are listed the changes in the earlier updates to Dungeon Keeper 2.



Dungeon Keeper 2 Update 1.51 to 1.61
====================================

THIS UPDATE WILL AFFECT YOUR SAVED GAMES. SAVED GAMES FROM PREVIOUS VERSIONS WILL START AT
THE BEGINNING OF THAT LEVEL.

Elite creatures in Dungeon Keeper 2!

This version adds Elite creatures to Dungeon Keeper 2.  Elite creatures are tougher, faster,
stronger versions of their ordinary creature counterparts, and add a deadly twist to any
Multiplayer, Skirmish or My Pet Dungeon game. However, attracting them in will not be easy,
and you will need to discover their particular preferences to draw them in.  
As a clue, to attract the Elite Mistress, build a 3x3 Prison surrounded by a Torture Chamber.
Good luck attracting in the others!

Other Changes in Version 1.61

In this version we have added the much requested functionality to replay completed levels,
as well as play levels that were not played when a choice was given in the Campaign game.
We have also tweaked a lot of features that were either in the release version (1.3) or in
the later update (1.51), like the ability to continue playing after completing a level.



Dungeon Keeper 2 Update 1.51
============================

THIS UPDATE WILL AFFECT YOUR SAVED GAMES.

This update does not fix all of the hardware configuration problems that we've seen as
addressing them is out of our control. We have found that memory resident programs,
overclocking and bad or outdated drivers account for 90% of the problems we've seen.
For more info, read the TroubleShooting guide in the Support section of
http://www.dungeonkeeper.com/

Gameplay tweaks and additions in Update 1.51
============================================

* 1024x768 resolution supported.
* Added 'No Mans Land' map. 2 player. Multiplayer and Skirmish.
* Added 'Circlet' map. 3 Player. Multiplayer and Skirmish.
* Added 'Patrol' map. 4 player. Multiplayer and Skirmish.
* Added 'Pressure' map. 3 player. Skirmish only.
* Added 'Torment' My Pet Dungeon level.
* Added 'Olympia' map. 4 player. Multiplayer and Skirmish. Also in Bonus Pack 1.
* "The battle in the Combat Pit is over" is played when only one creature remains.
* Selecting "Quit" from the main menu now requests confirmation before quitting.
* "Your Library does not have enough storage" now played when there are more spells to
  research but no more space.
* Added "Press Space to go to Debriefing Screen or Escape to continue" at end of levels.
* Added a bunch more Temple Recipes.
* Added a check for 150MB disk space (necessary for swap files).
* Added a flower icon to the Hero Alarm Trap.
* Added a multi-line chat message window to MP.
* Added a multiplayer option that makes reinforced walls impenetrable to enemy Imps.
* Added a new cheat for gaining mana.
* Added a warning box if player is timing out in multiplayer lobby.
* Added a warning for when all players in MP don't have the map chosen.
* Added A3D audio support.
* Added better confirmation that a game has been saved.
* Added better lava balls.
* Added clearer alliance notification in the Allies window in MP.
* Added current values on slider bars.
* Added functionality in MP to let you chat once you have died.
* Added functionality that lets you play on in Single Player with the GUI available.
* Added 'Message To' functionality in the Allies window in MP.
* Added Mistress whip effect for Torture Chamber.
* Added notification of player's defeats in MP.
* Added the ability to have an allied end in multiplayer.
* Added the ability to wander the realm as a lost soul once after a multiplayer defeat.
* Added tooltip for MPD Level Selector.
* Added Torture flower icon.
* Added Total Evil Rating calculations and names.
* Adjusted Freeze Trap mana costs.
* All players can now see a thumbnail pic of the level in the MP lobby.
* Alliance information can now be heard by all players.
* Ally window in MP now shows who is allied to whom.
* Altered terrain slightly in Hopping.
* Altered the amount of damage Boulder now takes when hitting a creature.
* Altered the Knight's melee sounds.
* Attacking sleeping creatures in first person now wakes them up.
* Boulders now take damage when slapped.
* Campaign progress now not saved with MPD or Skirmish.
* Certain creatures now use the Temple when idle.
* Changed "New Campaign - Are You Sure?" to only display if a Campaign has already started.
* Changed Combat Pit tooltip.  
* Changed CPU player so that it uses the Combat Pit.
* Changed MPD Level Selector tooltip.
* Changed Library tooltip.
* Changed Monk's heal usage.
* Changed the eye-height of some creatures.
* Changed the highlight on the Hero Portcullis.
* Changed the highlight on the Fear Trap.
* Changed the Horny melee timing.
* Changed the 'Imps moving spellbook' tooltip.
* Coloured MP messages to differentiate between private, public and server messages.
* Computer player can now be set in multiplayer games. Will not work properly with all maps.
* Computer player now deals with neutral rooms better.
* Computer player stats revised to make some attack earlier.
* CRC checking of .exes to ensure that in multiplayer the .exes match for all players.
* Creatures show how much mana they are generating from prayer.
* Dark Elves now eat and sleep if they need to when patrolling.
* De-activated game functions when in chat e.g. Zoom/Rotate.
* Debriefing stats revised.
* Disallow Full-screen map while game is paused.
* Each Mana Vault now provides an increase to the maximum amount of Mana.
* Efficiency tooltips now rounded up.
* Ensured that creatures cannot get stuck in doors which are opening or closing.
* Ensured that creatures killed in the Torture Chamber contribute to debriefing stats.
* Ensured that Imps only drag back unconscious bodies when there is one spare floor tile.
* Environmental effects default to 'off' (for EAX environment compatible soundcards).
* Creatures will no longer be targetted in First Person mode.
* Game settings now saved between games.
* Given creatures the ability to cast Heal / Armour / Invisibility on themselves, if they
  have that spell.
* Hand now taken out of possession spell mode when coming out of possession.
* 'Heal' Creature Spells now slightly more powerful.
* Hero Lair texture is now easier on the eyes.
* Host quitting out of lobby will now automatically kick other players out of that lobby.
* Imp priorities adjusted slightly.
* Imps now have their correct death effect in the Temple pool.
* Imps stats altered so that it's easier for them to reach level 10.
* Increased the health of MPD Hero Invasion Parties for higher levels.
* Increased the update rate of the full-screen map.
* Some Keeper Spells can now be cast into the Temple pool to contribute to Temple Recipes.
* Lobby chat text now more legible against the background.
* Made Casino jackpot music only play for one player.
* Made chat textbox more responsive.
* Made Hi-Score Table easier to enter.
* Made Horny immune to freezing.
* Made it easier to distinguish between separate sub-objectives.
* Made rewarded creature from Temple Recipe appear at average level of creatures dropped in
  pool.
* Magic Doors now only damaged by magic attacks.
* Mana from Prayer values tweaked for all creatures.
* More feedback when door is locked/unlocked.
* More silly speech implemented.
* MPD quicksave no longer writes over Campaign quicksave.
* Multiplayer lobby now shows IP Address of all machines.
* Now easier to get around traps in 1st person.
* Pay-day timer now starts when first creature joins your side.
* Player names now shown instead of Player 1, Player 2 etc.
* Players are now informed of players that have dropped out of a MP game.
* Reduced frequency of "There is nothing left to research" message.
* Reduced frequency of "Your Portals have attracted as many creatures as they can". 
* Removed cheats in MP.
* Rooms, doors and traps cannot be set to researchable in MP game options.
* Some creatures now use the Casino when idle.
* Subtitles on Level 5 appear in a better place.
* The F3 camera option is not as zoomed in.
* The Mistress now trains more and is less obsessed with the Torture Chamber.
* Tidied up 2D Front End map sections.
* Tidied up Control Options in Front End Menu.
* Tidied up text in the chat window.
* Tweaked music scripting.
* When trying to join a game in Internet Dungeon Watch you now also get a message.
* Winner's name now at the top of a MP debriefing.
* You can now assign ESC key in game and Front End.


Fixes in Update 1.51
====================

* After joining someone else's MP lobby, you no longer inherit their game settings.
* After loading a saved game, correct debriefing speech now plays.
* Fixed a Multiplayer problem related to items dangling in the hand.
* Fixed a problem in MPD Level 3 involving Continual Invasion.
* Fixed a problem related to decomposing in the Graveyard.
* Fixed a problem where "Locate Hidden Land" special was appearing at the end of a Skirmish
  game.
* Fixed a problem where creatures sometimes cannot get picked up in MPD mode.
* Fixed a problem where creatures sometimes could not be dropped in MPD mode.
* Fixed a problem where flowers of creatures in Toolbox flash for no apparent reason.
* Fixed a problem where movies don't show video on some CD drives.
* Fixed a problem where players were forced to replay completed missions.
* Fixed a problem where the first chicken to be picked up would be black.
* Fixed a problem where you could lose the spells you started with when taking over an enemy
  Library.
* Fixed a problem where you couldn't view the "Cut 1" movie.
* Fixed a problem with MPD Levels 2-6 so that creatures can be dropped back into the Toolbox.
* Fixed a problem with news update speech on map screen.
* Fixed a stalemate bug on Level 10.
* Fixed an Alt+Tab crash
* Fixed an out of sync problem between German and non-German versions involving Elven Archer torture anim.
* Fixed an out of sync problem related to jailbreaks.
* Fixed an "invisible Imp" problem.
* Fixed Level 20 so that Hero Gates can be collapsed.
* Fixed problem in multiplayer where loading screen was not shown at some stages.
* Fixed problem related to experience gained through destroying traps and Dungeon Heart.
* Fixed problem that caused framerate drop when exposing the map in MP.
* Fixed problem where "Player Not Responding - Kick Player" message wasn't clearing the
  thumbnail from the screen.
* Fixed problem where "Teach your prisoners a lesson - build them a Torture Chamber" got
  played incorrectly.
* Fixed problem where "There is nothing more to research" got played incorrectly.
* Fixed problem where "You have my allegiance, Keeper" got played incorrectly.
* Fixed problem where being kicked from a lobby was taking a long time before timing out.
* Fixed problem where creature spell 'Knives' were coming out in the wrong direction.
* Fixed problem where creatures were shown in the full-screen map at the point they were
  picked up from.
* Fixed problem where debriefing wouldn't show after continuing a MPD game.
* Fixed problem where fire sound would play indefinitely.
* Fixed problem where frozen creatures in possession won't animate when coming out of
  possession mode.
* Fixed problem where Horny would sometimes get stuck on the way to the Portal Gem.
* Fixed problem where Imps weren't taking crates to blueprints.
* Fixed problem where it was possible to get idle CPU Keepers in Skirmish.
* Fixed problem where it was possible to get overlapping GUI windows.  	
* Fixed problem where Portal Gem was created in an incorrect place after a game had been
  loaded.
* Fixed problem where rebelling creatures leaving the dungeon were getting stuck.
* Fixed problem where some walls weren't getting rendered when exiting possession mode.
* Fixed problem with duplicated level objectives screenshots in mission briefings.
* Fixed problem with sound in movie player on Aureal cards.
* Fixed Voodoo and non-MMX machine config problem.
* Game installed to a path without spaces in now runs from the Autorun menu.
* High Score Table no longer displays "Level 20" after completing a Secret Level.
* Temple created creatures now never get stuck in the pool.
* Valid video mode now set up for Matrox Millenium video card.

