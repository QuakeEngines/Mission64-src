/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2000-2002 Mr. Hyde and Mad Dog

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// g_weapon.c

#include "g_local.h"
#include "m_player.h"


static qboolean	is_quad;
static byte		is_silenced;

void cock_offhand_grenade(edict_t *ent); //mxd
void throw_offhand_grenade(edict_t *ent); //mxd

//mxd. G_ProjectSource, but takes player handness into account
void P_ProjectSource(gclient_t *client, vec3_t point, const vec3_t distance, vec3_t forward, vec3_t right, vec3_t result)
{
	vec3_t _distance;
	VectorCopy(distance, _distance);

	if (client->pers.hand == LEFT_HANDED)
		_distance[1] *= -1;
	else if (client->pers.hand == CENTER_HANDED)
		_distance[1] = 0;

	G_ProjectSource(point, _distance, forward, right, result);
}

//mxd. Same as P_ProjectSource, but with up vector...
void P_ProjectSource2(gclient_t *client, vec3_t point, const vec3_t distance, vec3_t forward, vec3_t right, vec3_t up, vec3_t result)
{
	vec3_t _distance;
	VectorCopy(distance, _distance);

	if (client->pers.hand == LEFT_HANDED)
		_distance[1] *= -1;
	else if (client->pers.hand == CENTER_HANDED)
		_distance[1] = 0;

	G_ProjectSource2(point, _distance, forward, right, up, result);
}

/*
===============
PlayerNoise

Each player can have two noise objects associated with it:
a personal noise (jumping, pain, weapon firing), and a weapon target noise (bullet wall impacts)

Monsters that don't directly see the player can move to a noise in hopes of seeing the player from there.
===============
*/
void PlayerNoise(edict_t *who, const vec3_t where, int type)
{
	edict_t *noise;

	if (type == PNOISE_WEAPON && who->client->silencer_shots)
	{
		who->client->silencer_shots--;
		return;
	}

	if (deathmatch->value)
		return;

	if (who->flags & FL_NOTARGET)
		return;

	if (who->flags & FL_DISGUISED)
	{
		if (type == PNOISE_WEAPON)
		{
			level.disguise_violator = who;
			level.disguise_violation_framenum = level.framenum + 5;
		}
		else
		{
			return;
		}
	}

	if (!who->mynoise)
	{
		noise = G_Spawn();
		noise->classname = "player_noise";
		VectorSet(noise->mins, -8, -8, -8);
		VectorSet(noise->maxs, 8, 8, 8);
		noise->owner = who;
		noise->svflags = SVF_NOCLIENT;
		who->mynoise = noise;

		noise = G_Spawn();
		noise->classname = "player_noise";
		VectorSet(noise->mins, -8, -8, -8);
		VectorSet(noise->maxs, 8, 8, 8);
		noise->owner = who;
		noise->svflags = SVF_NOCLIENT;
		who->mynoise2 = noise;
	}

	if (type == PNOISE_SELF || type == PNOISE_WEAPON)
	{
		noise = who->mynoise;
		level.sound_entity = noise;
		level.sound_entity_framenum = level.framenum;
	}
	else // type == PNOISE_IMPACT
	{
		noise = who->mynoise2;
		level.sound2_entity = noise;
		level.sound2_entity_framenum = level.framenum;
	}

	VectorCopy(where, noise->s.origin);
	VectorSubtract(where, noise->maxs, noise->absmin);
	VectorAdd(where, noise->maxs, noise->absmax);
	noise->teleport_time = level.time;
	gi.linkentity(noise);
}


qboolean Pickup_Weapon(edict_t *ent, edict_t *other)
{
	//Knightmare- override ammo pickup values with cvars
	SetAmmoPickupValues();

	const int index = ITEM_INDEX(ent->item);

	if (((int)dmflags->value & DF_WEAPONS_STAY || coop->value) && other->client->pers.inventory[index])
	{
		if (!(ent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)))
			return false;	// leave the weapon for others to pickup
	}

	other->client->pers.inventory[index]++;

	if (!(ent->spawnflags & DROPPED_ITEM) )
	{
		// give them some ammo with it

		// Lazarus: blaster doesn't use ammo
		if (ent->item->ammo)
		{
			gitem_t *ammo = FindItem(ent->item->ammo);
			const int ammount = ((int)dmflags->value & DF_INFINITE_AMMO ? 1000 : ammo->quantity);
			Add_Ammo(other, ammo, ammount);
		}

		if (!(ent->spawnflags & DROPPED_PLAYER_ITEM))
		{
			if (deathmatch->value)
			{
				if ((int)(dmflags->value) & DF_WEAPONS_STAY)
					ent->flags |= FL_RESPAWN;
				else
					SetRespawn(ent, 30);
			}

			if (coop->value)
				ent->flags |= FL_RESPAWN;
		}
	}

	if (other->client->pers.weapon != ent->item && (other->client->pers.inventory[index] == 1) 
		&& (!deathmatch->value || other->client->pers.weapon == FindItem("blaster") ||	other->client->pers.weapon == FindItem("No weapon")))
	{
		other->client->newweapon = ent->item;
	}

	// If rocket launcher, give the HML (but no ammo).
	//if (index == rl_index)
		//other->client->pers.inventory[hml_index] = other->client->pers.inventory[index]; //mxd

	return true;
}


/*
===============
ChangeWeapon

The old weapon has been dropped all the way, so make the new one current
===============
*/
void ChangeWeapon(edict_t *ent)
{
	//mxd. No throwable grenades in Q2 N64
	/*if (ent->client->grenade_time)
	{
		ent->client->grenade_time = level.time;
		ent->client->weapon_sound = 0;
		weapon_grenade_fire(ent, false);
		ent->client->grenade_time = 0;
	}*/

	ent->client->pers.lastweapon = ent->client->pers.weapon;
	ent->client->pers.weapon = ent->client->newweapon;
	ent->client->newweapon = NULL;
	ent->client->machinegun_shots = 0;

	// set visible model
	if (ent->s.modelindex == MAX_MODELS - 1)
	{
		int flag = 0;
		if (ent->client->pers.weapon)
			flag = ((ent->client->pers.weapon->weapmodel & 0xff) << 8);

		ent->s.skinnum = (ent - g_edicts - 1) | flag;
	}

	if (ent->client->pers.weapon && ent->client->pers.weapon->ammo)
		ent->client->ammo_index = ITEM_INDEX(FindItem(ent->client->pers.weapon->ammo));
	else
		ent->client->ammo_index = 0;

	if (!ent->client->pers.weapon)
	{
		// dead
		ent->client->ps.gunindex = 0;
		return;
	}

	ent->client->weaponstate = WEAPON_ACTIVATING;
	ent->client->ps.gunframe = 0;

	// DWH: Don't display weapon if in 3rd person
	if (!ent->client->chasetoggle)
		ent->client->ps.gunindex = gi.modelindex(ent->client->pers.weapon->view_model);

	// DWH: change weapon model index if necessary
	if (ITEM_INDEX(ent->client->pers.weapon) == noweapon_index)
		ent->s.modelindex2 = 0;
	else
		ent->s.modelindex2 = MAX_MODELS - 1;

	ent->client->anim_priority = ANIM_PAIN;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crpain1;
		ent->client->anim_end = FRAME_crpain4;
	}
	else
	{
		ent->s.frame = FRAME_pain301;
		ent->client->anim_end = FRAME_pain304;
	}
}

