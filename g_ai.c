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

// g_ai.c

#include "g_local.h"

//qboolean FindTarget(edict_t *self);
//extern cvar_t	*maxclients;

qboolean ai_checkattack(edict_t *self, float dist);
//edict_t *medic_FindDeadMonster(edict_t *self);

qboolean	enemy_vis;
qboolean	enemy_infront;
int			enemy_range;
float		enemy_yaw;

/*
=================
AI_SetSightClient

Called once each frame to set level.sight_client to the player to be checked for in findtarget.
If all clients are either dead or in notarget, sight_client will be null.
In coop games, sight_client will cycle between the clients.
=================
*/
void AI_SetSightClient(void)
{
	int start;

	if (level.sight_client == NULL)
		start = 1;
	else
		start = level.sight_client - g_edicts;

	int check = start;
	while (true)
	{
		check++;
		if (check > game.maxclients)
			check = 1;

		edict_t *ent = &g_edicts[check];
		if (ent->inuse && ent->health > 0 && !(ent->flags & (FL_NOTARGET | FL_DISGUISED)))
		{
			// If player is using func_monitor, make the sight_client = the fake player at the monitor currently taking the player's place.
			// Do NOT do this for players using a target_monitor, though... in this case both player and fake player are ignored.
			if (ent->client && ent->client->camplayer)
			{
				if (ent->client->spycam)
				{
					level.sight_client = ent->client->camplayer;
					return;
				}
			}
			else
			{
				level.sight_client = ent;
				return; // got one
			}
		}

		if (check == start)
		{
			level.sight_client = NULL;
			return; // nobody to see
		}
	}
}

//============================================================================

/*
=============
ai_move

Move the specified distance at current facing.
This replaces the QC functions: ai_forward, ai_back, ai_pain, and ai_painforward
==============
*/
void ai_move(edict_t *self, float dist)
{
	M_walkmove(self, self->s.angles[YAW], dist);
}


/*
=============
ai_stand

Used for standing around and looking for players.
Distance is for slight position adjustments needed by the animations.
==============
*/
void ai_stand(edict_t *self, float dist)
{
	if (dist)
		M_walkmove(self, self->s.angles[YAW], dist);

	if (self->monsterinfo.aiflags & AI_FOLLOW_LEADER && (!self->enemy || !self->enemy->inuse))
	{
		self->movetarget = self->goalentity = self->monsterinfo.leader;
		self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
		self->monsterinfo.pausetime = 0;
	}

	if (self->monsterinfo.aiflags & AI_CHICKEN)
	{
		if (level.framenum - self->monsterinfo.chicken_framenum > 200 || (self->enemy && (self->enemy->last_attacked_framenum > level.framenum - 2)))
		{
			self->monsterinfo.aiflags &= ~(AI_CHICKEN | AI_STAND_GROUND);
			self->monsterinfo.pausetime = 0;

			if (self->enemy)
				FoundTarget(self);
		}
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		if (self->enemy && self->enemy->inuse)
		{
			vec3_t v;
			VectorSubtract(self->enemy->s.origin, self->s.origin, v);
			const float length = VectorLength(v);
			self->ideal_yaw = vectoyaw(v);

			if (level.time >= self->monsterinfo.rangetime && (self->monsterinfo.aiflags & AI_RANGE_PAUSE))
			{
				if (length < self->monsterinfo.ideal_range[0] && (rand() & 3))
					self->monsterinfo.rangetime = level.time + 0.5f;

				if (length < self->monsterinfo.ideal_range[1] && length > self->monsterinfo.ideal_range[0] && (rand() & 1))
					self->monsterinfo.rangetime = level.time + 0.2f;
			}

			if (self->s.angles[YAW] != self->ideal_yaw && (self->monsterinfo.aiflags & AI_RANGE_PAUSE))
			{
				if (self->monsterinfo.rangetime < level.time)
				{
					// Lazarus: Don't run if we're still too close
					if (self->monsterinfo.min_range > 0)
					{
						if (length > self->monsterinfo.min_range)
						{
							self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_RANGE_PAUSE);
							self->monsterinfo.run(self);
						}
					}
					else
					{
						self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_RANGE_PAUSE);
						self->monsterinfo.run(self);
					}
				}
			}

			M_ChangeYaw(self);
			ai_checkattack(self, 0);
			if (!enemy_vis && (self->monsterinfo.aiflags & AI_RANGE_PAUSE))
			{
				self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_RANGE_PAUSE);
				self->monsterinfo.run(self);
			}
		}
		else
		{
			FindTarget(self);
		}

		return;
	}

	if (FindTarget(self))
		return;
	
	if (level.time > self->monsterinfo.pausetime)
	{
		// Lazarus: Solve problem of monsters pausing at path_corners, taking off in original direction
		vec3_t v;
		if (self->enemy && self->enemy->inuse)
		{
			VectorSubtract(self->enemy->s.origin, self->s.origin, v);
		}
		else if (self->goalentity)
		{
			VectorSubtract(self->goalentity->s.origin, self->s.origin, v);
		}
		else
		{
			self->monsterinfo.pausetime = level.time + random() * 15;
			return;
		}
		self->ideal_yaw = vectoyaw(v);

		// Lazarus: Let misc_actors who are following their leader RUN even when not mad
		if ((self->monsterinfo.aiflags & AI_FOLLOW_LEADER) && self->movetarget && self->movetarget->inuse)
		{
			const float dist = realrange(self, self->movetarget);
			if (dist > ACTOR_FOLLOW_RUN_RANGE)
				self->monsterinfo.run(self);
			else if (dist > ACTOR_FOLLOW_STAND_RANGE || !self->movetarget->client)
				self->monsterinfo.walk(self);
		}
		else
		{
			self->monsterinfo.walk(self);
		}

		return;
	}

	if (!(self->spawnflags & SF_MONSTER_SIGHT) && self->monsterinfo.idle && level.time > self->monsterinfo.idle_time)
	{
		if (self->monsterinfo.aiflags & AI_MEDIC)
			abortHeal(self, false);

		if (self->monsterinfo.idle_time)
		{
			self->monsterinfo.idle(self);
			self->monsterinfo.idle_time = level.time + 15 + random() * 15;
		}
		else
		{
			self->monsterinfo.idle_time = level.time + random() * 15;
		}
	}
}


