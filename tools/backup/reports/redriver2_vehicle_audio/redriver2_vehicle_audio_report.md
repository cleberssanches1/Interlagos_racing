# REDRIVER2 vehicle audio/physics extraction

Source root: `C:\saturn\SaturnRingLib-main\Projects\Projetos_Exemplos\REDRIVER2-master`

This report extracts the code most relevant to car acceleration, gear/revs, movement coupling and tyre/engine audio.

## HANDLING_DATA (struct)

- File: `src_rebuild/Game/dr2types.h`
- Lines: `332-351`
- Why it matters: Core drivetrain state: wheel speed, revs, gear, changingGear, autoBrake.

```c
typedef struct _HANDLING_DATA
{
	MATRIX where;
	MATRIX drawCarMat;
	LONGVECTOR4 acc;
	LONGVECTOR4 aacc;
	WHEEL wheel[4];
	int wheel_speed;
	int speed;
	int direction;
	int front_vel;
	int rear_vel;
	int mayBeColliding;		// [A] now used as a bitfield to create collision pairs
	short revs;
	char gear;
	char changingGear;
	char autoBrake;

	OrientedBox oBox;
} HANDLING_DATA;
```

## PLAYER (struct)

- File: `src_rebuild/Game/dr2types.h`
- Lines: `1235-1269`
- Why it matters: Per-player audio state: car_sound_timer, revsvol, idlevol, skidding, wheelnoise.

```c
typedef struct _PLAYER
{
	LONGVECTOR4 pos;
	int dir;
	VECTOR* spoolXZ;
	VECTOR cameraPos;
	int cameraDist;
	int maxCameraDist;
	int cameraAngle;
	int headPos;
	int headTarget;
	int viewChange;
	u_char dying;
	u_char upsideDown;
	char onGrass;
	char targetCarId;
	char cameraView;
	u_char headTimer;
	char playerType;
	char worldCentreCarId;
	char playerCarId;
	char cameraCarId;
	char padid;
	char car_is_sounding;
	LONGVECTOR3 camera_vel;
	int snd_cam_ang;
	skidinfo skidding;
	skidinfo wheelnoise;
	horninfo horn;
	int car_sound_timer;
	short revsvol;
	short idlevol;
	LPPEDESTRIAN pPed;
	int crash_timer;
} PLAYER;
```

## GEAR_DESC (struct)

- File: `src_rebuild/Game/C/gamesnd.c`
- Lines: `88-95`
- Why it matters: Gear band thresholds and rev ratios used by the audio model.

```c
struct GEAR_DESC
{
	int lowidl_ws;
	int low_ws;
	int hi_ws;
	int ratio_ac;
	int ratio_id;
};
```

## geard (array)

- File: `src_rebuild/Game/C/gamesnd.c`
- Lines: `97-111`
- Why it matters: Forward speed bands for civilian/player cars.

```c
GEAR_DESC geard[2][4] =
{
	{
		{ 0, 0, 163, 144, 135 },
		{ 86, 133, 260, 90, 85 },
		{ 186, 233, 360, 60, 57 },
		{ 286, 326, 9999, 48, 45 }
	},
	{
		{ 0, 0, 50, 144, 135 },
		{ 43, 66, 100, 90, 85 },
		{ 93, 116, 150, 60, 57 },
		{ 143, 163, 9999, 48, 45 }
	}
};
```

## GetEngineRevs (function)

- File: `src_rebuild/Game/C/gamesnd.c`
- Lines: `638-702`
- Why it matters: Maps wheel speed, thrust and gear into engine revs.

```c
ushort GetEngineRevs(CAR_DATA* cp)
{
	int acc;
	GEAR_DESC* gd;
	int gear;
	int lastgear;
	int ws, lws;
	int type;

	gear = cp->hd.gear;
	ws = cp->hd.wheel_speed;
	acc = cp->thrust;
	type = (cp->controlType == CONTROL_TYPE_CIV_AI);

	if (ws > 0)
	{
		ws >>= 11;

		if (gear > 3)
			gear = 3;

		gd = &geard[type][gear];

		do {
			if (acc < 1)
				lws = gd->lowidl_ws;
			else
				lws = gd->low_ws;

			lastgear = gear;

			if (ws < lws)
			{
				gd--;
				lastgear = gear - 1;
			}

			if (gd->hi_ws < ws)
			{
				gd++;
				lastgear++;
			}

			if (gear == lastgear)
				break;

			gear = lastgear;

		} while (true);

		cp->hd.gear = lastgear;
	}
	else
	{
		ws = -ws / 2048;
		lastgear = 0;

		cp->hd.gear = 0;
	}

	if (acc != 0)
		return ws * geard[type][lastgear].ratio_ac;

	return ws * geard[type][lastgear].ratio_id;
}
```

