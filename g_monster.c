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

#include "g_local.h"

void InitiallyDead(edict_t *self);

// Lazarus: If worldspawn CORPSE_SINK effects flag is set, monsters/actors fade out and sink into the floor 30 seconds after death

#define SINKAMT			1
void FadeSink(edict_t *ent)
{
	ent->count++;
	ent->s.origin[2] -= SINKAMT;
	ent->think = FadeSink;

	if (ent->count == 5)
	{
		ent->s.renderfx &= ~RF_TRANSLUCENT;
		ent->s.effects |= EF_SPHERETRANS;
	}

	if (ent->count == 10)
		ent->think = G_FreeEdict;

	ent->nextthink = level.time + FRAMETIME;
}

void FadeDieSink(edict_t *ent)
{
	ent->takedamage = DAMAGE_NO;	// can't gib 'em once they start sinking
	ent->s.effects &= ~EF_FLIES;
	ent->s.sound = 0;
	ent->s.origin[2] -= SINKAMT;
	ent->s.renderfx = RF_TRANSLUCENT;
	ent->think = FadeSink;
	ent->nextthink = level.time + FRAMETIME;
	ent->count = 0;
}


// Lazarus: M_SetDeath is used to restore the death movement,
//          bounding box, and a few other parameters for dead
//          monsters that change levels with a trigger_transition

qboolean M_SetDeath(edict_t *self, mmove_t **deathmoves)
{
	mmove_t	*move = NULL;

	if (self->health > 0)
		return false;

	while (*deathmoves && !move)
	{
		mmove_t *dmove = *deathmoves;
		if ( (self->s.frame >= dmove->firstframe) &&
			(self->s.frame <= dmove->lastframe)     )
			move = dmove;
		else
			deathmoves++;
	}
	if (move)
	{
		self->monsterinfo.currentmove = move;
		if (self->monsterinfo.currentmove->endfunc)
			self->monsterinfo.currentmove->endfunc(self);
		self->s.frame = move->lastframe;
		self->s.skinnum |= 1;
		return true;
	}
	return false;
}

//
// mxd. Monster shell casings...
//

void monster_eject_bullet_shell(edict_t *ent, vec3_t offset)
{
	if (deathmatch->value || !m64_spawn_casings->value) return;

	edict_t	*shell = ThrowGib(ent, "models/weapons/shells/bullet/tris.md3", 0, GIB_BULLET_SHELL);
	if (!shell) return;

	// Bounding box
	VectorSet(shell->mins, -4, -4, -2);
	VectorSet(shell->maxs, 4, 4, 4);

	vec3_t forward, right, angles;
	VectorCopy(ent->s.angles, angles);
	AngleVectors(angles, forward, right, NULL);

	// Angles
	shell->s.angles[YAW] = angles[YAW];
	shell->s.angles[PITCH] = angles[PITCH];

	// Position
	G_ProjectSource(ent->s.origin, offset, forward, right, shell->s.origin);

	// Velocity
	VectorScale(forward, ((rand() & 63) - 64), shell->velocity);
	VectorMA(shell->velocity, 60 + (rand() & 28), right, shell->velocity);
	VectorAdd(shell->velocity, ent->velocity, shell->velocity);
	if (ent->groundentity) VectorAdd(shell->velocity, ent->groundentity->velocity, shell->velocity);
	shell->velocity[2] += 100 + (rand() & 31);
}

void monster_eject_shotgun_shell(edict_t *ent, vec3_t offset)
{
	if (deathmatch->value || !m64_spawn_casings->value) return;

	edict_t	*shell = ThrowGib(ent, "models/weapons/shells/shell/tris.md3", 0, GIB_SHOTGUN_SHELL);
	if (!shell) return;

	// Bounding box
	VectorSet(shell->mins, -4, -4, -2);
	VectorSet(shell->maxs, 4, 4, 4);

	vec3_t forward, right, angles;
	VectorCopy(ent->s.angles, angles);
	AngleVectors(angles, forward, right, NULL);

	// Angles
	shell->s.angles[YAW] = angles[YAW];
	shell->s.angles[PITCH] = angles[PITCH];

	// Position
	G_ProjectSource(ent->s.origin, offset, forward, right, shell->s.origin);

	// Velocity
	VectorScale(forward, ((rand() & 31) - 32), shell->velocity);
	VectorMA(shell->velocity, 62 + (rand() & 28), right, shell->velocity);
	VectorAdd(shell->velocity, ent->velocity, shell->velocity);
	if (ent->groundentity) VectorAdd(shell->velocity, ent->groundentity->velocity, shell->velocity);
	shell->velocity[2] += 100 + (rand() & 28);
}

//
// monster weapons
//

//FIXME monsters should call these with a totally accurate direction
// and we can mess it up based on skill.  Spread should be for normal
// and we can tighten or loosen based on skill.  We could muck with
// the damages too, but I'm not sure that's such a good idea.
void monster_fire_bullet(edict_t *self, vec3_t start, vec3_t dir, int damage, int kick, int hspread, int vspread, int flashtype)
{
	fire_bullet(self, start, dir, damage, kick, hspread, vspread, MOD_UNKNOWN);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}

void monster_fire_shotgun(edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int hspread, int vspread, int count, int flashtype)
{
	fire_shotgun (self, start, aimdir, damage, kick, hspread, vspread, count, MOD_UNKNOWN);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}

void monster_fire_blaster(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int flashtype, int effect, int color)
{
	fire_blaster(self, start, dir, damage, speed, effect, false, color);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}	

void monster_fire_grenade(edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, int flashtype)
{
	fire_grenade(self, start, aimdir, damage, speed, 2.5, damage+40, false);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}

void monster_fire_rocket(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int flashtype, edict_t *homing_target)
{
	fire_rocket(self, start, dir, damage, speed, damage+20, damage, homing_target);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}	

void monster_fire_railgun(edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int flashtype)
{
	fire_rail(self, start, aimdir, damage, kick);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}