void ai_walk(edict_t *self, float dist)
{
	// Lazarus: If we're following the leader and have no enemy, run to him
	if (!self->enemy && (self->monsterinfo.aiflags & AI_FOLLOW_LEADER))
		self->movetarget = self->goalentity = self->monsterinfo.leader;

	M_MoveToGoal(self, dist);

	// check for noticing a player
	if (FindTarget(self))
		return;

	if (self->monsterinfo.search && level.time > self->monsterinfo.idle_time)
	{
		if (self->monsterinfo.aiflags & AI_MEDIC)
			abortHeal(self, false);

		if (self->monsterinfo.idle_time)
		{
			self->monsterinfo.search(self);
			self->monsterinfo.idle_time = level.time + 15 + random() * 15;
		}
		else
		{
			self->monsterinfo.idle_time = level.time + random() * 15;
		}
	}
}


/*
=============
ai_charge

Turns towards target and advances
Use this call with a distnace of 0 to replace ai_face
==============
*/
void ai_charge(edict_t *self, float dist)
{
	// Lazarus: Check for existence and validity of enemy.
	// This is normally not necessary, but target_anger making monster mad at a static object (a pickup, for example) previously resulted in weirdness here
	if (!self->enemy || !self->enemy->inuse)
		return;

	vec3_t v;
	VectorSubtract(self->enemy->s.origin, self->s.origin, v);
	self->ideal_yaw = vectoyaw(v);
	M_ChangeYaw(self);

	if (dist)
	{
		//mxd. Circle-strafe support
		if (self->monsterinfo.attack_state == AS_SLIDING)
		{
			const float ofs = (self->monsterinfo.lefty ? 90.0f : -90.0f);

			if (M_walkmove(self, self->ideal_yaw + ofs, dist))
				return;

			self->monsterinfo.lefty = 1 - self->monsterinfo.lefty;
			M_walkmove(self, self->ideal_yaw - ofs, dist);
		}
		else
		{
			M_walkmove(self, self->s.angles[YAW], dist);
		}
	}
}


/*
=============
ai_turn

don't move, but turn towards ideal_yaw
Distance is for slight position adjustments needed by the animations
=============
*/
void ai_turn(edict_t *self, float dist)
{
	if (dist)
		M_walkmove(self, self->s.angles[YAW], dist);

	if (FindTarget(self))
		return;
	
	M_ChangeYaw(self);
}


/*

.enemy
Will be world if not currently angry at anyone.

.movetarget
The next path spot to walk toward.  If .enemy, ignore .movetarget.
When an enemy is killed, the monster will try to return to it's path.

.hunt_time
Set to time + something when the player is in sight, but movement straight for
him is blocked.  This causes the monster to use wall following code for
movement direction instead of sighting on the player.

.ideal_yaw
A yaw angle of the intended direction, which will be turned towards at up
to 45 deg / state.  If the enemy is in view and hunt_time is not active,
this will be the exact line towards the enemy.

.pausetime
A monster will leave it's stand state and head towards it's .movetarget when
time > .pausetime.

walkmove(angle, speed) primitive is all or nothing
*/

/*
=============
range

returns the range catagorization of an entity reletive to self
0	melee range, will become hostile even if back is turned
1	visibility and infront, or visibility and show hostile
2	infront and show hostile
3	only triggered by damage
=============
*/
int range(edict_t *self, edict_t *other)
{
	vec3_t v;
	VectorSubtract(self->s.origin, other->s.origin, v);
	const float len = VectorLength(v);

	if (len < MELEE_DISTANCE)
		return RANGE_MELEE;

	if (len < 500)
		return RANGE_NEAR;

	if (len < self->monsterinfo.max_range)
		return RANGE_MID;

	return RANGE_FAR;
}

/*
=============
visible

returns 1 if the entity is visible to self, even if not infront()
=============
*/
qboolean visible(edict_t *self, edict_t *other)
{
	vec3_t	spot1;
	vec3_t	spot2;

	if (!self || !other) // Knightmare- crash protect
		return false;

	VectorCopy(self->s.origin, spot1);
	spot1[2] += self->viewheight;
	VectorCopy(other->s.origin, spot2);
	spot2[2] += other->viewheight;
	const trace_t trace = gi.trace(spot1, vec3_origin, vec3_origin, spot2, self, MASK_OPAQUE);

	// Lazarus: Take fog into account for monsters
	if (trace.fraction == 1.0f || trace.ent == other)
	{
		if (level.active_fog && (self->svflags & SVF_MONSTER))
		{
			fog_t *pfog = &level.fog;

			vec3_t v;
			VectorSubtract(spot2, spot1, v);
			const float r = VectorLength(v);
			const float dw = pfog->Density / 10000.0f * r;

			switch(pfog->Model)
			{
			case 1:
				self->monsterinfo.visibility = expf(-dw);
				break;

			case 2:
				self->monsterinfo.visibility = expf(-dw * dw);
				break;

			default:
				if (r < pfog->Near || pfog->Near == pfog->Far)
					self->monsterinfo.visibility = 1.0;
				else if (r > pfog->Far)
					self->monsterinfo.visibility = 0.0;
				else
					self->monsterinfo.visibility = 1.0 - (r - pfog->Near) / (pfog->Far - pfog->Near);
				break;
			}

			if (self->monsterinfo.visibility < 0.05f)
				return false;

			return true;
		}

		self->monsterinfo.visibility = 1.0f;
		return true;
	}

	return false;
}


/*
=============
infront

returns 1 if the entity is in front (in sight) of self
=============
*/
qboolean infront(edict_t *self, edict_t *other)
{
	vec3_t	vec;
	vec3_t	forward;
	
	if (!self || !other) // Knightmare- crash protect
		return false;

	AngleVectors(self->s.angles, forward, NULL, NULL);
	VectorSubtract(other->s.origin, self->s.origin, vec);
	VectorNormalize(vec);
	const float dot = DotProduct(vec, forward);
	
	return dot > 0.3f;
}

/*
=============
canReach

similar to visible, but uses a different mask
=============
*/
qboolean canReach(edict_t *self, edict_t *other)
{
	vec3_t	spot1;
	vec3_t	spot2;

	VectorCopy(self->s.origin, spot1);
	spot1[2] += self->viewheight;
	VectorCopy(other->s.origin, spot2);
	spot2[2] += other->viewheight;
	const trace_t trace = gi.trace(spot1, vec3_origin, vec3_origin, spot2, self, MASK_SHOT|MASK_WATER);
	
	return (trace.fraction == 1.0f || trace.ent == other);
}

