/*
    waveout output plugin for DeaDBeeF Player
    Copyright (C) 2016-2017 Elio Blanca

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


#define AUDIO_BUFFER_NO                                    10  /* max number of audio blocks */
/* block buffers have to be large enough to maintain at least 20 ms of music (maybe 25?), else stuttering may be heard */
#define AUDIO_BUFFER_SIZE          (AUDIO_BUFFER_NO*120*1024)  /* max size reserved for audio buffers - rarely used */
/* 20 ms of a 192 KHz 7.1 (32 bit/sample) stream require 120 kB */
/* 25 ms of the same audio stream require 150 kB */
/* cannot rely on time measures below 10 ms because we are really close to the NT time granularity (1/64 s),
   so even the sound card driver may behave oddly */
#define AUDIO_BUFFER_DURATION                              20  /* ms of audio data per block */

#define COMBOBOX_LAYOUT_STRING_LENGTH                    1024
#define COMBOBOX_LAYOUT_ENTRY_LEN             (MAXPNAMELEN+4)   /* need further room for quotes */

#include <unistd.h>

#include <windows.h>
#include <mmreg.h>
#include "../../deadbeef.h"

//#define trace(...) { fprintf(stderr, __VA_ARGS__); fflush(stderr); }
#define trace(fmt,...)