void monster_fire_bfg(edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, int kick, float damage_radius, int flashtype)
{
	fire_bfg(self, start, aimdir, damage, speed, damage_radius);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}



//
// Monster utility functions
//

void M_FliesOff(edict_t *self)
{
	self->s.effects &= ~EF_FLIES;
	self->s.sound = 0;
}

void M_FliesOn(edict_t *self)
{
	if (self->waterlevel)
		return;
	self->s.effects |= EF_FLIES;
	self->s.sound = gi.soundindex("infantry/inflies1.wav");
	self->think = M_FliesOff;
	self->nextthink = level.time + 60;
}

void M_FlyCheck(edict_t *self)
{
	//Knightmare- keep running lava check
	self->postthink = deadmonster_think;

	if (self->monsterinfo.flies > 1.0)
	{
		// should ALREADY have flies
		self->think = M_FliesOff;
		self->nextthink = level.time + 60;
		return;
	}

	if (self->waterlevel)
		return;

	if (random() > self->monsterinfo.flies)
		return;

	if (world->effects & FX_WORLDSPAWN_CORPSEFADE)
		return;

	self->think = M_FliesOn;
	self->nextthink = level.time + 5 + 10 * random();
}

void AttackFinished(edict_t *self, float time)
{
	self->monsterinfo.attack_finished = level.time + time;
}

//mxd
void AdjustAccuracy(edict_t *self, vec3_t target)
{
	// Lazarus fog reduction of accuracy
	if (self->monsterinfo.visibility < FOG_CANSEEGOOD)
	{
		target[0] += crandom() * 640 * (FOG_CANSEEGOOD - self->monsterinfo.visibility);
		target[1] += crandom() * 640 * (FOG_CANSEEGOOD - self->monsterinfo.visibility);
		target[2] += crandom() * 320 * (FOG_CANSEEGOOD - self->monsterinfo.visibility);
	}

	// Make enemies miss invisible players
	if (self->enemy->flags & FL_INVISIBLE)
	{
		vec3_t v;
		VectorSubtract(target, self->s.origin, v);
		const float missmultiplier = min(VectorLength(v), 512.0f) / 512.0f; // 0..1

		target[0] += crandom() * 128 * missmultiplier;
		target[1] += crandom() * 128 * missmultiplier;
		target[2] += crandom() * 64  * missmultiplier;
	}
}

void M_CheckGround(edict_t *ent)
{
	if (level.time < ent->gravity_debounce_time)
		return;

	if (ent->flags & (FL_SWIM|FL_FLY))
		return;

	if (ent->velocity[2] > 100)
	{
		ent->groundentity = NULL;
		return;
	}

// if the hull point one-quarter unit down is solid the entity is on ground
	vec3_t point;
	point[0] = ent->s.origin[0];
	point[1] = ent->s.origin[1];
	point[2] = ent->s.origin[2] - 0.25;

	const trace_t trace = gi.trace(ent->s.origin, ent->mins, ent->maxs, point, ent, MASK_MONSTERSOLID);

	// check steepness
	if (trace.plane.normal[2] < 0.7 && !trace.startsolid)
	{
		ent->groundentity = NULL;
		return;
	}

	// Lazarus: The following 2 lines were in the original code and commented out
	//          by id. However, the effect of this is that a player walking over
	//          a dead monster who is laying on a brush model will cause the 
	//          dead monster to drop through the brush model. This change *may*
	//          have other consequences, though, so watch out for this.

	ent->groundentity = trace.ent;
	ent->groundentity_linkcount = trace.ent->linkcount;

//	if (!trace.startsolid && !trace.allsolid)
//		VectorCopy(trace.endpos, ent->s.origin);

	if (!trace.startsolid && !trace.allsolid)
	{
		VectorCopy(trace.endpos, ent->s.origin);
		//ent->groundentity = trace.ent; //mxd. Already assigned above
		//ent->groundentity_linkcount = trace.ent->linkcount; //mxd. Already assigned above
//		ent->velocity[2] = 0; Lazarus: what if the groundentity is moving?
		ent->velocity[2] = trace.ent->velocity[2];
	}
}


void M_CatagorizePosition(edict_t *ent)
{
	vec3_t point;

//
// get waterlevel
//
// Lazarus... more broken code because of origin being screwed up
//	point[0] = ent->s.origin[0];
//	point[1] = ent->s.origin[1];
//	point[2] = ent->s.origin[2] + ent->mins[2] + 1;	
	point[0] = (ent->absmax[0] + ent->absmin[0]) / 2;
	point[1] = (ent->absmax[1] + ent->absmin[1]) / 2;
	point[2] =  ent->absmin[2] + 2;

	int cont = gi.pointcontents(point);

	if (!(cont & MASK_WATER))
	{
		ent->waterlevel = 0;
		ent->watertype = 0;
		return;
	}

	ent->watertype = cont;
	ent->waterlevel = 1;
	point[2] += 26;
	cont = gi.pointcontents(point);
	if (!(cont & MASK_WATER))
		return;

	ent->waterlevel = 2;
	point[2] += 22;
	cont = gi.pointcontents(point);
	if (cont & MASK_WATER)
		ent->waterlevel = 3;
}


