# Collision/Ground Logic Extraction Report

- Root: `C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\mariokart64-master`
- Files scanned: **44**
- Files with hits: **16**
- Total keyword hits: **1384**

## File types
- `.txt`: 33
- `.c`: 4
- `.asm`: 4
- `.s`: 2
- `.md`: 1

## Keywords Used
- High-signal: collision, collide, ground, floor, surface, wall, face, triangle, height, slope, plane, ray, friction, gravity
- Low-signal: segment, road, kart, track

## Top 10 relevant code files
## Top 10 relevant notes/docs files
### C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\mariokart64-master\old notes\Rainbow Road Command Log.txt
- Score: 5270 | Weighted: 4216 | High-signal hits: 1054 | Hits: 1054 | Type: `.txt`
- Line 3807 | (no function/label found) | keywords: triangle
```text
   3805: 00738C 53 ??? - 17 42 11 22 0B 21 8B
   3806: 007394 8B ??? - 91 97 55 11 43 FE 2D
>  3807: 00739C CC Shade Triangle - E7 7A D1 62 8D 31 8F
   3808: 0073A4 11 ??? - 49 D7 A7 19 4B 19 09
   3809: 0073AC 21 ??? - 4D C6 5D 84 8B E4 59
```
- Line 3925 | (no function/label found) | keywords: triangle
```text
   3923: 007724 20 ??? - 85 28 45 49 8B 8B 15
   3924: 00772C FF Set CIMG - B7 EE 27 D5 E5 A3 97
>  3925: 007734 CC Shade Triangle - DF ED 63 AB D9 9B 97
   3926: 00773C 7A ??? - 91 D5 63 EE A5 DE 29
   3927: 007744 ED Set Scissor - 23 DC E1 61 CD FF E3
```
- Line 4022 | (no function/label found) | keywords: triangle
```text
   4020: 007A24 9B ??? - 81 EE 01 FF C1 FE 41
   4021: 007A2C 78 ??? - 49 E0 0D F8 0F 28 C1
>  4022: 007A34 CD Shade/zbuff Triangle - 81 93 41 AC 01 20 01
   4023: 007A3C 58 ??? - 03 B0 09 72 81 BD 01
   4024: 007A44 41 ??? - 41 A0 09 38 01 30 03
```
- Line 4030 | (no function/label found) | keywords: triangle
```text
   4028: 007A64 29 ??? - 01 49 81 20 C1 6A 41
   4029: 007A6C 38 ??? - 03 49 C1 41 81 48 43
>  4030: 007A74 CD Shade/zbuff Triangle - 03 7A C3 EE C1 18 41
   4031: 007A7C B8 End Display List - 4D DD 41 20 41 28 43
   4032: 007A84 20 ??? - 03 C8 4D 51 81 30 C1
```
- Line 4036 | (no function/label found) | keywords: triangle
```text
   4034: 007A94 C0 RDP NOP - 8F 39 01 E5 03 28 41
   4035: 007A9C F0 Load TLUT - texcoord=2463, 31; tile=120, ?=89E051
>  4036: 007AA4 B1 Draw 2 Triangles - tri1=98, 32, 0, tri2=65, 127, 32, ?=FF
   4037: 007AAC C4 ??? - 01 F5 83 91 89 98 4B
   4038: 007AB4 10 ??? - 81 19 01 19 C1 22 81
```
- Line 4059 | (no function/label found) | keywords: triangle
```text
   4057: 007B4C 29 ??? - 05 20 C3 91 C1 E8 5D
   4058: 007B54 C4 ??? - D7 F4 2B 9C 63 52 4D
>  4059: 007B5C CD Shade/zbuff Triangle - A1 FC FF 9B D7 28 81
   4060: 007B64 F8 Set Fog Colour - 29 68 15 A0 17 C0 25
   4061: 007B6C CF Shade/Texture/zbuff Triangle - 13 ED 19 62 93 FF C5
```