/*
=================
NoAmmoWeaponChange
=================
*/
void NoAmmoWeaponChange(edict_t *ent)
{
	if (ent->client->pers.inventory[slugs_index] && ent->client->pers.inventory[ITEM_INDEX(FindItem("railgun"))])
		ent->client->newweapon = FindItem("railgun");
	else if (ent->client->pers.inventory[cells_index] && ent->client->pers.inventory[ITEM_INDEX(FindItem("hyperblaster"))])
		ent->client->newweapon = FindItem("hyperblaster");
	else if (ent->client->pers.inventory[bullets_index] && ent->client->pers.inventory[ITEM_INDEX(FindItem("chaingun"))])
		ent->client->newweapon = FindItem("chaingun");
	else if (ent->client->pers.inventory[bullets_index] && ent->client->pers.inventory[ITEM_INDEX(FindItem("machinegun"))])
		ent->client->newweapon = FindItem("machinegun");
	else if (ent->client->pers.inventory[shells_index] > 1 && ent->client->pers.inventory[ITEM_INDEX(FindItem("super shotgun"))])
		ent->client->newweapon = FindItem("super shotgun");
	else if (ent->client->pers.inventory[shells_index] && ent->client->pers.inventory[ITEM_INDEX(FindItem("shotgun"))])
		ent->client->newweapon = FindItem("shotgun");
	else if (ent->client->pers.inventory[ITEM_INDEX(FindItem("blaster"))]) // DWH: Dude may not HAVE a blaster
		ent->client->newweapon = FindItem("blaster");
	else
		ent->client->newweapon = FindItem("No Weapon");
}

/*
=================
Think_Weapon

Called by ClientBeginServerFrame and ClientThink
=================
*/
void Think_Weapon(edict_t *ent)
{
	// if just died, put the weapon away
	if (ent->health < 1)
	{
		ent->client->newweapon = NULL;
		ChangeWeapon(ent);
	}

	// Lazarus: Don't fire if game is frozen
	if (level.freeze)
		return;

	if (ent->flags & FL_TURRET_OWNER)
	{
		if ((ent->client->latched_buttons|ent->client->buttons) & BUTTONS_ATTACK)
		{
			ent->client->latched_buttons &= ~BUTTONS_ATTACK;
			turret_breach_fire(ent->turret);
		}

		return;
	}

	// call active weapon think routine
	if (ent->client->pers.weapon && ent->client->pers.weapon->weaponthink)
	{
		is_quad = (ent->client->quad_framenum > level.framenum);
		is_silenced = (ent->client->silencer_shots ? MZ_SILENCED : 0);
		ent->client->pers.weapon->weaponthink(ent);
	}
}


/*
================
Use_Weapon

Make the weapon ready if there is ammo
================
*/
void Use_Weapon(edict_t *ent, gitem_t *in_item)
{
	gitem_t *item = in_item;
	const int index = ITEM_INDEX(item);
	const int current_weapon_index = ITEM_INDEX(ent->client->pers.weapon);

	// see if we're already using it
	if (index == current_weapon_index) 
		return; //mxd. No HML ples.

	if (item->ammo && !g_select_empty->value && !(item->flags & IT_AMMO))
	{
		gitem_t *ammo_item = FindItem(item->ammo);
		const int ammo_index = ITEM_INDEX(ammo_item);

		if (!ent->client->pers.inventory[ammo_index])
		{
			if (!(item->flags & IT_NO_NOAMMO_MESSAGES)) //mxd
				safe_cprintf(ent, PRINT_HIGH, "^3No %s for %s.\n", ammo_item->pickup_name, item->pickup_name);

			return;
		}

		if (ent->client->pers.inventory[ammo_index] < item->quantity)
		{
			if (!(item->flags & IT_NO_NOAMMO_MESSAGES)) //mxd
				safe_cprintf(ent, PRINT_HIGH, "^3Not enough %s for %s.\n", ammo_item->pickup_name, item->pickup_name);

			return;
		}
	}

	// change to this weapon when down
	ent->client->newweapon = item;
}


/*
================
Drop_Weapon
================
*/
void Drop_Weapon(edict_t *ent, gitem_t *item)
{
	if ((int)(dmflags->value) & DF_WEAPONS_STAY)
		return;

	// see if we're already using it
	const int index = ITEM_INDEX(item);
	if ((item == ent->client->pers.weapon || item == ent->client->newweapon) && ent->client->pers.inventory[index] == 1)
	{
		safe_cprintf(ent, PRINT_HIGH, "Can't drop current weapon\n");
		return;
	}

	Drop_Item(ent, item);
	ent->client->pers.inventory[index]--;
}


/*
================
mxd. Shells and casings
================
*/

void eject_bullet_shell(edict_t *ent, vec3_t offset) // offset must be local (e.g. without viewheight)
{
	if (!ent->client || deathmatch->value || !m64_spawn_casings->value) return;

	edict_t	*shell = ThrowGib(ent, "models/weapons/shells/bullet/tris.md3", 0, GIB_BULLET_SHELL);
	if (!shell) return;

	// Bounding box
	VectorSet(shell->mins, -4, -4, -2);
	VectorSet(shell->maxs, 4, 4, 4);

	vec3_t forward, right, up;
	AngleVectors(ent->client->v_angle, forward, right, up);

	// Angles
	shell->s.angles[YAW] = ent->client->v_angle[YAW];
	shell->s.angles[PITCH] = ent->client->v_angle[PITCH];

	// Position
	vec3_t view_offset;
	P_ProjectSource(ent->client, ent->s.origin, tv(0, 0, ent->viewheight), forward, right, shell->s.origin);
	P_ProjectSource2(ent->client, vec3_origin, offset, forward, right, up, view_offset);
	VectorAdd(shell->s.origin, view_offset, shell->s.origin);

	// Modify position by velocity to reduce visual discrepancy... max. horiz. velocity: 300, max. vert. velocity: ~1200.
	for (int i = 0; i < 3; i++)
		shell->s.origin[i] += 4 * (ent->velocity[i] / 300.0f);

	// Velocity
	VectorScale(forward, ((rand() % 17) + 8), shell->velocity);
	VectorMA(shell->velocity, 70 + (rand() % 48), right, shell->velocity);
	VectorAdd(shell->velocity, ent->velocity, shell->velocity);
	if (ent->groundentity) VectorAdd(shell->velocity, ent->groundentity->velocity, shell->velocity);
	shell->velocity[2] += 120 + (rand() % 33);

	// Angular velocity
	VectorSet(shell->avelocity, (rand() % 33) + 64, (rand() % 33) + 32, (rand() % 33) + 64);
	if (rand() % 2) shell->avelocity[0] *= -1;
	if (rand() % 2) shell->avelocity[1] *= -1;
	if (rand() % 2) shell->avelocity[2] *= -1;
}