void M_WorldEffects(edict_t *ent)
{
	int		dmg;

	if (ent->health > 0)
	{
		if (!(ent->flags & FL_SWIM))
		{
			if (ent->waterlevel < 3)
			{
				ent->air_finished = level.time + 12;
			}
			else if (ent->air_finished < level.time)
			{	// drown!
				if (ent->pain_debounce_time < level.time)
				{
					dmg = 2 + 2 * floorf(level.time - ent->air_finished);
					if (dmg > 15)
						dmg = 15;
					T_Damage(ent, world, world, vec3_origin, ent->s.origin, vec3_origin, dmg, 0, DAMAGE_NO_ARMOR, MOD_WATER);
					ent->pain_debounce_time = level.time + 1;
				}
			}
		}
		else
		{
			if (ent->waterlevel > 0)
			{
				ent->air_finished = level.time + 9;
			}
			else if (ent->air_finished < level.time)
			{	// suffocate!
				if (ent->pain_debounce_time < level.time)
				{
					dmg = 2 + 2 * floorf(level.time - ent->air_finished);
					if (dmg > 15)
						dmg = 15;
					T_Damage(ent, world, world, vec3_origin, ent->s.origin, vec3_origin, dmg, 0, DAMAGE_NO_ARMOR, MOD_WATER);
					ent->pain_debounce_time = level.time + 1;
				}
			}
		}
	}
	
	if (ent->waterlevel == 0)
	{
		if (ent->flags & FL_INWATER)
		{	
			if (ent->watertype & CONTENTS_MUD)
				gi.sound(ent, CHAN_BODY, gi.soundindex("mud/mud_out1.wav"), 1, ATTN_NORM, 0);
			else
				gi.sound(ent, CHAN_BODY, gi.soundindex("player/watr_out.wav"), 1, ATTN_NORM, 0);
			ent->flags &= ~FL_INWATER;
		}
		return;
	}

	if ((ent->watertype & CONTENTS_LAVA) && !(ent->flags & FL_IMMUNE_LAVA))
	{
		if (ent->damage_debounce_time < level.time)
		{
			ent->damage_debounce_time = level.time + 0.2;
			T_Damage(ent, world, world, vec3_origin, ent->s.origin, vec3_origin, 10*ent->waterlevel, 0, 0, MOD_LAVA);
		}
	}
	// slime doesn't damage dead monsters
	if ((ent->watertype & CONTENTS_SLIME) && !(ent->flags & FL_IMMUNE_SLIME) && !(ent->svflags & SVF_DEADMONSTER))
	{
		if (ent->damage_debounce_time < level.time)
		{
			ent->damage_debounce_time = level.time + 1;
			T_Damage(ent, world, world, vec3_origin, ent->s.origin, vec3_origin, 4*ent->waterlevel, 0, 0, MOD_SLIME);
		}
	}
	
	if ( !(ent->flags & FL_INWATER) )
	{	
		if (!(ent->svflags & SVF_DEADMONSTER))
		{
			if (ent->watertype & CONTENTS_LAVA)
				if (random() <= 0.5)
					gi.sound(ent, CHAN_BODY, gi.soundindex("player/lava1.wav"), 1, ATTN_NORM, 0);
				else
					gi.sound(ent, CHAN_BODY, gi.soundindex("player/lava2.wav"), 1, ATTN_NORM, 0);
			else if (ent->watertype & CONTENTS_SLIME)
				gi.sound(ent, CHAN_BODY, gi.soundindex("player/watr_in.wav"), 1, ATTN_NORM, 0);
			else if (ent->watertype & CONTENTS_MUD)
				gi.sound(ent, CHAN_BODY, gi.soundindex("mud/mud_in2.wav"), 1, ATTN_NORM, 0);
			else if (ent->watertype & CONTENTS_WATER)
				gi.sound(ent, CHAN_BODY, gi.soundindex("player/watr_in.wav"), 1, ATTN_NORM, 0);
		}

		ent->flags |= FL_INWATER;
		ent->old_watertype = ent->watertype;
		ent->damage_debounce_time = 0;
	}
}


void M_droptofloor(edict_t *ent)
{
	vec3_t		end;

	ent->s.origin[2] += 1;
	VectorCopy(ent->s.origin, end);
	end[2] -= 256;

	const trace_t trace = gi.trace(ent->s.origin, ent->mins, ent->maxs, end, ent, MASK_MONSTERSOLID);

	if (trace.fraction == 1 || trace.allsolid)
		return;

	VectorCopy(trace.endpos, ent->s.origin);

	gi.linkentity(ent);
	M_CheckGround(ent);
	M_CatagorizePosition(ent);
}


void M_SetEffects(edict_t *ent)
{
	ent->s.effects &= ~(EF_COLOR_SHELL|EF_POWERSCREEN);
	ent->s.renderfx &= ~(RF_SHELL_RED|RF_SHELL_GREEN|RF_SHELL_BLUE);

	if (ent->monsterinfo.aiflags & AI_RESURRECTING)
	{
		ent->s.effects |= EF_COLOR_SHELL;
		ent->s.renderfx |= RF_SHELL_RED;
	}

	if (ent->health <= 0)
		return;

	if (ent->powerarmor_time > level.time)
	{
		if (ent->monsterinfo.power_armor_type == POWER_ARMOR_SCREEN)
		{
			ent->s.effects |= EF_POWERSCREEN;
		}
		else if (ent->monsterinfo.power_armor_type == POWER_ARMOR_SHIELD)
		{
			ent->s.effects |= EF_COLOR_SHELL;
			ent->s.renderfx |= RF_SHELL_GREEN;
		}
	}
}


void M_MoveFrame(edict_t *self)
{
	// Lazarus: For live monsters weaker than gladiator who aren't already running from
	//          something, evade live grenades on the ground.
	if ((self->health > 0) && (self->max_health < 400) && !(self->monsterinfo.aiflags & AI_CHASE_THING) && self->monsterinfo.run)
		Grenade_Evade(self);

	mmove_t *move = self->monsterinfo.currentmove;
	self->nextthink = level.time + FRAMETIME;

	if ((self->monsterinfo.nextframe) && (self->monsterinfo.nextframe >= move->firstframe) && (self->monsterinfo.nextframe <= move->lastframe))
	{
		self->s.frame = self->monsterinfo.nextframe;
		self->monsterinfo.nextframe = 0;
	}
	else
	{
		if (self->s.frame == move->lastframe)
		{
			if (move->endfunc)
			{
				move->endfunc (self);

				// regrab move, endfunc is very likely to change it
				move = self->monsterinfo.currentmove;

				// check for death
				if (self->svflags & SVF_DEADMONSTER)
					return;
			}
		}

		if (self->s.frame < move->firstframe || self->s.frame > move->lastframe)
		{
			self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
			self->s.frame = move->firstframe;
		}
		else if (!(self->monsterinfo.aiflags & AI_HOLD_FRAME))
		{
			self->s.frame++;
			if (self->s.frame > move->lastframe)
				self->s.frame = move->firstframe;
		}
	}

	const int index = self->s.frame - move->firstframe;
	if (move->frame[index].aifunc)
	{
		if (!(self->monsterinfo.aiflags & AI_HOLD_FRAME))
			move->frame[index].aifunc(self, move->frame[index].dist * self->monsterinfo.scale);
		else
			move->frame[index].aifunc(self, 0);
	}

	if (move->frame[index].thinkfunc)
		move->frame[index].thinkfunc (self);
}


