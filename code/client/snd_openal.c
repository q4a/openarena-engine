/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2005 Stuart Dalton (badcdev@gmail.com)

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "snd_local.h"
#include "snd_codec.h"
#include "client.h"

#if USE_OPENAL

#include "qal.h"

// Console variables specific to OpenAL
cvar_t *s_alPrecache;
cvar_t *s_alGain;
cvar_t *s_alSources;
cvar_t *s_alDopplerFactor;
cvar_t *s_alDopplerSpeed;
cvar_t *s_alMinDistance;
cvar_t *s_alRolloff;
cvar_t *s_alDriver;

/*
=================
S_AL_Format
=================
*/
ALuint S_AL_Format(int width, int channels)
{
	ALuint format = AL_FORMAT_MONO16;

	// Work out format
	if(width == 1)
	{
		if(channels == 1)
			format = AL_FORMAT_MONO8;
		else if(channels == 2)
			format = AL_FORMAT_STEREO8;
	}
	else if(width == 2)
	{
		if(channels == 1)
			format = AL_FORMAT_MONO16;
		else if(channels == 2)
			format = AL_FORMAT_STEREO16;
	}

	return format;
}

/*
=================
S_AL_ErrorMsg
=================
*/
char *S_AL_ErrorMsg(ALenum error)
{
	switch(error)
	{
		case AL_NO_ERROR:
			return "No error";
		case AL_INVALID_NAME:
			return "Invalid name";
		case AL_INVALID_ENUM:
			return "Invalid enumerator";
		case AL_INVALID_VALUE:
			return "Invalid value";
		case AL_INVALID_OPERATION:
			return "Invalid operation";
		case AL_OUT_OF_MEMORY:
			return "Out of memory";
		default:
			return "Unknown error";
	}
}


//===========================================================================


typedef struct alSfx_s
{
	char						filename[MAX_QPATH];
	ALuint					buffer;		// OpenAL buffer
	qboolean				isDefault;	// Couldn't be loaded - use default FX
	qboolean				inMemory;	// Sound is stored in memory
	qboolean				isLocked;	// Sound is locked (can not be unloaded)
	int							used;		// Time last used
	struct alSfx_t	*next;		// Next entry in hash list
} alSfx_t;

static qboolean alBuffersInitialised = qfalse;

// Sound effect storage, data structures
#define MAX_SFX 4096
static alSfx_t knownSfx[MAX_SFX];
static int numSfx;

static sfxHandle_t default_sfx;

/*
=================
S_AL_BufferFindFree

Find a free handle
=================
*/
static sfxHandle_t S_AL_BufferFindFree( void )
{
	int i;

	for(i = 0; i < MAX_SFX; i++)
	{
		// Got one
		if(knownSfx[i].filename[0] == '\0')
			return i;
	}

	// Shit...
	Com_Error(ERR_FATAL, "S_AL_BufferFindFree: No free sound handles");
	return -1;
}

/*
=================
S_AL_BufferFind

Find a sound effect if loaded, set up a handle otherwise
=================
*/
static sfxHandle_t S_AL_BufferFind(const char *filename)
{
	// Look it up in the hash table
	sfxHandle_t sfx = -1;
	int i;

	for(i = 0; i < MAX_SFX; i++)
	{
		if(!Q_stricmp(knownSfx[i].filename, filename))
		{
			sfx = i;
			break;
		}
	}

	// Not found in hash table?
	if(sfx == -1)
	{
		alSfx_t *ptr;

		sfx = S_AL_BufferFindFree();

		// Clear and copy the filename over
		ptr = &knownSfx[sfx];
		memset(ptr, 0, sizeof(*ptr));
		strcpy(ptr->filename, filename);
	}

	// Return the handle
	return sfx;
}

/*
=================
S_AL_BufferUseDefault
=================
*/
static void S_AL_BufferUseDefault(sfxHandle_t sfx)
{
	if(sfx == default_sfx)
		Com_Error(ERR_FATAL, "Can't load default sound effect %s\n", knownSfx[sfx].filename);

	Com_Printf( "Warning: Using default sound for %s\n", knownSfx[sfx].filename);
	knownSfx[sfx].isDefault = qtrue;
	knownSfx[sfx].buffer = knownSfx[default_sfx].buffer;
}

/*
=================
S_AL_BufferEvict
	
Doesn't work yet, so if OpenAL reports that you're out of memory, you'll just
get "Catastrophic sound memory exhaustion". Whoops.
=================
*/
static qboolean S_AL_BufferEvict( void )
{
	return qfalse;
}