## ControlCarRevs (function)

- File: `src_rebuild/Game/C/gamesnd.c`
- Lines: `708-802`
- Why it matters: Smooths rev changes, marks changingGear, and crossfades idle/rev volumes.

```c
void ControlCarRevs(CAR_DATA* cp)
{
	char spin;
	int player_id, acc, oldvol;
	short oldRevs, newRevs, desiredRevs;

	acc = cp->thrust;
	spin = cp->wheelspin;
	oldRevs = cp->hd.revs;
	player_id = GetPlayerId(cp);

	cp->hd.changingGear = 0;

	if (spin == 0 && (cp->hd.wheel[1].susCompression || cp->hd.wheel[3].susCompression || acc == 0))
	{
		desiredRevs = GetEngineRevs(cp);
	}
	else
	{
		desiredRevs = 20160;

		if (cp->hd.wheel[1].susCompression == 0 && cp->hd.wheel[3].susCompression == 0)
		{
			desiredRevs = 30719;
			spin = 1;
		}

		if (oldRevs < 8000)
			oldRevs = 8000;

		cp->hd.gear = 0;
	}

	newRevs = desiredRevs;
	desiredRevs = (oldRevs - newRevs);

	if (maxrevdrop < desiredRevs)
	{
		acc = 0;
		cp->hd.changingGear = 1;
		newRevs = oldRevs - maxrevdrop;
	}

	desiredRevs = newRevs - oldRevs;

	if (maxrevrise < desiredRevs)
		newRevs = oldRevs + maxrevrise;

	cp->hd.revs = newRevs;
	if (player_id != -1)
	{
		if (acc == 0 && newRevs < 7001)
		{
			acc = player[player_id].revsvol;

			player[player_id].idlevol += 200;
			player[player_id].revsvol = acc - 200;

			if (player[player_id].idlevol > -6000)
				player[player_id].idlevol = -6000;

			if (player[player_id].revsvol < -10000)
				player[player_id].revsvol = -10000;
		}
		else
		{
			int revsmax;

			if (acc != 0)
				revsmax = -5500;
			else
				revsmax = -6750;

			if (spin == 0)
				acc = -64;
			else
				acc = -256;

			player[player_id].idlevol += acc;

			if (spin == 0)
				acc = 175;
			else
				acc = 700;

			player[player_id].revsvol = player[player_id].revsvol + acc;

			if (player[player_id].idlevol < -10000)
				player[player_id].idlevol = -10000;

			if (player[player_id].revsvol > revsmax)
				player[player_id].revsvol = revsmax;
		}
	}
}
```

## car engine sounds (context)

- File: `src_rebuild/Game/C/gamesnd.c`
- Lines: `1652-1686`
- Why it matters: Updates two engine channels every frame: rev bed and idle bed.

```c
		if (lcp->car_sound_timer == 0)
			lcp->car_is_sounding = 0;

		if (lcp->crash_timer > 0)
			lcp->crash_timer--;

		// car engine sounds
		if (cp)
		{
			position = (VECTOR*)cp->hd.where.t;
			velocity = (LONGVECTOR3*)cp->st.n.linearVelocity;

			if (lcp->car_is_sounding < 2)
				vol = lcp->revsvol;
			else
				vol = -10000;

			chan = i * 3;
			SetChannelPosition3(chan, position, velocity, vol, cp->hd.revs / 4 + lcp->revsvol / 64 + 1500, 0);
			
			if (lcp->car_is_sounding == 0)
				vol = lcp->idlevol;
			else
				vol = -10000;

			chan = i * 3 + 1;
			SetChannelPosition3(chan, position, velocity, vol, cp->hd.revs / 4 + 4096, 0);

			// siren sound control
			if (CarHasSiren(cp->ap.model) != 0)
			{
				chan = i  * 3 + 2;

				if (lcp->horn.on == 0)
					SpuSetVoicePitch(chan, 0);		// don't stop it really
```

## GetFrictionScalesDriver1 (function)