/* both found on the internet... */
static const GUID  KSDATAFORMAT_SUBTYPE_PCM =        {0x00000001,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
static const GUID  KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {0x00000003,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};

static DB_output_t plugin;
DB_functions_t *deadbeef;

static unsigned int waveout_device;
static db_thread_t waveout_tid;
static int wave_terminate, state;
static int audio_blocks_sent, audio_block_write_index;
static const char settings_dlg[COMBOBOX_LAYOUT_STRING_LENGTH];
static int bytesread, bytes_per_block, avail_audio_buffers;
static WAVEFORMATEXTENSIBLE wave_format;
static HWAVEOUT device_handle;
static WAVEHDR waveout_headers[AUDIO_BUFFER_NO];
static CRITICAL_SECTION waveoutCS;
static void *audio_data;

char audio_format_change_pending, output_device_change_pending;


static void
pwaveout_thread (void *context);

int
pwaveout_stop (void);

int
pwaveout_init (void)
{
    trace("pwaveout_init\n");
    state = OUTPUT_STATE_STOPPED;
    wave_terminate = 0;

    return 0;
}

static int
waveout_message (uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2)
{
    unsigned int new_wdevice;

    switch (id) {
        case DB_EV_CONFIGCHANGED:
        new_wdevice = deadbeef->conf_get_int("waveout.dev", 0) - 1;
        trace("waveout_message: selected device code is %d\n",new_wdevice);

        /* new device? */
        if (waveout_device != new_wdevice)
        {
            /* replace wave device */
            waveout_device = new_wdevice;
            /* flag for the 'pwaveout_stop' routine */
            output_device_change_pending = 1;
            /* stop playback (if any) */
            deadbeef->sendmessage (DB_EV_STOP, 0, 0, 0);
        }
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
    trace("pwaveout_setformat: new settings sr %d ch %d bps %d channelmask=%08X\n",plugin.fmt.samplerate,plugin.fmt.channels,plugin.fmt.bps,fmt->channelmask);

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


    if (state == OUTPUT_STATE_STOPPED && audio_blocks_sent == 0)
    {
        openresult = waveOutOpen(NULL, waveout_device, (WAVEFORMATEX *)&wave_format, 0, 0, WAVE_FORMAT_QUERY);
        if (openresult != MMSYSERR_NOERROR)
        {
            trace("pwaveout_setformat: audio format not supported by the selected device (result=%d)\n",openresult);
            memset((void *)&wave_format, 0, sizeof(WAVEFORMATEXTENSIBLE));
            result = -1;
        }
        else
            trace("pwaveout_setformat: new format supported\n");
    }
    else
    {
        /* cannot query the audio device just now - promise we'll do this later */
        trace("pwaveout_setformat: audio format query deferred\n");
        audio_format_change_pending = 1;
    }
    return result;
}

int
pwaveout_free (void)
{
    trace("pwaveout_free\n");
    if (!wave_terminate)
    {
        if (deadbeef->thread_exist(waveout_tid))
        {
            wave_terminate = 1;
            trace("pwaveout_free: waiting for thread join...\n");
            deadbeef->thread_join(waveout_tid);
            trace("pwaveout_free: thread join done\n");
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
        EnterCriticalSection(&waveoutCS);
        audio_blocks_sent--;
        LeaveCriticalSection(&waveoutCS);
        /* DON'T DO THIS! */
        /* Calling waveOutAnything functions from within this callback will cause a deadlock! */
        //waveOutUnprepareHeader(device_handle, (LPWAVEHDR)dwParam1, sizeof(WAVEHDR));
    }
}

int
pwaveout_play (void)
{
    int result = -1;

    trace("pwaveout_play\n");
    if (!deadbeef->thread_exist(waveout_tid))
    {
        waveout_tid = deadbeef->thread_start(pwaveout_thread, NULL);
    }
    if (deadbeef->thread_exist(waveout_tid))
    {
        if (wave_format.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            if (audio_blocks_sent)
                trace("pwaveout_play: audio_blocks_sent=%d\n",audio_blocks_sent);
            /* device setup is ok, let's open */
            if (waveOutOpen(&device_handle, waveout_device, (WAVEFORMATEX *)&wave_format, (DWORD_PTR)waveOutProc, 0, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
            {
                /* let's dance! - setup buffers */
                bytes_per_block     = wave_format.Format.nAvgBytesPerSec * AUDIO_BUFFER_DURATION / 1000;
                avail_audio_buffers = AUDIO_BUFFER_SIZE / bytes_per_block;
                if (avail_audio_buffers > AUDIO_BUFFER_NO) avail_audio_buffers = AUDIO_BUFFER_NO;

                audio_block_write_index = 0;
                result = 0;
                state = OUTPUT_STATE_PLAYING;
                trace("pwaveout_play: go! (bytes_per_block=%d)\n",bytes_per_block);
            }
        }
    }
    return result;
}

int
pwaveout_stop (void)
{
    int idx;

    trace("pwaveout_stop\n");

    if (device_handle != NULL)
    {
        if (state == OUTPUT_STATE_PAUSED)
            /* cannot close a paused device */
            waveOutRestart(device_handle);

        waveOutReset(device_handle);

        if (audio_blocks_sent)
        {
            trace("pwaveout_stop: audio_blocks_sent = %d\n",audio_blocks_sent);
            do
            {
                __mingw_sleep(0, AUDIO_BUFFER_DURATION*1000000);
                trace(".");
            }
            while(audio_blocks_sent != 0);
            trace("\n");
        }

        /* unprepare waveheaders and free resources */
        for (idx=0; idx<AUDIO_BUFFER_NO; idx++)
        {
            if (waveout_headers[idx].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(device_handle, &waveout_headers[idx], sizeof(WAVEHDR));
        }
    }

    state = OUTPUT_STATE_STOPPED;

    deadbeef->streamer_reset(1);

    if (output_device_change_pending)
    {
        /* a different WaveOut device was selected */
        trace("pwaveout_stop: changing waveout device\n");

        /* close the current device */
        if (device_handle != NULL)
        {
            waveOutClose(device_handle);
            device_handle = NULL;
        }
        /* force setting output format on new device */
        audio_format_change_pending = 1;

        output_device_change_pending = 0;
    }

    return 0;
}

int
pwaveout_pause (void)
{
    trace("pwaveout_pause\n");
    if (state == OUTPUT_STATE_STOPPED) {
        return -1;
    }

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

    if (state == OUTPUT_STATE_PAUSED)
    {
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


static void
pwaveout_thread (void *context)
{
    MMRESULT mmresult;
    int idx;

    trace("pwaveout_thread started\n");

    while (1)
    {
        if (wave_terminate)
            break;

        if (state != OUTPUT_STATE_PLAYING && !audio_format_change_pending)
        {
            __mingw_sleep(0, AUDIO_BUFFER_DURATION*1000000); /* __mingw_sleep(seconds, nanoseconds) */
        }
        else
        {
            /* is the device full? */
            if (audio_blocks_sent >= avail_audio_buffers)
                /* idle wait */
                __mingw_sleep(0, AUDIO_BUFFER_DURATION*1000000);

            /* 'consuming' audio data */
            if (deadbeef->streamer_ok_to_read(bytes_per_block)
                &&
                audio_blocks_sent < avail_audio_buffers
                &&
                !audio_format_change_pending)
            {
                bytesread = deadbeef->streamer_read(audio_data+audio_block_write_index*bytes_per_block, bytes_per_block);
                if (bytesread > 0)
                {
                    /* I cannot "unprepare" into the callback, so this seems a good place to do it */
                    if (waveout_headers[audio_block_write_index].dwFlags & WHDR_PREPARED)
                        waveOutUnprepareHeader(device_handle, &waveout_headers[audio_block_write_index], sizeof(WAVEHDR));
                    waveout_headers[audio_block_write_index].dwBufferLength = bytesread;
                    waveout_headers[audio_block_write_index].lpData =         audio_data+audio_block_write_index*bytes_per_block;
                    waveout_headers[audio_block_write_index].dwFlags =        0;

                    mmresult = waveOutPrepareHeader(device_handle, &waveout_headers[audio_block_write_index], sizeof(WAVEHDR));
                    if (mmresult == MMSYSERR_NOERROR)
                    {
                        waveOutWrite(device_handle, &waveout_headers[audio_block_write_index], sizeof(WAVEHDR));
                        EnterCriticalSection(&waveoutCS);
                        audio_blocks_sent++;
                        LeaveCriticalSection(&waveoutCS);
                        audio_block_write_index = (audio_block_write_index+1) % avail_audio_buffers;
                    }
                }
                else
                {
                    trace("pwaveout_thread: read nothing\n");
                }
            }
            if (audio_blocks_sent == 0 && audio_format_change_pending)
            {
                trace("pwaveout_thread: there is a format change pending\n");

                /* handle the format change... */
                mmresult = waveOutOpen(&device_handle, waveout_device, (WAVEFORMATEX *)&wave_format, (DWORD_PTR)waveOutProc, 0, CALLBACK_FUNCTION);
                if (mmresult != MMSYSERR_NOERROR)
                {
                    /* FATAL! cannot continue playback */
                    trace("pwaveout_thread: FATAL audio format not supported by the selected device (result=%d)\n",mmresult);
                    memset((void *)&wave_format, 0, sizeof(WAVEFORMATEXTENSIBLE));
                    //result = -1;
                    break;
                }
                else
                {
                    /* let's dance! - setup buffers */
                    bytes_per_block     = wave_format.Format.nAvgBytesPerSec * AUDIO_BUFFER_DURATION / 1000;
                    avail_audio_buffers = AUDIO_BUFFER_SIZE / bytes_per_block;
                    if (avail_audio_buffers > AUDIO_BUFFER_NO) avail_audio_buffers = AUDIO_BUFFER_NO;

                    audio_block_write_index = 0;
                    trace("pwaveout_thread: format change done (bytes_per_block=%d)\n",bytes_per_block);
                }

                audio_format_change_pending = 0;
            }
        }
    }
    if (device_handle != NULL)
    {
        /* stop playback */
        waveOutReset(device_handle);
        while (audio_blocks_sent)
            __mingw_sleep(0, AUDIO_BUFFER_DURATION*1000000);

        for (idx=0; idx<AUDIO_BUFFER_NO; idx++)
        {
            /* free resources still locked */
            if (waveout_headers[idx].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(device_handle, &waveout_headers[idx], sizeof(WAVEHDR));
        }
        /* close the device */
        waveOutClose(device_handle);
    }

    /* clean the thread ID struct - pretty ugly doin' this here, but useful */
    waveout_tid.p = NULL; waveout_tid.x = 0;

    trace("pwaveout_thread terminating\n");
}

int
pwaveout_get_state (void) {
    //trace("pwaveout_get_state\n"); /* this routine is called several times a second, so too much logging */
    return state;
}


/*
 * 'start' is the very first function called after loading
 */
int
waveout_start (void) {
    int num_devices, idx, result = 0;
    WAVEOUTCAPS woc;
    char new_dev[COMBOBOX_LAYOUT_ENTRY_LEN];

    trace("waveout_start\n");
    state = OUTPUT_STATE_STOPPED;
    audio_blocks_sent = 0;
    audio_format_change_pending = 0;
    output_device_change_pending = 0;
    device_handle = NULL;
    waveout_device = WAVE_MAPPER;
    waveout_tid.p = NULL; waveout_tid.x = 0;

    num_devices = waveOutGetNumDevs();
    if (num_devices != 0)
    {
        snprintf((char *)settings_dlg, COMBOBOX_LAYOUT_STRING_LENGTH,
                 "property \"Audio device\" select[%d] waveout.dev 0 Default", num_devices+1);
        for (idx=0; idx<num_devices; idx++)
        {
            if (waveOutGetDevCaps(idx, &woc, sizeof(WAVEOUTCAPS)) == MMSYSERR_NOERROR)
            {
                snprintf(new_dev, COMBOBOX_LAYOUT_ENTRY_LEN, " \"%s\"", woc.szPname);
                strncat((char *)settings_dlg, new_dev, COMBOBOX_LAYOUT_STRING_LENGTH-4);
            }
            else
                strncat((char *)settings_dlg, " \"(no description)\"", COMBOBOX_LAYOUT_STRING_LENGTH-4);
        }
        strncat((char *)settings_dlg, " ;\n", COMBOBOX_LAYOUT_STRING_LENGTH);
        /* combo selector is ready */
        //trace("%s",settings_dlg);

        /* let's allocate enough room for needed audio data */
        audio_data = malloc(AUDIO_BUFFER_SIZE);
        if (audio_data != NULL)
        {
            InitializeCriticalSection(&waveoutCS);
            trace("waveout_start: setup successful\n");
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
    .plugin.api_vminor = 9,
    .plugin.version_major = 0,
    .plugin.version_minor = 1,
    .plugin.type = DB_PLUGIN_OUTPUT,
    .plugin.id = "waveout",
    .plugin.name = "WaveOut output plugin",
    .plugin.descr = "Output plugin for the WaveOut interface on Windows(R) OSs.\nRequires Windows XP at least.",
    .plugin.copyright = 
    "WaveOut output plugin for DeaDBeeF Player\n"
    "Copyright (C) 2016-2017 Elio Blanca\n"
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