void eject_shotgun_shell(edict_t *ent, vec3_t offset) // offset must be local (e.g. without viewheight)
{
	if (!ent->client || deathmatch->value || !m64_spawn_casings->value) return;

	edict_t	*shell = ThrowGib(ent, "models/weapons/shells/shell/tris.md3", 0, GIB_SHOTGUN_SHELL);
	if (!shell) return;

	// Bounding box
	VectorSet(shell->mins, -4, -4, -2);
	VectorSet(shell->maxs, 4, 4, 4);

	vec3_t forward, right, up;
	AngleVectors(ent->client->v_angle, forward, right, up);

	// Angles
	shell->s.angles[YAW] = ent->client->v_angle[YAW] + 15;
	shell->s.angles[PITCH] = ent->client->v_angle[PITCH];

	// Position
	vec3_t view_offset;
	P_ProjectSource(ent->client, ent->s.origin, tv(0, 0, ent->viewheight), forward, right, shell->s.origin);
	P_ProjectSource2(ent->client, vec3_origin, offset, forward, right, up, view_offset);
	VectorAdd(shell->s.origin, view_offset, shell->s.origin);

	// Modify position by velocity to reduce visual discrepancy... max. horiz. velocity: 300, max. vert. velocity: ~1200.
	for (int i = 0; i < 3; i++)
		shell->s.origin[i] += 4 * (ent->velocity[i] / 300.0f);

	// Velocity
	VectorScale(forward, ((rand() % 17) + 8), shell->velocity);
	VectorMA(shell->velocity, 70 + (rand() % 48), right, shell->velocity);
	VectorAdd(shell->velocity, ent->velocity, shell->velocity);
	if (ent->groundentity) VectorAdd(shell->velocity, ent->groundentity->velocity, shell->velocity);
	shell->velocity[2] += 100 + (rand() % 28);
	shell->velocity[1] += 28 + (rand() % 9);

	// Angular velocity
	VectorSet(shell->avelocity, (rand() % 33) + 64, (rand() % 33) + 32, (rand() % 33) + 64);
	if (rand() % 2) shell->avelocity[0] *= -1;
	if (rand() % 2) shell->avelocity[1] *= -1;
	if (rand() % 2) shell->avelocity[2] *= -1;
}

/*
================
Weapon_Generic

A generic function to handle the basics of weapon thinking
================
*/
#define FRAME_FIRE_FIRST		(FRAME_ACTIVATE_LAST + 1)
#define FRAME_IDLE_FIRST		(FRAME_FIRE_LAST + 1)
#define FRAME_DEACTIVATE_FIRST	(FRAME_IDLE_LAST + 1)

//mxd. Added select sounds
void Weapon_Generic2(edict_t *ent, int FRAME_ACTIVATE_LAST, int FRAME_FIRE_LAST, int FRAME_IDLE_LAST, int FRAME_DEACTIVATE_LAST, int FRAME_SELECT_SOUND, char *PICKUP_SOUND, int *pause_frames, int *fire_frames, void(*fire)(edict_t *ent2, qboolean altfire))
{
	if (ent->deadflag || ent->s.modelindex != MAX_MODELS - 1)
		return; // VWep animations screw up corpses

// Weapon deselecting
	if (ent->client->weaponstate == WEAPON_DROPPING)
	{
		if (ent->client->ps.gunframe == FRAME_DEACTIVATE_LAST)
		{
			ChangeWeapon(ent);
			return;
		}

		if ((FRAME_DEACTIVATE_LAST - ent->client->ps.gunframe) == 4)
		{
			ent->client->anim_priority = ANIM_REVERSE;
			if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crpain4 + 1;
				ent->client->anim_end = FRAME_crpain1;
			}
			else
			{
				ent->s.frame = FRAME_pain304 + 1;
				ent->client->anim_end = FRAME_pain301;
			}
		}

		//ent->client->ps.gunframe++;
		ent->client->ps.gunframe = min(ent->client->ps.gunframe + 3, FRAME_DEACTIVATE_LAST); //mxd. 3x faster weapon switching
		return;
	}

// Weapon selecting
	if (ent->client->weaponstate == WEAPON_ACTIVATING)
	{
		if (ent->client->ps.gunframe == FRAME_ACTIVATE_LAST)
		{
			ent->client->weaponstate = WEAPON_READY;
			ent->client->ps.gunframe = FRAME_IDLE_FIRST;
			return;
		}

		//mxd. Weapon select sound
		if (ent->client->ps.gunframe == FRAME_SELECT_SOUND)
			gi.sound(ent, CHAN_VOICE, gi.soundindex(PICKUP_SOUND), 1, ATTN_NORM, 0);

		//ent->client->ps.gunframe++;
		ent->client->ps.gunframe = min(ent->client->ps.gunframe + 2, FRAME_ACTIVATE_LAST); //mxd. 2x faster weapon switching
		return;
	}

// Start weapon deselecting
	if (ent->client->newweapon && ent->client->weaponstate != WEAPON_FIRING)
	{
		ent->client->weaponstate = WEAPON_DROPPING;
		ent->client->ps.gunframe = FRAME_DEACTIVATE_FIRST;

		if (FRAME_DEACTIVATE_LAST - FRAME_DEACTIVATE_FIRST < 4)
		{
			ent->client->anim_priority = ANIM_REVERSE;
			if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crpain4+1;
				ent->client->anim_end = FRAME_crpain1;
			}
			else
			{
				ent->s.frame = FRAME_pain304+1;
				ent->client->anim_end = FRAME_pain301;
			}
		}

		return;
	}

// Weapon idle
	if (ent->client->weaponstate == WEAPON_READY)
	{
		if ((ent->client->latched_buttons | ent->client->buttons) & BUTTON_ATTACK) //mxd. BUTTONS_ATTACK -> BUTTON_ATTACK
		{
			if (!ent->client->ammo_index || ent->client->pers.inventory[ent->client->ammo_index] >= ent->client->pers.weapon->quantity)
			{
				ent->client->ps.gunframe = FRAME_FIRE_FIRST;
				ent->client->weaponstate = WEAPON_FIRING;

				// start the animation
				ent->client->anim_priority = ANIM_ATTACK;
				if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
				{
					ent->s.frame = FRAME_crattak1 - 1;
					ent->client->anim_end = FRAME_crattak9;
				}
				else
				{
					ent->s.frame = FRAME_attack1 - 1;
					ent->client->anim_end = FRAME_attack8;
				}
			}
			else
			{
				if (level.time >= ent->pain_debounce_time)
				{
					gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
					ent->pain_debounce_time = level.time + 1;
				}

				NoAmmoWeaponChange(ent);
			}

			ent->client->latched_buttons &= ~BUTTON_ATTACK; //mxd. BUTTONS_ATTACK -> BUTTON_ATTACK
		}
		else
		{
			if (ent->client->ps.gunframe == FRAME_IDLE_LAST)
			{
				ent->client->ps.gunframe = FRAME_IDLE_FIRST;
				return;
			}

			if (pause_frames)
			{
				for (int i = 0; pause_frames[i]; i++)
					if (ent->client->ps.gunframe == pause_frames[i] && rand() & 15)
						return;
			}

			ent->client->ps.gunframe++;
			return;
		}
	}