- File: `src_rebuild/Game/C/wheelforces.c`
- Lines: `57-166`
- Why it matters: How thrust, handbrake and wheelspin alter traction and wheel lock.

```c
void GetFrictionScalesDriver1(CAR_DATA* cp, CAR_LOCALS* cl, int* frontFS, int* rearFS)
{
	int autoBrake;
	int q;
	_HANDLING_TYPE* hp;

	hp = &handlingType[cp->hndType];

	if (cp->thrust < 0)
		*frontFS = 1453;
	else if (cp->thrust < 1)
		*frontFS = 937;
	else
		*frontFS = 820;

	autoBrake = cp->hd.autoBrake;

	if (cp->wheelspin == 0 && hp->autoBrakeOn != 0 && autoBrake > 0 && cp->hd.wheel_speed > 0)
	{
		q = autoBrake << 1;

		if (autoBrake > 13)
		{
			autoBrake = 13;
			q = 26;
		}

		*frontFS += (q + autoBrake) * 15;

		D_CHECK_ERROR(hp->autoBrakeOn == 2, "invalid autoBrakeOn");
	}

	if ((cp->thrust < 0 && cp->hd.wheel_speed > 41943 && cp->hndType == 0) ||
		(cp->controlType == CONTROL_TYPE_CIV_AI && cp->ai.c.thrustState == 3 && cp->ai.c.ctrlState != 9))
	{
		cp->hd.wheel[3].locked = 1;
		cp->hd.wheel[2].locked = 1;
		cp->hd.wheel[1].locked = 1;
		cp->hd.wheel[0].locked = 1;
	}
	else
	{
		cp->hd.wheel[3].locked = 0;
		cp->hd.wheel[2].locked = 0;
		cp->hd.wheel[1].locked = 0;
		cp->hd.wheel[0].locked = 0;
	}

	if (cp->handbrake == 0)
	{
		if (cp->wheelspin != 0)
			*frontFS += 600;
	}
	else
	{
		if (cp->thrust > -1)
			cp->thrust = 0;

		if (cp->hd.wheel_speed < 1)
			*frontFS -= 375;
		else
			*frontFS += 656;

		cp->hd.wheel[1].locked = 1;
		cp->hd.wheel[3].locked = 1;
		cp->wheelspin = 0;
	}

	if (cp->hd.wheel_speed < 0 && cp->thrust > -1 && cp->handbrake == 0)
	{
		*frontFS -= 400;
	}

	*rearFS = 0x780 - *frontFS;

	if (cp->wheelspin != 0)
	{
		cp->thrust = FIXEDH(cp->ap.carCos->powerRatio * 5000);
	}

	if (cp->thrust < 0 && cp->hd.wheel_speed > 41943 && cl->aggressive != 0)
	{
		*frontFS = (*frontFS * 10) / 8;
		*rearFS = (*rearFS * 10) / 8;
	}
	else
	{
		if (cp->hd.wheel[0].onGrass == 0)
			*frontFS = (*frontFS * 36 - *frontFS) / 32;
		else
			*frontFS = (*frontFS * 40 - *frontFS) / 32;
	}

	*frontFS = (*frontFS * hp->frictionScaleRatio) / 32;
	*rearFS = (*rearFS * hp->frictionScaleRatio) / 32;

	if ((cp->hndType == 5) && (cp->ai.l.dstate == 5))
	{
		*frontFS = (*frontFS * 3) / 2;
		*rearFS = (*rearFS * 3) / 2;
	}

	int traction = cp->ap.carCos->traction;

	if (traction != 4096)
	{
		*frontFS = FIXEDH(*frontFS * traction);
		*rearFS = FIXEDH(*rearFS * traction);
	}
}
```

## AddWheelForcesDriver1 (function)

- File: `src_rebuild/Game/C/wheelforces.c`
- Lines: `191-532`
- Why it matters: Wheel contact, surface sampling and slip metrics used by tyre audio.

