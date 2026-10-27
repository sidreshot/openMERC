# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 (black-box test scenarios for OMsnd against Defsnd; no game code)
#
# Runs named scenarios with run_mp.ps1 (dedicated server + scripting relay + client) for each backend.
# Output: C:\openmerc-data\i5s2\runs\<scenario>_<backend>.{wav,log,proxy.jsonl,srv.log}
#   -Scenarios m1,m2 ...   -Backends defsnd,omsnd
# Times in the relay arguments are seconds of server frames after the client's first frame.
param(
    [string[]]$Scenarios = @('m1'),
    [string[]]$Backends = @('defsnd', 'omsnd'),
    [string]$ClientExtra = '',
    [string]$Suffix = ''
)
$ErrorActionPreference = 'Stop'
$cfg = 'C:\openmerc-data\i5s2\cfg\mp_conn.cfg'
$S = @{
    # music modes with the JPN3 set (Tension type 1 vol 180, Action type 3 vol 225); stopsound and s_musicvolume
    m1 = @{ Map = 'dm/jpndm2'; Secs = 104; Proxy = '--cs 13=JPN3 --music 0:2,20:3,40:2,60:1,66:2,76:0,84:3,90:2 ' +
            '--stuff 30:stopsound --stuff 50:"set s_musicvolume 0.35" --stuff 56:"set s_musicvolume 0.7" --stuff 102:quit' }
    # designer pieces with TSR1 (Intro trigger 1 flag 0 vol 220, Tension 180, Action 250): play-once, 0xFF return
    # to the last mode, intensity, value 0
    m2 = @{ Map = 'dm/jpndm2'; Secs = 104; Proxy = '--cs 13=TSR1 --music 0:2,85:3 --info 8:2:1 --info 45:2:255 ' +
            '--info 55:1:2 --info 57:2:1 --info 67:2:255 --info 77:2:0 --stuff 102:quit' }
    # trn1 (all type 0: 1 TrainIntro flag 0, 2 TrainAction flag 1, 3 TrainTension flag 1, 4 TrainExplosion flag 0):
    # loop flag, 0xFF with intensity 3 (silence), mode switch between designer pieces, forcesong
    m3 = @{ Map = 'dm/jpndm2'; Secs = 104; Proxy = '--cs 13=trn1 --music 0:5,50:4 --info 40:2:255 ' +
            '--stuff 80:"forcesong GER3" --stuff 102:quit' }
    # piece ends (SPEC-040 5.2): trn1 designer 4 TrainExplosion (flag 0, 13.3 s) plays once and the music stays
    # silent; forcesong GER4 in mode 7 (no trigger 4 there): silence; mode 4 starts GER4 Swarm (flag 1, 33.8 s), which
    # loops; mode 2 (GER4 has no calm piece): the designer fades out, silence
    m5 = @{ Map = 'dm/jpndm2'; Secs = 82; Proxy = '--cs 13=trn1 --music 0:7,20:4,72:2 --stuff 18:"forcesong GER4" ' +
            '--stuff 78:soundinfo --stuff 80:quit' }
    # mixer rate 11025 Hz (s_khz 11, SPEC-040 "Terms", 1.3). s_khz is read at start-up from user\config.cfg: the
    # suite sets `set s_khz "11"` there first (Khz; a snd_restart in the script would bring Defsnd up without the
    # dsfocus proxy, i.e. muted); the relay sets it back to 22 before quit (archived cvar). Effects without music
    # (16-bit 22050 Hz wav: resampled, 8-bit 11025 Hz wav, .adp: every second sample), then JPN3 calm and action
    # (fades twice as long)
    k1 = @{ Map = 'dm/jpndm2'; Secs = 48; Khz = '11';
            Proxy = '--cs 13=JPN3 --music 0:1,16:2,30:3 --stuff 4:"play player/Jump" ' +
            '--stuff 7:"play player/foot/gravel/Gravel1" --stuff 10:"play cin/tsr1/Laugh.adp" --stuff 40:soundinfo ' +
            '--stuff 42:"set s_khz 22" --stuff 44:quit' }
    # deathmatch map as it is (DM1, designer 1, flag 1) for the natural start-up
    m4 = @{ Map = 'dm/jpndm2'; Secs = 60; Proxy = '--stuff 58:quit' }
    # sound-processing gain: 16-bit, 8-bit and .adp local sounds with type 0, 1, 0; no music (mode 1)
    u1 = @{ Map = 'dm/jpndm2'; Secs = 64; Proxy = '--cs 13=JPN3 --music 0:1 ' +
            '--stuff 6:"play player/Jump" --stuff 9:"play player/foot/gravel/Gravel1" --stuff 12:"play cin/tsr1/Laugh.adp" ' +
            '--info 20:8:1 ' +
            '--stuff 24:"play player/Jump" --stuff 27:"play player/foot/gravel/Gravel1" --stuff 30:"play cin/tsr1/Laugh.adp" ' +
            '--info 38:8:0 ' +
            '--stuff 42:"play player/Jump" --stuff 45:"play player/foot/gravel/Gravel1" --stuff 48:"play cin/tsr1/Laugh.adp" ' +
            '--stuff 62:quit' }
    # music level under sound-processing type 1 (JPN3 calm): type 1 at 20 s, 0 at 40 s
    # output-path continuity in game: the mixer's test tone (s_testsound 1) for 60 s while connected
    t1 = @{ Map = 'dm/jpndm2'; Secs = 76; Proxy = '--music 0:1 --stuff 2:"set s_testsound 1" --stuff 64:"set s_testsound 0" ' +
            '--stuff 66:soundinfo --stuff 68:quit' }
    # general ambient sets (SPEC-040 3): the deathmatch map registers none of the ambient waves, so sound configstrings
    # 1..8 (CS_SOUNDS 291 + i) are renamed to the waves of ElevatorShaft (14), ParkingRamp (5) and CityRoughNeighborhood
    # (57); the relay sets the general set field: fade-in, one-shots every 7-10 s, crossfades, S_SetGeneralAmbientSet
    # undone by the next frame, interval 0, fade-out. No music (mode 1).
    a1 = @{ Map = 'dm/jpndm2'; Secs = 106; Proxy = '--music 0:1 --ambient 0:0,8:14,48:5,64:57,80:14,96:0 ' +
            '--info 34:4:5 ' +
            '--cs 292=ambient/locs/elevshaft/ShaftLP.wav --cs 293=ambient/locs/elevshaft/Metal1.wav ' +
            '--cs 294=ambient/locs/elevshaft/Metal2.wav --cs 295=ambient/locs/rmtone/ParkRamp.wav ' +
            '--cs 296=ambient/gen/traffic/Trafflp.wav --cs 297=ambient/gen/traffic/TraffClr.wav ' +
            '--cs 298=ambient/gen/traffic/Horn1.wav --cs 299=ambient/gen/traffic/CarBy1.wav ' +
            '--stuff 102:soundinfo --stuff 104:quit' }
    # sound data rules: an .adp sound far away (attenuation 1, ~5700 units from every spawn point) is heard at full
    # volume after its first block, a .wav there is silent, the same .wav with attenuation 0 is full (SPEC-040 1.1,
    # 1.3); `*` name -> sound/player/male/<name> (1.4; needs the loose file base\sound\player\male\omtest.wav in
    # the test copy, a copy of menu/End.wav); a .wav with a cue chunk started once loops until stopsound (1.3);
    # a local .adp line for the trailer (1.3). Slots 9/10 (CS 300/301) are renamed to the two test sounds.
    x1 = @{ Map = 'dm/jpndm2'; Secs = 56; Proxy = '--music 0:1 --cs 300=cin/tsr1/Laugh.adp --cs 301=menu/First.wav ' +
            '--sound 6:9:1:1:-4000:3800:3000 --sound 14:10:1:1:-4000:3800:3000 --sound 18:10:1:0:-4000:3800:3000 ' +
            '--stuff 22:"play *omtest" --stuff 26:"play ambient/locs/elevshaft/ShaftLP.wav" --stuff 40:stopsound ' +
            '--stuff 44:"play cin/tsr1/Laugh.adp" --stuff 50:soundinfo --stuff 52:quit' }
    # spatialization (SPEC-040 1.1, 1.3): menu/First.wav (16-bit) and cin/tsr1/Laugh.adp started by svc_sound at
    # points straight above the listener (no panning: the right vector is horizontal), expected 0-255 volumes:
    # 500 attn 1 -> 100; 300 attn 3 -> 43; 1000 attn 2 -> 10; 500 attn 0 -> 255; .adp 500 attn 1 -> 255 after the
    # first block; 2500 attn 1 -> 0 (.wav), 255 (.adp); 200 attn 1 volume 0.5 -> 59
    p1 = @{ Map = 'dm/jpndm2'; Secs = 46; Proxy = '--music 0:1 --cs 300=cin/tsr1/Laugh.adp --cs 301=menu/First.wav ' +
            '--sound 6:10:1:1:@0:0:500 --sound 9:10:1:3:@0:0:300 --sound 12:10:1:2:@0:0:1000 ' +
            '--sound 15:10:1:0:@0:0:500 --sound 18:9:1:1:@0:0:500 --sound 24:10:1:1:@0:0:2500 ' +
            '--sound 27:9:1:1:@0:0:2500 --sound 34:10:0.5:1:@0:0:200 --stuff 40:soundinfo --stuff 42:quit' }
    u2 = @{ Map = 'dm/jpndm2'; Secs = 56; Proxy = '--cs 13=JPN3 --music 0:2 --info 20:8:1 --info 40:8:0 --stuff 54:quit' }
}
foreach ($sc in $Scenarios) {
    $d = $S[$sc]
    if (-not $d) { throw "unknown scenario $sc" }
    foreach ($b in $Backends) {
        $tag = "${sc}_${b}$Suffix"
        Write-Host "=== $tag"
        $c = $(if ($d.Cfg) { $d.Cfg } else { $cfg })
        if ($d.Khz) {
            # archived s_khz for the next start (the relay sets it back to 22 before quit)
            $cc = 'D:\Games\SoF-test-I5s2\user\config.cfg'
            (Get-Content $cc) -replace '^set s_khz ".*"$', "set s_khz `"$($d.Khz)`"" | Set-Content $cc -Encoding ascii
        }
        & (Join-Path $PSScriptRoot 'run_mp.ps1') -Backend $b -Script $c -Tag $tag -Map $d.Map -Proxy $d.Proxy `
            -Extra $ClientExtra -Timeout ($d.Secs + 30) 2>&1 | Select-String 'exit code|proccap:|WARNING|note:'
    }
}