//============================================================================

void HuntTarget(edict_t *self)
{
	// Lazarus: avert impending disaster
	if (self->monsterinfo.aiflags & AI_DUCKED)
		return;

	self->goalentity = self->enemy;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.stand(self);
	else
		self->monsterinfo.run(self);

	vec3_t vec;
	VectorSubtract(self->enemy->s.origin, self->s.origin, vec);
	self->ideal_yaw = vectoyaw(vec);

	// Wait a while before first attack
	if (!(self->monsterinfo.aiflags & AI_STAND_GROUND))
		AttackFinished(self, 1);
}

void FoundTarget(edict_t *self)
{
	// Lazarus: avert impending disaster
	if ((self->monsterinfo.aiflags & AI_DUCKED) || (self->monsterinfo.aiflags & AI_CHICKEN))
		return;

	// Let other monsters see this monster for a while, but not if it's simply a reflection
	if (self->enemy->client && !(self->enemy->flags & FL_REFLECT))
	{
		self->enemy->flags &= ~FL_DISGUISED;

		level.sight_entity = self;
		level.sight_entity_framenum = level.framenum;
		level.sight_entity->light_level = 128;

		edict_t *goodguy = G_Find(NULL, FOFS(dmgteam), "player");
		while (goodguy)
		{
			if (goodguy->health > 0 && !goodguy->enemy && goodguy->monsterinfo.aiflags & AI_ACTOR)
			{
				// Can he see enemy?
//				tr = gi.trace(goodguy->s.origin,vec3_origin,vec3_origin,self->enemy->s.origin,goodguy,MASK_OPAQUE);
//				if (tr.fraction == 1.0)
				if (gi.inPVS(goodguy->s.origin,self->enemy->s.origin))
				{
					goodguy->monsterinfo.aiflags |= AI_FOLLOW_LEADER;
					goodguy->monsterinfo.old_leader = NULL;
					goodguy->monsterinfo.leader = self->enemy;
				}
			}

			goodguy = G_Find(goodguy, FOFS(dmgteam), "player");
		}
	}

	self->show_hostile = level.time + 1; // wake up other monsters
	VectorCopy(self->enemy->s.origin, self->monsterinfo.last_sighting);
	self->monsterinfo.trail_time = level.time;

	if (!self->combattarget)
	{
		HuntTarget(self);
		return;
	}

	self->goalentity = self->movetarget = G_PickTarget(self->combattarget);
	if (!self->movetarget)
	{
		self->goalentity = self->movetarget = self->enemy;
		HuntTarget(self);
		gi.dprintf("%s at %s, combattarget %s not found\n", self->classname, vtos(self->s.origin), self->combattarget);
		return;
	}

	// Lazarus: Huh? How come yaw for combattarget isn't set?
	vec3_t v;
	VectorSubtract(self->movetarget->s.origin, self->s.origin, v);
	self->ideal_yaw = vectoyaw(v);

	// Clear out our combattarget, these are a one shot deal
	self->combattarget = NULL;
	self->monsterinfo.aiflags |= AI_COMBAT_POINT;

	// clear the targetname, that point is ours!
	// Lazarus: Why, why, why???? This doesn't remove the point_combat, only makes it inaccessible to other monsters. 
	//self->movetarget->targetname = NULL;
	self->monsterinfo.pausetime = 0;

	// run for it
	self->monsterinfo.run(self);
}