```c
void AddWheelForcesDriver1(CAR_DATA* cp, CAR_LOCALS* cl)
{
	int oldCompression, newCompression;
	int dir;
	int forcefac;
	int angle;
	int lfx, lfz;
	int sidevel, slidevel;
	int susForce;
	int chan;
	WHEEL* wheel;
	int friction_coef;
	int oldSpeed, wheelspd;
	LONGVECTOR4 wheelPos, surfacePoint, surfaceNormal;
	VECTOR force;
	LONGVECTOR4 pointVel;
	int frontFS, rearFS;
	sdPlane* SurfacePtr;
	int i;
	int cdx, cdz;
	int sdx, sdz;
	CAR_COSMETICS* car_cos;
	int player_id;
	int oldCutRoughness;

	oldSpeed = cp->hd.speed * 3 >> 1;

	if (oldSpeed < 32)
		oldSpeed = oldSpeed * -72 + 3696;
	else
		oldSpeed = 1424 - oldSpeed;

	dir = cp->hd.direction;
	cdx = RSIN(dir);
	cdz = RCOS(dir);

	dir += cp->wheel_angle;
	sdx = RSIN(dir);
	sdz = RCOS(dir);

	player_id = GetPlayerId(cp);
	car_cos = &car_cosmetics[cp->ap.model];
	oldCutRoughness = gInGameCutsceneActive && gCurrentMissionNumber == 23 && gInGameCutsceneID == 0; // [A] hack moved here

	GetFrictionScalesDriver1(cp, cl, &frontFS, &rearFS);
	cp->hd.front_vel = 0;
	cp->hd.rear_vel = 0;

	if (oldSpeed > 3300)
		oldSpeed = 3300;

	i = 3;
	wheel = cp->hd.wheel + 3;
	do {
		gte_ldv0(&car_cos->wheelDisp[i]);

		gte_rtv0tr();
		gte_stlvnl(wheelPos);

		FindSurfaceD2((VECTOR*)&wheelPos, (VECTOR*)&surfaceNormal, (VECTOR*)&surfacePoint, &SurfacePtr);

		if (SurfacePtr && SurfacePtr->surface == SURF_GRASS)
		{
			int roughness;
			roughness = RSIN((surfacePoint[0] + surfacePoint[2]) * 2) >> 8;

			surfacePoint[1] += oldCutRoughness ? (roughness >> 1) : (roughness / 3);

			forcefac = 2048;
		}
		else
		{
			forcefac = 4096;
		}

		friction_coef = (forcefac * (32400 - wetness) >> 15) + 500;

		if (SurfacePtr != NULL)
			wheel->onGrass = SurfacePtr->surface == SURF_GRASS;
		else
			wheel->onGrass = 0;

		if (SurfacePtr)
		{
			switch (SurfacePtr->surface)
			{
				case SURF_GRASS:
				case SURF_WATER:
				case SURF_DEEPWATER:
				case SURF_SAND:
					wheel->surface = 0x80;
					break;
				default:
					wheel->surface = 0;
			}

			// [A] indication of Event surface which means we can't add tyre tracks for that wheel
			if (SurfacePtr->surface - 16U < 16)
				wheel->surface |= 0x8;

			switch (SurfacePtr->surface)
			{
				case SURF_ALLEY:
					wheel->surface |= 0x2;
					break;
				case SURF_WATER:
				case SURF_DEEPWATER:
					wheel->surface |= 0x1;
					break;
				case SURF_SAND:
					wheel->surface |= 0x3;
					break;
			}
		}
		else
		{
			wheel->surface = 0;
		}

		oldCompression = wheel->susCompression;
		newCompression = FIXEDH((surfacePoint[1] - wheelPos[1]) * surfaceNormal[1]) + 14;

		if (newCompression < 0)
			newCompression = 0;

		if (newCompression > 800)
			newCompression = 12;

		// play wheel collision sound
		// and apply vibration to player controller
		if (cp->controlType == CONTROL_TYPE_PLAYER)
		{
			if (ABS(newCompression - oldCompression) > 12 && (i & 1U) != 0)
			{
				chan = GetFreeChannel(0);
				if(chan > -1)
				{
					if (NumPlayers > 1 && NoPlayerControl == 0)
						SetPlayerOwnsChannel(chan, player_id);

					Start3DSoundVolPitch(chan, SOUND_BANK_SFX, 1, cp->hd.where.t[0], cp->hd.where.t[1], cp->hd.where.t[2], -2500, 400);
					SetChannelPosition3(chan, (VECTOR*)cp->hd.where.t, NULL, -2500, 400, 0);
				}
			}

			if (newCompression >= 65)
				SetPadVibration(*cp->ai.padid, 1);
			else if (newCompression >= 35)
				SetPadVibration(*cp->ai.padid, 2);
			else if (newCompression > 25)
				SetPadVibration(*cp->ai.padid, 3);
		}

		if (newCompression > 42)
			newCompression = 42;

		if (newCompression == 0 && oldCompression == 0)
		{
			wheel->susCompression = 0;
		}
		else
		{
			wheelPos[2] = wheelPos[2] - cp->hd.where.t[2];
			wheelPos[1] = wheelPos[1] - cp->hd.where.t[1];
			wheelPos[0] = wheelPos[0] - cp->hd.where.t[0];

			force.vz = 0;
			force.vx = 0;

			pointVel[0] = FIXEDH(cl->avel[1] * wheelPos[2] - cl->avel[2] * wheelPos[1]) + cl->vel[0];
			pointVel[2] = FIXEDH(cl->avel[0] * wheelPos[1] - cl->avel[1] * wheelPos[0]) + cl->vel[2];

			// that's our spring
			susForce = newCompression * 230 - oldCompression * 100;

			if (wheel->locked)
			{
				dir = ratan2(pointVel[0] >> 6, pointVel[2] >> 6);

				lfx = RSIN(dir);
				lfz = RCOS(dir);

				if (ABS(pointVel[0]) + ABS(pointVel[2]) < 8000)
				{
					surfaceNormal[0] = 0;
					surfaceNormal[1] = ONE;
					surfaceNormal[2] = 0;
				}
			}
			else
			{
				if (i & 1)
				{
					lfz = -cdx;
					lfx = cdz;
				}
				else
				{
					lfz = -sdx;
					lfx = sdz;
				}
			}

			slidevel = (pointVel[0] / 64) * (lfx / 64) + (pointVel[2] / 64) * (lfz / 64);
			wheelspd = ABS((oldSpeed / 64) * (slidevel / 64));

			if (slidevel > 50000)
			{
				slidevel = 12500;
			}
			else if (slidevel < -50000)
			{
				slidevel = -12500;
			}
			else
			{
				slidevel = FIXEDH(oldSpeed * slidevel);

				if (slidevel > 12500)
					slidevel = 12500;

				if (slidevel < -12500)
					slidevel = -12500;
			}
			
			if ((i & 1U) != 0)
			{
				// rear wheels
				if (wheel->locked == 0)
				{
					sidevel = FIXEDH(rearFS * slidevel);

					if (handlingType[cp->hndType].autoBrakeOn != 0 && 0 < sidevel * cp->wheel_angle)
						cp->hd.autoBrake = -1;

					force.vx = -lfz * cp->thrust;
					force.vz = lfx * cp->thrust;
				}
				else
				{
					sidevel = FixHalfRound(frontFS * slidevel, 14);
				}

				if (cp->hd.rear_vel < wheelspd)
					cp->hd.rear_vel = wheelspd;
			}
			else
			{
				// front wheels
				sidevel = frontFS * slidevel + 2048 >> 12;
				
				if (wheel->locked)
				{
					sidevel = (frontFS * slidevel + 2048 >> 13) + sidevel >> 1;
					
					forcefac = FixHalfRound(FIXEDH(-sidevel * lfx) * sdz - FIXEDH(-sidevel * lfz) * sdx, 11);
					
					force.vx = forcefac * sdz;
					force.vz = -forcefac * sdx;
				}
				else
				{
					if (cp->controlType == CONTROL_TYPE_PURSUER_AI)
					{
						force.vx = sdx * cp->thrust;
						force.vz = sdz * cp->thrust;
					}
				}

				if (cp->hd.front_vel < wheelspd)
					cp->hd.front_vel = wheelspd;

			}

			force.vx += (susForce * surfaceNormal[0] - sidevel * lfx) - cl->vel[0] * 12;
			force.vz += (susForce * surfaceNormal[2] - sidevel * lfz) - cl->vel[2] * 12;

			// apply speed reduction by water
			if ((wheel->surface & 7) == 1)
			{
				force.vx -= cl->vel[0] * 75;
				force.vz -= cl->vel[2] * 75;
			}

			angle = cp->hd.where.m[1][1];

			if (angle < 2048)
			{
				angle = 4096 - angle;

				if (angle <= 4096)
					angle = 4096 - FIXEDH(angle * angle);
				else
					angle = 0;

				friction_coef = FIXEDH(friction_coef * angle);
			}

			if (surfaceNormal[1] < 3276)
				friction_coef = friction_coef * surfaceNormal[1] * 5 >> 0xe;

			force.vy = FIXEDH(susForce * surfaceNormal[1] - cl->vel[1] * 12);
			force.vx = FIXEDH(force.vx) * friction_coef >> 0xc;
			force.vz = FIXEDH(force.vz) * friction_coef >> 0xc;

			// pursuer cars have more stability
			if (cp->controlType == CONTROL_TYPE_PURSUER_AI)
			{
				if (gCopDifficultyLevel == 2)
					wheelPos[1] = (wheelPos[1] * 12) / 32;
				else
					wheelPos[1] = (wheelPos[1] * 19) / 32;
			}

			cp->hd.acc[0] += force.vx;
			cp->hd.acc[1] += force.vy;
			cp->hd.acc[2] += force.vz;
	
			cp->hd.aacc[0] += FIXEDH(wheelPos[1] * force.vz - wheelPos[2] * force.vy);
			cp->hd.aacc[1] += FIXEDH(wheelPos[2] * force.vx - wheelPos[0] * force.vz);
			cp->hd.aacc[2] += FIXEDH(wheelPos[0] * force.vy - wheelPos[1] * force.vx);

			wheel->susCompression = newCompression;
		}
		wheel--;
		i--;
	} while (i >= 0);

	if (cp->hd.wheel[1].susCompression == 0 && cp->hd.wheel[3].susCompression == 0)
	{
		if (cp->thrust >= 1)
			cp->hd.wheel_speed = 1703936 + 0x4000;
		else if (cp->thrust <= -1)
			cp->hd.wheel_speed = -1245184 + 0x4000;
		else
			cp->hd.wheel_speed = 0;
	}
	else
	{
		cp->hd.wheel_speed = cdz / 64 * (cl->vel[2] / 64) + cdx / 64 * (cl->vel[0] / 64);
	}
}
```