/*
=================
S_AL_BufferLoad
=================
*/
static void S_AL_BufferLoad(sfxHandle_t sfx)
{
	ALenum error;

	void *data;
	snd_info_t info;
	ALuint format;

	// Nothing?
	if(knownSfx[sfx].filename[0] == '\0')
		return;

	// Player SFX
	if(knownSfx[sfx].filename[0] == '*')
		return;

	// Already done?
	if((knownSfx[sfx].inMemory) || (knownSfx[sfx].isDefault))
		return;

	// Try to load
	data = S_CodecLoad(knownSfx[sfx].filename, &info);
	if(!data)
	{
		Com_Printf( "Can't load %s\n", knownSfx[sfx].filename);
		S_AL_BufferUseDefault(sfx);
		return;
	}

	format = S_AL_Format(info.width, info.channels);

	// Create a buffer
	qalGenBuffers(1, &knownSfx[sfx].buffer);
	if((error = qalGetError()) != AL_NO_ERROR)
	{
		S_AL_BufferUseDefault(sfx);
		Z_Free(data);
		Com_Printf( "Can't create a sound buffer for %s - %s\n", knownSfx[sfx].filename, S_AL_ErrorMsg(error));
		return;
	}

	// Fill the buffer
	qalGetError();
	qalBufferData(knownSfx[sfx].buffer, format, data, info.size, info.rate);
	error = qalGetError();

	// If we ran out of memory, start evicting the least recently used sounds
	while(error == AL_OUT_OF_MEMORY)
	{
		qboolean rv = S_AL_BufferEvict();
		if(!rv)
		{
			S_AL_BufferUseDefault(sfx);
			Z_Free(data);
			Com_Printf( "Out of memory loading %s\n", knownSfx[sfx].filename);
			return;
		}

		// Try load it again
		qalGetError();
		qalBufferData(knownSfx[sfx].buffer, format, data, info.size, info.rate);
		error = qalGetError();
	}

	// Some other error condition
	if(error != AL_NO_ERROR)
	{
		S_AL_BufferUseDefault(sfx);
		Z_Free(data);
		Com_Printf( "Can't fill sound buffer for %s - %s", knownSfx[sfx].filename, S_AL_ErrorMsg(error));
		return;
	}

	// Free the memory
	Z_Free(data);

	// Woo!
	knownSfx[sfx].inMemory = qtrue;
}

/*
=================
S_AL_BufferUse
=================
*/
void S_AL_BufferUse(sfxHandle_t sfx)
{
	if(knownSfx[sfx].filename[0] == '\0')
		return;

	if((!knownSfx[sfx].inMemory) && (!knownSfx[sfx].isDefault))
		S_AL_BufferLoad(sfx);
	knownSfx[sfx].used = Com_Milliseconds();
}

/*
=================
S_AL_BufferInit
=================
*/
qboolean S_AL_BufferInit( void )
{
	if(alBuffersInitialised)
		return qtrue;

	// Clear the hash table, and SFX table
	memset(knownSfx, 0, sizeof(knownSfx));
	numSfx = 0;

	// Load the default sound, and lock it
	default_sfx = S_AL_BufferFind("sound/feedback/hit.wav");
	S_AL_BufferUse(default_sfx);
	knownSfx[default_sfx].isLocked = qtrue;

	// All done
	alBuffersInitialised = qtrue;
	return qtrue;
}

/*
=================
S_AL_BufferUnload
=================
*/
static void S_AL_BufferUnload(sfxHandle_t sfx)
{
	ALenum error;

	if(knownSfx[sfx].filename[0] == '\0')
		return;

	if(!knownSfx[sfx].inMemory)
		return;

	// Delete it
	qalDeleteBuffers(1, &knownSfx[sfx].buffer);
	if((error = qalGetError()) != AL_NO_ERROR)
		Com_Printf( "Can't delete sound buffer for %s", knownSfx[sfx].filename);

	knownSfx[sfx].inMemory = qfalse;
}

/*
=================
S_AL_BufferShutdown
=================
*/
void S_AL_BufferShutdown( void )
{
	int i;

	if(!alBuffersInitialised)
		return;

	// Unlock the default sound effect
	knownSfx[default_sfx].isLocked = qfalse;

	// Free all used effects
	for(i = 0; i < MAX_SFX; i++)
		S_AL_BufferUnload(i);

	// Clear the tables
	memset(knownSfx, 0, sizeof(knownSfx));

	// All undone
	alBuffersInitialised = qfalse;
}

/*
=================
S_AL_RegisterSound
=================
*/
sfxHandle_t S_AL_RegisterSound( const char *sample, qboolean compressed )
{
	sfxHandle_t sfx = S_AL_BufferFind(sample);

	if((s_alPrecache->integer == 1) && (!knownSfx[sfx].inMemory) && (!knownSfx[sfx].isDefault))
		S_AL_BufferLoad(sfx);
	knownSfx[sfx].used = Com_Milliseconds();

	return sfx;
}

/*
=================
S_AL_BufferGet

Return's an sfx's buffer
=================
*/
ALuint S_AL_BufferGet(sfxHandle_t sfx)
{
	return knownSfx[sfx].buffer;
}


//===========================================================================


typedef struct src_s
{
	ALuint					source;			// OpenAL source object
	sfxHandle_t 		sfx;				// Sound effect in use

	int							lastUse;		// Last time used
	alSrcPriority_t	priority;		// Priority
	int							entity;			// Owning entity (-1 if none)
	int							channel;		// Associated channel (-1 if none)

	int							isActive;		// Is this source currently in use?
	int							isLocked;		// This is locked (un-allocatable)
	int							isLooping;	// Is this a looping effect (attached to an entity)
	int							isTracking;	// Is this object tracking it's owner

	qboolean				local;			// Is this local (relative to the cam)
} src_t;

#define MAX_SRC 128
static src_t srcList[MAX_SRC];
static int srcCount = 0;
static qboolean alSourcesInitialised = qfalse;