/*
===========
FindTarget

Self is currently not attacking anything, so try to find a target

Returns TRUE if an enemy was sighted

When a player fires a missile, the point of impact becomes a fakeplayer so
that monsters that see the impact will respond as if they had seen the player.

To avoid spending too much time, only a single client (or fakeclient) is checked each frame.
This means multi player games will have slightly slower noticing monsters.
============
*/
qboolean FindTarget(edict_t *self)
{
	if (self->monsterinfo.aiflags & (AI_CHASE_THING | AI_HINT_TEST))
		return false;

	if (self->monsterinfo.aiflags & AI_GOOD_GUY)
	{
		if (self->goalentity && self->goalentity->inuse && self->goalentity->classname && strcmp(self->goalentity->classname, "target_actor") == 0)
			return false;

		// Lazarus: Look for monsters
		if (!self->enemy)
		{
			if (self->monsterinfo.aiflags & AI_FOLLOW_LEADER)
			{
				edict_t	*best = NULL;
				vec_t best_dist = self->monsterinfo.max_range;
				for (int i = game.maxclients + 1; i < globals.num_edicts; i++)
				{
					edict_t *e = &g_edicts[i];
					if (!e->inuse || !(e->svflags & SVF_MONSTER) || e->svflags & SVF_NOCLIENT || e->solid == SOLID_NOT || e->monsterinfo.aiflags & AI_GOOD_GUY || !visible(self, e))
						continue;

					if ((self->monsterinfo.aiflags & AI_BRUTAL && e->health <= e->gib_health) || e->health <= 0)
						continue;

					const vec_t dist = realrange(self, e);
					if (dist < best_dist)
					{
						best_dist = dist;
						best = e;
					}
				}

				if (best)
				{
					self->enemy = best;
					FoundTarget(self);

					return true;
				}
			}

			return false;
		}

		if (level.time < self->monsterinfo.pausetime)
			return false;

		if (!visible(self, self->enemy))
			return false;

		FoundTarget(self);
		return true;
	}

	// if we're going to a combat point, just proceed
	if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
		return false;

// if the first spawnflag bit is set, the monster will only wake up on
// really seeing the player, not another monster getting angry or hearing something

// revised behavior so they will wake up if they "see" a player make a noise
// but not weapon impact/explosion noises

	edict_t *client;
	qboolean heardit = false;
	if (level.sight_entity_framenum >= level.framenum - 1 && !(self->spawnflags & SF_MONSTER_SIGHT))
	{
		client = level.sight_entity;
		if (client->enemy == self->enemy)
			return false;
	}
	else if (level.disguise_violation_framenum > level.framenum)
	{
		client = level.disguise_violator;
	}
	else if (level.sound_entity_framenum >= level.framenum - 1)
	{
		client = level.sound_entity;
		heardit = true;
	}
	else if (!self->enemy && level.sound2_entity_framenum >= level.framenum - 1 && !(self->spawnflags & SF_MONSTER_SIGHT) )
	{
		client = level.sound2_entity;
		heardit = true;
	}
	else
	{
		client = level.sight_client;
	}

	// if the entity went away, forget it
	if (!client || !client->inuse)
		return false;

	// Lazarus
	if (client->client && client->client->camplayer)
		client = client->client->camplayer;

	if (client == self->enemy)
		return true;	// JDC false;

	// Lazarus: Force idle medics to look for dead monsters
	if (!self->enemy && !Q_stricmp(self->classname, "monster_medic") && medic_FindDeadMonster(self))
		return true;

	// in coop mode, ignore sounds if we're following a hint_path
	if (coop && coop->value && (self->monsterinfo.aiflags & AI_HINT_PATH))
		heardit = false;

	if (client->client)
	{
		if (client->flags & FL_NOTARGET)
			return false;
	}
	else if (client->svflags & SVF_MONSTER)
	{
		if (!client->enemy || (client->enemy->flags & FL_NOTARGET))
			return false;
	}
	else if (heardit)
	{
		if (client->owner && (client->owner->flags & FL_NOTARGET))
			return false;
	}
	else
	{
		return false;
	}

	edict_t *reflection = NULL;
	edict_t *self_reflection = NULL;
	if (level.num_reflectors)
	{
		for (int i = 0; i < 6 && !reflection; i++)
		{
			edict_t *ref = client->reflection[i];
			if (ref && visible(self, ref) && infront(self, ref))
			{
				reflection = ref;
				self_reflection = self->reflection[i];
			}
		}
	}

	if (!heardit)
	{
		const int r = range(self, client);

		if (r == RANGE_FAR)
			return false;

// this is where we would check invisibility

		// is client in an spot too dark to be seen?
		if (client->light_level <= 5) 
			return false;

		if (!visible(self, client))
		{
			if (!reflection)
				return false;

			self->goalentity = self->movetarget = reflection;

			vec3_t temp;
			VectorSubtract(reflection->s.origin, self->s.origin, temp);
			self->ideal_yaw = vectoyaw(temp);
			M_ChangeYaw(self);

			// If MORON (=4) is set, then the reflection becomes the enemy. Otherwise if DUMMY (=8) is set, reflection
			// becomes the enemy ONLY if the monster cannot see his own reflection in the same mirror. And if neither situation
			// applies, then reflection is treated identically to a player noise.
			// Don't do the MORON/DUMMY bit if SF_MONSTER_KNOWS_MIRRORS is set (set automatically for melee-only monsters, and
			// turned on once other monsters have figured out the truth)
			if (!(self->spawnflags & SF_MONSTER_KNOWS_MIRRORS))
			{
				if (reflection->activator->spawnflags & 4 ||
				   (reflection->activator->spawnflags & 8 && (!self_reflection || !visible(self, self_reflection))))
				{
					self->monsterinfo.attack_state = 0;
					self->enemy = reflection;
					goto got_one;
				}
			}

			self->monsterinfo.pausetime = 0;
			self->monsterinfo.aiflags &= ~AI_STAND_GROUND;
			self->monsterinfo.run(self);

			return false;
		}

		// Knightmare- commented this out because it causes a crash
		/*if (reflection && !(self->spawnflags & SF_MONSTER_KNOWS_MIRRORS) &&
			!infront(self,client))
		{
			// Client is visible but behind monster.
			// If MORON or DUMMY for the parent func_reflect is set,
			// attack the reflection (in the case of DUMMY, only
			// if monster doesn't see himself in the same mirror)
			if ( (reflection->activator->spawnflags & 4) ||
				( (reflection->activator->spawnflags & 8) &&
				(!self_reflection || !visible(self,self_reflection)) ) ) // crashes here
			{
				vec3_t	temp;
				
				self->goalentity = self->movetarget = reflection;
				VectorSubtract(reflection->s.origin,self->s.origin,temp);
				self->ideal_yaw = vectoyaw(temp);
				M_ChangeYaw(self);
				self->enemy = reflection;
				goto got_one;
			}
		}*/

		if (!reflection)
		{
			if (r == RANGE_NEAR)
			{
				if (client->show_hostile < level.time && !infront(self, client))
					return false;
			}
			else if (r == RANGE_MID)
			{
				if (!infront(self, client))
					return false;
			}
		}

		self->enemy = client;

		if (strcmp(self->enemy->classname, "player_noise") != 0)
		{
			self->monsterinfo.aiflags &= ~AI_SOUND_TARGET;

			if (!self->enemy->client)
			{
				self->enemy = self->enemy->enemy;
				if (!self->enemy->client)
				{
					self->enemy = NULL;
					return false;
				}
			}
		}
	}
	else // heardit
	{
		if (self->spawnflags & SF_MONSTER_SIGHT)
		{
			if (!visible(self, client))
				return false;
		}
		else if (!(client->flags & FL_REFLECT))
		{
			if (!gi.inPHS(self->s.origin, client->s.origin))
				return false;
		}

		vec3_t temp;
		VectorSubtract(client->s.origin, self->s.origin, temp);

		if (VectorLength(temp) > 1000)	// too far to hear
			return false;

		// Check area portals - if they are different and not connected then we can't hear it
		if (!(client->flags & FL_REFLECT) && client->areanum != self->areanum && !gi.AreasConnected(self->areanum, client->areanum))
			return false;

		self->ideal_yaw = vectoyaw(temp);
		M_ChangeYaw(self);

		// Hunt the sound for a bit; hopefully find the real player
		self->monsterinfo.aiflags |= AI_SOUND_TARGET;
		self->enemy = client;
	}

got_one:

	// Stop following hint_paths if we've found our enemy
	if (self->monsterinfo.aiflags & AI_HINT_PATH)
		hintpath_stop(self); // hintpath_stop calls foundtarget
	else if (self->monsterinfo.aiflags & AI_MEDIC_PATROL)
		medic_StopPatrolling(self);
	else
		FoundTarget(self);

	if (!(self->monsterinfo.aiflags & AI_SOUND_TARGET) && self->monsterinfo.sight)
		self->monsterinfo.sight(self, self->enemy);

	return true;
}


