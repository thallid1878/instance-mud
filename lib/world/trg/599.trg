#59900
Respawn~
2 gs 100
~
if %actor%
  * Randomly wait between 30 and 90 seconds (30s + 0-60s)
  eval rand_wait (30 + %random.60%)
  wait %rand_wait%sec
  
  * Check if the player is still here after waiting
  if %actor.room.vnum% == 59900
    %echo% A mysterious vortex appears and sucks %actor.name% away!
    %teleport% %actor% 3001
    %force% %actor% look
  end
end
~
$~