static int ambientCount = 0;

typedef struct sentity_s
{
	vec3_t origin;		// Object position

	int has_sfx;		// Associated sound source
	int sfx;
	int touched;		// Sound present this update?
} sentity_t;

static sentity_t entityList[MAX_GENTITIES];

/*
=================
S_AL_SrcInit
=================
*/
qboolean S_AL_SrcInit( void )
{
	int i;
	int limit;
	ALenum error;

	// Clear the sources data structure
	memset(srcList, 0, sizeof(srcList));
	srcCount = 0;

	// Cap s_sources to MAX_SRC
	limit = s_alSources->integer;
	if(limit > MAX_SRC)
		limit = MAX_SRC;
	else if(limit < 16)
		limit = 16;

	// Allocate as many sources as possible
	for(i = 0; i < limit; i++)
	{
		qalGenSources(1, &srcList[i].source);
		if((error = qalGetError()) != AL_NO_ERROR)
			break;
		srcCount++;
	}

	// All done. Print this for informational purposes
	Com_Printf( "Allocated %d sources.\n", srcCount);
	alSourcesInitialised = qtrue;
	return qtrue;
}

/*
=================
S_AL_SrcShutdown
=================
*/
void S_AL_SrcShutdown( void )
{
	int i;

	if(!alSourcesInitialised)
		return;

	// Destroy all the sources
	for(i = 0; i < srcCount; i++)
	{
		if(srcList[i].isLocked)
			Com_DPrintf("Warning: Source %d is locked\n", i);

		qalSourceStop(srcList[i].source);
		qalDeleteSources(1, &srcList[i].source);
	}

	memset(srcList, 0, sizeof(srcList));

	alSourcesInitialised = qfalse;
}

/*
=================
S_AL_SrcSetup
=================
*/
static void S_AL_SrcSetup(srcHandle_t src, sfxHandle_t sfx, alSrcPriority_t priority,
		int entity, int channel, qboolean local)
{
	ALuint buffer;
	float null_vector[] = {0, 0, 0};

	// Mark the SFX as used, and grab the raw AL buffer
	S_AL_BufferUse(sfx);
	buffer = S_AL_BufferGet(sfx);

	// Set up src struct
	srcList[src].lastUse = Sys_Milliseconds();
	srcList[src].sfx = sfx;
	srcList[src].priority = priority;
	srcList[src].entity = entity;
	srcList[src].channel = channel;
	srcList[src].isActive = qtrue;
	srcList[src].isLocked = qfalse;
	srcList[src].isLooping = qfalse;
	srcList[src].isTracking = qfalse;
	srcList[src].local = local;

	// Set up OpenAL source
	qalSourcei(srcList[src].source, AL_BUFFER, buffer);
	qalSourcef(srcList[src].source, AL_PITCH, 1.0f);
	qalSourcef(srcList[src].source, AL_GAIN, s_alGain->value * s_volume->value);
	qalSourcefv(srcList[src].source, AL_POSITION, null_vector);
	qalSourcefv(srcList[src].source, AL_VELOCITY, null_vector);
	qalSourcei(srcList[src].source, AL_LOOPING, AL_FALSE);
	qalSourcef(srcList[src].source, AL_REFERENCE_DISTANCE, s_alMinDistance->value);

	if(local)
	{
		qalSourcei(srcList[src].source, AL_SOURCE_RELATIVE, AL_TRUE);
		qalSourcef(srcList[src].source, AL_ROLLOFF_FACTOR, 0);
	}
	else
	{
		qalSourcei(srcList[src].source, AL_SOURCE_RELATIVE, AL_FALSE);
		qalSourcef(srcList[src].source, AL_ROLLOFF_FACTOR, s_alRolloff->value);
	}
}

/*
=================
S_AL_SrcKill
=================
*/
static void S_AL_SrcKill(srcHandle_t src)
{
	// I'm not touching it. Unlock it first.
	if(srcList[src].isLocked)
		return;

	// Stop it if it's playing
	if(srcList[src].isActive)
		qalSourceStop(srcList[src].source);

	// Remove the entity association
	if((srcList[src].isLooping) && (srcList[src].entity != -1))
	{
		int ent = srcList[src].entity;
		entityList[ent].has_sfx = 0;
		entityList[ent].sfx = -1;
		entityList[ent].touched = qfalse;
	}

	// Remove the buffer
	qalSourcei(srcList[src].source, AL_BUFFER, 0);

	srcList[src].sfx = 0;
	srcList[src].lastUse = 0;
	srcList[src].priority = 0;
	srcList[src].entity = -1;
	srcList[src].channel = -1;
	srcList[src].isActive = qfalse;
	srcList[src].isLocked = qfalse;
	srcList[src].isLooping = qfalse;
	srcList[src].isTracking = qfalse;
}