## desired_skid (context)

- File: `src_rebuild/Game/C/handling.c`
- Lines: `1468-1502`
- Why it matters: Skid sound selection, restart and continuous update.

```c
	static char last_track_state[MAX_TYRE_PLAYERS][MAX_TYRE_TRACK_WHEELS] = { -1 };
	int skidsound, cnt;

	char wheels_on_ground;
	char lay_down_tracks;
	char tracks_and_smoke;
	char channel, desired_skid, desired_wheel;

	if (cp->controlType != CONTROL_TYPE_PLAYER && 
		cp->controlType != CONTROL_TYPE_LEAD_AI && 
		cp->controlType != CONTROL_TYPE_CUTSCENE)
	{
		TerminateSkidding(player_id);
		return;
	}

	// PHYSICS! jumping effects, also make car nose down
	jump_debris(cp);

	// [A] do hubcaps here
	HandlePlayerHubcaps(player_id);

	wheels_on_ground = 0;
	lay_down_tracks = 0;
	tracks_and_smoke = 0;

	for (cnt = 0; cnt < 4; cnt++)
	{
		if (cp->hd.wheel[cnt].susCompression != 0)
			wheels_on_ground |= 1 << cnt;
	}

	skidsound = 0;

	// make tyre tracks and skid sound if needed
```