//=============================================================================

/*
============
FacingIdeal

============
*/
qboolean FacingIdeal(edict_t *self)
{
	const float delta = anglemod(self->s.angles[YAW] - self->ideal_yaw);
	if (delta > 45 && delta < 315)
		return false;
	return true;
}


//=============================================================================

qboolean M_CheckAttack(edict_t *self)
{
	vec3_t spot1, spot2;

	// Lazarus: Paranoia check
	if (!self->enemy)
		return false;

	if (self->enemy->health > 0)
	{
		// See if any entities are in the way of the shot
		VectorCopy(self->s.origin, spot1);
		spot1[2] += self->viewheight;
		VectorCopy(self->enemy->s.origin, spot2);
		spot2[2] += self->enemy->viewheight;
		const trace_t tr = gi.trace(spot1, NULL, NULL, spot2, self, CONTENTS_SOLID | CONTENTS_MONSTER | CONTENTS_SLIME | CONTENTS_LAVA | CONTENTS_WINDOW);

		// Do we have a clear shot?
		if (tr.ent != self->enemy && (!(self->enemy->flags & FL_REFLECT) || tr.ent != world))
			return false;
	}
	
	// Melee attack
	if (enemy_range == RANGE_MELEE)
	{
		// Don't always melee in easy mode
		if (skill->value == 0 && (rand() & 3))
			return false;

		if (self->monsterinfo.melee)
			self->monsterinfo.attack_state = AS_MELEE;
		else
			self->monsterinfo.attack_state = AS_MISSILE;

		return true;
	}
	
	// Missile attack
	if (!self->monsterinfo.attack)
		return false;
		
	if (level.time < self->monsterinfo.attack_finished)
		return false;
		
	if (enemy_range == RANGE_FAR)
		return false;

	float chance;
	if (self->enemy->flags == FL_REFLECT)
		chance = 2.0f; // no waiting for reflections - shoot 'em NOW
	else if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		chance = 0.4f;
	else if (enemy_range == RANGE_MELEE)
		chance = 0.2f;
	else if (enemy_range == RANGE_NEAR)
		chance = 0.1f;
	else if (enemy_range == RANGE_MID)
		chance = 0.02f;
	else
		return false;

	if (skill->value == 0)
		chance *= 0.5f;
	else if (skill->value >= 2)
		chance *= 2.0f;

	if (random() < chance)
	{
		self->monsterinfo.attack_state = AS_MISSILE;
		self->monsterinfo.attack_finished = level.time + 2 * random();

		return true;
	}

	if (self->flags & FL_FLY)
		self->monsterinfo.attack_state = (random() < 0.3f ? AS_SLIDING : AS_STRAIGHT);

	return false;
}


/*
=============
ai_run_melee

Turn and close until within an angle to launch a melee attack
=============
*/
void ai_run_melee(edict_t *self)
{
	self->ideal_yaw = enemy_yaw;
	M_ChangeYaw(self);

	if (FacingIdeal(self))
	{
		if (self->monsterinfo.melee)
			self->monsterinfo.melee(self);

		self->monsterinfo.attack_state = AS_STRAIGHT;
	}
}


/*
=============
ai_run_missile

Turn in place until within an angle to launch a missile attack
=============
*/
void ai_run_missile(edict_t *self)
{
	self->ideal_yaw = enemy_yaw;
	M_ChangeYaw(self);

	if (FacingIdeal(self))
	{
		if (self->monsterinfo.attack)
			self->monsterinfo.attack(self);

		if (self->monsterinfo.attack_state == AS_MISSILE || self->monsterinfo.attack_state == AS_BLIND) //mxd
			self->monsterinfo.attack_state = AS_STRAIGHT;
	}
};


/*
=============
ai_run_slide

Strafe sideways, but stay at aproximately the same range
=============
*/
void ai_run_slide(edict_t *self, float distance)
{
	self->ideal_yaw = enemy_yaw;
	M_ChangeYaw(self);

	const float ofs = (self->monsterinfo.lefty ? 90 : -90);

	if (M_walkmove(self, self->ideal_yaw + ofs, distance))
		return;
		
	self->monsterinfo.lefty = 1 - self->monsterinfo.lefty;
	M_walkmove(self, self->ideal_yaw - ofs, distance);
}