void monster_think(edict_t *self)
{
	M_MoveFrame(self);
	if (self->linkcount != self->monsterinfo.linkcount)
	{
		self->monsterinfo.linkcount = self->linkcount;
		M_CheckGround(self);
	}
	M_CatagorizePosition(self);
	M_WorldEffects (self);
	M_SetEffects (self);
}

// Knightmare- for dead monsters to check
// if they've fallen into lava, etc.
void deadmonster_think(edict_t *self)
{
	M_CatagorizePosition(self);
	M_WorldEffects (self);
	M_SetEffects (self);
}

//mxd. Translates position to world space...
void PositionToWorld(edict_t *self, vec3_t localpos, vec3_t result)
{
	vec3_t forward, right;
	AngleVectors(self->s.angles, forward, right, NULL);
	G_ProjectSource(self->s.origin, localpos, forward, right, result);
}

//mxd. Rotates normal to match model angles...
void NormalToWorld(edict_t *self, vec3_t localnormal, vec3_t result)
{
	vec3_t angles, axis[3];

	VectorCopy(self->s.angles, angles);
	angles[1] *= -1; // I have no clue why this is needed...
	AnglesToAxis(angles, axis);
	VectorRotate(localnormal, axis, result);
}

//mxd. Only target_effect_sparks-compatible effects are actually supported...
void M_SpawnEffect(edict_t *self, int effect, vec3_t localpos, vec3_t localnormal)
{
	vec3_t pos, normal;

	// Translate spawn position to world space...
	PositionToWorld(self, localpos, pos);

	// Rotate normal to match model angles...
	NormalToWorld(self, localnormal, normal);

	// Spawn blood
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(effect);
	gi.WritePosition(pos);
	if (effect != TE_CHAINFIST_SMOKE)
		gi.WriteDir(normal);
	gi.multicast(pos, MULTICAST_PVS);
}


/*
================
monster_use

Using a monster makes it angry at the current activator
================
*/
void monster_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (self->enemy)
		return;
	if (self->health <= 0)
		return;
	if (activator->flags & FL_NOTARGET)
		return;
	if (!(activator->client) && !(activator->monsterinfo.aiflags & AI_GOOD_GUY))
		return;
	if (activator->flags & FL_DISGUISED)
		return;

	// if monster is "used" by player, turn off good guy stuff
	if (activator->client)
	{
		self->spawnflags &= ~SF_MONSTER_GOODGUY;
		self->monsterinfo.aiflags &= ~(AI_GOOD_GUY + AI_FOLLOW_LEADER);
		if (self->dmgteam && !Q_stricmp(self->dmgteam,"player"))
			self->dmgteam = NULL;
	}

// delay reaction so if the monster is teleported, its sound is still heard
	self->enemy = activator;
	FoundTarget(self);
}


void monster_start_go(edict_t *self);


void monster_triggered_spawn(edict_t *self)
{
	self->s.origin[2] += 1;
	KillBox(self);

	self->solid = SOLID_BBOX;
	self->movetype = MOVETYPE_STEP;
	self->svflags &= ~SVF_NOCLIENT;
	self->air_finished = level.time + 12;
	gi.linkentity(self);

	monster_start_go(self);

	if (self->enemy && !(self->spawnflags & SF_MONSTER_SIGHT) && !(self->enemy->flags & FL_NOTARGET))
	{
		if (!(self->enemy->flags & FL_DISGUISED))
			FoundTarget(self);
		else
			self->enemy = NULL;
	}
	else
		self->enemy = NULL;
}

void monster_triggered_spawn_use(edict_t *self, edict_t *other, edict_t *activator)
{
	// we have a one frame delay here so we don't telefrag the guy who activated us
	self->think = monster_triggered_spawn;
	self->nextthink = level.time + FRAMETIME;
	// Knightmare- good guy monsters shouldn't have an enemy from this
	if (activator->client && !(self->monsterinfo.aiflags & AI_GOOD_GUY))
		self->enemy = activator;
	// Lazarus: Add 'em up
	//if (!(self->monsterinfo.aiflags & AI_GOOD_GUY)) //mxd. Already counted in monster_start
		//level.total_monsters++;
	self->use = monster_use;
}

void monster_triggered_start(edict_t *self)
{
	self->solid = SOLID_NOT;
	self->movetype = MOVETYPE_NONE;
	self->svflags |= SVF_NOCLIENT;
	self->nextthink = 0;
	self->use = monster_triggered_spawn_use;
	// Lazarus
	self->spawnflags &= ~SF_MONSTER_TRIGGER_SPAWN;
}


/*
================
monster_death_use

When a monster dies, it fires all of its targets with the current
enemy as activator.
================
*/
void monster_death_use(edict_t *self)
{
	edict_t	*player;
	int		i;

	self->flags &= ~(FL_FLY|FL_SWIM);
	self->monsterinfo.aiflags &= AI_GOOD_GUY;

	// Lazarus: If actor/monster is being used as a camera by a player,
	// turn camera off for that player
	for (i=0,player=g_edicts+1; i<maxclients->value; i++, player++) {
		if (player->client && player->client->spycam == self)
			camera_off(player);
	}

	if (self->item)
	{
		Drop_Item(self, self->item);
		self->item = NULL;
	}

	if (self->deathtarget)
		self->target = self->deathtarget;

	if (!self->target)
		return;

	G_UseTargets(self, self->enemy);
}


