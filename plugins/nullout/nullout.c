/*
    Null output plugin for DeaDBeeF Player
    Copyright (C) 2009-2014 Alexey Yakovenko

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
*/

#include <stdint.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <stdio.h>
#include <string.h>
#include "../../deadbeef.h"

#define NULLOUT_AUDIO_BLOCK_DURATION                   32 /* ms */

//#define trace(...) { fprintf(stderr, __VA_ARGS__); fflush(stderr);}
#define trace(fmt,...)

static DB_output_t plugin;
DB_functions_t *deadbeef;

static db_thread_t null_tid;
static int null_terminate;
static int state;
static int nullout_simulate = 0;
int bytesread, block_size;

//static void
//pnull_callback (char *stream, int len);

static void
pnull_thread (void *context);

static int
pnull_init (void);

static int
pnull_free (void);

int
pnull_setformat (ddb_waveformat_t *fmt);

static int
pnull_play (void);

static int
pnull_stop (void);

static int
pnull_pause (void);

static int
pnull_unpause (void);

int
pnull_init (void) {
    trace ("pnull_init\n");
    state = OUTPUT_STATE_STOPPED;
    nullout_simulate = deadbeef->conf_get_int ("null.simulate", 0);
    null_terminate = 0;
    null_tid = deadbeef->thread_start (pnull_thread, NULL);
    return 0;
}

static int
null_message (uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2) {
    switch (id) {
    case DB_EV_CONFIGCHANGED:
        nullout_simulate = deadbeef->conf_get_int ("null.simulate", 0);
        break;
    }
    return 0;
}

int
pnull_setformat (ddb_waveformat_t *fmt) {
    memcpy (&plugin.fmt, fmt, sizeof (ddb_waveformat_t));
    block_size = (plugin.fmt.bps*plugin.fmt.samplerate*plugin.fmt.channels)*NULLOUT_AUDIO_BLOCK_DURATION/8000;
    trace("nullout: new format sr %d ch %d bps %d - block_size=%d\n",plugin.fmt.samplerate,plugin.fmt.channels,plugin.fmt.bps,block_size);
    return 0;
}

int
pnull_free (void) {
    trace ("pnull_free\n");
    if (!null_terminate) {
        if (deadbeef->thread_alive (null_tid)) {
            null_terminate = 1;
            deadbeef->thread_join (null_tid);
        }
        deadbeef->thread_wipeid (&null_tid);
        state = OUTPUT_STATE_STOPPED;
        null_terminate = 0;
    }
    return 0;
}

int
pnull_play (void) {
    if (!deadbeef->thread_alive (null_tid)) {
        pnull_init ();
    }
    state = OUTPUT_STATE_PLAYING;
    return 0;
}

int
pnull_stop (void) {
    state = OUTPUT_STATE_STOPPED;
    deadbeef->streamer_reset (1);
    return 0;
}

int
pnull_pause (void) {
    if (state == OUTPUT_STATE_STOPPED) {
        return -1;
    }
    // set pause state
    state = OUTPUT_STATE_PAUSED;
    return 0;
}

int
pnull_unpause (void) {
    // unset pause state
    if (state == OUTPUT_STATE_PAUSED) {
        state = OUTPUT_STATE_PLAYING;
    }
    return 0;
}

#if 0
static int
pnull_get_endianness (void) {
#if WORDS_BIGENDIAN
    return 1;
#else
    return 0;
#endif
}
#endif

static void
pnull_thread (void *context) {
#define AUDIO_BUFFER_SIZE (4*1024)
    char buf[AUDIO_BUFFER_SIZE];
    int playable=0;
    float sleep_time;
#ifdef __linux__
    prctl (PR_SET_NAME, "deadbeef-null", 0, 0, 0, 0);
#endif
    for (;;) {
        if (null_terminate) {
            break;
        }
        if (state != OUTPUT_STATE_PLAYING) {
            usleep (16000);
            continue;
        }

        //pnull_callback (buf, AUDIO_BUFFER_SIZE);
        /* receiving audio data */
        do
        {
            bytesread = 0;
            if(deadbeef->streamer_ok_to_read(AUDIO_BUFFER_SIZE))
            {
                bytesread = deadbeef->streamer_read (buf, AUDIO_BUFFER_SIZE);
                playable += bytesread;
            }
        }
        while(bytesread > 0 && playable < block_size);
        /* 'consuming' audio data */
        if (nullout_simulate)
        {
            sleep_time = 8000000.0/plugin.fmt.samplerate*((playable<block_size)?playable:block_size)/plugin.fmt.channels/plugin.fmt.bps;
            playable = (playable<block_size)? 0 : (playable%block_size);
            usleep((int)sleep_time);
        }
    }
}

#if 0
static void
pnull_callback (char *stream, int len) {
    if (!deadbeef->streamer_ok_to_read (len)) {
        memset (stream, 0, len);
        bytesread = 0;
        return;
    }
    bytesread = deadbeef->streamer_read (stream, len);

    if (bytesread < len) {
        memset (stream + bytesread, 0, len-bytesread);
    }
}
#endif

int
pnull_get_state (void) {
    return state;
}

int
null_start (void) {
    return 0;
}

int
null_stop (void) {
    return 0;
}

DB_plugin_t *
nullout_load (DB_functions_t *api) {
    deadbeef = api;
    return DB_PLUGIN (&plugin);
}

static const char settings_dlg[] =
    "property \"Simulate audio output\" checkbox null.simulate 0;\n"
;

// define plugin interface
static DB_output_t plugin = {
    .plugin.api_vmajor = 1,
    .plugin.api_vminor = 0,
    .plugin.version_major = 1,
    .plugin.version_minor = 1,
    .plugin.type = DB_PLUGIN_OUTPUT,
    .plugin.id = "nullout",
    .plugin.name = "Null output plugin",
    .plugin.descr = "This plugin takes the audio data, and discards it,\nso nothing will play.\nThis is useful for testing.",
    .plugin.copyright = 
    "Null output plugin for DeaDBeeF Player\n"
    "Copyright (C) 2009-2014 Alexey Yakovenko\n"
    "\n"
    "This software is provided 'as-is', without any express or implied\n"
    "warranty.  In no event will the authors be held liable for any damages\n"
    "arising from the use of this software.\n"
    "\n"
    "Permission is granted to anyone to use this software for any purpose,\n"
    "including commercial applications, and to alter it and redistribute it\n"
    "freely, subject to the following restrictions:\n"
    "\n"
    "1. The origin of this software must not be misrepresented; you must not\n"
    " claim that you wrote the original software. If you use this software\n"
    " in a product, an acknowledgment in the product documentation would be\n"
    " appreciated but is not required.\n"
    "\n"
    "2. Altered source versions must be plainly marked as such, and must not be\n"
    " misrepresented as being the original software.\n"
    "\n"
    "3. This notice may not be removed or altered from any source distribution.\n"
    ,
    .plugin.website = "http://deadbeef.sf.net",
    .plugin.start = null_start,
    .plugin.stop = null_stop,
    .plugin.configdialog = settings_dlg,
    .plugin.message = null_message,
    .init = pnull_init,
    .free = pnull_free,
    .setformat = pnull_setformat,
    .play = pnull_play,
    .stop = pnull_stop,
    .pause = pnull_pause,
    .unpause = pnull_unpause,
    .state = pnull_get_state,
    .fmt = {.samplerate = 44100, .channels = 2, .bps = 16, .channelmask = DDB_SPEAKER_FRONT_LEFT | DDB_SPEAKER_FRONT_RIGHT}
};