### C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\mariokart64-master\old notes\mk64levelhacking.txt
- Score: 518 | Weighted: 419 | High-signal hits: 99 | Hits: 88 | Type: `.txt`
- Line 4 | (no function/label found) | keywords: ground
```text
      2: -See the notes in the bottom left screen about calculating the start position
      3: -Make Lakitu stop being a douchebag >8^(
>     4:  -When he tries to put you on the ramps, he can't find the ground; maybe he's
      5:   chosen a point with no ground below it. Look at paths in DK Jungle and Royal
      6:   Raceway; are there points in the air over jumps? We might just be able to
```
- Line 5 | (no function/label found) | keywords: ground
```text
      3: -Make Lakitu stop being a douchebag >8^(
      4:  -When he tries to put you on the ramps, he can't find the ground; maybe he's
>     5:   chosen a point with no ground below it. Look at paths in DK Jungle and Royal
      6:   Raceway; are there points in the air over jumps? We might just be able to
      7:   delete those.
```
- Line 11 | (no function/label found) | keywords: face
```text
      9: -Test in various modes (ensure jumps can be made with all characters on 50 & 150cc)
     10: -Decoration:
>    11:  -Do something with the floating faces
     12:  -BG images (see notebook)
     13:  -Images under track?
```
- Line 28 | (no function/label found) | keywords: surface
```text
     26: -How the Set Combine command works - it enables textures and alpha among other things
     27:  Just go to 801E3290 and play with the commands and see how the texture changes in realtime, it's neat
>    28: -More about how dlist pointers, paths, and surface maps relate.
     29:  -Notice how the dlists work in Sherbet Land and Koopa Beach, drawing the ice walls/water separate from the
     30:   track. This suggests somehow two dlists are called; maybe one for the translucent objects and one for the
```
- Line 29 | (no function/label found) | keywords: wall
```text
     27:  Just go to 801E3290 and play with the commands and see how the texture changes in realtime, it's neat
     28: -More about how dlist pointers, paths, and surface maps relate.
>    29:  -Notice how the dlists work in Sherbet Land and Koopa Beach, drawing the ice walls/water separate from the
     30:   track. This suggests somehow two dlists are called; maybe one for the translucent objects and one for the
     31:   normal ones? This mechanism might be used for other missing things.
```
- Line 269 | (no function/label found) | keywords: surface
```text
    267: 13 /
    268:
>   269: 8002A10C checks if surface type ($V1 copied from $V0) is not 0xFE.
    270: 800F6A88 surface type you're on (16-bit)
    271: 800F6A80 and 800F6A84 are related to height/Y speed
```

### C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\mariokart64-master\old notes\surface map fix.txt
- Score: 456 | Weighted: 367 | High-signal hits: 89 | Hits: 87 | Type: `.txt`
- Line 1 | (no function/label found) | keywords: surface, track
```text
>     1: MK64: Fixing the surface map issues in the new track
      2: See also: mk64 notes Aug 9 2008.txt
      3:
```
- Line 74 | (no function/label found) | keywords: surface
```text
     72: 				A3 = (S2 + S8) + 40;
     73: 				u16 *Coord = &RSP[15][Idx]; //this seems to be reading coords -
>    74: 					//RSP seg15 has processed surface maps in it
     75: 				Idx += 22;
     76: 				A1 = TRUNC16((S1 + S6) + 40);
```
- Line 80 | (no function/label found) | keywords: surface
```text
     78:
     79: 				//802AF448
>    80: 				//This refers to the halfwords in the processed surface map
     81: 				//data,  at offsets 0x0E, 0x08, 0x0A, 0x04. Unfortunately, I
     82: 				//don't know  what any of them do.
```
- Line 96 | (no function/label found) | keywords: ray
```text
     94: 				 *  -5491, -5151
     95: 				 * stack4 is 0x69 which is 105, so it's rejected 105 elements of
>    96: 				 * this array before even
     97: 				 * getting here
     98: 				 * The patch changes this call to effectively if(1 == 1), so
```
- Line 107 | (no function/label found) | keywords: surface
```text
    105: 				 * be.
    106: 				 * This function only seems to be called when loading a track,
>   107: 				 * so it looks like the only purpose is to check which surface
    108: 				 * poly you start on. This is the only call to this function.
    109: 				 */
```
- Line 171 | (no function/label found) | keywords: ray
```text
    169: this runs multiple times with $V0 being 0x0C, 0x0D, 0x1E, 0x1F, and then 0x20
    170: At 802ADC20: LHU $V0, 0000($T1); $T1 = 801F4C86, 801F4C88, 801F4C8A, 801F4C8C,
>   171:  801F4C8E, sure enough it looks like an array of map indexes; written at
    172:  802AF510 from $S0
    173:  that's somewhere up in _802AF314(), inside the if after calling the subroutine;
```