//============================================================================

qboolean monster_start(edict_t *self)
{
	if (deathmatch->value)
	{
		G_FreeEdict(self);
		return false;
	}

	// Lazarus: Already gibbed monsters passed across levels via trigger_transition:
	if ( (self->max_health > 0) && (self->health <= self->gib_health) && !(self->spawnflags & SF_MONSTER_NOGIB) )
	{
		void	SP_gibhead(edict_t *);

		SP_gibhead(self);
		return true;
	}

	// Lazarus: Good guys
	if (self->spawnflags & SF_MONSTER_GOODGUY) {
		self->monsterinfo.aiflags |= AI_GOOD_GUY;
		if (!self->dmgteam) {
			self->dmgteam = gi.TagMalloc(8*sizeof(char), TAG_LEVEL);
		//	strncpy(self->dmgteam,"player");
			Q_strncpyz(self->dmgteam,"player", 8);
		}
	}

	// Lazarus: Max range for sight/attack
	if (st.distance)
		self->monsterinfo.max_range = max(500,st.distance);
	else
		self->monsterinfo.max_range = 1280;	// Q2 default is 1000. We're mean.

	// Lazarus: We keep SIGHT to mean what old AMBUSH does, and AMBUSH additionally
	//          now means don't play idle sounds
/*	if ((self->spawnflags & MONSTER_SIGHT) && !(self->monsterinfo.aiflags & AI_GOOD_GUY))
	{
		self->spawnflags &= ~MONSTER_SIGHT;
		self->spawnflags |= MONSTER_AMBUSH;
	} */
	if ((self->spawnflags & SF_MONSTER_AMBUSH) && !(self->monsterinfo.aiflags & AI_GOOD_GUY))
		self->spawnflags |= SF_MONSTER_SIGHT;

	// Lazarus: Don't add trigger spawned monsters until they are actually spawned
	/*if (!(self->monsterinfo.aiflags & AI_GOOD_GUY) && !(self->spawnflags & SF_MONSTER_TRIGGER_SPAWN))
		level.total_monsters++;*/

	//mxd. Count them regardless of SF_MONSTER_TRIGGER_SPAWN / AI_GOOD_GUY flags, like in N64 version.
	level.total_monsters++;

	self->nextthink = level.time + FRAMETIME;
	self->svflags |= SVF_MONSTER;
	self->s.renderfx |= RF_FRAMELERP;
	self->air_finished = level.time + 12;
	self->use = monster_use;
	// Lazarus - don't reset max_health unnecessarily
	if (!self->max_health)
		self->max_health = self->health;
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
	self->clipmask = MASK_MONSTERSOLID;

	if (self->s.skinnum < 1) // Knightmare added
		self->s.skinnum = 0;
	self->deadflag = DEAD_NO;
	self->svflags &= ~SVF_DEADMONSTER;

	if (self->monsterinfo.flies > 1.0)
	{
		self->s.effects |= EF_FLIES;
		self->s.sound = gi.soundindex("infantry/inflies1.wav");
	}

	// Lazarus
	if (self->health <=0)
	{
		self->svflags |= SVF_DEADMONSTER;
		self->movetype = MOVETYPE_TOSS;
		self->takedamage = DAMAGE_YES;
		self->monsterinfo.pausetime = 100000000;
		self->monsterinfo.aiflags &= ~AI_RESPAWN_FINDPLAYER;
		if (self->max_health > 0)
		{
			// This must be a dead monster who changed levels
			// via trigger_transition
			self->nextthink = 0;
			self->deadflag = DEAD_DEAD;
		}
		if (self->s.effects & EF_FLIES && self->monsterinfo.flies <= 1.0)
		{
			self->think = M_FliesOff;
			self->nextthink = level.time + 1 + random()*60;
		}
		return true;
	}
	else
	{
		// make sure red shell is turned off in case medic got confused:
		self->monsterinfo.aiflags &= ~AI_RESURRECTING;
		self->svflags &= ~SVF_DEADMONSTER;
		self->takedamage = DAMAGE_AIM;
	}

	if (!self->monsterinfo.checkattack)
		self->monsterinfo.checkattack = M_CheckAttack;
	VectorCopy(self->s.origin, self->s.old_origin);

	if (st.item)
	{
		self->item = FindItemByClassname(st.item);
		if (!self->item)
			gi.dprintf("%s at %s has bad item: %s\n", self->classname, vtos(self->s.origin), st.item);
	}

	// randomize what frame they start on
	// Lazarus: preserve frame if set for monsters changing levels
	if (!self->s.frame)
	{
		if (self->monsterinfo.currentmove)
			self->s.frame = self->monsterinfo.currentmove->firstframe + (rand() % (self->monsterinfo.currentmove->lastframe - self->monsterinfo.currentmove->firstframe + 1));
	}

	return true;
}