// Weapon firing
	if (ent->client->weaponstate == WEAPON_FIRING)
	{
		int i;
		for (i = 0; fire_frames[i]; i++)
		{
			if (ent->client->ps.gunframe == fire_frames[i])
			{
				if (!CTFApplyStrengthSound(ent) && ent->client->quad_framenum > level.framenum)
					gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);

				CTFApplyHasteSound(ent); //ZOID

				fire(ent, (ent->client->latched_buttons|ent->client->buttons) & BUTTON_ATTACK2);
				break;
			}
		}

		if (!fire_frames[i])
			ent->client->ps.gunframe++;

		if (ent->client->ps.gunframe == FRAME_IDLE_FIRST + 1)
			ent->client->weaponstate = WEAPON_READY;
	}
}


//ZOID
//mxd. Added select sounds
void Weapon_Generic(edict_t *ent, int FRAME_ACTIVATE_LAST, int FRAME_FIRE_LAST, int FRAME_IDLE_LAST, int FRAME_DEACTIVATE_LAST, int FRAME_SELECT_SOUND, char *PICKUP_SOUND, int *pause_frames, int *fire_frames, void (*fire)(edict_t *ent2, qboolean altfire))
{
	//mxd. Cock offhand grenade?
	if ((ent->client->latched_buttons | ent->client->buttons) & BUTTON_ATTACK2)
	{
		cock_offhand_grenade(ent);
		ent->client->latched_buttons &= ~BUTTON_ATTACK2;
	}

	//mxd. Time to throw offhand grenade?
	if (ent->client->grenade_blew_up && ent->client->grenade_time <= level.time)
	{
		throw_offhand_grenade(ent);
		ent->client->grenade_blew_up = false;
	}

	const int oldstate = ent->client->weaponstate;

	Weapon_Generic2 (ent, FRAME_ACTIVATE_LAST, FRAME_FIRE_LAST, 
		FRAME_IDLE_LAST, FRAME_DEACTIVATE_LAST, 
		FRAME_SELECT_SOUND, PICKUP_SOUND, //mxd
		pause_frames, fire_frames, fire);

	// run the weapon frame again if hasted
	if (Q_stricmp(ent->client->pers.weapon->pickup_name, "Grapple") == 0 &&
		ent->client->weaponstate == WEAPON_FIRING)
		return;

	if (oldstate == ent->client->weaponstate
		&& (CTFApplyHaste(ent) || (Q_stricmp(ent->client->pers.weapon->pickup_name, "Grapple") == 0 && ent->client->weaponstate != WEAPON_FIRING)))
	{
		Weapon_Generic2 (ent, FRAME_ACTIVATE_LAST, FRAME_FIRE_LAST, 
			FRAME_IDLE_LAST, FRAME_DEACTIVATE_LAST, 
			FRAME_SELECT_SOUND, PICKUP_SOUND, //mxd
			pause_frames, fire_frames, fire);
	}

	//mxd. Eject shotgun shells...
	if (Q_stricmp(ent->client->pers.weapon->classname, "weapon_shotgun") == 0 && ent->client->ps.gunframe == 14)
	{
		eject_shotgun_shell(ent, tv(12, 2, -3)); // +x - forward, +y - right
	}
	else if (Q_stricmp(ent->client->pers.weapon->classname, "weapon_supershotgun") == 0 && ent->client->ps.gunframe == 14)
	{
		eject_shotgun_shell(ent, tv(8, 5, -3));
		eject_shotgun_shell(ent, tv(8, 8, -3));
	}
}

/*
======================================================================
GRENADE
======================================================================
*/

#define GRENADE_TIMER			3.0
#define GRENADE_RETHROW_DELAY	1.0f //mxd
#define GRENADE_MINSPEED		250  //mxd. Was 400
#define GRENADE_MAXSPEED		500  //mxd. Was 800

// mxd. Offhand grenade logic...
void throw_offhand_grenade(edict_t *ent)
{
	vec3_t offset, forward, right, start, v_angle;
	int damage = sk_hand_grenade_damage->value;
	const float	radius = sk_hand_grenade_radius->value; //was damage + 40

	if (is_quad) 
		damage *= 4;

	VectorCopy(ent->client->v_angle, v_angle);
	v_angle[ROLL] = max(-88, v_angle[ROLL] - 15); // Offset vertical angle a bit
	AngleVectors(v_angle, forward, right, NULL);

	VectorSet(offset, 8, -8, ent->viewheight - 8); // +x - forward, +y - right
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	// Convert player camera angle to 0..1 range, 0 - looking 45 deg or less down, 1 - looking 45 deg or more up // ent->client->v_angle[PITCH]: 88 - max down, -88 - max. up, 0 - center
	const float v_mul = max(-45.0f, min(45.0f, ent->client->v_angle[PITCH])) / -90.0f + 0.5f;
	const int speed = GRENADE_MINSPEED + (GRENADE_MAXSPEED - GRENADE_MINSPEED) * v_mul;

	gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/hgrent1a.wav"), 1, ATTN_NORM, 0);
	fire_grenade2(ent, start, forward, damage, speed, GRENADE_TIMER, radius, false); // held == detonate in hand

	// Also push the view up-right a bit
	ent->client->v_dmg_pitch = -8 - random() * 6;
	ent->client->v_dmg_roll =  -4 - random() * 3;
	ent->client->v_dmg_time = level.time + DAMAGE_TIME;
}

//mxd
void cock_offhand_grenade(edict_t *ent)
{
	// Too early to rethrow?
	if (ent->client->grenade_time + GRENADE_RETHROW_DELAY > level.time)
		return;

	// Check ammo...
	const int item_index = ITEM_INDEX(FindItem("grenades"));
	if (ent->client->pers.inventory[item_index])
	{
		// Play cock sound...
		gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/hgrena1b.wav"), 1, ATTN_NORM, 0);

		// Schedule the throw...
		ent->client->grenade_time = level.time + FRAMETIME * 8.0f;

		// Reduce ammo count
		if (!((int)dmflags->value & DF_INFINITE_AMMO))
			ent->client->pers.inventory[item_index]--;

		// Hijack to indicate busy state
		ent->client->grenade_blew_up = true;

		// Also push the view down a bit
		ent->client->v_dmg_pitch = 3 + random() * 3;
		ent->client->v_dmg_time = level.time + FALL_TIME;
	}
	else if (level.time >= ent->pain_debounce_time)
	{
		// Play "no ammo" sound...
		gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
		ent->pain_debounce_time = level.time + 1;
	}
}


/*
======================================================================
GRENADE LAUNCHER
======================================================================
*/