/*
=================
S_AL_SrcAlloc
=================
*/
srcHandle_t S_AL_SrcAlloc( alSrcPriority_t priority, int entnum, int channel )
{
	int i;
	int empty = -1;
	int weakest = -1;
	int weakest_time = Sys_Milliseconds();
	int weakest_pri = 999;

	for(i = 0; i < srcCount; i++)
	{
		// If it's locked, we aren't even going to look at it
		if(srcList[i].isLocked)
			continue;

		// Is it empty or not?
		if((!srcList[i].isActive) && (empty == -1))
			empty = i;
		else if(srcList[i].priority < priority)
		{
			// If it's older or has lower priority, flag it as weak
			if((srcList[i].priority < weakest_pri) ||
				(srcList[i].lastUse < weakest_time))
			{
				weakest_pri = srcList[i].priority;
				weakest_time = srcList[i].lastUse;
				weakest = i;
			}
		}

		// Is it an exact match, and not on channel 0?
		if((srcList[i].entity == entnum) && (srcList[i].channel == channel) && (channel != 0))
		{
			S_AL_SrcKill(i);
			return i;
		}
	}

	// Do we have an empty one?
	if(empty != -1)
		return empty;

	// No. How about an overridable one?
	if(weakest != -1)
	{
		S_AL_SrcKill(weakest);
		return weakest;
	}

	// Nothing. Return failure (cries...)
	return -1;
}

/*
=================
S_AL_SrcFind

Finds an active source with matching entity and channel numbers
Returns -1 if there isn't one
=================
*/
srcHandle_t S_AL_SrcFind(int entnum, int channel)
{
	int i;
	for(i = 0; i < srcCount; i++)
	{
		if(!srcList[i].isActive)
			continue;
		if((srcList[i].entity == entnum) && (srcList[i].channel == channel))
			return i;
	}
	return -1;
}

/*
=================
S_AL_SrcLock

Locked sources will not be automatically reallocated or managed
=================
*/
void S_AL_SrcLock(srcHandle_t src)
{
	srcList[src].isLocked = qtrue;
}

/*
=================
S_AL_SrcUnlock

Once unlocked, the source may be reallocated again
=================
*/
void S_AL_SrcUnlock(srcHandle_t src)
{
	srcList[src].isLocked = qfalse;
}

/*
=================
S_AL_UpdateEntityPosition
=================
*/
void S_AL_UpdateEntityPosition( int entityNum, const vec3_t origin )
{
	if ( entityNum < 0 || entityNum > MAX_GENTITIES )
		Com_Error( ERR_DROP, "S_UpdateEntityPosition: bad entitynum %i", entityNum );
	VectorCopy( origin, entityList[entityNum].origin );
}

/*
=================
S_AL_StartLocalSound

Play a local (non-spatialized) sound effect
=================
*/
void S_AL_StartLocalSound(sfxHandle_t sfx, int channel)
{
	// Try to grab a source
	srcHandle_t src = S_AL_SrcAlloc(SRCPRI_LOCAL, -1, channel);
	if(src == -1)
		return;

	// Set up the effect
	S_AL_SrcSetup(src, sfx, SRCPRI_LOCAL, -1, channel, qtrue);

	// Start it playing
	qalSourcePlay(srcList[src].source);
}

#define POSITION_SCALE 1.0f

/*
=================
S_AL_StartSound

Play a one-shot sound effect
=================
*/
void S_AL_StartSound( vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx )
{
	vec3_t sorigin;

	// Try to grab a source
	srcHandle_t src = S_AL_SrcAlloc(SRCPRI_ONESHOT, entnum, entchannel);
	if(src == -1)
		return;

	// Set up the effect
	S_AL_SrcSetup(src, sfx, SRCPRI_ONESHOT, entnum, entchannel, qfalse);

	if(origin == NULL)
	{
		srcList[src].isTracking = qtrue;
		VectorScale(entityList[entnum].origin, POSITION_SCALE, sorigin);
	}
	else
		VectorScale(origin, POSITION_SCALE, sorigin);
	qalSourcefv(srcList[src].source, AL_POSITION, sorigin);

	// Start it playing
	qalSourcePlay(srcList[src].source);
}

/*
=================
S_AL_ClearLoopingSounds
=================
*/
void S_AL_ClearLoopingSounds( qboolean killall )
{
	int i;
	for(i = 0; i < srcCount; i++)
	{
		if((srcList[i].isLooping) && (srcList[i].entity != -1))
			entityList[srcList[i].entity].touched = qfalse;
	}
}

/*
=================
S_AL_SrcLoop
=================
*/
static void S_AL_SrcLoop( alSrcPriority_t priority, sfxHandle_t sfx,
		const vec3_t origin, const vec3_t velocity, int entnum)
{
	int src;
	qboolean need_to_play = qfalse;
	vec3_t sorigin;

	// Do we need to start a new sound playing?
	if(!entityList[entnum].has_sfx)
	{
		// Try to get a channel
		ambientCount++;
		src = S_AL_SrcAlloc(priority, entnum, -1);
		if(src == -1)
			return;
		need_to_play = qtrue;
	}
	else if(srcList[entityList[entnum].sfx].sfx != sfx)
	{
		// Need to restart. Just re-use this channel
		src = entityList[entnum].sfx;
		S_AL_SrcKill(src);
		need_to_play = qtrue;
	}
	else
		src = entityList[entnum].sfx;

	if(need_to_play)
	{
		// Set up the effect
		S_AL_SrcSetup(src, sfx, priority, entnum, -1, qfalse);
		qalSourcei(srcList[src].source, AL_LOOPING, AL_TRUE);
		srcList[src].isLooping = qtrue;

		// Set up the entity
		entityList[entnum].has_sfx = qtrue;
		entityList[entnum].sfx = src;
		need_to_play = qtrue;
	}

	// Set up the position and velocity
	VectorScale(entityList[entnum].origin, POSITION_SCALE, sorigin);
	qalSourcefv(srcList[src].source, AL_POSITION, sorigin);
	qalSourcefv(srcList[src].source, AL_VELOCITY, velocity);

	// Flag it
	entityList[entnum].touched = qtrue;

	// Play if need be
	if(need_to_play)
		qalSourcePlay(srcList[src].source);
}