/*
=============
ai_checkattack

Decides if we're going to attack or do something else used by ai_run and ai_stand
=============
*/
qboolean ai_checkattack(edict_t *self, float dist)
{
	// this causes monsters to run blindly to the combat point w/o firing
	if (self->goalentity)
	{
		if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
			return false;

		if (!visible(self, self->goalentity) && self->monsterinfo.aiflags & AI_SOUND_TARGET) //mxd. https://github.com/yquake2/xatrix/commit/20cdbfe1c03b94e6349134094d1859ec37bf14d1
		{
			if (self->enemy && (level.time - self->enemy->teleport_time) > 5.0f) //mxd. Added self->enemy check
			{
				if (self->goalentity == self->enemy)
					self->goalentity = (self->movetarget ? self->movetarget : NULL);

				self->monsterinfo.aiflags &= ~AI_SOUND_TARGET;

				if (self->monsterinfo.aiflags & AI_TEMP_STAND_GROUND)
					self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
			}
			else
			{
				self->show_hostile = level.time + 1;
				return false;
			}
		}
	}

	enemy_vis = false;

	// See if the enemy is dead
	qboolean hesDeadJim = false;
	if (!self->enemy || !self->enemy->inuse)
	{
		hesDeadJim = true;
	}
	else if (self->monsterinfo.aiflags & AI_MEDIC)
	{
		if (self->enemy->health > 0)
		{
			hesDeadJim = true;
			self->monsterinfo.aiflags &= ~AI_MEDIC;
		}
	}
	else if (self->enemy->flags & FL_REFLECT)
	{
		hesDeadJim = false;
	}
	else
	{
		if (self->monsterinfo.aiflags & AI_BRUTAL)
		{
			// Lazarus: This value should be enemy class-dependent
			//if (self->enemy->health <= -80)
			if (self->enemy->health <= self->enemy->gib_health)
				hesDeadJim = true;
		}
		else
		{
			if (self->enemy->health <= 0)
				hesDeadJim = true;
		}
	}

	if (hesDeadJim)
	{
		self->enemy = NULL;

		// FIXME: look all around for other targets
		if (self->oldenemy && self->oldenemy->health > 0)
		{
			self->enemy = self->oldenemy;
			self->oldenemy = NULL;
			HuntTarget(self);
		}
		else
		{
			if (self->movetarget)
			{
				self->goalentity = self->movetarget;

				// Lazarus: Let misc_actors who are following their leader RUN even when not mad
				if ((self->monsterinfo.aiflags & AI_FOLLOW_LEADER) && self->movetarget && self->movetarget->inuse)
				{
					const float dist = realrange(self, self->movetarget); 
					if (dist > ACTOR_FOLLOW_RUN_RANGE)
					{
						self->monsterinfo.run(self);
					}
					else if (dist > ACTOR_FOLLOW_STAND_RANGE || !self->movetarget->client)
					{
						self->monsterinfo.walk(self);
					}
					else
					{
						self->monsterinfo.pausetime = level.time + 0.5f;
						self->monsterinfo.stand(self);
					}
				}
				else
				{
					self->monsterinfo.walk(self);
				}
			}
			else
			{
				// We need the pausetime, otherwise the stand code will just revert to walking with no target and
				// the monster will wonder around aimlessly trying to hunt the world entity
				self->monsterinfo.pausetime = level.time + 100000000;
				self->monsterinfo.stand(self);
			}

			return true;
		}
	}

	self->show_hostile = level.time + 1; // wake up other monsters

	// Check knowledge of enemy
	enemy_vis = visible(self, self->enemy);

	if (enemy_vis)
	{
		self->monsterinfo.search_time = level.time + 5;
		VectorCopy(self->enemy->s.origin, self->monsterinfo.last_sighting);
	}

// look for other coop players here
//	if (coop && self->monsterinfo.search_time < level.time)
//	{
//		if (FindTarget(self))
//			return true;
//	}

	if (self->monsterinfo.aiflags & AI_CHICKEN)
	{
		if (enemy_vis)
		{
			if (ai_chicken(self, self->enemy))
				return false;

			self->monsterinfo.aiflags &= ~(AI_CHICKEN | AI_STAND_GROUND);
			self->monsterinfo.pausetime = 0;
			FoundTarget(self);
		}
		else
		{
			return false;
		}
	}

	enemy_infront = infront(self, self->enemy);
	enemy_range = range(self, self->enemy);

	vec3_t temp;
	VectorSubtract(self->enemy->s.origin, self->s.origin, temp);
	enemy_yaw = vectoyaw(temp);


	// JDC self->ideal_yaw = enemy_yaw;
	if (self->monsterinfo.attack_state == AS_MISSILE)
	{
		ai_run_missile(self);
		return true;
	}

	if (self->monsterinfo.attack_state == AS_MELEE)
	{
		ai_run_melee(self);
		return true;
	}

	// If enemy is not currently visible, we will never attack
	if (!enemy_vis)
		return false;

	if (self->monsterinfo.checkattack(self))
	{
		self->enemy->last_attacked_framenum = level.framenum;
		return true;
	}

	return false;
}


#define HINT_PATH_START_TIME 3
#define HINT_PATH_RESTART_TIME 5