void weapon_grenadelauncher_fire(edict_t *ent, qboolean altfire)
{
	vec3_t	offset, view_offset;
	vec3_t	forward, right, up;
	vec3_t	start;

	int damage = sk_grenade_damage->value;
	const float radius = sk_grenade_radius->value; // damage+40;

	if (is_quad)
		damage *= 4;

	AngleVectors(ent->client->v_angle, forward, right, up);

	//mxd. First project firing position without offsets...
	VectorSet(offset, 0, 0, ent->viewheight);
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	//mxd. Then project view offset only... Fixes incorrect projectile position when looking up/down...
	VectorSet(offset, 8, 7, -8); //was 8, 8, -8
	P_ProjectSource2(ent->client, vec3_origin, offset, forward, right, up, view_offset);
	VectorAdd(start, view_offset, start);

	VectorScale(forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	fire_grenade(ent, start, forward, damage, sk_grenade_speed->value, 2.5, radius, altfire);

	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte(MZ_GRENADE | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ent->client->ammo_index]--;
}

void Weapon_GrenadeLauncher(edict_t *ent)
{
	static int	pause_frames[]	= {34, 51, 59, 0};
	static int	fire_frames[]	= {6, 0};

	Weapon_Generic(ent, 5, 16, 59, 64, 2, "weapons/Grenlr1b.wav", pause_frames, fire_frames, weapon_grenadelauncher_fire); //mxd. Select sounds
}

/*
======================================================================
ROCKET
======================================================================
*/
edict_t	*rocket_target(edict_t *self, vec3_t start, vec3_t forward)
{
	vec3_t end;
	VectorMA(start, 8192, forward, end);

	// Check for aiming directly at a damageable entity
	trace_t tr = gi.trace(start, NULL, NULL, end, self, MASK_SHOT);
	if (tr.ent->takedamage != DAMAGE_NO && tr.ent->solid != SOLID_NOT)
		return tr.ent;

	// Check for damageable entity within a tolerance of view angle
	float bd = 0;
	edict_t *best = NULL;
	edict_t *who = g_edicts + 1;
	for (int i = 1; i < globals.num_edicts; i++, who++)
	{
		if (!who->inuse || who == self || who->takedamage == DAMAGE_NO || who->solid == SOLID_NOT)
			continue;

		VectorMA(who->absmin, 0.5, who->size, end);
		tr = gi.trace(start, vec3_origin, vec3_origin, end, self, MASK_OPAQUE);
		if (tr.fraction < 1.0)
			continue;

		vec3_t dir;
		VectorSubtract(end, self->s.origin, dir);
		VectorNormalize(dir);
		const float d = DotProduct(forward, dir);
		if (d > bd)
		{
			bd = d;
			best = who;
		}
	}

	if (bd > 0.9f)
		return best;

	return NULL;
}

void Weapon_RocketLauncher_Fire(edict_t *ent, qboolean altfire)
{
	vec3_t offset, start, view_offset;
	vec3_t forward, right, up;

	int damage = sk_rocket_damage->value + (int)(random() * sk_rocket_damage2->value);
	int radius_damage = sk_rocket_rdamage->value;
	const float damage_radius = sk_rocket_radius->value;
	
	if (is_quad)
	{
		damage *= 4;
		radius_damage *= 4;
	}

	AngleVectors(ent->client->v_angle, forward, right, up);

	VectorScale(forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	//mxd. First project firing position without offsets...
	VectorSet(offset, 0, 0, ent->viewheight); 
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	//mxd. Then project view offset only... Fixes incorrect projectile position when looking up/down...
	VectorSet(offset, 8, 4, -8); // was 8, 8, -8
	P_ProjectSource2(ent->client, vec3_origin, offset, forward, right, up, view_offset);
	VectorAdd(start, view_offset, start);
	
	// Knightmare- changed constant 650 for cvar sk_rocket_speed->value
	fire_rocket(ent, start, forward, damage, sk_rocket_speed->value, damage_radius, radius_damage, NULL);

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte(MZ_ROCKET | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ent->client->ammo_index]--;
}

void Weapon_RocketLauncher(edict_t *ent)
{
	static int pause_frames[] = { 25, 33, 42, 50, 0 };
	static int fire_frames[]  = { 5, 0 };

	Weapon_Generic(ent, 4, 12, 50, 54, 2, "weapons/Rocklr1b.wav", pause_frames, fire_frames, Weapon_RocketLauncher_Fire); //mxd. Select sounds
}


/*
======================================================================
BLASTER / HYPERBLASTER
======================================================================
*/

void Blaster_Fire(edict_t *ent, const vec3_t g_offset, int damage, qboolean hyper, int effect, int color)
{
	vec3_t	forward, right, up;
	vec3_t	start, offset, view_offset;
	int		muzzleflash;

	if (is_quad)
		damage *= 4;

	AngleVectors(ent->client->v_angle, forward, right, up);

	//mxd. First project firing position without offsets...
	VectorSet(offset, 0, 0, ent->viewheight); 
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	//mxd. Then project view offset only... Fixes incorrect projectile position when looking up/down...
	VectorSet(offset, 24, 8, -8);
	VectorAdd(offset, g_offset, offset);
	P_ProjectSource2(ent->client, vec3_origin, offset, forward, right, up, view_offset);
	VectorAdd(start, view_offset, start);

	VectorScale(forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	fire_blaster(ent, start, forward, damage, (hyper ? sk_hyperblaster_speed->value : sk_blaster_speed->value), effect, hyper, color);

	// Knightmare- select muzzle flash
	if (hyper)
	{
		if (color == BLASTER_GREEN)
	#ifdef KMQUAKE2_ENGINE_MOD
			muzzleflash = MZ_GREENHYPERBLASTER;
	#else
			muzzleflash = MZ_HYPERBLASTER;
	#endif
		else if (color == BLASTER_BLUE)
			muzzleflash = MZ_BLUEHYPERBLASTER;
	#ifdef KMQUAKE2_ENGINE_MOD
		else if (color == BLASTER_RED)
			muzzleflash = MZ_REDHYPERBLASTER;
	#endif
		else //standard orange
			muzzleflash = MZ_HYPERBLASTER;
	}
	else
	{
		if (color == BLASTER_GREEN)
			muzzleflash = MZ_BLASTER2;
		else if (color == BLASTER_BLUE)
	#ifdef KMQUAKE2_ENGINE_MOD
			muzzleflash = MZ_BLUEBLASTER;
	#else
			muzzleflash = MZ_BLASTER;
	#endif
	#ifdef KMQUAKE2_ENGINE_MOD
		else if (color == BLASTER_RED)
			muzzleflash = MZ_REDBLASTER;
	#endif
		else //standard orange
			muzzleflash = MZ_BLASTER;
	}

	
	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte(muzzleflash | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	PlayerNoise(ent, start, PNOISE_WEAPON);
}


void Weapon_Blaster_Fire(edict_t *ent, qboolean altfire)
{
	// select color
	int color = sk_blaster_color->value;

	// blaster_color could be any other value, so clamp it
	if (sk_blaster_color->value < 2 || sk_blaster_color->value > 4)
		color = BLASTER_ORANGE; 

	// CTF color override
	if (ctf->value && ctf_blastercolors->value && ent->client)
		color = 5-ent->client->resp.ctf_team;

#ifndef KMQUAKE2_ENGINE_MOD
	if (color == BLASTER_RED)
		color = BLASTER_ORANGE;
#endif

	int effect;
	if (color == BLASTER_GREEN)
		effect = (EF_BLASTER|EF_TRACKER);
	else if (color == BLASTER_BLUE)
#ifdef KMQUAKE2_ENGINE_MOD
		effect = EF_BLASTER|EF_BLUEHYPERBLASTER;
#else
		effect = EF_BLUEHYPERBLASTER;
#endif
	else if (color == BLASTER_RED)
		effect = EF_BLASTER|EF_IONRIPPER;
	else // standard orange
		effect = EF_BLASTER;

	const int damage = (deathmatch->value ? sk_blaster_damage_dm->value : sk_blaster_damage->value);
	Blaster_Fire(ent, tv(0, -1, 1), damage, false, effect, color);
	ent->client->ps.gunframe++;
}

void Weapon_Blaster(edict_t *ent)
{
	static int pause_frames[] = { 19, 32, 0 };
	static int fire_frames[]  = { 5, 0 };

	Weapon_Generic(ent, 4, 8, 52, 55, 2, "weapons/noammo.wav", pause_frames, fire_frames, Weapon_Blaster_Fire); //mxd. Select sounds
}


void Weapon_HyperBlaster_Fire(edict_t *ent, qboolean altfire)
{
	ent->client->weapon_sound = gi.soundindex("weapons/hyprbl1a.wav");

	if (!(ent->client->buttons & BUTTONS_ATTACK))
	{
		ent->client->ps.gunframe++;
	}
	else
	{
		if (!ent->client->pers.inventory[ent->client->ammo_index])
		{
			if (level.time >= ent->pain_debounce_time)
			{
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
				ent->pain_debounce_time = level.time + 1;
			}

			NoAmmoWeaponChange(ent);
		}
		else
		{
			const float rotation = (ent->client->ps.gunframe - 5) * 2 * M_PI / 6;
			vec3_t offset;
			offset[0] = -4 * sinf(rotation);
			offset[1] = 0;
			offset[2] = 4 * cosf(rotation);

			// Knightmare- select color
			int color = sk_hyperblaster_color->value;
			
			// hyperblaster_color could be any other value, so clamp this
			if (sk_hyperblaster_color->value < 2 || sk_hyperblaster_color->value > 4)
				color = BLASTER_ORANGE;
			
			// CTF color override
			if (ctf->value && ctf_blastercolors->value && ent->client)
				color = 5 - ent->client->resp.ctf_team;

		#ifndef KMQUAKE2_ENGINE_MOD
			if (color == BLASTER_RED)
				color = BLASTER_ORANGE;
		#endif

			int effect;
			if (ent->client->ps.gunframe == 6 || ent->client->ps.gunframe == 9)
			{
				if (color == BLASTER_GREEN)
					effect = (EF_HYPERBLASTER | EF_TRACKER);
				else if (color == BLASTER_BLUE)
					effect = EF_BLUEHYPERBLASTER;
				else if (color == BLASTER_RED)
					effect = EF_HYPERBLASTER | EF_IONRIPPER;
				else // standard orange
					effect = EF_HYPERBLASTER;
			}
			else
			{
				effect = 0;
			}

			const int damage = (deathmatch->value ? sk_hyperblaster_damage_dm->value : sk_hyperblaster_damage->value);
			Blaster_Fire(ent, offset, damage, true, effect, color);
			
			if (!((int)dmflags->value & DF_INFINITE_AMMO))
				ent->client->pers.inventory[ent->client->ammo_index]--;

			ent->client->anim_priority = ANIM_ATTACK;
			if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crattak1 - 1;
				ent->client->anim_end = FRAME_crattak9;
			}
			else
			{
				ent->s.frame = FRAME_attack1 - 1;
				ent->client->anim_end = FRAME_attack8;
			}
		}

		ent->client->ps.gunframe++;
		if (ent->client->ps.gunframe == 12 && ent->client->pers.inventory[ent->client->ammo_index])
			ent->client->ps.gunframe = 6;
	}

	if (ent->client->ps.gunframe == 12)
	{
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/hyprbd1a.wav"), 1, ATTN_NORM, 0);
		ent->client->weapon_sound = 0;
	}
}

void Weapon_HyperBlaster(edict_t *ent)
{
	static int pause_frames[] = { 0 };
	static int fire_frames[]  = { 6, 7, 8, 9, 10, 11, 0 };

	Weapon_Generic(ent, 5, 20, 49, 53, 2, "weapons/Hyprbu1a.wav", pause_frames, fire_frames, Weapon_HyperBlaster_Fire); //mxd. Select sounds
}

/*
======================================================================
MACHINEGUN / CHAINGUN
======================================================================
*/

void Machinegun_Fire(edict_t *ent, qboolean altfire)
{
	vec3_t		forward, right, up, start, view_offset;
	vec3_t		angles;
	vec3_t		offset;

	if (!(ent->client->buttons & BUTTONS_ATTACK))
	{
		ent->client->machinegun_shots = 0;
		ent->client->ps.gunframe++;

		return;
	}

	ent->client->ps.gunframe = (ent->client->ps.gunframe == 5 ? 4 : 5);

	if (ent->client->pers.inventory[ent->client->ammo_index] < 1)
	{
		ent->client->ps.gunframe = 6;
		if (level.time >= ent->pain_debounce_time)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
			ent->pain_debounce_time = level.time + 1;
		}

		NoAmmoWeaponChange(ent);
		return;
	}

	int damage = sk_machinegun_damage->value;
	int kick = 2;

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	for (int i = 1; i < 3; i++)
	{
		ent->client->kick_origin[i] = crandom() * 0.35;
		ent->client->kick_angles[i] = crandom() * 0.7;
	}

	ent->client->kick_origin[0] = crandom() * 0.35;
	ent->client->kick_angles[0] = ent->client->machinegun_shots * -1.5;

	// raise the gun as it is firing
	if (!deathmatch->value)
	{
		ent->client->machinegun_shots++;
		if (ent->client->machinegun_shots > 9)
			ent->client->machinegun_shots = 9;
	}

	// get start / end positions
	VectorAdd(ent->client->v_angle, ent->client->kick_angles, angles);
	AngleVectors(angles, forward, right, up);
	VectorSet(offset, 0, 0, ent->viewheight); //mxd. First project firing position without offsets...
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	//mxd. Then project view offset only... Fixes incorrect tracers position when looking up/down...
	VectorSet(offset, 24, 8, -8); // was 0, 8, -8
	P_ProjectSource2(ent->client, vec3_origin, offset, forward, right, up, view_offset);
	VectorAdd(start, view_offset, start);

	fire_bullet(ent, start, forward, damage, kick, sk_machinegun_hspread->value, sk_machinegun_vspread->value, MOD_MACHINEGUN);
	
	//mxd. Eject shell...
	eject_bullet_shell(ent, tv(13, 5, -6)); // +x - forward, +y - right 

	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte(MZ_MACHINEGUN | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index]--;

	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crattak1 - (int) (random()+0.25);
		ent->client->anim_end = FRAME_crattak9;
	}
	else
	{
		ent->s.frame = FRAME_attack1 - (int) (random()+0.25);
		ent->client->anim_end = FRAME_attack8;
	}
}

void Weapon_Machinegun(edict_t *ent)
{
	static int pause_frames[] = { 23, 45, 0 };
	static int fire_frames[]  = { 4, 5, 0 };

	Weapon_Generic(ent, 3, 5, 45, 49, 2, "weapons/HGRENT1A.WAV", pause_frames, fire_frames, Machinegun_Fire); //mxd. Select sounds
}

void Chaingun_Fire(edict_t *ent, qboolean altfire)
{
	int			shots;
	vec3_t		start;
	vec3_t		forward, right, up;
	vec3_t		offset;

	if (ent->client->ps.gunframe == 5)
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnu1a.wav"), 1, ATTN_IDLE, 0);

	if (ent->client->ps.gunframe == 14 && !(ent->client->buttons & BUTTONS_ATTACK))
	{
		ent->client->ps.gunframe = 32;
		ent->client->weapon_sound = 0;

		return;
	}

	if (ent->client->ps.gunframe == 21 && ent->client->buttons & BUTTONS_ATTACK && ent->client->pers.inventory[ent->client->ammo_index])
		ent->client->ps.gunframe = 15;
	else
		ent->client->ps.gunframe++;

	if (ent->client->ps.gunframe == 22)
	{
		ent->client->weapon_sound = 0;
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnd1a.wav"), 1, ATTN_IDLE, 0);
	}
	else
	{
		ent->client->weapon_sound = gi.soundindex("weapons/chngnl1a.wav");
	}

	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crattak1 - (ent->client->ps.gunframe & 1);
		ent->client->anim_end = FRAME_crattak9;
	}
	else
	{
		ent->s.frame = FRAME_attack1 - (ent->client->ps.gunframe & 1);
		ent->client->anim_end = FRAME_attack8;
	}

	if (ent->client->ps.gunframe <= 9)
		shots = 1;
	else if (ent->client->ps.gunframe <= 14)
		shots = (ent->client->buttons & BUTTONS_ATTACK ? 2 : 1);
	else
		shots = 3;

	if (ent->client->pers.inventory[ent->client->ammo_index] < shots)
		shots = ent->client->pers.inventory[ent->client->ammo_index];

	if (!shots)
	{
		if (level.time >= ent->pain_debounce_time)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
			ent->pain_debounce_time = level.time + 1;
		}

		NoAmmoWeaponChange(ent);
		return;
	}

	int damage = (deathmatch->value ? sk_chaingun_damage_dm->value : sk_chaingun_damage->value);
	int kick = 2;

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	for (int i = 0; i < 3; i++)
	{
		ent->client->kick_origin[i] = crandom() * 0.35;
		ent->client->kick_angles[i] = crandom() * 0.7;
	}

	for (int i = 0; i < shots; i++)
	{
		// get start / end positions
		AngleVectors(ent->client->v_angle, forward, right, up);
		const float r = 7 + crandom() * 4;
		const float u = crandom() * 4;
		VectorSet(offset, 0, r, u + ent->viewheight - 8);
		P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

		fire_bullet(ent, start, forward, damage, kick, sk_chaingun_hspread->value, sk_chaingun_vspread->value, MOD_CHAINGUN);
	}

	//mxd. Eject shell...
	eject_bullet_shell(ent, tv(8, 7, -5)); // +x - forward, +y - right 

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	//gi.WriteByte((MZ_CHAINGUN1 + shots - 1) | is_silenced);
	gi.WriteByte(MZ_CHAINGUN1 | is_silenced); //mxd. Don't play up to 3 firing sounds at once. Q2 N64 doesn't do that
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ent->client->ammo_index] -= shots;
}


