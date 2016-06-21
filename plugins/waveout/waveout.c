/*
    waveout output plugin for DeaDBeeF Player
    Copyright (C) 2016 Elio Blanca

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

#ifdef __MINGW32__

//#include <stdint.h>
#include <unistd.h>
//#ifdef __linux__
//#include <sys/prctl.h>
//#endif
//#include <stdio.h>
//#include <string.h>
//#define WINVER 0x0501
#include <windows.h>
#include <mmreg.h>
#include "../../deadbeef.h"

#define trace(...) { fprintf(stderr, __VA_ARGS__); }
//#define trace(fmt,...)

static DB_output_t plugin;
DB_functions_t *deadbeef;

static unsigned int waveout_device;
static db_thread_t waveout_tid;
static int wave_terminate;
static int state;
static int waveout_simulate = 1;
int bytesread;

//static void
//pwaveout_callback (char *stream, int len);

static void
pwaveout_thread (void *context);

//static int
//pwaveout_init (void);

//static int
//pwaveout_free (void);

//int
//pwaveout_setformat (ddb_waveformat_t *fmt);

//static int
//pwaveout_play (void);

//static int
//pwaveout_stop (void);

//static int
//pwaveout_pause (void);

//static int
//pwaveout_unpause (void);

int
pwaveout_init (void) {
    trace ("pwaveout_init\n");
    state = OUTPUT_STATE_STOPPED;
//    waveout_simulate = deadbeef->conf_get_int ("null.simulate", 0);
    waveout_device = WAVE_MAPPER;
    wave_terminate = 0;
    waveout_tid = deadbeef->thread_start (pwaveout_thread, NULL);
    return 0;
}

static int
waveout_message (uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2) {
    trace ("waveout_message\n");
/*
    switch (id) {
    case DB_EV_CONFIGCHANGED:
        nullout_simulate = deadbeef->conf_get_int ("null.simulate", 0);
        break;
    }
*/
    return 0;
}

int
pwaveout_setformat (ddb_waveformat_t *fmt) {
    int result = 0;
    MMRESULT openresult;
    WAVEFORMATEXTENSIBLE wave_format;

    trace("pwaveout_setformat\n");
    memcpy (&plugin.fmt, fmt, sizeof (ddb_waveformat_t));
    trace("waveout: new format sr %d ch %d bps %d\n",plugin.fmt.samplerate,plugin.fmt.channels,plugin.fmt.bps);

    openresult = waveOutOpen(NULL, waveout_device, (WAVEFORMATEX *)&wave_format, (DWORD)NULL, (DWORD)NULL, WAVE_FORMAT_QUERY);
    if (openresult != MMSYSERR_NOERROR)
    {
        trace("waveout: audio format not supported by the selected device\n");
        result = -1;
    }
    return result;
}

int
pwaveout_free (void) {
    trace ("pwaveout_free\n");
    if (!wave_terminate) {
        if (deadbeef->thread_exist (waveout_tid)) {
            wave_terminate = 1;
            deadbeef->thread_join (waveout_tid);
        }
        state = OUTPUT_STATE_STOPPED;
        wave_terminate = 0;
    }
    return 0;
}

int
pwaveout_play (void) {
    int result = -1;

    trace("pwaveout_play\n");
    if (!deadbeef->thread_exist (waveout_tid)) {
        pwaveout_init ();
    }
    if (deadbeef->thread_exist (waveout_tid))
    {
        state = OUTPUT_STATE_PLAYING;
        result = 0;
    }
    return result;
}

int
pwaveout_stop (void) {
    trace("pwaveout_stop\n");
    state = OUTPUT_STATE_STOPPED;
    deadbeef->streamer_reset (1);
    return 0;
}

int
pwaveout_pause (void) {
    trace("pwaveout_pause\n");
    if (state == OUTPUT_STATE_STOPPED) {
        return -1;
    }
    // set pause state
    state = OUTPUT_STATE_PAUSED;
    return 0;
}

int
pwaveout_unpause (void) {
    trace("pwaveout_unpause\n");
    // unset pause state
    if (state == OUTPUT_STATE_PAUSED) {
        state = OUTPUT_STATE_PLAYING;
    }
    return 0;
}

static int
pwaveout_get_endianness (void) {
    trace("pwaveout_get_endianness\n");
#if WORDS_BIGENDIAN
    return 1;
#else
    return 0;
#endif
}

static void
pwaveout_callback (char *stream, int len) {
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

static void
pwaveout_thread (void *context) {
#define AUDIO_BUFFER_SIZE 4096
    char buf[AUDIO_BUFFER_SIZE];
    trace("pwaveout_thread started\n");
    for (;;) {
        if (wave_terminate) {
            break;
        }
        if (state != OUTPUT_STATE_PLAYING) {
            usleep (10000);
            continue;
        }

        pwaveout_callback (buf, AUDIO_BUFFER_SIZE);
        /* 'consuming' audio data */
        if (waveout_simulate && bytesread > 0)
            usleep((1000000/(plugin.fmt.samplerate*plugin.fmt.channels*(plugin.fmt.bps/8)))*bytesread);
    }
    trace("pwaveout_thread terminating\n");
}

int
pwaveout_get_state (void) {
    return state;
}

int
waveout_start (void) {
    trace("waveout_start\n");
    state = OUTPUT_STATE_STOPPED;
    return 0;
}

int
waveout_stop (void) {
    trace("waveout_stop\n");
    return 0;
}

DB_plugin_t *
waveout_load (DB_functions_t *api) {
    trace("waveout_load\n");
    deadbeef = api;
    return DB_PLUGIN (&plugin);
}

//static const char settings_dlg[] =
//    "property \"Simulate audio output\" checkbox null.simulate 0;\n"
//;

// define plugin interface
static DB_output_t plugin = {
    .plugin.api_vmajor = 1,
    .plugin.api_vminor = 0,
    .plugin.version_major = 0,
    .plugin.version_minor = 1,
    .plugin.type = DB_PLUGIN_OUTPUT,
    .plugin.id = "waveout",
    .plugin.name = "WaveOut output plugin",
    .plugin.descr = "Output plugin for the WaveOut interface on Windows(R) OSs.\nRequires Windows XP at least.",
    .plugin.copyright = 
    "WaveOut output plugin for DeaDBeeF Player\n"
    "Copyright (C) 2016 Elio Blanca\n"
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
    "   claim that you wrote the original software. If you use this software\n"
    "   in a product, an acknowledgment in the product documentation would be\n"
    "   appreciated but is not required.\n"
    "\n"
    "2. Altered source versions must be plainly marked as such, and must not be\n"
    "   misrepresented as being the original software.\n"
    "\n"
    "3. This notice may not be removed or altered from any source distribution.\n"
    ,
    .plugin.website = "http://github.com/eblanca/deadbeef",
    .plugin.start = waveout_start,
    .plugin.stop = waveout_stop,
//    .plugin.configdialog = settings_dlg,
//    .plugin.message = waveout_message,
    .init = pwaveout_init,
    .free = pwaveout_free,
    .setformat = pwaveout_setformat,
    .play = pwaveout_play,
    .stop = pwaveout_stop,
    .pause = pwaveout_pause,
    .unpause = pwaveout_unpause,
    .state = pwaveout_get_state,
    .fmt = {.samplerate = 44100, .channels = 2, .bps = 16, .channelmask = DDB_SPEAKER_FRONT_LEFT | DDB_SPEAKER_FRONT_RIGHT}
};
#endif /* __MINGW32__ */