void monster_start_go(edict_t *self)
{
	vec3_t v;

	if (self->health <= 0)
	{
		if (self->max_health <= 0)
			InitiallyDead(self);
		return;
	}

	// Lazarus: move_origin for func_monitor
	if (!VectorLengthSquared(self->move_origin))
		VectorSet(self->move_origin, 0, 0, self->viewheight);

	// check for target to point_combat and change to combattarget
	if (self->target)
	{
		edict_t *target = NULL;
		qboolean notcombat = false;
		qboolean fixup = false;
		while ((target = G_Find(target, FOFS(targetname), self->target)) != NULL)
		{
			if (strcmp(target->classname, "point_combat") == 0)
			{
				self->combattarget = self->target;
				fixup = true;
			}
			else
			{
				notcombat = true;
			}
		}

		if (notcombat && self->combattarget)
			gi.dprintf("%s at %s has target with mixed types\n", self->classname, vtos(self->s.origin));

		if (fixup)
			self->target = NULL;
	}

	// validate combattarget
	if (self->combattarget)
	{
		edict_t *target = NULL;
		while ((target = G_Find(target, FOFS(targetname), self->combattarget)) != NULL)
		{
			if (strcmp(target->classname, "point_combat") != 0)
			{
				gi.dprintf("%s at (%i %i %i) has a bad combattarget %s : %s at (%i %i %i)\n",
					self->classname, (int)self->s.origin[0], (int)self->s.origin[1], (int)self->s.origin[2],
					self->combattarget, target->classname, (int)target->s.origin[0], (int)target->s.origin[1],
					(int)target->s.origin[2]);
			}
		}
	}

	if (self->target)
	{
		self->goalentity = self->movetarget = G_PickTarget(self->target);
		if (!self->movetarget)
		{
			gi.dprintf("%s can't find target %s at %s\n", self->classname, self->target, vtos(self->s.origin));
			self->target = NULL;
			self->monsterinfo.pausetime = 100000000;
			self->monsterinfo.stand(self);
		}
		else if (strcmp (self->movetarget->classname, "path_corner") == 0)
		{
			// Lazarus: Don't wipe out target for trigger spawned monsters that aren't triggered yet
			if ( !(self->spawnflags & SF_MONSTER_TRIGGER_SPAWN) )
			{
				VectorSubtract(self->goalentity->s.origin, self->s.origin, v);
				self->ideal_yaw = self->s.angles[YAW] = vectoyaw(v);
				self->monsterinfo.walk(self);
				self->target = NULL;
			}
		}
		else
		{
			self->goalentity = self->movetarget = NULL;
			self->monsterinfo.pausetime = 100000000;
			self->monsterinfo.stand(self);
		}
	}
	else
	{
		self->monsterinfo.pausetime = 100000000;
		self->monsterinfo.stand(self);
	}

	self->think = monster_think;
	self->nextthink = level.time + FRAMETIME;
}


void walkmonster_start_go(edict_t *self)
{
	if (!(self->spawnflags & SF_MONSTER_TRIGGER_SPAWN) && level.time < 1)
	{
		M_droptofloor(self);

		if (self->groundentity && !M_walkmove(self, 0, 0))
			gi.dprintf("%s in solid at %s\n", self->classname, vtos(self->s.origin));
	}
	
	if (!self->yaw_speed)
		self->yaw_speed = 20;

	self->viewheight = 25;

	monster_start_go(self);

	if (self->spawnflags & SF_MONSTER_TRIGGER_SPAWN)
		monster_triggered_start(self);
}

void walkmonster_start(edict_t *self)
{
	self->think = walkmonster_start_go;
	monster_start(self);
}


void flymonster_start_go(edict_t *self)
{
	if (!M_walkmove(self, 0, 0))
		gi.dprintf("%s in solid at %s\n", self->classname, vtos(self->s.origin));

	if (!self->yaw_speed)
		self->yaw_speed = 10;
	self->viewheight = 25;

	self->monsterinfo.flies = 0.0;

	monster_start_go(self);

	if (self->spawnflags & SF_MONSTER_TRIGGER_SPAWN)
		monster_triggered_start(self);
}


void flymonster_start(edict_t *self)
{
	self->flags |= FL_FLY;
	self->think = flymonster_start_go;
	monster_start(self);
}


void swimmonster_start_go(edict_t *self)
{
	if (!self->yaw_speed)
		self->yaw_speed = 10;
	self->viewheight = 10;

	monster_start_go(self);

	if (self->spawnflags & SF_MONSTER_TRIGGER_SPAWN)
		monster_triggered_start(self);
}

void swimmonster_start(edict_t *self)
{
	self->flags |= FL_SWIM;
	self->think = swimmonster_start_go;
	monster_start(self);
}


//===============================================================
// Following functions unique to Lazarus

void InitiallyDead(edict_t *self)
{
	if (self->max_health > 0) return;

//	gi.dprintf("InitiallyDead on %s at %s\n",self->classname,vtos(self->s.origin));
	
	// initially dead bad guys shouldn't count against totals
	if ((self->max_health <= 0) && !(self->monsterinfo.aiflags & AI_GOOD_GUY))
	{
		level.total_monsters--;
		if (self->deadflag != DEAD_DEAD)
			level.killed_monsters--;
	}

	if (self->deadflag != DEAD_DEAD)
	{
		const int damage = 1 - self->health;
		self->health = 1;
		T_Damage(self, world, world, vec3_origin, self->s.origin, vec3_origin, damage, 0, DAMAGE_NO_ARMOR, 0);
		
		if (self->svflags & SVF_MONSTER)
		{
			self->svflags |= SVF_DEADMONSTER;
			self->think = monster_think;
			self->nextthink = level.time + FRAMETIME;
		}
	}

	gi.linkentity(self);
}

#define MAX_SKINS		24 //max is 32, but we only need 24
#define MAX_SKINNAME	64

#include "pak.h"