## desired_wheel (context)

- File: `src_rebuild/Game/C/handling.c`
- Lines: `1468-1502`
- Why it matters: Wheel noise selection by surface and speed.

```c
	static char last_track_state[MAX_TYRE_PLAYERS][MAX_TYRE_TRACK_WHEELS] = { -1 };
	int skidsound, cnt;

	char wheels_on_ground;
	char lay_down_tracks;
	char tracks_and_smoke;
	char channel, desired_skid, desired_wheel;

	if (cp->controlType != CONTROL_TYPE_PLAYER && 
		cp->controlType != CONTROL_TYPE_LEAD_AI && 
		cp->controlType != CONTROL_TYPE_CUTSCENE)
	{
		TerminateSkidding(player_id);
		return;
	}

	// PHYSICS! jumping effects, also make car nose down
	jump_debris(cp);

	// [A] do hubcaps here
	HandlePlayerHubcaps(player_id);

	wheels_on_ground = 0;
	lay_down_tracks = 0;
	tracks_and_smoke = 0;

	for (cnt = 0; cnt < 4; cnt++)
	{
		if (cp->hd.wheel[cnt].susCompression != 0)
			wheels_on_ground |= 1 << cnt;
	}

	skidsound = 0;

	// make tyre tracks and skid sound if needed
```