### C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\mariokart64-master\old notes\rom addrs.txt
- Score: 264 | Weighted: 212 | High-signal hits: 52 | Hits: 50 | Type: `.txt`
- Line 7 | (no function/label found) | keywords: wall
```text
      5:
      6: 8002E000 0010
>     7: 8002E058 0010 is drive through walls, so this must be related to hit detection (duh)
      8:
      9: 700, 0, -840
```
- Line 19 | (no function/label found) | keywords: collision
```text
     17:
     18: Around 8027D850 is Mario Raceway's geometry data in RAM. You can edit it here and the
>    19: changes will be immediately visible. This data is also the collision map - remove a
     20: polygon and you can drive right through. However this is very strange, if you MOVE a
     21: polygon, it is no longer solid, nor is there an invisible barrier where it used to be.
```
- Line 22 | (no function/label found) | keywords: wall
```text
     20: polygon and you can drive right through. However this is very strange, if you MOVE a
     21: polygon, it is no longer solid, nor is there an invisible barrier where it used to be.
>    22: This isn't always true? If you move the very end of the wall at the first corner you can
     23: drive through it, but if you move the next 2 points to meet it, shifting the wall
     24: itself, the hit detection doesn't change. Also, you can go through it from the other side
```
- Line 23 | (no function/label found) | keywords: wall
```text
     21: polygon, it is no longer solid, nor is there an invisible barrier where it used to be.
     22: This isn't always true? If you move the very end of the wall at the first corner you can
>    23: drive through it, but if you move the next 2 points to meet it, shifting the wall
     24: itself, the hit detection doesn't change. Also, you can go through it from the other side
     25: that would have been inside the wall and thus not rendered.
```
- Line 25 | (no function/label found) | keywords: wall
```text
     23: drive through it, but if you move the next 2 points to meet it, shifting the wall
     24: itself, the hit detection doesn't change. Also, you can go through it from the other side
>    25: that would have been inside the wall and thus not rendered.
     26:
     27: 001DB92C: 000002BC0000FCB8
```
- Line 73 | (no function/label found) | keywords: collision
```text
     71: By changing the geometry data file's output size to 4 bytes, you fall for a looooong time
     72: through glitched crap before hitting the water, and then the game can't figure out where
>    73: to put you back. So the collision data must be linked to this.
     74:
     75: 8002CDF4 [0C023B70] handles boost ramps and runs only when you touch one. Disable this
```