int PatchMonsterModel(char *modelname)
{
	char		skins[MAX_SKINS][MAX_SKINNAME];	// skin entries
	char		infilename[MAX_OSPATH];
	char		outfilename[MAX_OSPATH];
	char		*p;
	dmdl_t		model;				// model header
	byte		*data;				// model data
	int			datasize = 0;		// model data size (bytes)
	qboolean	is_tank = false;
	qboolean	is_soldier = false;

	// get game (moddir) name
	cvar_t *gamedir = gi.cvar("game", "", 0);
	if (!*gamedir->string)
		return 0;	// we're in baseq2

	Com_sprintf(outfilename, sizeof(outfilename), "%s/%s", gamedir->string, modelname);
	FILE *outfile = fopen(outfilename, "rb");
	if (outfile)
	{
		// output file already exists, move along
		fclose(outfile);
		return 0;
	}

	int numskins = 8;

	// special cases
	if (!strcmp(modelname,"models/monsters/tank/tris.md2"))
	{
		is_tank = true;
		numskins = 16;
	}
	else if (!strcmp(modelname,"models/monsters/soldier/tris.md2"))
	{
		is_soldier = true;
		numskins = 24;
	}

	for (int j = 0; j < numskins; j++)
	{
		memset(skins[j], 0, MAX_SKINNAME);
		Q_strncpyz(skins[j], modelname, sizeof(skins[j]));
		p = strstr(skins[j], "tris.md2");
		if (!p)
		{
			fclose(outfile);
			gi.dprintf("Error patching %s\n",modelname);
			return 0;
		}

		*p = 0;
		if (is_soldier)
		{
			switch (j)
			{
			case 0: strcat(skins[j], "skin_lt.pcx"); break;
			case 1: strcat(skins[j], "skin_ltp.pcx"); break;
			case 2: strcat(skins[j], "skin.pcx"); break;
			case 3: strcat(skins[j], "pain.pcx"); break;
			case 4: strcat(skins[j], "skin_ss.pcx"); break;
			case 5: strcat(skins[j], "skin_ssp.pcx"); break;
			case 6: strcat(skins[j], "custom1_lt.pcx"); break;
			case 7: strcat(skins[j], "custompain1_lt.pcx"); break;
			case 8: strcat(skins[j], "custom1.pcx"); break;
			case 9: strcat(skins[j], "custompain1.pcx"); break;
			case 10: strcat(skins[j], "custom1_ss.pcx"); break;
			case 11: strcat(skins[j], "custompain1_ss.pcx"); break;
			case 12: strcat(skins[j], "custom2_lt.pcx"); break;
			case 13: strcat(skins[j], "custompain2_lt.pcx"); break;
			case 14: strcat(skins[j], "custom2.pcx"); break;
			case 15: strcat(skins[j], "custompain2.pcx"); break;
			case 16: strcat(skins[j], "custom2_ss.pcx"); break;
			case 17: strcat(skins[j], "custompain2_ss.pcx"); break;
			case 18: strcat(skins[j], "custom3_lt.pcx"); break;
			case 19: strcat(skins[j], "custompain3_lt.pcx"); break;
			case 20: strcat(skins[j], "custom3.pcx"); break;
			case 21: strcat(skins[j], "custompain3.pcx"); break;
			case 22: strcat(skins[j], "custom3_ss.pcx"); break;
			case 23: strcat(skins[j], "custompain3_ss.pcx"); break;
			}
		}
		else if (is_tank)
		{
			switch (j)
			{
			case 0: strcat(skins[j], "skin.pcx"); break;
			case 1: strcat(skins[j], "pain.pcx"); break;
			case 2: strcat(skins[j], "../ctank/skin.pcx"); break;
			case 3: strcat(skins[j], "../ctank/pain.pcx"); break;
			case 4: strcat(skins[j], "custom1.pcx"); break;
			case 5: strcat(skins[j], "custompain1.pcx"); break;
			case 6: strcat(skins[j], "../ctank/custom1.pcx"); break;
			case 7: strcat(skins[j], "../ctank/custompain1.pcx"); break;
			case 8: strcat(skins[j], "custom2.pcx"); break;
			case 9: strcat(skins[j], "custompain2.pcx"); break;
			case 10: strcat(skins[j], "../ctank/custom2.pcx"); break;
			case 11: strcat(skins[j], "../ctank/custompain2.pcx"); break;
			case 12: strcat(skins[j], "custom3.pcx"); break;
			case 13: strcat(skins[j], "custompain3.pcx"); break;
			case 14: strcat(skins[j], "../ctank/custom3.pcx"); break;
			case 15: strcat(skins[j], "../ctank/custompain3.pcx"); break;
			}
		}
		else
		{
			switch (j)
			{
			case 0: strcat(skins[j], "skin.pcx"); break;
			case 1: strcat(skins[j], "pain.pcx"); break;
			case 2: strcat(skins[j], "custom1.pcx"); break;
			case 3: strcat(skins[j], "custompain1.pcx"); break;
			case 4: strcat(skins[j], "custom2.pcx"); break;
			case 5: strcat(skins[j], "custompain2.pcx"); break;
			case 6: strcat(skins[j], "custom3.pcx"); break;
			case 7: strcat(skins[j], "custompain3.pcx"); break;
			}
		}
	}

	// load original model
	Com_sprintf(infilename, sizeof(infilename), "baseq2/%s", modelname);
	FILE *infile = fopen(infilename, "rb");
	if (!infile)
	{
		// If file doesn't exist on user's hard disk, it must be in pak0.pak
		pak_header_t	pakheader;
		pak_item_t		pakitem;

		FILE *fpak = fopen("baseq2/pak0.pak","rb");
		if (!fpak)
		{
			char	pakfile[MAX_OSPATH];
			cvar_t *cddir = gi.cvar("cddir", "", 0);
			Com_sprintf(pakfile, sizeof(pakfile), "%s/baseq2/pak0.pak", cddir->string);
			fpak = fopen(pakfile, "rb");

			if (!fpak)
			{
				gi.dprintf("PatchMonsterModel: Cannot find pak0.pak\n");
				return 0;
			}
		}

		fread(&pakheader, 1, sizeof(pak_header_t), fpak);
		const int numitems = pakheader.dsize / sizeof(pak_item_t);
		fseek(fpak, pakheader.dstart, SEEK_SET);
		data = NULL;
		for (int k = 0; k<numitems && !data; k++)
		{
			fread(&pakitem, 1, sizeof(pak_item_t), fpak);
			if (!Q_stricmp(pakitem.name,modelname))
			{
				fseek(fpak,pakitem.start,SEEK_SET);
				fread(&model, sizeof(dmdl_t), 1, fpak);
				datasize = model.ofs_end - model.ofs_skins;
				data = malloc(datasize);

				if (!data)	// make sure freed locally
				{
					fclose(fpak);
					gi.dprintf("PatchMonsterModel: Could not allocate memory for model\n");
					return 0;
				}

				fread(data, sizeof(byte), datasize, fpak);
			}
		}

		fclose(fpak);
		if (!data)
		{
			gi.dprintf("PatchMonsterModel: Could not find %s in baseq2/pak0.pak\n", modelname);
			return 0;
		}
	}
	else
	{
		fread(&model, sizeof(dmdl_t), 1, infile);
	
		datasize = model.ofs_end - model.ofs_skins;
		data = malloc(datasize);
		if (!data)	// make sure freed locally
		{
			gi.dprintf("PatchMonsterModel: Could not allocate memory for model\n");
			return 0;
		}

		fread(data, sizeof(byte), datasize, infile);
		fclose(infile);
	}
	
	// update model info
	model.num_skins = numskins;

	const int newoffset = numskins * MAX_SKINNAME;
	model.ofs_st     += newoffset;
	model.ofs_tris   += newoffset;
	model.ofs_frames += newoffset;
	model.ofs_glcmds += newoffset;
	model.ofs_end    += newoffset;
	
	// save new model
	Com_sprintf(outfilename, sizeof(outfilename), "%s/models", gamedir->string);	// make some dirs if needed
	_mkdir(outfilename);
	strcat(outfilename, "/monsters");
	_mkdir(outfilename);
	Com_sprintf(outfilename, sizeof(outfilename), "%s/%s", gamedir->string, modelname);
	p = strstr(outfilename, "/tris.md2");
	*p = 0;
	_mkdir(outfilename);

	Com_sprintf(outfilename, sizeof(outfilename), "%s/%s", gamedir->string, modelname);
	outfile = fopen(outfilename, "wb");
	if (!outfile)
	{
		// file couldn't be created for some other reason
		gi.dprintf("PatchMonsterModel: Could not save %s\n", outfilename);
		free(data);
		return 0;
	}
	
	fwrite(&model, sizeof(dmdl_t), 1, outfile);
	fwrite(skins, sizeof(char), newoffset, outfile);
	fwrite(data, sizeof(byte), datasize, outfile);
	
	fclose(outfile);
	gi.dprintf("PatchMonsterModel: Saved %s\n", outfilename);
	free(data);
	return 1;
}