void Weapon_Chaingun(edict_t *ent)
{
	static int pause_frames[] = { 38, 43, 51, 61, 0 };
	static int fire_frames[]  = { 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 0 };

	Weapon_Generic(ent, 4, 31, 61, 64, 2, "weapons/HGRENT1A.WAV", pause_frames, fire_frames, Chaingun_Fire); //mxd. Select sounds
}


/*
======================================================================
SHOTGUN / SUPERSHOTGUN
======================================================================
*/

void weapon_shotgun_fire(edict_t *ent, qboolean altfire)
{
	vec3_t		start;
	vec3_t		forward, right, up;
	vec3_t		offset, view_offset;

	if (ent->client->ps.gunframe == 9)
	{
		ent->client->ps.gunframe++;
		return;
	}

	AngleVectors(ent->client->v_angle, forward, right, up);

	VectorScale(forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -2;

	//mxd. First project firing position without offsets...
	VectorSet(offset, 0, 0, ent->viewheight); 
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	//mxd. Then project view offset only... Fixes incorrect tracers position when looking up/down...
	VectorSet(offset, 16, 8, -8); // was 0, 8, -8
	P_ProjectSource2(ent->client, vec3_origin, offset, forward, right, up, view_offset);
	VectorAdd(start, view_offset, start);

	int damage = sk_shotgun_damage->value;
	int kick = 8;

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	fire_shotgun(ent, start, forward, damage, kick, sk_shotgun_hspread->value, sk_shotgun_vspread->value, sk_shotgun_count->value, MOD_SHOTGUN); //mxd

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte(MZ_SHOTGUN | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ent->client->ammo_index]--;
}

void Weapon_Shotgun(edict_t *ent)
{
	static int pause_frames[] = { 22, 28, 34, 0 };
	static int fire_frames[]  = { 8, 9, 0 };

	Weapon_Generic (ent, 7, 18, 36, 39, 2, "weapons/shotgr1b.wav", pause_frames, fire_frames, weapon_shotgun_fire); //mxd. Select sounds
}


void weapon_supershotgun_fire(edict_t *ent, qboolean altfire)
{
	vec3_t		start;
	vec3_t		forward, right, up;
	vec3_t		offset, view_offset;
	vec3_t		v;
	
	AngleVectors(ent->client->v_angle, forward, right, up);
	VectorScale(forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -2;

	//mxd. First project firing position without offsets...
	VectorSet(offset, 0, 0, ent->viewheight); 
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	//mxd. Then project view offset only... Fixes incorrect tracers position when looking up/down...
	VectorSet(offset, 16, 8, -5); // was 0, 8, -8
	P_ProjectSource2(ent->client, vec3_origin, offset, forward, right, up, view_offset);
	VectorAdd(start, view_offset, start);

	int damage = sk_sshotgun_damage->value;
	int kick = 12;

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	v[PITCH] = ent->client->v_angle[PITCH];
	v[YAW]   = ent->client->v_angle[YAW] - 5;
	v[ROLL]  = ent->client->v_angle[ROLL];
	AngleVectors(v, forward, NULL, NULL);
	fire_shotgun(ent, start, forward, damage, kick, sk_sshotgun_hspread->value, sk_sshotgun_vspread->value, sk_sshotgun_count->value/2, MOD_SSHOTGUN);

	v[YAW]   = ent->client->v_angle[YAW] + 5;
	AngleVectors(v, forward, NULL, NULL);
	fire_shotgun(ent, start, forward, damage, kick, sk_sshotgun_hspread->value, sk_sshotgun_vspread->value, sk_sshotgun_count->value/2, MOD_SSHOTGUN);

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte(MZ_SSHOTGUN | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ent->client->ammo_index] -= 2;
}

void Weapon_SuperShotgun(edict_t *ent)
{
	static int pause_frames[] = { 29, 42, 57, 0 };
	static int fire_frames[]  = { 7, 0 };

	Weapon_Generic(ent, 6, 17, 57, 61, 2, "weapons/sshotr1b.wav", pause_frames, fire_frames, weapon_supershotgun_fire); //mxd. Select sounds
}


/*
======================================================================
RAILGUN
======================================================================
*/

void weapon_railgun_fire(edict_t *ent, qboolean altfire)
{
	vec3_t		start;
	vec3_t		forward, right, up;
	vec3_t		offset, view_offset;
	int			damage;
	int			kick;

	if (deathmatch->value)
	{
		// normal damage is too extreme in dm
		damage = sk_railgun_damage_dm->value;
		kick = 200;
	}
	else
	{
		damage = sk_railgun_damage->value;
		kick = 250;
	}

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	AngleVectors(ent->client->v_angle, forward, right, up);
	VectorScale(forward, -3, ent->client->kick_origin);
	ent->client->kick_angles[0] = -3;

	//mxd. First project firing position without offsets...
	VectorSet(offset, 0, 0, ent->viewheight);
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	//mxd. Then project view offset only... Fixes incorrect projectile position when looking up/down...
	VectorSet(offset, 0, 7, -6); // was 0, 7, -8
	P_ProjectSource2(ent->client, vec3_origin, offset, forward, right, up, view_offset);
	VectorAdd(start, view_offset, start);
	
	fire_rail(ent, start, forward, damage, kick);

	// send muzzle flash
	gi.WriteByte(svc_muzzleflash);
	gi.WriteShort(ent - g_edicts);
	gi.WriteByte(MZ_RAILGUN | is_silenced);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO))
		ent->client->pers.inventory[ent->client->ammo_index]--;
}


