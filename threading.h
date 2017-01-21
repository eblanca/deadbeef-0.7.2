/*
  This file is part of Deadbeef Player source code
  http://deadbeef.sourceforge.net

  threading functions wrapper

  Copyright (C) 2009-2013 Alexey Yakovenko

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Alexey Yakovenko waker@users.sourceforge.net
*/
#ifndef __THREADING_H
#define __THREADING_H

#ifdef __MINGW32__
#include <pthread.h>
typedef pthread_t       db_thread_t;
typedef pthread_mutex_t *db_mutex_t;
typedef pthread_cond_t  *db_cond_t;
#else
#include <stdint.h>
#define db_thread_t intptr_t
#define db_mutex_t  uintptr_t
#define db_cond_t   uintptr_t
#endif

db_thread_t//intptr_t
thread_start (void (*fn)(void *ctx), void *ctx);

db_thread_t//intptr_t
thread_start_low_priority (void (*fn)(void *ctx), void *ctx);

int
thread_join (db_thread_t/*intptr_t*/ tid);

int
thread_detach (db_thread_t/*intptr_t*/ tid);

int
thread_alive (db_thread_t/*intptr_t*/ tid);

void
thread_wipeid (db_thread_t *tid);

void
thread_exit (void *retval);

db_mutex_t//uintptr_t
mutex_create (void);

db_mutex_t//uintptr_t
mutex_create_nonrecursive (void);

void
mutex_free (db_mutex_t/*uintptr_t*/ mtx);

int
mutex_lock (db_mutex_t/*uintptr_t*/ mtx);

int
mutex_unlock (db_mutex_t/*uintptr_t*/ mtx);

db_cond_t//uintptr_t
cond_create (void);

void
cond_free (db_cond_t/*uintptr_t*/ cond);

int
cond_wait (db_cond_t/*uintptr_t*/ cond, db_mutex_t/*uintptr_t*/ mutex);

int
cond_signal (db_cond_t/*uintptr_t*/ cond);

int
cond_broadcast (db_cond_t/*uintptr_t*/ cond);

#endif