### C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\mariokart64-master\old notes\new track - dlists.txt
- Score: 150 | Weighted: 120 | High-signal hits: 30 | Hits: 30 | Type: `.txt`
- Line 9 | (no function/label found) | keywords: triangle
```text
      7: ;Draw first several pieces
      8: 04008200 0400C270 Load 32 vertices from 04:00C270
>     9: B1040200 00060400 Triangle (0, 1, 2) (0, 2, 3)
     10: B10C0A08 000E0C08 Triangle (4, 5, 6) (4, 6, 7)
     11: B1141210 00161410 Triangle (8, 9, 10) (8, 10, 11)
```
- Line 10 | (no function/label found) | keywords: triangle
```text
      8: 04008200 0400C270 Load 32 vertices from 04:00C270
      9: B1040200 00060400 Triangle (0, 1, 2) (0, 2, 3)
>    10: B10C0A08 000E0C08 Triangle (4, 5, 6) (4, 6, 7)
     11: B1141210 00161410 Triangle (8, 9, 10) (8, 10, 11)
     12: B11C1A18 001E1C18 Triangle (12, 13, 14) (12, 14, 15)
```
- Line 11 | (no function/label found) | keywords: triangle
```text
      9: B1040200 00060400 Triangle (0, 1, 2) (0, 2, 3)
     10: B10C0A08 000E0C08 Triangle (4, 5, 6) (4, 6, 7)
>    11: B1141210 00161410 Triangle (8, 9, 10) (8, 10, 11)
     12: B11C1A18 001E1C18 Triangle (12, 13, 14) (12, 14, 15)
     13: B1242220 00262420 Triangle (16, 17, 18) (16, 18, 19)
```
- Line 12 | (no function/label found) | keywords: triangle
```text
     10: B10C0A08 000E0C08 Triangle (4, 5, 6) (4, 6, 7)
     11: B1141210 00161410 Triangle (8, 9, 10) (8, 10, 11)
>    12: B11C1A18 001E1C18 Triangle (12, 13, 14) (12, 14, 15)
     13: B1242220 00262420 Triangle (16, 17, 18) (16, 18, 19)
     14: B12C2A28 002E2C28 Triangle (20, 21, 22) (20, 22, 23)
```
- Line 13 | (no function/label found) | keywords: triangle
```text
     11: B1141210 00161410 Triangle (8, 9, 10) (8, 10, 11)
     12: B11C1A18 001E1C18 Triangle (12, 13, 14) (12, 14, 15)
>    13: B1242220 00262420 Triangle (16, 17, 18) (16, 18, 19)
     14: B12C2A28 002E2C28 Triangle (20, 21, 22) (20, 22, 23)
     15: B1343230 00363430 Triangle (24, 25, 26) (24, 26, 27)
```
- Line 14 | (no function/label found) | keywords: triangle
```text
     12: B11C1A18 001E1C18 Triangle (12, 13, 14) (12, 14, 15)
     13: B1242220 00262420 Triangle (16, 17, 18) (16, 18, 19)
>    14: B12C2A28 002E2C28 Triangle (20, 21, 22) (20, 22, 23)
     15: B1343230 00363430 Triangle (24, 25, 26) (24, 26, 27)
     16: B13C3A38 003E3C38 Triangle (28, 29, 30) (28, 30, 31)
```

### C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\mariokart64-master\old notes\Aug 9 2008.txt
- Score: 120 | Weighted: 96 | High-signal hits: 24 | Hits: 22 | Type: `.txt`
- Line 113 | (no function/label found) | keywords: surface
```text
    111:
    112:
>   113: 801D65A0 surface map
    114: 8027A260 pointed to by first entry - giant mushroom vertices
    115: 8027A260|09 03 00 00|FC 17 00 00|02 3F 04 00|88 88 88 FF
```
- Line 127 | (no function/label found) | keywords: gravity
```text
    125: 801D65C0: BED21B52 BF6973B0 C3DEF6FE
    126:
>   127: The first gravity float seems to be how close you can get to it or something.
    128: 3F800000: solid
    129: 3FC00000: can drive into it slightly at some places, right through at others.
```
- Line 133 | (no function/label found) | keywords: surface, triangle
```text
    131: worth noting the normal value doesn't let you get right up to it, 3F800000 does
    132: the second and third are similar, this could be one per vertex
>   133: moving the triangle doesn't seem to affect the surface map. there are only three
    134: floats here, so they could only be coords for one vertex. wtf? maybe this stuff
    135: gets cached somewhere
```
- Line 148 | (no function/label found) | keywords: surface
```text
    146: it not solid
    147:
>   148: best guess: "surface type" is the effects applied, the coords are taken from the
    149: vertices, and the 3 floats are physics values of some sort.
    150:
```
- Line 200 | (no function/label found) | keywords: triangle
```text
    198: 802A4CC4 - the RSP instructions are hardcoded in the ASM here. If we change B1 to
    199: B8, the screen blacks out, but the game still runs and shows up again when we
>   200: change it back. If we change the Triangles command, the sky breaks, so this is
    201: indeed drawing the sky. If we NOP it, the sky is black, but everything else seems
    202: fine - no failure to clear the buffer, clouds are still there, etc.
```
- Line 311 | (no function/label found) | keywords: ray, wall
```text
    309: 802928B8 writes the 0xB7 at 01:01A6D0 (80117F30) just before fences are drawn
    310: the 0x20 part is at 8029285C, it's used in another instruction too
>   311: if we set the high bit the fence becomes a gray wall that changes colour and
    312: disappears as you move; other changes seem to have no effect
    313: maybe this disables textures?
```