/*
=================
S_AL_AddLoopingSound
=================
*/
void S_AL_AddLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx )
{
	S_AL_SrcLoop(SRCPRI_AMBIENT, sfx, origin, velocity, entityNum);
}

/*
=================
S_AL_AddRealLoopingSound
=================
*/
void S_AL_AddRealLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx )
{
	S_AL_SrcLoop(SRCPRI_ENTITY, sfx, origin, velocity, entityNum);
}

/*
=================
S_AL_StopLoopingSound
=================
*/
void S_AL_StopLoopingSound(int entityNum )
{
	if(entityList[entityNum].has_sfx)
		S_AL_SrcKill(entityList[entityNum].sfx);
}

/*
=================
S_AL_SrcUpdate

Update state (move things around, manage sources, and so on)
=================
*/
void S_AL_SrcUpdate( void )
{
	int i;
	int ent;
	ALint state;

	for(i = 0; i < srcCount; i++)
	{
		if(srcList[i].isLocked)
			continue;

		if(!srcList[i].isActive)
			continue;

		// Check if it's done, and flag it
		qalGetSourcei(srcList[i].source, AL_SOURCE_STATE, &state);
		if(state == AL_STOPPED)
		{
			S_AL_SrcKill(i);
			continue;
		}

		// Update source parameters
		if((s_alGain->modified)||(s_volume->modified))
			qalSourcef(srcList[i].source, AL_GAIN, s_alGain->value * s_volume->value);
		if((s_alRolloff->modified)&&(!srcList[i].local))
			qalSourcef(srcList[i].source, AL_ROLLOFF_FACTOR, s_alRolloff->value);
		if(s_alMinDistance->modified)
			qalSourcef(srcList[i].source, AL_REFERENCE_DISTANCE, s_alMinDistance->value);

		ent = srcList[i].entity;

		// If a looping effect hasn't been touched this frame, kill it
		if(srcList[i].isLooping)
		{
			if(!entityList[ent].touched)
			{
				ambientCount--;
				S_AL_SrcKill(i);
			}
			continue;
		}

		// See if it needs to be moved
		if(srcList[i].isTracking)
		{
			vec3_t sorigin;
			VectorScale(entityList[ent].origin, POSITION_SCALE, sorigin);
			qalSourcefv(srcList[i].source, AL_POSITION, entityList[ent].origin);
		}
	}
}

/*
=================
S_AL_SrcShutup
=================
*/
void S_AL_SrcShutup( void )
{
	int i;
	for(i = 0; i < srcCount; i++)
		S_AL_SrcKill(i);
}

/*
=================
S_AL_SrcGet
=================
*/
ALuint S_AL_SrcGet(srcHandle_t src)
{
	return srcList[src].source;
}


//===========================================================================


static srcHandle_t streamSourceHandle = -1;
static qboolean streamPlaying = qfalse;
static ALuint streamSource;

/*
=================
S_AL_AllocateStreamChannel
=================
*/
static void S_AL_AllocateStreamChannel( void )
{
	// Allocate a streamSource at high priority
	streamSourceHandle = S_AL_SrcAlloc(SRCPRI_STREAM, -2, 0);
	if(streamSourceHandle == -1)
		return;

	// Lock the streamSource so nobody else can use it, and get the raw streamSource
	S_AL_SrcLock(streamSourceHandle);
	streamSource = S_AL_SrcGet(streamSourceHandle);

	// Set some streamSource parameters
	qalSourcei (streamSource, AL_BUFFER,          0            );
	qalSourcei (streamSource, AL_LOOPING,         AL_FALSE     );
	qalSource3f(streamSource, AL_POSITION,        0.0, 0.0, 0.0);
	qalSource3f(streamSource, AL_VELOCITY,        0.0, 0.0, 0.0);
	qalSource3f(streamSource, AL_DIRECTION,       0.0, 0.0, 0.0);
	qalSourcef (streamSource, AL_ROLLOFF_FACTOR,  0.0          );
	qalSourcei (streamSource, AL_SOURCE_RELATIVE, AL_TRUE      );
}

/*
=================
S_AL_FreeStreamChannel
=================
*/
static void S_AL_FreeStreamChannel( void )
{
	// Release the output streamSource
	S_AL_SrcUnlock(streamSourceHandle);
	streamSource = 0;
	streamSourceHandle = -1;
}