void Weapon_Railgun(edict_t *ent)
{
	static int pause_frames[] = { 56, 0 };
	static int fire_frames[]  = { 4, 0 };

	Weapon_Generic(ent, 3, 18, 56, 61, 0, "weapons/RAILGR1A.WAV", pause_frames, fire_frames, weapon_railgun_fire); //mxd. Select sounds
}


/*
======================================================================
BFG10K
======================================================================
*/

void weapon_bfg_fire(edict_t *ent, qboolean altfire)
{
	vec3_t	offset, start;
	vec3_t	forward, right;
	
	int damage = (deathmatch->value ? sk_bfg_damage_dm->value : sk_bfg_damage->value);
	const float damage_radius = sk_bfg_radius->value;

	if (ent->client->ps.gunframe == 9)
	{
		// send muzzle flash
		gi.WriteByte(svc_muzzleflash);
		gi.WriteShort(ent - g_edicts);
		gi.WriteByte(MZ_BFG | is_silenced);
		gi.multicast(ent->s.origin, MULTICAST_PVS);

		ent->client->ps.gunframe++;

		PlayerNoise(ent, start, PNOISE_WEAPON);
		return;
	}

	// cells can go down during windup (from power armor hits), so check again and abort firing if we don't have enough now
	if (ent->client->pers.inventory[ent->client->ammo_index] < 50)
	{
		ent->client->ps.gunframe++;
		return;
	}

	if (is_quad)
		damage *= 4;

	AngleVectors(ent->client->v_angle, forward, right, NULL);

	VectorScale(forward, -2, ent->client->kick_origin);

	// make a big pitch kick with an inverse fall
	ent->client->v_dmg_pitch = -40;
	ent->client->v_dmg_roll = crandom() * 8;
	ent->client->v_dmg_time = level.time + DAMAGE_TIME;

	VectorSet(offset, 8, 8, ent->viewheight - 8);
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);
	fire_bfg(ent, start, forward, damage, sk_bfg_speed->value, damage_radius);

	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (!((int)dmflags->value & DF_INFINITE_AMMO) )
		ent->client->pers.inventory[ent->client->ammo_index] -= 50;
}

