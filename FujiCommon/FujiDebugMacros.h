/****************************************************************************
 *   mac68k-fuji-drivers (c) 2024 Marcio Teixeira                           *
 *                                                                          *
 *   This program is free software: you can redistribute it and/or modify   *
 *   it under the terms of the GNU General Public License as published by   *
 *   the Free Software Foundation, either version 3 of the License, or      *
 *   (at your option) any later version.                                    *
 *                                                                          *
 *   This program is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU General Public License for more details.                           *
 *                                                                          *
 *   To view a copy of the GNU General Public License, go to the following  *
 *   location: <http://www.gnu.org/licenses/>.                              *
 ****************************************************************************/

#pragma once

#if DEBUG
	#include <stdio.h>
	#define DEBUG_STAGE(a) {printf(a " (press key to proceed)\n"); getchar();}
	#define ON_ERROR(a) if (err != noErr) {printf("%s %d line %d\n", errorStr(err), err, __LINE__); a;}
	#define CHECK_ERR ON_ERROR(return err)
	char *errorStr(OSErr err);
#else
	#define DEBUG_STAGE(a)
	#define ON_ERROR(a) if (err != noErr) {a;}
	#define CHECK_ERR ON_ERROR(return err)
#endif
