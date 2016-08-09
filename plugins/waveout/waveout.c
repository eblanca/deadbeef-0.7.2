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

/* some functions will require WindowsXP at least */
#define WINVER 0x0501

        #define __USE_MINGW_ANSI_STDIO 1


#define AUDIO_BUFFER_NO         10
#define AUDIO_BUFFER_SIZE     3072


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

static const GUID  KSDATAFORMAT_SUBTYPE_PCM =        {0x00000001,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
static const GUID  KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {0x00000003,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};

static DB_output_t plugin;
DB_functions_t *deadbeef;

static unsigned int waveout_device;
static db_thread_t waveout_tid;
static int wave_terminate;
static int state, waveout_shutdown;
static int audio_blocks_sent, audio_block_write_index, idle_pause;
#define COMBOBOX_LAYOUT_STRING_LENGTH   1024
static const char settings_dlg[COMBOBOX_LAYOUT_STRING_LENGTH];
int bytesread;
static WAVEFORMATEXTENSIBLE wave_format;
static HWAVEOUT device_handle;
static WAVEHDR waveout_headers[AUDIO_BUFFER_NO];
static CRITICAL_SECTION waveoutCS;
static void *audio_data;

int audio_format_change_pending;

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
pwaveout_init (void)
{
    trace ("pwaveout_init\n");
    state = OUTPUT_STATE_STOPPED;
//    waveout_simulate = deadbeef->conf_get_int ("null.simulate", 0);
    wave_terminate = 0;
    waveout_tid = deadbeef->thread_start (pwaveout_thread, NULL);
    return 0;
}

static int
waveout_message (uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2)
{
//    trace ("waveout_message\n");

    switch (id) {
        case DB_EV_CONFIGCHANGED:
        waveout_device = deadbeef->conf_get_int ("waveout.dev", 0) - 1;
        trace("waveout_message: selected device code is %d\n",waveout_device);
        break;
    }
    return 0;
}

int
pwaveout_setformat (ddb_waveformat_t *fmt)
{
    int result = 0;
    MMRESULT openresult;

    trace("pwaveout_setformat\n");
    memcpy (&plugin.fmt, fmt, sizeof (ddb_waveformat_t));
    trace("waveout: new format sr %d ch %d bps %d\n",plugin.fmt.samplerate,plugin.fmt.channels,plugin.fmt.bps);

    wave_format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wave_format.Format.nChannels = plugin.fmt.channels;
    wave_format.Format.nSamplesPerSec = plugin.fmt.samplerate;
    wave_format.Format.wBitsPerSample = plugin.fmt.bps;
    wave_format.Format.nBlockAlign = wave_format.Format.nChannels * wave_format.Format.wBitsPerSample / 8;
    wave_format.Format.nAvgBytesPerSec = wave_format.Format.nSamplesPerSec * wave_format.Format.nBlockAlign;
    wave_format.Format.cbSize = 22;
    wave_format.Samples.wValidBitsPerSample = wave_format.Format.wBitsPerSample;
    wave_format.SubFormat = (fmt->is_float)? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
    /* set channel mask */
    wave_format.dwChannelMask = 0;
    if (fmt->channelmask & DDB_SPEAKER_FRONT_LEFT)
        wave_format.dwChannelMask |= SPEAKER_FRONT_LEFT;
    if (fmt->channelmask & DDB_SPEAKER_FRONT_RIGHT)
        wave_format.dwChannelMask |= SPEAKER_FRONT_RIGHT;
    if (fmt->channelmask & DDB_SPEAKER_FRONT_CENTER)
        wave_format.dwChannelMask |= SPEAKER_FRONT_CENTER;
    if (fmt->channelmask & DDB_SPEAKER_LOW_FREQUENCY)
        wave_format.dwChannelMask |= SPEAKER_LOW_FREQUENCY;
    if (fmt->channelmask & DDB_SPEAKER_BACK_LEFT)
        wave_format.dwChannelMask |= SPEAKER_BACK_LEFT;
    if (fmt->channelmask & DDB_SPEAKER_BACK_RIGHT)
        wave_format.dwChannelMask |= SPEAKER_BACK_RIGHT;
    if (fmt->channelmask & DDB_SPEAKER_FRONT_LEFT_OF_CENTER)
        wave_format.dwChannelMask |= SPEAKER_FRONT_LEFT_OF_CENTER;
    if (fmt->channelmask & DDB_SPEAKER_FRONT_RIGHT_OF_CENTER)
        wave_format.dwChannelMask |= SPEAKER_FRONT_RIGHT_OF_CENTER;
    if (fmt->channelmask & DDB_SPEAKER_BACK_CENTER)
        wave_format.dwChannelMask |= SPEAKER_BACK_CENTER;
    if (fmt->channelmask & DDB_SPEAKER_SIDE_LEFT)
        wave_format.dwChannelMask |= SPEAKER_SIDE_LEFT;
    if (fmt->channelmask & DDB_SPEAKER_SIDE_RIGHT)
        wave_format.dwChannelMask |= SPEAKER_SIDE_RIGHT;
    if (fmt->channelmask & DDB_SPEAKER_TOP_CENTER)
        wave_format.dwChannelMask |= SPEAKER_TOP_CENTER;
    if (fmt->channelmask & DDB_SPEAKER_TOP_FRONT_LEFT)
        wave_format.dwChannelMask |= SPEAKER_TOP_FRONT_LEFT;
    if (fmt->channelmask & DDB_SPEAKER_TOP_FRONT_CENTER)
        wave_format.dwChannelMask |= SPEAKER_TOP_FRONT_CENTER;
    if (fmt->channelmask & DDB_SPEAKER_TOP_FRONT_RIGHT)
        wave_format.dwChannelMask |= SPEAKER_TOP_FRONT_RIGHT;
    if (fmt->channelmask & DDB_SPEAKER_TOP_BACK_LEFT)
        wave_format.dwChannelMask |= SPEAKER_TOP_BACK_LEFT;
    if (fmt->channelmask & DDB_SPEAKER_TOP_BACK_CENTER)
        wave_format.dwChannelMask |= SPEAKER_TOP_BACK_CENTER;
    if (fmt->channelmask & DDB_SPEAKER_TOP_BACK_RIGHT)
        wave_format.dwChannelMask |= SPEAKER_TOP_BACK_RIGHT;

    if (state != OUTPUT_STATE_PLAYING)
    {
        openresult = waveOutOpen(NULL, waveout_device, (WAVEFORMATEX *)&wave_format, 0, 0, WAVE_FORMAT_QUERY);
        if (openresult != MMSYSERR_NOERROR)
        {
            trace("waveout: audio format not supported by the selected device (result=%d)\n",openresult);
            memset((void *)&wave_format, 0, sizeof(WAVEFORMATEXTENSIBLE));
            result = -1;
        }
        else
            trace ("waveout: the selected device is able to play what we want!\n");
    }
    else
    {
        /* cannot query the audio device just now - promise we'll do this later */
        audio_format_change_pending = 1;
    }
    return result;
}

int
pwaveout_free (void)
{
    trace ("pwaveout_free\n");
    if (!wave_terminate)
    {
        if (deadbeef->thread_exist (waveout_tid))
        {
            wave_terminate = 1;
            deadbeef->thread_join (waveout_tid);
        }
        state = OUTPUT_STATE_STOPPED;
        wave_terminate = 0;
    }
    return 0;
}

void CALLBACK waveOutProc(HWAVEOUT  hwo,
                          UINT      uMsg,
                          DWORD_PTR dwInstance,
                          DWORD_PTR dwParam1,
                          DWORD_PTR dwParam2)
{
    if (uMsg == WOM_DONE)
    {
//fprintf(stderr,"we're into waveOutProc!\n")
        EnterCriticalSection(&waveoutCS);
        audio_blocks_sent--;
        LeaveCriticalSection(&waveoutCS);
        waveOutUnprepareHeader(device_handle, (LPWAVEHDR)dwParam1, sizeof(WAVEHDR));
        if (audio_blocks_sent == 0 && state == OUTPUT_STATE_STOPPED)
        {
            /* we've finished playing sounds after a 'stop' signal, shut down audio device */
            waveout_shutdown = 1;
        }
    }
}

int
pwaveout_play (void)
{
    int result = -1;

    trace("pwaveout_play\n");
    if (!deadbeef->thread_exist (waveout_tid))
    {
        pwaveout_init ();
    }
    if (deadbeef->thread_exist (waveout_tid))
    {
        if (wave_format.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            /* device setup is ok, let's open */
            if (waveOutOpen(&device_handle, waveout_device, (WAVEFORMATEX *)&wave_format, (DWORD_PTR)waveOutProc, 0, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
            {
                state = OUTPUT_STATE_PLAYING;
                /* prepare a pause value being slightly littler than a single audio block duration */
                idle_pause = (int)((double)AUDIO_BUFFER_SIZE / (double)wave_format.Format.nAvgBytesPerSec * (double)800000000 ); /* nanoseconds */
                result = 0;
            }
        }
    }
    return result;
}

int
pwaveout_stop (void)
{
    trace("pwaveout_stop\n");
    state = OUTPUT_STATE_STOPPED;
//    waveOutReset(device_handle);
//    waveOutClose(device_handle);
    deadbeef->streamer_reset (1);
    return 0;
}

int
pwaveout_pause (void)
{
    trace("pwaveout_pause\n");
    if (state == OUTPUT_STATE_STOPPED) {
        return -1;
    }
    // set pause state
    if (state == OUTPUT_STATE_PLAYING)
    {
        waveOutPause(device_handle);
        state = OUTPUT_STATE_PAUSED;
    }
    return 0;
}

int
pwaveout_unpause (void)
{
    trace("pwaveout_unpause\n");
    // unset pause state
    if (state == OUTPUT_STATE_PAUSED) {
        waveOutRestart(device_handle);
        state = OUTPUT_STATE_PLAYING;
    }
    return 0;
}

static int
pwaveout_get_endianness (void)
{
    trace("pwaveout_get_endianness\n");
#if WORDS_BIGENDIAN
    return 1;
#else
    return 0;
#endif
}

/*
static void
pwaveout_callback (char *stream, int len)
{
    if (!deadbeef->streamer_ok_to_read (len))
    {
        memset (stream, 0, len);
        bytesread = 0;
        return;
    }
    bytesread = deadbeef->streamer_read (stream, len);

    if (bytesread < len)
    {
        memset (stream + bytesread, 0, len-bytesread);
    }
}*/

static void
pwaveout_thread (void *context)
{
//    char buf[AUDIO_BUFFER_SIZE];
    float sleep_time;
    trace("pwaveout_thread started\n");
    while (1)
    {
        if (wave_terminate)
            break;

        if (state != OUTPUT_STATE_PLAYING)
        {
fprintf(stderr,"waveout_shutdown=%d\n",waveout_shutdown);
            if (state == OUTPUT_STATE_STOPPED && waveout_shutdown)
            {
                trace("pwaveout_thread: shutting down device\n");
                waveOutReset(device_handle);
                waveOutClose(device_handle);

                waveout_shutdown = 0;
            }
            else
            {
                __mingw_sleep(0, 20000000); /* __mingw_sleep(seconds, nanoseconds) */
            }
        }
        else
//        if (!audio_format_change_pending)
        {
            /* is the device full? */
            if (audio_blocks_sent == AUDIO_BUFFER_NO)
                /* idle wait */
                __mingw_sleep(0, idle_pause);
            /* 'consuming' audio data */
            while (deadbeef->streamer_ok_to_read(AUDIO_BUFFER_SIZE)
                   &&
                   audio_blocks_sent<AUDIO_BUFFER_NO)
//            if (/*waveout_simulate &&*/ bytesread > 0)
            {
fprintf(stderr, "ciclo %d\n",audio_blocks_sent);
                memset((void *)&waveout_headers[audio_block_write_index], 0, sizeof(WAVEHDR));
//                pwaveout_callback (buf, AUDIO_BUFFER_SIZE);
                bytesread = deadbeef->streamer_read(audio_data+audio_block_write_index*AUDIO_BUFFER_SIZE, AUDIO_BUFFER_SIZE);
                waveout_headers[audio_block_write_index].dwBufferLength = bytesread;
                waveout_headers[audio_block_write_index].lpData = audio_data+audio_block_write_index*AUDIO_BUFFER_SIZE;
                waveOutPrepareHeader(device_handle, &waveout_headers[audio_block_write_index], sizeof(WAVEHDR));
                waveOutWrite(device_handle, &waveout_headers[audio_block_write_index], sizeof(WAVEHDR));
                EnterCriticalSection(&waveoutCS);
                audio_blocks_sent++;
                LeaveCriticalSection(&waveoutCS);
                audio_block_write_index = (audio_block_write_index+1) % AUDIO_BUFFER_NO;
//                usleep(10000);
//                waveOutUnprepareHeader(device_handle, &waveout_header, sizeof(WAVEHDR));
                //sleep_time = 8000000.0/wave_format.Format.nSamplesPerSec*bytesread/wave_format.Format.nChannels/wave_format.Format.wBitsPerSample;
                //usleep((int)sleep_time);
            }
            if (audio_blocks_sent == 0 && audio_format_change_pending)
            {
                /* handle the format change... */

                audio_format_change_pending = 0;
            }
        }
    }
    trace("pwaveout_thread terminating\n");
}

int
pwaveout_get_state (void) {
    return state;
}

/*
 * 'start' is the very first function called after loading
 */
int
waveout_start (void) {
    int num_devices, idx, result = 0;
    WAVEOUTCAPS woc;
    char new_dev[40];

    trace("waveout_start\n");
    state = OUTPUT_STATE_STOPPED;
    audio_blocks_sent = 0;
    audio_block_write_index = 0;
    audio_format_change_pending = 0;
    waveout_shutdown = 0;

    num_devices = waveOutGetNumDevs();
    if (num_devices != 0)
    {
        snprintf((char *)settings_dlg, COMBOBOX_LAYOUT_STRING_LENGTH,
                 "property \"Audio device\" select[%d] waveout.dev 0 Default ", num_devices+1);
        for (idx=0; idx<num_devices; idx++)
        {
            if (waveOutGetDevCaps(idx, &woc, sizeof(WAVEOUTCAPS)) == MMSYSERR_NOERROR)
            {
                snprintf(new_dev, 40, " \"%s\"", woc.szPname);
                strncat((char *)settings_dlg, new_dev, COMBOBOX_LAYOUT_STRING_LENGTH);
            }
            else
                strncat((char *)settings_dlg, "\"(no description)\"", COMBOBOX_LAYOUT_STRING_LENGTH-4);
        }
        strncat((char *)settings_dlg, " ;\n", COMBOBOX_LAYOUT_STRING_LENGTH);
        /* combo selector is ready */

        /* let's allocate enough room for needed audio data */
        audio_data = malloc(AUDIO_BUFFER_NO * AUDIO_BUFFER_SIZE);
        if (audio_data != NULL)
        {
            InitializeCriticalSection(&waveoutCS);
        }
        else
            /* memory allocation failed - fatal */
            result = -1;
    }
    else
        /* no audio devices detected - communicate this failure */
        result = -1;

    return result;
}

/*
 * 'stop' is the very last function called before closing the output plugin
 * this is the best place for freeing resources
 */
int
waveout_stop (void) {
    trace("waveout_stop\n");
    free(audio_data);
    DeleteCriticalSection(&waveoutCS);
    return 0;
}

DB_plugin_t *
waveout_load (DB_functions_t *api) {
    trace("waveout_load\n");
    deadbeef = api;
    return DB_PLUGIN (&plugin);
}


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
    "   in a product, an acknowledgement in the product documentation would be\n"
    "   appreciated but is not required.\n"
    "\n"
    "2. Altered source versions must be plainly marked as such, and must not be\n"
    "   misrepresented as being the original software.\n"
    "\n"
    "3. This notice may not be removed or altered from any source distribution.\n"
    ,
    .plugin.website = "https://github.com/eblanca/deadbeef-0.7.2",
    .plugin.start = waveout_start,
    .plugin.stop = waveout_stop,
    .plugin.configdialog = settings_dlg,
    .plugin.message = waveout_message,
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