### C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\mariokart64-master\old notes\segment 6 2.txt
- Score: 80 | Weighted: 64 | High-signal hits: 16 | Hits: 16 | Type: `.txt`
- Line 11 | (no function/label found) | keywords: surface
```text
      9: 9570 - 9577 ?
     10: 9578 - 964F Trees
>    11: 9650 - 97EF Surface map
     12: 97F7 EOF
     13:
```
- Line 21 | (no function/label found) | keywords: surface
```text
     19: 7238 - 724F Falling rocks
     20: 7258 - 72CF Item boxes
>    21: 72D0 - 74FF Surface map
     22: 7507 EOF
     23:
```
- Line 30 | (no function/label found) | keywords: surface
```text
     28: 9298 - 936F Trees
     29: 9378 - 93D7 Item boxes
>    30: 93D8 - 94F7 Surface map
     31: 94FF EOF
     32:
```
- Line 38 | (no function/label found) | keywords: surface
```text
     36: 4578 - 5C7F Course paths
     37: 5C80 - B457 ?
>    38: B458 - B5D7 Surface map
     39: B5DF EOF
     40:
```
- Line 49 | (no function/label found) | keywords: surface
```text
     47: 180A8 - 1810F Trees
     48: 18118 - 1823F Item boxes
>    49: 18240 - 183E7 Surface map
     50: 185DF EOF
     51:
```
- Line 60 | (no function/label found) | keywords: surface
```text
     58: 7720 - 780F Trees
     59: 7818 - 788F Item boxes
>    60: 79A0 - 7B17 Surface map
     61: 7B1F EOF
     62:
```

### C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\mariokart64-master\old notes\new track.txt
- Score: 51 | Weighted: 41 | High-signal hits: 10 | Hits: 10 | Type: `.txt`
- Line 8 | (no function/label found) | keywords: surface
```text
      6: 2 [X]Add new dlists and vertices to the end of them
      7: 3 [X]Point the dlist pointers to these new lists
>     8: 4 [X]Point the surface maps to these new lists
      9: 5 [ ]Design the track
     10: 6 [ ]Add new AI path data
```
- Line 12 | (no function/label found) | keywords: face
```text
     10: 6 [ ]Add new AI path data
     11: 7 [ ]Add new item box data
>    12: 8 [ ]Do something with the neon faces - maybe just blank out the graphics
     13: 9 [ ]Do something with the chomps
     14:
```
- Line 123 | (no function/label found) | keywords: triangle
```text
    121: B9000314 005049D8 Set Other Mode L - quad kinda follows us around O_o
    122: 0400103F 0400C270 Load 4 vertices from 04:00C270
>   123: B1040200 00060400 Triangle (0, 1, 2) (0, 2, 3)
    124: B8000000 00000000 End Dlist
    125:
```
- Line 139 | (no function/label found) | keywords: surface
```text
    137:
    138: *******************************
>   139: * STEP THREE: NEW SURFACE MAP *
    140: *******************************
    141:
```
- Line 144 | (no function/label found) | keywords: surface
```text
    142: First we have to find the map:
    143:
>   144: 802B94A4 surface map jump table
    145: Rainbow Road routine:
    146: 80296328 LUI $A0, 0601
```
- Line 155 | (no function/label found) | keywords: surface
```text
    153: Format:
    154: Display list offset, 4 bytes
>   155: Surface type, 1 byte
    156: Display list index, 1 byte
    157: Flags, 2 bytes
```