/*
=================
S_AL_RawSamples
=================
*/
void S_AL_RawSamples(int samples, int rate, int width, int channels, const byte *data, float volume)
{
	ALuint buffer;
	ALuint format = AL_FORMAT_STEREO16;
	ALint state;

	// Work out AL format
	if(width == 1)
	{
		if(channels == 1)
			format = AL_FORMAT_MONO8;
		else if(channels == 2)
			format = AL_FORMAT_STEREO8;
	}
	else if(width == 2)
	{
		if(channels == 1)
			format = AL_FORMAT_MONO16;
		else if(channels == 2)
			format = AL_FORMAT_STEREO16;
	}

	// Create the streamSource if necessary
	if(streamSourceHandle == -1)
	{
		S_AL_AllocateStreamChannel();
	
		// Failed?
		if(streamSourceHandle == -1)
		{
			Com_Printf( "Can't allocate streaming streamSource\n");
			return;
		}
	}

	// Create a buffer, and stuff the data into it
	qalGenBuffers(1, &buffer);
	qalBufferData(buffer, format, data, (samples * width * channels), rate);

	// Shove the data onto the streamSource
	qalSourceQueueBuffers(streamSource, 1, &buffer);

	// Start the streamSource playing if necessary
	qalGetSourcei(streamSource, AL_SOURCE_STATE, &state);

	// Volume
	qalSourcef (streamSource, AL_GAIN, volume * s_volume->value * s_alGain->value);

	if(!streamPlaying)
	{
		qalSourcePlay(streamSource);
		streamPlaying = qtrue;
	}
}

/*
=================
S_AL_StreamUpdate
=================
*/
void S_AL_StreamUpdate( void )
{
	int processed;
	ALint state;

	if(streamSourceHandle == -1)
		return;

	// Un-queue any buffers, and delete them
	qalGetSourcei(streamSource, AL_BUFFERS_PROCESSED, &processed);
	if(processed)
	{
		while(processed--)
		{
			ALuint buffer;
			qalSourceUnqueueBuffers(streamSource, 1, &buffer);
			qalDeleteBuffers(1, &buffer);
		}
	}

	// If it's stopped, release the streamSource
	qalGetSourcei(streamSource, AL_SOURCE_STATE, &state);
	if(state == AL_STOPPED)
	{
		streamPlaying = qfalse;
		qalSourceStop(streamSource);
		S_AL_FreeStreamChannel();
	}
}

/*
=================
S_AL_StreamDie
=================
*/
void S_AL_StreamDie( void )
{
	if(streamSourceHandle == -1)
		return;

	streamPlaying = qfalse;
	qalSourceStop(streamSource);
	S_AL_FreeStreamChannel();
}


//===========================================================================


#define NUM_MUSIC_BUFFERS	4
#define	MUSIC_BUFFER_SIZE 4096

static qboolean musicPlaying = qfalse;
static srcHandle_t musicSourceHandle = -1;
static ALuint musicSource;
static ALuint musicBuffers[NUM_MUSIC_BUFFERS];

static snd_stream_t *mus_stream;
static char s_backgroundLoop[MAX_QPATH];

static byte decode_buffer[MUSIC_BUFFER_SIZE];

/*
=================
S_AL_MusicSourceGet
=================
*/
static void S_AL_MusicSourceGet( void )
{
	// Allocate a musicSource at high priority
	musicSourceHandle = S_AL_SrcAlloc(SRCPRI_STREAM, -2, 0);
	if(musicSourceHandle == -1)
		return;

	// Lock the musicSource so nobody else can use it, and get the raw musicSource
	S_AL_SrcLock(musicSourceHandle);
	musicSource = S_AL_SrcGet(musicSourceHandle);

	// Set some musicSource parameters
	qalSource3f(musicSource, AL_POSITION,        0.0, 0.0, 0.0);
	qalSource3f(musicSource, AL_VELOCITY,        0.0, 0.0, 0.0);
	qalSource3f(musicSource, AL_DIRECTION,       0.0, 0.0, 0.0);
	qalSourcef (musicSource, AL_ROLLOFF_FACTOR,  0.0          );
	qalSourcei (musicSource, AL_SOURCE_RELATIVE, AL_TRUE      );
}

/*
=================
S_AL_MusicSourceFree
=================
*/
static void S_AL_MusicSourceFree( void )
{
	// Release the output musicSource
	S_AL_SrcUnlock(musicSourceHandle);
	musicSource = 0;
	musicSourceHandle = -1;
}

/*
=================
S_AL_StopBackgroundTrack
=================
*/
void S_AL_StopBackgroundTrack( void )
{
	if(!musicPlaying)
		return;

	// Stop playing
	qalSourceStop(musicSource);

	// De-queue the musicBuffers
	qalSourceUnqueueBuffers(musicSource, NUM_MUSIC_BUFFERS, musicBuffers);

	// Destroy the musicBuffers
	qalDeleteBuffers(NUM_MUSIC_BUFFERS, musicBuffers);

	// Free the musicSource
	S_AL_MusicSourceFree();

	// Unload the stream
	if(mus_stream)
		S_CodecCloseStream(mus_stream);
	mus_stream = NULL;

	musicPlaying = qfalse;
}