void HintTestNext(edict_t *self, edict_t *hint)
{
	edict_t		*next = NULL;
	vec3_t		dir;

	self->monsterinfo.aiflags &= ~AI_HINT_TEST;
	if (self->goalentity == hint)
		self->goalentity = NULL;

	if (self->movetarget == hint)
		self->movetarget = NULL;

	if (self->monsterinfo.pathdir == 1)
	{
		if (hint->hint_chain)
			next = hint->hint_chain;
		else
			self->monsterinfo.pathdir = -1;
	}

	if (self->monsterinfo.pathdir == -1)
	{
		edict_t *e = hint_chain_starts[hint->hint_chain_id];
		while (e)
		{
			if (e->hint_chain == hint)
			{
				next = e;
				break;
			}

			e = e->hint_chain;
		}
	}

	if (!next)
	{
		self->monsterinfo.pathdir = 1;
		next = hint->hint_chain;
	}

	if (next)
	{
		self->hint_chain_id = next->hint_chain_id;
		VectorSubtract(next->s.origin, self->s.origin, dir);
		self->ideal_yaw = vectoyaw(dir);
		self->goalentity = self->movetarget = next;
		self->monsterinfo.pausetime = 0;
		self->monsterinfo.aiflags = AI_HINT_TEST;
		// run for it
		self->monsterinfo.run(self);

		gi.dprintf("%s (%s): Reached hint_path %s,\nsearching for hint_path %s at %s. %s\n",
			self->classname, (self->targetname ? self->targetname : "<noname>"),
			(hint->targetname ? hint->targetname : "<noname>"),
			(next->targetname ? next->targetname : "<noname>"),
			vtos(next->s.origin),
			(visible(self, next) ? "I see it." : "I don't see it."));
	}
	else
	{
		self->monsterinfo.pausetime = level.time + 100000000;
		self->monsterinfo.stand(self);
		gi.dprintf("%s (%s): Error finding next/previous hint_path from %s at %s.\n",
			self->classname, (self->targetname ? self->targetname : "<noname>"),
			(hint->targetname ? hint->targetname : "<noname>"),
			vtos(hint->s.origin));
	}
}

int HintTestStart(edict_t *self)
{
	edict_t	*hint = NULL;
	vec3_t	dir;
	float	bestdistance = 99999;

	if (!hint_chains_exist)
		return 0;

	for (int i = game.maxclients + 1; i < globals.num_edicts; i++)
	{
			edict_t *e = &g_edicts[i];
			if (!e->inuse || Q_stricmp(e->classname, "hint_path") || !visible(self, e) || !canReach(self, e))
				continue;

			VectorSubtract(e->s.origin, self->s.origin, dir);
			const float dist = VectorLength(dir);
			if (dist < bestdistance)
			{
				hint = e;
				bestdistance = dist;
			}
	}

	if (hint)
	{
		self->hint_chain_id = hint->hint_chain_id;

		if (!self->monsterinfo.pathdir)
			self->monsterinfo.pathdir = 1;

		VectorSubtract(hint->s.origin, self->s.origin, dir);
		self->ideal_yaw = vectoyaw(dir);
		self->enemy = self->oldenemy = NULL;
		self->goalentity = self->movetarget = hint;
		self->monsterinfo.pausetime = 0;
		self->monsterinfo.aiflags = AI_HINT_TEST;
		// run for it
		self->monsterinfo.run(self);

		return 1;
	}

	return -1;
}