### C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\mariokart64-master\old notes\texture commands.txt
- Score: 50 | Weighted: 40 | High-signal hits: 10 | Hits: 10 | Type: `.txt`
- Line 28 | (no function/label found) | keywords: height
```text
     26: There are two ways of loading textures: block or tile mode. Block mode assumes that the texture map is a contiguous block of texels that represents the whole texture map. Tile mode can lift a subrectangle out of a larger image. The following tables list block and tile mode texture-loading GBI commands respectively.
     27:
>    28: gsDPLoadTextureTile(timg,fmt,siz,width,height,uls,ult,lrs,lrt,pal,cms,cmt,masks,maskt,shifts,shiftt)
     29: /n64man/gdp/gDPLoadTextureTile.htm
     30:
```
- Line 31 | (no function/label found) | keywords: height
```text
     29: /n64man/gdp/gDPLoadTextureTile.htm
     30:
>    31: #define	gDPLoadTextureTile(pkt, timg, fmt, siz, width, height,		\
     32: 		uls, ult, lrs, lrt, pal,				\
     33: 		cms, cmt, masks, maskt, shifts, shiftt)			\
```
- Line 59 | (no function/label found) | keywords: height
```text
     57:
     58:
>    59: gDPLoadTextureBlock(Gfx *gdl, u32 timg, u32 fmt, u32 siz, u32 width, u32 height, u32 pal, u32 cms, u32 cmt, u32 masks, u32 maskt, u32 shifts, u32 shiftt)
     60: /n64man/gdp/gDPLoadTextureBlock.htm
     61:
```
- Line 62 | (no function/label found) | keywords: height
```text
     60: /n64man/gdp/gDPLoadTextureBlock.htm
     61:
>    62: #define	gDPLoadTextureBlock(pkt, timg, fmt, siz, width, height,		\
     63: 		pal, cms, cmt, masks, maskt, shifts, shiftt)		\
     64: {									\
```
- Line 70 | (no function/label found) | keywords: height
```text
     68: 	gDPLoadSync(pkt);						\
     69: 	gDPLoadBlock(pkt, G_TX_LOADTILE, 0, 0, 				\
>    70: 		(((width)*(height) + siz##_INCR) >> siz##_SHIFT) -1,	\
     71: 		CALC_DXT(width, siz##_BYTES)); 				\
     72: 	gDPPipeSync(pkt);						\
```
- Line 79 | (no function/label found) | keywords: height
```text
     77: 	gDPSetTileSize(pkt, G_TX_RENDERTILE, 0, 0,			\
     78: 		((width)-1) << G_TEXTURE_IMAGE_FRAC,			\
>    79: 		((height)-1) << G_TEXTURE_IMAGE_FRAC)			\
     80: }
     81:
```

### C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\mariokart64-master\old notes\surface hax.txt
- Score: 20 | Weighted: 16 | High-signal hits: 4 | Hits: 4 | Type: `.txt`
- Line 1 | (no function/label found) | keywords: surface
```text
>     1: 8002A10C checks if surface type ($V1 copied from $V0) is not 0xFE.
      2: 800F6A88 surface type you're on (16-bit)
      3: 800F6A80 and 800F6A84 are related to height/Y speed
```
- Line 2 | (no function/label found) | keywords: surface
```text
      1: 8002A10C checks if surface type ($V1 copied from $V0) is not 0xFE.
>     2: 800F6A88 surface type you're on (16-bit)
      3: 800F6A80 and 800F6A84 are related to height/Y speed
      4: 800798FC reads surface type, compares to 0xFD (forbidden)
```
- Line 3 | (no function/label found) | keywords: height
```text
      1: 8002A10C checks if surface type ($V1 copied from $V0) is not 0xFE.
      2: 800F6A88 surface type you're on (16-bit)
>     3: 800F6A80 and 800F6A84 are related to height/Y speed
      4: 800798FC reads surface type, compares to 0xFD (forbidden)
      5: 8007AA6C looks like it's reading a jump table at ~800EED1C
```
- Line 4 | (no function/label found) | keywords: surface
```text
      2: 800F6A88 surface type you're on (16-bit)
      3: 800F6A80 and 800F6A84 are related to height/Y speed
>     4: 800798FC reads surface type, compares to 0xFD (forbidden)
      5: 8007AA6C looks like it's reading a jump table at ~800EED1C
```