/*
=============
ai_run

The monster has an enemy it is trying to kill
=============
*/
void ai_run(edict_t *self, float dist)
{
	vec3_t		v;
	edict_t		*marker;
	qboolean	alreadyMoved = false;
	qboolean	pounce = false;
	
	// If we're going to a combat point, just proceed
	if (self->monsterinfo.aiflags & (AI_COMBAT_POINT | AI_CHASE_THING | AI_HINT_TEST))
	{
		M_MoveToGoal(self, dist);
		return;
	}

	if ((self->monsterinfo.aiflags & AI_MEDIC_PATROL) && !FindTarget(self))
	{
		M_MoveToGoal(self, dist);
		return;
	}

	// If currently mad at a reflection, AND we've already shot at it once, set flag indicating 
	// that this monster got suddenly smarter about mirrors, and turn him on the real enemy
	if (self->enemy && (self->enemy->flags & FL_REFLECT))
	{
		if (self->enemy->last_attacked_framenum > 0 &&
			self->enemy->last_attacked_framenum < level.framenum - 5)
		{
			self->enemy->last_attacked_framenum = 0;
			self->spawnflags |= SF_MONSTER_KNOWS_MIRRORS;
			self->enemy = self->enemy->owner;
			self->movetarget = self->goalentity = self->enemy;

			VectorSubtract(self->enemy->s.origin, self->s.origin, v);
			self->ideal_yaw = vectoyaw(v);

			M_MoveToGoal(self, dist);
			return;
		}
	}

	// Lazarus: If running at a reflection, go ahead, until source of reflection is visible
	if (!self->enemy && self->movetarget && (self->movetarget->flags & FL_REFLECT) && !visible(self, self->movetarget->owner))
	{
		M_MoveToGoal(self, dist);
		return;
	}

	// Lazarus: If we're following the leader and have no enemy, go ahead
	if (!self->enemy && (self->monsterinfo.aiflags & AI_FOLLOW_LEADER))
	{
		self->movetarget = self->goalentity = self->monsterinfo.leader;

		if (!self->movetarget)
		{
			self->monsterinfo.pausetime = level.time + 2;
			self->monsterinfo.stand(self);

			return;
		}

		self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
		self->monsterinfo.pausetime = 0;

		M_MoveToGoal(self, dist);
		return;
	}

	// If monster is looking for a trail
	if (self->monsterinfo.aiflags & AI_HINT_PATH)
	{
		M_MoveToGoal(self, dist); // Find a new path
		if (!self->inuse)
			return;

		// Foremost, look for thine enemy, not his echoes
		edict_t *realenemy;
		if (self->enemy && self->enemy->inuse && strcmp(self->enemy->classname, "player_noise"))
		{
			realenemy = self->enemy;
		}
		else if (self->enemy && self->enemy->inuse && self->enemy->owner)
		{
			realenemy = self->enemy->owner;
		}
		else
		{
			// No enemy or enemy went away or don't know thy enemy
			self->enemy = NULL;
			hintpath_stop(self);

			return;
		}

		if (coop && coop->value)
		{
			// if enemy is visible, ATTACK!
			if (self->enemy && visible(self, realenemy))
				pounce = true;
			else
				FindTarget(self); //else let FindTarget handle it
		}
		else
		{
			// Lazarus: special case for medics with AI_MEDIC and AI_HINT_PATH set. If range is farther than MEDIC_MAX_HEAL_DISTANCE we essentially lie... pretend the enemy isn't seen.
			if (self->monsterinfo.aiflags & AI_MEDIC && realrange(self, realenemy) > MEDIC_MAX_HEAL_DISTANCE)
			{
				// Since we're on a hint_path trying to get in position to heal monster, rather than actually healing him, allow more time
				self->timestamp = level.time + MEDIC_TRY_TIME;

				return;
			}

			// If enemy is visible, ATTACK!
			if (self->enemy && visible(self, realenemy))
				pounce = true;
		}

		// if we attack, stop following trail
		if (pounce) 
			hintpath_stop(self);

		return;
	}

	if (self->monsterinfo.aiflags & AI_SOUND_TARGET)
	{
		if (self->enemy)
			VectorSubtract(self->s.origin, self->enemy->s.origin, v);

		if (!self->enemy || VectorLength(v) < 64)
		{
			self->monsterinfo.aiflags |= (AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
			self->monsterinfo.stand(self);

			return;
		}

		M_MoveToGoal(self, dist);
		alreadyMoved = true; // this prevents double moves
		if (!self->inuse)
			return;

		if (!FindTarget(self))
			return;
	}

	if (ai_checkattack(self, dist))
		return;

	if (self->monsterinfo.attack_state == AS_SLIDING)
	{
		ai_run_slide(self, dist);
		return;
	}

	if (self->enemy && self->enemy->inuse && enemy_vis)
	{
		if (!alreadyMoved)
			M_MoveToGoal(self, dist);

		if (!self->inuse)
			return;

		self->monsterinfo.aiflags &= ~AI_LOST_SIGHT;
		VectorCopy(self->enemy->s.origin, self->monsterinfo.last_sighting);
		self->monsterinfo.trail_time = level.time;

		return;
	}

	// If monster has been hunting player for > 3 secs without avail, and for the last 5 secs hasn't bothered to update the trail...
	if (self->monsterinfo.trail_time + HINT_PATH_START_TIME < level.time && self->monsterinfo.last_hint_time + HINT_PATH_RESTART_TIME < level.time)
	{
		// ...then go find a path, you foo!
		self->monsterinfo.last_hint_time = level.time;
		if (hintcheck_monsterlost(self)) return;
	}

	// coop will change to another enemy if visible
	if (coop->value && FindTarget(self)) // FIXME: insane guys get mad with FindTarget, which causes crashes!
		return;

	// Lazarus: for medics, IF hint_paths are present then cut back a bit on max search time and let him go idle so he'll start tracking hint_paths
	if (self->monsterinfo.search_time)
	{
		if (!Q_stricmp(self->classname, "monster_medic") && hint_chains_exist)
		{
			if (developer->value)
				gi.dprintf("medic search_time=%g\n", level.time - self->monsterinfo.search_time);

			if (level.time > self->monsterinfo.search_time + 15)
			{
				if (developer->value)
					gi.dprintf("medic search timeout, going idle\n");

				if (!alreadyMoved)
					M_MoveToGoal(self, dist);

				self->monsterinfo.search_time = 0;

				if (self->goalentity == self->enemy)
					self->goalentity = NULL;

				if (self->movetarget == self->enemy)
					self->movetarget = NULL;

				self->enemy = self->oldenemy = NULL;
				self->monsterinfo.pausetime = level.time + 2;
				self->monsterinfo.stand(self);

				return;
			}
		}
		else if (level.time > (self->monsterinfo.search_time + 20))
		{
			if (!alreadyMoved)
				M_MoveToGoal(self, dist);

			self->monsterinfo.search_time = 0;

			return;
		}
	}

	edict_t *save = self->goalentity;
	edict_t *tempgoal = G_Spawn();
	self->goalentity = tempgoal;

	qboolean new = false;

	if (!(self->monsterinfo.aiflags & AI_LOST_SIGHT))
	{
		// Just lost sight of the player, decide where to go first
		self->monsterinfo.aiflags |= (AI_LOST_SIGHT | AI_PURSUIT_LAST_SEEN);
		self->monsterinfo.aiflags &= ~(AI_PURSUE_NEXT | AI_PURSUE_TEMP);
		new = true;
	}

	if (self->monsterinfo.aiflags & AI_PURSUE_NEXT)
	{
		self->monsterinfo.aiflags &= ~AI_PURSUE_NEXT;

		// Give ourself more time since we got this far
		self->monsterinfo.search_time = level.time + 5;

		if (self->monsterinfo.aiflags & AI_PURSUE_TEMP)
		{
			self->monsterinfo.aiflags &= ~AI_PURSUE_TEMP;
			marker = NULL;
			VectorCopy(self->monsterinfo.saved_goal, self->monsterinfo.last_sighting);
			new = true;
		}
		else if (self->monsterinfo.aiflags & AI_PURSUIT_LAST_SEEN)
		{
			self->monsterinfo.aiflags &= ~AI_PURSUIT_LAST_SEEN;
			marker = PlayerTrail_PickFirst(self);
		}
		else
		{
			marker = PlayerTrail_PickNext(self);
		}

		if (marker)
		{
			VectorCopy(marker->s.origin, self->monsterinfo.last_sighting);
			self->monsterinfo.trail_time = marker->timestamp;
			self->s.angles[YAW] = self->ideal_yaw = marker->s.angles[YAW];
			new = true;
		}
	}

	VectorSubtract(self->s.origin, self->monsterinfo.last_sighting, v);
	float d1 = VectorLength(v);
	if (d1 <= dist)
	{
		self->monsterinfo.aiflags |= AI_PURSUE_NEXT;
		dist = d1;
	}

	VectorCopy(self->monsterinfo.last_sighting, self->goalentity->s.origin);

	if (new)
	{
		trace_t tr = gi.trace(self->s.origin, self->mins, self->maxs, self->monsterinfo.last_sighting, self, MASK_PLAYERSOLID);
		if (tr.fraction < 1)
		{
			vec3_t v_forward, v_right;
			
			VectorSubtract(self->goalentity->s.origin, self->s.origin, v);
			d1 = VectorLength(v);
			float center = tr.fraction;
			const float d2 = d1 * ((center + 1) / 2);
			self->s.angles[YAW] = self->ideal_yaw = vectoyaw(v);
			AngleVectors(self->s.angles, v_forward, v_right, NULL);

			vec3_t left_target;
			VectorSet(v, d2, -16, 0);
			G_ProjectSource(self->s.origin, v, v_forward, v_right, left_target);
			tr = gi.trace(self->s.origin, self->mins, self->maxs, left_target, self, MASK_PLAYERSOLID);
			const float left = tr.fraction;

			vec3_t right_target;
			VectorSet(v, d2, 16, 0);
			G_ProjectSource(self->s.origin, v, v_forward, v_right, right_target);
			tr = gi.trace(self->s.origin, self->mins, self->maxs, right_target, self, MASK_PLAYERSOLID);
			const float right = tr.fraction;

			center = d1 * center / d2;
			if (left >= center && left > right)
			{
				if (left < 1)
				{
					VectorSet(v, d2 * left * 0.5, -16, 0);
					G_ProjectSource(self->s.origin, v, v_forward, v_right, left_target);
				}

				VectorCopy(self->monsterinfo.last_sighting, self->monsterinfo.saved_goal);
				self->monsterinfo.aiflags |= AI_PURSUE_TEMP;
				VectorCopy(left_target, self->goalentity->s.origin);
				VectorCopy(left_target, self->monsterinfo.last_sighting);
				VectorSubtract(self->goalentity->s.origin, self->s.origin, v);
				self->s.angles[YAW] = self->ideal_yaw = vectoyaw(v);
			}
			else if (right >= center && right > left)
			{
				if (right < 1)
				{
					VectorSet(v, d2 * right * 0.5, 16, 0);
					G_ProjectSource(self->s.origin, v, v_forward, v_right, right_target);
				}

				VectorCopy(self->monsterinfo.last_sighting, self->monsterinfo.saved_goal);
				self->monsterinfo.aiflags |= AI_PURSUE_TEMP;
				VectorCopy(right_target, self->goalentity->s.origin);
				VectorCopy(right_target, self->monsterinfo.last_sighting);
				VectorSubtract(self->goalentity->s.origin, self->s.origin, v);
				self->s.angles[YAW] = self->ideal_yaw = vectoyaw(v);
			}
		}
	}

	M_MoveToGoal(self, dist);
	if (!self->inuse)
		return;

	G_FreeEdict(tempgoal);

	if (self)
		self->goalentity = save;
}

static int chase_angle[] = { 270, 450, 225, 495, 540 };
qboolean ai_chicken(edict_t *self, edict_t *badguy)
{
	vec3_t	dir, best_dir, end, forward;
	vec3_t	mins, maxs;
	vec3_t	testpos;
	vec_t	best_dist = 0;

	// No point in hiding from attacker if he's gone
	if (!badguy || !badguy->inuse)
		return false;

	if (!self || !self->inuse || self->health <= 0)
		return false;

	if (!actorchicken->value)
		return false;

	// If we've already been here, quit
	if (self->monsterinfo.aiflags & AI_CHICKEN && self->movetarget && !Q_stricmp(self->movetarget->classname, "thing"))
		return true;

	VectorCopy(self->mins, mins);
	mins[2] = min(0, mins[2] + 18);
	VectorCopy(self->maxs, maxs);

	// Find a vector that will hide the actor from his enemy
	vec3_t atk;
	VectorCopy(badguy->s.origin, atk);
	atk[2] += badguy->viewheight;
	VectorClear(best_dir);
	AngleVectors(self->s.angles, forward, NULL, NULL);
	dir[2] = 0;

	for (int travel = 512; travel > 63 && best_dist == 0; travel /= 2)
	{
		for (int i = 0; i < 5 && best_dist == 0; i++)
		{
			vec_t yaw = self->s.angles[YAW] + chase_angle[i];
			yaw = (int)(yaw / 45) * 45;
			yaw = anglemod(yaw);
			yaw *= M_PI / 180;

			dir[0] = cos(yaw);
			dir[1] = sin(yaw);
			VectorMA(self->s.origin, travel, dir, end);
			trace_t trace1 = gi.trace(self->s.origin, mins, maxs, end, self, MASK_MONSTERSOLID);
			
			// Test whether proposed position can be seen by badguy. Test isn't foolproof - tests against 1) new origin, 2) new origin + maxs,
			// 3) new origin + mins, and 4) new origin + min x, y, max z.
			trace_t trace2 = gi.trace(trace1.endpos, NULL, NULL, atk, self, MASK_SOLID);
			if (trace2.fraction == 1.0)
				continue;

			VectorAdd(trace1.endpos, self->maxs, testpos);
			trace2 = gi.trace(testpos, NULL, NULL, atk, self, MASK_SOLID);
			if (trace2.fraction == 1.0)
				continue;

			VectorAdd(trace1.endpos, self->mins, testpos);
			trace2 = gi.trace(testpos, NULL, NULL, atk, self, MASK_SOLID);
			if (trace2.fraction == 1.0)
				continue;

			testpos[2] = trace1.endpos[2] + self->maxs[2];
			trace2 = gi.trace(testpos, NULL, NULL, atk, self, MASK_SOLID);
			if (trace2.fraction == 1.0)
				continue;

			best_dist = trace1.fraction * travel;
			if (best_dist < 32) // not much point to this move
				continue;

			VectorCopy(dir, best_dir);
		}
	}

	return false;

	/*if (best_dist < 32)
		return false;

	// This snaps the angles, which may not be all that good but it sure
	// is quicker than turning in place
	vectoangles(best_dir,self->s.angles);
	thing = SpawnThing();
	VectorMA(self->s.origin,best_dist,best_dir,thing->s.origin);
	thing->touch_debounce_time = level.time + 3.0;
	thing->target_ent = self;
	ED_CallSpawn(thing);
	self->movetarget = self->goalentity = thing;
	self->monsterinfo.aiflags &= ~(AI_SOUND_TARGET | AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
	self->monsterinfo.pausetime = 0;
	self->monsterinfo.aiflags |= (AI_CHASE_THING | AI_CHICKEN);
	gi.linkentity(self);
	self->monsterinfo.run(self);
	self->monsterinfo.chicken_framenum = level.framenum;
	return true;*/
}