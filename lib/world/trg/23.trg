#2300
Boss Killed~
0 f 100
~
%echo% The air suddenly goes still and then shifts. 
%echo% You look around and suddenly you're somewhere else! 
give 1000 coins %actor.name%
%exitinstance% %actor% 3001
~
#2301
Sanc~
1 s 100
~
dg_cast 'sanctuary' %actor%
dg_cast 'bless' %actor%
dg_cast 'armor' %actor%
%echo% %actor.name% has been empowered!
~
#2399
Newbie Setup~
2 gs 100
~
* New Player Starter Equipment.
wait 1
if %actor.level% <= 1
  %echo% Welcome to the world %actor.name%, you will find you have been given some basic tools
  %echo% Use them well, seek out and remove the creatures in this dungeon and find the exit at the bottom.
  %echo% I suggest you destroy everything so you can upgrade your stats and learn skills.
  %echo% At any time you are not in combat you can use the following commands to upgrade yourself:
  %echo% buystat <stat> This will allow you to spend exp to upgrade your stats, but the more you put
  %echo% into a stat the more it will cost you.
  %echo% practice <skill name> This will allow you spend exp to learn or upgrade your skills, but the
  %echo% more you train a skill the more exp it will cost you. But with higher ranks of skills the more they
  %echo% can do for you.
  %echo% As a gift, I will unlock the Dual wield skill for you at rank 1.
  %actor.skillset("Dual Wield" 10)%
  if !%actor.eq(*)%
    %load% obj 2300 %actor% light
    %load% obj 2301 %actor% rfinger
    %load% obj 2301 %actor% lfinger
    %load% obj 2302 %actor% neck1
    %load% obj 2302 %actor% neck2
    %load% obj 2303 %actor% body
    %load% obj 2304 %actor% head
    %load% obj 2305 %actor% legs
    %load% obj 2306 %actor% feet
    %load% obj 2307 %actor% hands
    %load% obj 2308 %actor% arms
    %load% obj 2310 %actor% about
    %load% obj 2309 %actor% waist
    %load% obj 2311 %actor% rwrist
    %load% obj 2311 %actor% lwrist
    %load% obj 2312 %actor% wield
    %load% obj 2312 %actor% hold
  end
  if %actor.hitp% < %actor.maxhitp%
    eval missing %actor.maxhitp% - %actor.hitp%
    %damage% %actor% -%missing%
    %send% %actor% You feel much better!
  end
end
~
$~