void Weapon_BFG(edict_t *ent)
{
	static int pause_frames[] = { 39, 45, 50, 55, 0 };
	static int fire_frames[]  = { 9, 17, 0 };

	Weapon_Generic(ent, 8, 32, 55, 58, 2, "weapons/bfg_select.wav", pause_frames, fire_frames, weapon_bfg_fire); //mxd. Select sounds
}

//======================================================================
void Weapon_Null(edict_t *ent)
{
	if (ent->client->newweapon)
		ChangeWeapon(ent);
}
//======================================================================
qboolean Pickup_Health(edict_t *ent, edict_t *other);

void kick_attack(edict_t *ent)
{
	vec3_t start, end;
	vec3_t forward, right;
	vec3_t offset;

	int damage = sk_jump_kick_damage->value;
	int kick = 300;

	if (ent->client->quad_framenum > level.framenum)
	{
		damage *= 4;
		kick *= 4;
	}

	AngleVectors(ent->client->v_angle, forward, right, NULL);
	VectorClear(ent->client->kick_origin);
	
	VectorSet(offset, 0, 0, ent->viewheight - 28); //mxd. was viewheight-20
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);
	
	VectorMA(start, 28, forward, end); //mxd. was 25

	//mxd. It's really hard to kick even larger monsers (like gunner or berserk), because player just jumps over them,
	// so try several traces with different vertical offsets...
	end[2] -= 25;
	for (int i = 0; i < 3; i++)
	{
		end[2] += 25;
		trace_t tr = gi.trace(ent->s.origin, NULL, NULL, end, ent, MASK_SHOT);

		// don't need to check for water
		if ((tr.surface && tr.surface->flags & SURF_SKY) || tr.fraction >= 1.0f || !tr.ent->takedamage || tr.ent->health <= 0)
			continue;

		//Knightmare- don't jump kick exploboxes or pushable crates, or insanes, or ambient models
		if (!strcmp(tr.ent->classname, "misc_explobox") || !strcmp(tr.ent->classname, "func_pushable") || !strcmp(tr.ent->classname, "model_spawn")
			|| !strcmp(tr.ent->classname, "model_train") || !strcmp(tr.ent->classname, "misc_insane"))
			continue;

		//also don't jumpkick actors, unless they're bad guys
		if (!strcmp(tr.ent->classname, "misc_actor") && (tr.ent->monsterinfo.aiflags & AI_GOOD_GUY))
			continue;

		//nor goodguy monsters
		if (strstr(tr.ent->classname, "monster_") && tr.ent->monsterinfo.aiflags & AI_GOOD_GUY)
			continue;

		//nor shootable items
		if (tr.ent->item && !strstr(tr.ent->classname, "monster_") && (strstr(tr.ent->classname, "ammo_") || strstr(tr.ent->classname, "weapon_")
			|| strstr(tr.ent->classname, "item_") || strstr(tr.ent->classname, "key_") || tr.ent->item->pickup == Pickup_Health))
			continue;

		if (tr.ent != ent && ((deathmatch->value && ((int)(dmflags->value) & (DF_MODELTEAMS | DF_SKINTEAMS))) || coop->value) && OnSameTeam(tr.ent, ent))
			continue;

		//mxd. Push player back a bit
		vec3_t dir = { forward[0] * -1, forward[1] * -1, forward[2] };
		VectorMA(ent->velocity, kick, dir, ent->velocity);

		// Also push the view up a bit
		ent->client->kick_angles[0] -= 6 + random() * 4;

		// Do damage and SFX
		T_Damage(tr.ent, ent, ent, forward, tr.endpos, tr.plane.normal, damage, kick, 0, MOD_KICK);
		gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/kick.wav"), 1, ATTN_NORM, 0);
		PlayerNoise(ent, ent->s.origin, PNOISE_SELF);
		ent->client->jumping = 0; // only 1 jumpkick per jump

		//mxd. Tweak velocity a bit...
		const float mul = kick / (float)tr.ent->mass;
		const float hmul = min(1.0f, mul * 0.5f);
		tr.ent->velocity[0] *= hmul;
		tr.ent->velocity[1] *= hmul;
		tr.ent->velocity[2] += max(mul * 120.0f, 120.0f) + rand() % 31;

		//mxd. Break the loop...
		break;
	}
}