/*
=================
S_AL_MusicProcess
=================
*/
void S_AL_MusicProcess(ALuint b)
{
	int l;
	ALuint format;

	l = S_CodecReadStream(mus_stream, MUSIC_BUFFER_SIZE, decode_buffer);

	if(l == 0)
	{
		S_CodecCloseStream(mus_stream);
		mus_stream = S_CodecOpenStream(s_backgroundLoop);
		if(!mus_stream)
		{
			S_AL_StopBackgroundTrack();
			return;
		}

		l = S_CodecReadStream(mus_stream, MUSIC_BUFFER_SIZE, decode_buffer);
	}

	format = S_AL_Format(mus_stream->info.width, mus_stream->info.channels);
	qalBufferData(b, format, decode_buffer, l, mus_stream->info.rate);
}

/*
=================
S_AL_StartBackgroundTrack
=================
*/
void S_AL_StartBackgroundTrack( const char *intro, const char *loop )
{
	int i;

	// Stop any existing music that might be playing
	S_AL_StopBackgroundTrack();

	if ( !intro || !intro[0] ) {
		intro = loop;
	}
	if ( !loop || !loop[0] ) {
		loop = intro;
	}

	if((!intro || !intro[0]) && (!intro || !intro[0]))
		return;

	// Copy the loop over
	strncpy( s_backgroundLoop, loop, sizeof( s_backgroundLoop ) );

	// Open the intro
	mus_stream = S_CodecOpenStream(intro);

	if(!mus_stream)
		return;

	// Allocate a musicSource
	S_AL_MusicSourceGet();
	if(musicSourceHandle == -1)
		return;

	// Generate the musicBuffers
	qalGenBuffers(NUM_MUSIC_BUFFERS, musicBuffers);
	
	// Queue the musicBuffers up
	for(i = 0; i < NUM_MUSIC_BUFFERS; i++)
		S_AL_MusicProcess(musicBuffers[i]);
	qalSourceQueueBuffers(musicSource, NUM_MUSIC_BUFFERS, musicBuffers);
	
	// Start playing
	qalSourcePlay(musicSource);

	musicPlaying = qtrue;
}

/*
=================
S_AL_MusicUpdate
=================
*/
void S_AL_MusicUpdate( void )
{
	int processed;
	ALint state;

	if(!musicPlaying)
		return;

	qalGetSourcei(musicSource, AL_BUFFERS_PROCESSED, &processed);
	if(processed)
	{
		while(processed--)
		{
			ALuint b;
			qalSourceUnqueueBuffers(musicSource, 1, &b);
			S_AL_MusicProcess(b);
			qalSourceQueueBuffers(musicSource, 1, &b);
		}
	}

	// If it's not still playing, give it a kick
	qalGetSourcei(musicSource, AL_SOURCE_STATE, &state);
	if(state == AL_STOPPED)
	{
		Com_DPrintf( "Restarted OpenAL music musicSource\n");
		qalSourcePlay(musicSource);
	}

	// Set the gain property
	qalSourcef(musicSource, AL_GAIN, s_alGain->value * s_musicVolume->value);
}


//===========================================================================


// Local state variables
static ALCdevice *alDevice;
static ALCcontext *alContext;

#ifdef _WIN32
#define ALDRIVER_DEFAULT "OpenAL32.dll"
#else
#define ALDRIVER_DEFAULT "libopenal.so.0"
#endif

/*
=================
S_AL_StopAllSounds
=================
*/
void S_AL_StopAllSounds( void )
{
	S_AL_SrcShutup();
	S_AL_StopBackgroundTrack();
}

/*
=================
S_AL_Respatialize
=================
*/
void S_AL_Respatialize( int entityNum, const vec3_t origin, vec3_t axis[3], int inwater )
{
	// Axis[0] = Forward
	// Axis[2] = Up
	float velocity[] = {0.0f, 0.0f, 0.0f};
	float orientation[] = {axis[0][0], axis[0][1], axis[0][2],
		axis[2][0], axis[2][1], axis[2][2]};
		vec3_t sorigin;

	// Set OpenAL listener paramaters
	VectorScale(origin, POSITION_SCALE, sorigin);
	qalListenerfv(AL_POSITION, origin);
	qalListenerfv(AL_VELOCITY, velocity);
	qalListenerfv(AL_ORIENTATION, orientation);
}

/*
=================
S_AL_Update
=================
*/
void S_AL_Update( void )
{
	// Update SFX channels
	S_AL_SrcUpdate();

	// Update streams
	S_AL_StreamUpdate();
	S_AL_MusicUpdate();

	// Doppler
	if(s_doppler->modified)
	{
		s_alDopplerFactor->modified = qtrue;
		s_doppler->modified = qfalse;
	}

	// Doppler parameters
	if(s_alDopplerFactor->modified)
	{
		if(s_doppler->integer)
			qalDopplerFactor(s_alDopplerFactor->value);
		else
			qalDopplerFactor(0.0f);
		s_alDopplerFactor->modified = qfalse;
	}
	if(s_alDopplerSpeed->modified)
	{
		qalDopplerVelocity(s_alDopplerSpeed->value);
		s_alDopplerSpeed->modified = qfalse;
	}

	// Clear the modified flags on the other cvars
	s_alGain->modified = qfalse;
	s_volume->modified = qfalse;
	s_musicVolume->modified = qfalse;
	s_alMinDistance->modified = qfalse;
	s_alRolloff->modified = qfalse;
}

