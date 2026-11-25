/* Discord status: port/runtime/discord.c */
#pragma once

/* starts the status thread if it is wanted (headless: only with MSSAGA_DISCORD=1) */
void discord_start(int headless);

/* a code module of the game was loaded: it says where the player is */
void discord_note_dll(const char *name);