/*
=================
S_AL_DisableSounds
=================
*/
void S_AL_DisableSounds( void )
{
	S_AL_StopAllSounds();
}

/*
=================
S_AL_BeginRegistration
=================
*/
void S_AL_BeginRegistration( void )
{
}

/*
=================
S_AL_ClearSoundBuffer
=================
*/
void S_AL_ClearSoundBuffer( void )
{
}

/*
=================
S_AL_SoundList
=================
*/
void S_AL_SoundList( void )
{
}

/*
=================
S_AL_SoundInfo
=================
*/
void S_AL_SoundInfo( void )
{
	Com_Printf( "OpenAL info:\n" );
	Com_Printf( "  Vendor:     %s\n", qalGetString( AL_VENDOR ) );
	Com_Printf( "  Version:    %s\n", qalGetString( AL_VERSION ) );
	Com_Printf( "  Renderer:   %s\n", qalGetString( AL_RENDERER ) );
	Com_Printf( "  Extensions: %s\n", qalGetString( AL_EXTENSIONS ) );

}

/*
=================
S_AL_Shutdown
=================
*/
void S_AL_Shutdown( void )
{
	// Shut down everything
	S_AL_StreamDie( );
	S_AL_StopBackgroundTrack( );
	S_AL_SrcShutdown( );
	S_AL_BufferShutdown( );

	// Check for Linux shutdown race condition
	// FIXME: this will probably not be necessary once OpenAL CVS
	//        from 11/11/05 is released and prevelant
	if( Q_stricmp( qalGetString( AL_VENDOR ), "J. Valenzuela" ) ) {
		qalcMakeContextCurrent( NULL );
	}

	qalcDestroyContext(alContext);
	qalcCloseDevice(alDevice);

	QAL_Shutdown();
}

#endif

/*
=================
S_AL_Init
=================
*/
qboolean S_AL_Init( soundInterface_t *si )
{
#if USE_OPENAL
	if( !si ) {
		return qfalse;
	}

	// New console variables
	s_alPrecache = Cvar_Get( "s_alPrecache", "0", CVAR_ARCHIVE );
	s_alGain = Cvar_Get( "s_alGain", "0.4", CVAR_ARCHIVE );
	s_alSources = Cvar_Get( "s_alSources", "64", CVAR_ARCHIVE );
	s_alDopplerFactor = Cvar_Get( "s_alDopplerFactor", "1.0", CVAR_ARCHIVE );
	s_alDopplerSpeed = Cvar_Get( "s_alDopplerSpeed", "2200", CVAR_ARCHIVE );
	s_alMinDistance = Cvar_Get( "s_alMinDistance", "80", CVAR_ARCHIVE );
	s_alRolloff = Cvar_Get( "s_alRolloff", "0.25", CVAR_ARCHIVE );

	s_alDriver = Cvar_Get( "s_alDriver", ALDRIVER_DEFAULT, CVAR_ARCHIVE );

	// Load QAL
	if( !QAL_Init( s_alDriver->string ) )
	{
		Com_Printf(  "Failed to load library: \"%s\".\n", s_alDriver->string );
		return qfalse;
	}

	// Open default device
	alDevice = qalcOpenDevice( NULL );
	if( !alDevice )
	{
		QAL_Shutdown( );
		Com_Printf(  "Failed to open OpenAL device.\n" );
		return qfalse;
	}

	// Create OpenAL context
	alContext = qalcCreateContext( alDevice, NULL );
	if( !alContext )
	{
		QAL_Shutdown( );
		qalcCloseDevice( alDevice );
		Com_Printf(  "Failed to create OpenAL context.\n" );
		return qfalse;
	}
	qalcMakeContextCurrent( alContext );

	// Initialize sources, buffers, music
	S_AL_BufferInit( );
	S_AL_SrcInit( );

	// Set up OpenAL parameters (doppler, etc)
	qalDopplerFactor( s_alDopplerFactor->value );
	qalDopplerVelocity( s_alDopplerSpeed->value );

	si->Shutdown = S_AL_Shutdown;
	si->StartSound = S_AL_StartSound;
	si->StartLocalSound = S_AL_StartLocalSound;
	si->StartBackgroundTrack = S_AL_StartBackgroundTrack;
	si->StopBackgroundTrack = S_AL_StopBackgroundTrack;
	si->RawSamples = S_AL_RawSamples;
	si->StopAllSounds = S_AL_StopAllSounds;
	si->ClearLoopingSounds = S_AL_ClearLoopingSounds;
	si->AddLoopingSound = S_AL_AddLoopingSound;
	si->AddRealLoopingSound = S_AL_AddRealLoopingSound;
	si->StopLoopingSound = S_AL_StopLoopingSound;
	si->Respatialize = S_AL_Respatialize;
	si->UpdateEntityPosition = S_AL_UpdateEntityPosition;
	si->Update = S_AL_Update;
	si->DisableSounds = S_AL_DisableSounds;
	si->BeginRegistration = S_AL_BeginRegistration;
	si->RegisterSound = S_AL_RegisterSound;
	si->ClearSoundBuffer = S_AL_ClearSoundBuffer;
	si->SoundInfo = S_AL_SoundInfo;
	si->SoundList = S_AL_SoundList;

	return qtrue;
#else
	return qfalse;
#endif
}

