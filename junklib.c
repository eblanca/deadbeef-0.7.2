/*
    DeaDBeeF - ultimate music player for GNU/Linux systems with X11
    Copyright (C) 2009-2010 Alexey Yakovenko <waker@users.sourceforge.net>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "junklib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LIBICONV_PLUG
#include <iconv.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include "playlist.h"
#include "utf8.h"
#include "plugins.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#pragma GCC optimize("O0")

#define UTF8 "utf-8"

#define trace(...) { fprintf(stderr, __VA_ARGS__); }
//#define trace(fmt,...)

#define min(x,y) ((x)<(y)?(x):(y))
#define max(x,y) ((x)>(y)?(x):(y))

static uint32_t
extract_i32 (const uint8_t *buf)
{
    uint32_t x;
    // big endian extract

    x = buf[0];
    x <<= 8;
    x |= buf[1];
    x <<= 8;
    x |= buf[2];
    x <<= 8;
    x |= buf[3];

    return x;
}

static inline uint32_t
extract_i32_le (unsigned char *buf)
{
    uint32_t x;
    // little endian extract

    x = buf[3];
    x <<= 8;
    x |= buf[2];
    x <<= 8;
    x |= buf[1];
    x <<= 8;
    x |= buf[0];

    return x;
}
static inline uint16_t
extract_i16 (const uint8_t *buf)
{
    uint16_t x;
    // big endian extract

    x = buf[0];
    x <<= 8;
    x |= buf[1];
    x <<= 8;

    return x;
}

static inline float
extract_f32 (unsigned char *buf) {
    float f;
    uint32_t *x = (uint32_t *)&f;
    *x = buf[0];
    *x <<= 8;
    *x |= buf[1];
    *x <<= 8;
    *x |= buf[2];
    *x <<= 8;
    *x |= buf[3];
    return f;
}
static const char *junk_genretbl[] = {
    "Blues",
    "Classic Rock",
    "Country",
    "Dance",
    "Disco",
    "Funk",
    "Grunge",
    "Hip-Hop",
    "Jazz",
    "Metal",
    "New Age",
    "Oldies",
    "Other",
    "Pop",
    "R&B",
    "Rap",
    "Reggae",
    "Rock",
    "Techno",
    "Industrial",
    "Alternative",
    "Ska",
    "Death Metal",
    "Pranks",
    "Soundtrack",
    "Euro-Techno",
    "Ambient",
    "Trip-Hop",
    "Vocal",
    "Jazz+Funk",
    "Fusion",
    "Trance",
    "Classical",
    "Instrumental",
    "Acid",
    "House",
    "Game",
    "Sound Clip",
    "Gospel",
    "Noise",
    "AlternRock",
    "Bass",
    "Soul",
    "Punk",
    "Space",
    "Meditative",
    "Instrumental Pop",
    "Instrumental Rock",
    "Ethnic",
    "Gothic",
    "Darkwave",
    "Techno-Industrial",
    "Electronic",
    "Pop-Folk",
    "Eurodance",
    "Dream",
    "Southern Rock",
    "Comedy",
    "Cult",
    "Gangsta",
    "Top 40",
    "Christian Rap",
    "Pop/Funk",
    "Jungle",
    "Native American",
    "Cabaret",
    "New Wave",
    "Psychadelic",
    "Rave",
    "Showtunes",
    "Trailer",
    "Lo-Fi",
    "Tribal",
    "Acid Punk",
    "Acid Jazz",
    "Polka",
    "Retro",
    "Musical",
    "Rock & Roll",
    "Hard Rock",
    "Folk",
    "Folk-Rock",
    "National Folk",
    "Swing",
    "Fast Fusion",
    "Bebob",
    "Latin",
    "Revival",
    "Celtic",
    "Bluegrass",
    "Avantgarde",
    "Gothic Rock",
    "Progressive Rock",
    "Psychedelic Rock",
    "Symphonic Rock",
    "Slow Rock",
    "Big Band",
    "Chorus",
    "Easy Listening",
    "Acoustic",
    "Humour",
    "Speech",
    "Chanson",
    "Opera",
    "Chamber Music",
    "Sonata",
    "Symphony",
    "Booty Bass",
    "Primus",
    "Porn Groove",
    "Satire",
    "Slow Jam",
    "Club",
    "Tango",
    "Samba",
    "Folklore",
    "Ballad",
    "Power Ballad",
    "Rhythmic Soul",
    "Freestyle",
    "Duet",
    "Punk Rock",
    "Drum Solo",
    "Acapella",
    "Euro-House",
    "Dance Hall",
    "Goa",
    "Drum & Bass",
    "Club-House",
    "Hardcore",
    "Terror",
    "Indie",
    "BritPop",
    "Negerpunk",
    "Polsk Punk",
    "Beat",
    "Christian Gangsta",
    "Heavy Metal",
    "Black Metal",
    "Crossover",
    "Contemporary C",
    "Christian Rock",
    "Merengue",
    "Salsa",
    "Thrash Metal",
    "Anime",
    "JPop",
    "SynthPop",
};

static int
can_be_russian (const signed char *str) {
    int latin = 0;
    int rus = 0;
    for (; *str; str++) {
        if ((*str >= 'A' && *str <= 'Z')
                || *str >= 'a' && *str <= 'z') {
            latin++;
        }
        else if (*str < 0) {
            rus++;
        }
    }
    if (rus > latin/2) {
        return 1;
    }
    return 0;
}

static char *
convstr_id3v2_2to3 (const unsigned char* str, int sz) {
    static char out[2048];
    const char *enc = "iso8859-1";
    char *ret = out;

    // hack to add limited cp1251 recoding support
    if (*str == 1) {
        if (str[1] == 0xff && str[2] == 0xfe) {
            enc = "UCS-2LE";
            str += 2;
            sz -= 2;
        }
        else if (str[2] == 0xff && str[1] == 0xfe) {
            enc = "UCS-2BE";
            str += 2;
            sz -= 2;
        }
        else {
            trace ("invalid ucs-2 signature %x %x\n", (int)str[1], (int)str[2]);
            return NULL;
        }
    }
    else {
        if (can_be_russian (&str[1])) {
            enc = "cp1251";
        }
    }
    str++;
    sz--;
    iconv_t cd = iconv_open (UTF8, enc);
    if (cd == (iconv_t)-1) {
        trace ("iconv can't recoode from %s to utf8", enc);
        return strdup ("-");
    }
    else {
        size_t inbytesleft = sz;
        size_t outbytesleft = 2047;
#ifdef __linux__
        char *pin = (char*)str;
#else
        const char *pin = str;
#endif
        char *pout = out;
        memset (out, 0, sizeof (out));
        /*size_t res = */iconv (cd, &pin, &inbytesleft, &pout, &outbytesleft);
        iconv_close (cd);
        ret = out;
    }
    return strdup (ret);
}

static char *
convstr_id3v2_4 (const unsigned char* str, int sz) {
    static char out[2048];
    const char *enc = "iso8859-1";
    char *ret = out;

    // hack to add limited cp1251 recoding support

    if (*str == 3) {
        // utf8
        trace ("utf8\n");
        strncpy (out, str+1, 2047);
        sz--;
        out[min (sz, 2047)] = 0;
        return strdup (out);
    }
    else if (*str == 1) {
        trace ("utf16\n");
        enc = "UTF-16";
    }
    else if (*str == 2) {
        trace ("utf16be\n");
        enc = "UTF-16BE";
    }
#if 0
    // NOTE: some dumb taggers put non-iso8859-1 text with enc=0
    else if (*str == 0) {
        // iso8859-1
        trace ("iso8859-1\n");
        enc = "iso8859-1";
    }
#endif
    else {
        if (can_be_russian (&str[1])) {
            enc = "cp1251";
        }
    }
    str++;
    sz--;
    iconv_t cd = iconv_open (UTF8, enc);
    if (cd == (iconv_t)-1) {
        trace ("iconv can't recode from %s to utf8\n", enc);
        return strdup ("-");
    }
    else {
        size_t inbytesleft = sz;
        size_t outbytesleft = 2047;
#ifdef __linux__
        char *pin = (char*)str;
#else
        const char *pin = str;
#endif
        char *pout = out;
        memset (out, 0, sizeof (out));
        /*size_t res = */iconv (cd, &pin, &inbytesleft, &pout, &outbytesleft);
        iconv_close (cd);
        ret = out;
    }
//    trace ("decoded %s\n", out+3);
    return strdup (ret);
}

static const char *
convstr_id3v1 (const char* str, int sz) {
    static char out[2048];
    int i;
    for (i = 0; i < sz; i++) {
        if (str[i] != ' ') {
            break;
        }
    }
    if (i == sz) {
        out[0] = 0;
        return out;
    }

    // check for utf8 (hack)
    iconv_t cd;
    cd = iconv_open (UTF8, UTF8);
    if (cd == (iconv_t)-1) {
        trace ("iconv doesn't support utf8\n");
        return str;
    }
    size_t inbytesleft = sz;
    size_t outbytesleft = 2047;
#ifdef __linux__
        char *pin = (char*)str;
#else
        const char *pin = str;
#endif
    char *pout = out;
    memset (out, 0, sizeof (out));
    size_t res = iconv (cd, &pin, &inbytesleft, &pout, &outbytesleft);
    iconv_close (cd);
    if (res == 0) {
        strncpy (out, str, 2047);
        out[min (sz, 2047)] = 0;
        return out;
    }

    const char *enc = "iso8859-1";
    if (can_be_russian (str)) {
        enc = "cp1251";
    }
    cd = iconv_open (UTF8, enc);
    if (cd == (iconv_t)-1) {
        trace ("iconv can't recode from %s to utf8\n", enc);
        return str;
    }
    else {
        size_t inbytesleft = sz;
        size_t outbytesleft = 2047;
#ifdef __linux__
        char *pin = (char*)str;
#else
        const char *pin = str;
#endif
        char *pout = out;
        memset (out, 0, sizeof (out));
        /*size_t res = */iconv (cd, &pin, &inbytesleft, &pout, &outbytesleft);
        iconv_close (cd);
    }
    return out;
}

static void
str_trim_right (uint8_t *str, int len) {
    uint8_t *p = str + len - 1;
    while (p >= str && *p <= 0x20) {
        p--;
    }
    p++;
    *p = 0;
}

// should read both id3v1 and id3v1.1
int
junk_read_id3v1 (playItem_t *it, DB_FILE *fp) {
    if (!it || !fp) {
        trace ("bad call to junk_read_id3v1!\n");
        return -1;
    }
    uint8_t buffer[128];
    // try reading from end
    deadbeef->fseek (fp, -128, SEEK_END);
    if (deadbeef->fread (buffer, 1, 128, fp) != 128) {
        return -1;
    }
    if (strncmp (buffer, "TAG", 3)) {
        return -1; // no tag
    }
    char title[31];
    char artist[31];
    char album[31];
    char year[5];
    char comment[31];
    uint8_t genreid;
    uint8_t tracknum;
    const char *genre = NULL;
    memset (title, 0, 31);
    memset (artist, 0, 31);
    memset (album, 0, 31);
    memset (year, 0, 5);
    memset (comment, 0, 31);
    memcpy (title, &buffer[3], 30);
    str_trim_right (title, 30);
    memcpy (artist, &buffer[3+30], 30);
    str_trim_right (artist, 30);
    memcpy (album, &buffer[3+60], 30);
    str_trim_right (album, 30);
    memcpy (year, &buffer[3+90], 4);
    str_trim_right (year, 4);
    memcpy (comment, &buffer[3+94], 30);
    str_trim_right (comment, 30);
    genreid = buffer[3+124];
    tracknum = 0xff;
    if (comment[28] == 0) {
        tracknum = comment[29];
    }
//    255 = "None",
//    "CR" = "Cover" (id3v2)
//    "RX" = "Remix" (id3v2)

    if (genreid == 0xff) {
        genre = "None";
    }
    else if (genreid <= 147) {
        genre = junk_genretbl[genreid];
    }
    else {
        genre = "";
    }

    // add meta
//    trace ("%s - %s - %s - %s - %s - %s\n", title, artist, album, year, comment, genre);
    pl_add_meta (it, "title", convstr_id3v1 (title, strlen (title)));
    pl_add_meta (it, "artist", convstr_id3v1 (artist, strlen (artist)));
    pl_add_meta (it, "album", convstr_id3v1 (album, strlen (album)));
    pl_add_meta (it, "year", year);
    pl_add_meta (it, "comment", convstr_id3v1 (comment, strlen (comment)));
    pl_add_meta (it, "genre", convstr_id3v1 (genre, strlen (genre)));
    if (tracknum != 0xff) {
        char s[4];
        snprintf (s, 4, "%d", tracknum);
        pl_add_meta (it, "track", s);
    }

    char new_tags[100] = "";
    const char *tags = pl_find_meta (it, "tags");
    if (tags) {
        strcpy (new_tags, tags);
    }
    strcat (new_tags, "ID3v1 ");
    pl_replace_meta (it, "tags", new_tags);
// FIXME: that should be accounted for
//    if (it->endoffset < 128) {
//        it->endoffset = 128;
//    }

    return 0;
}

int
junk_read_ape (playItem_t *it, DB_FILE *fp) {
//    trace ("trying to read ape tag\n");
    // try to read footer, position must be already at the EOF right before
    // id3v1 (if present)
    uint8_t header[32];
    if (deadbeef->fseek (fp, -32, SEEK_END) == -1) {
        return -1; // something bad happened
    }

    if (deadbeef->fread (header, 1, 32, fp) != 32) {
        return -1; // something bad happened
    }
    if (strncmp (header, "APETAGEX", 8)) {
        // try to skip 128 bytes backwards (id3v1)
        if (deadbeef->fseek (fp, -128-32, SEEK_END) == -1) {
            return -1; // something bad happened
        }
        if (deadbeef->fread (header, 1, 32, fp) != 32) {
            return -1; // something bad happened
        }
        if (strncmp (header, "APETAGEX", 8)) {
            return -1; // no ape tag here
        }
    }

    // end of footer must be 0
//    if (memcmp (&header[24], "\0\0\0\0\0\0\0\0", 8)) {
//        trace ("bad footer\n");
//        return -1;
//    }

    uint32_t version = extract_i32_le (&header[8]);
    int32_t size = extract_i32_le (&header[12]);
    uint32_t numitems = extract_i32_le (&header[16]);
    uint32_t flags = extract_i32_le (&header[20]);

    trace ("APEv%d, size=%d, items=%d, flags=%x\n", version, size, numitems, flags);
    char new_tags[100] = "";
    const char *tags = pl_find_meta (it, "tags");
    if (tags) {
        strcpy (new_tags, tags);
    }
    strcat (new_tags, "APEv2 ");
    pl_replace_meta (it, "tags", new_tags);
    // now seek to beginning of the tag (exluding header)
    if (deadbeef->fseek (fp, -size, SEEK_CUR) == -1) {
        trace ("failed to seek to tag start (-%d)\n", size);
        return -1;
    }
    int i;
    for (i = 0; i < numitems; i++) {
        uint8_t buffer[8];
        if (deadbeef->fread (buffer, 1, 8, fp) != 8) {
            return -1;
        }
        uint32_t itemsize = extract_i32_le (&buffer[0]);
        uint32_t itemflags = extract_i32_le (&buffer[4]);
        // read key until 0 (stupid and slow)
        char key[256];
        int keysize = 0;
        while (keysize <= 255) {
            if (deadbeef->fread (&key[keysize], 1, 1, fp) != 1) {
                return -1;
            }
            if (key[keysize] == 0) {
                break;
            }
            if (key[keysize] < 0x20) {
                return -1; // non-ascii chars and chars with codes 0..0x1f not allowed in ape item keys
            }
            keysize++;
        }
        key[255] = 0;
        trace ("item %d, size %d, flags %08x, keysize %d, key %s\n", i, itemsize, itemflags, keysize, key);
        // read value
        if (itemsize <= 100000) // just a sanity check
        {
            char *value = malloc (itemsize+1);
            if (deadbeef->fread (value, 1, itemsize, fp) != itemsize) {
                free (value);
                return -1;
            }
            value[itemsize] = 0;
            if (!u8_valid (value, itemsize, NULL)) {
                strcpy (value, "<bad encoding>");
            }
            // add metainfo only if it's textual
            int valuetype = ((itemflags & (0x3<<1)) >> 1);
            if (valuetype == 0) {
                if (!strcasecmp (key, "artist")) {
                    pl_add_meta (it, "artist", value);
                }
                else if (!strcasecmp (key, "title")) {
                    pl_add_meta (it, "title", value);
                }
                else if (!strcasecmp (key, "album")) {
                    pl_add_meta (it, "album", value);
                }
                else if (!strcasecmp (key, "track")) {
                    pl_add_meta (it, "track", value);
                }
                else if (!strcasecmp (key, "year")) {
                    pl_add_meta (it, "year", value);
                }
                else if (!strcasecmp (key, "genre")) {
                    pl_add_meta (it, "genre", value);
                }
                else if (!strcasecmp (key, "composer")) {
                    pl_add_meta (it, "composer", value);
                }
                else if (!strcasecmp (key, "comment")) {
                    pl_add_meta (it, "comment", value);
                }
                else if (!strcasecmp (key, "copyright")) {
                    pl_add_meta (it, "copyright", value);
                }
                else if (!strcasecmp (key, "cuesheet")) {
                    pl_add_meta (it, "cuesheet", value);
                }
                else if (!strncasecmp (key, "replaygain_album_gain", 21)) {
                    it->replaygain_album_gain = atof (value);
                    trace ("album_gain=%s\n", value);
                }
                else if (!strncasecmp (key, "replaygain_album_peak", 21)) {
                    it->replaygain_album_peak = atof (value);
                    trace ("album_peak=%s\n", value);
                }
                else if (!strncasecmp (key, "replaygain_track_gain", 21)) {
                    it->replaygain_track_gain = atof (value);
                    trace ("track_gain=%s\n", value);
                }
                else if (!strncasecmp (key, "replaygain_track_peak", 21)) {
                    it->replaygain_track_peak = atof (value);
                    trace ("track_peak=%s\n", value);
                }
            }
            free (value);
        }
        else {
            // try to skip
            if (0 != deadbeef->fseek (fp, SEEK_CUR, itemsize)) {
                fprintf (stderr, "junklib: corrupted APEv2 tag\n");
                return -1;
            }
        }
    }

    return 0;
}

static void
id3v2_string_read (int version, uint8_t *out, int sz, int unsync, const uint8_t *pread) {
    if (!unsync) {
        memcpy (out, pread, sz);
        out[sz] = 0;
        out[sz+1] = 0;
        return;
    }
    uint8_t prev = 0;
    while (sz > 0) {
        if (prev == 0xff && !(*pread)) {
//            trace ("found unsync 0x00 byte\n");
            prev = 0;
            pread++;
            continue;
        }
        prev = *out = *pread;
//        trace ("%02x ", prev);
        pread++;
        out++;
        sz--;
    }
//    trace ("\n");
    *out++ = 0;
}

int
junk_get_leading_size_stdio (FILE *fp) {
    uint8_t header[10];
    int pos = ftell (fp);
    if (fread (header, 1, 10, fp) != 10) {
        fseek (fp, pos, SEEK_SET);
        return -1; // too short
    }
    fseek (fp, pos, SEEK_SET);
    if (strncmp (header, "ID3", 3)) {
        return -1; // no tag
    }
    uint8_t flags = header[5];
    if (flags & 15) {
        return -1; // unsupported
    }
    int footerpresent = (flags & (1<<4)) ? 1 : 0;
    // check for bad size
    if ((header[9] & 0x80) || (header[8] & 0x80) || (header[7] & 0x80) || (header[6] & 0x80)) {
        return -1; // bad header
    }
    uint32_t size = (header[9] << 0) | (header[8] << 7) | (header[7] << 14) | (header[6] << 21);
    //trace ("junklib: leading junk size %d\n", size);
    return size + 10 + 10 * footerpresent;
}

int
junk_get_leading_size (DB_FILE *fp) {
    uint8_t header[10];
    int pos = deadbeef->ftell (fp);
    if (deadbeef->fread (header, 1, 10, fp) != 10) {
        deadbeef->fseek (fp, pos, SEEK_SET);
        return -1; // too short
    }
    deadbeef->fseek (fp, pos, SEEK_SET);
    if (strncmp (header, "ID3", 3)) {
        return -1; // no tag
    }
    uint8_t flags = header[5];
    if (flags & 15) {
        trace ("unsupported flags in id3v2\n");
        return -1; // unsupported
    }
    int footerpresent = (flags & (1<<4)) ? 1 : 0;
    // check for bad size
    if ((header[9] & 0x80) || (header[8] & 0x80) || (header[7] & 0x80) || (header[6] & 0x80)) {
        trace ("bad header in id3v2\n");
        return -1; // bad header
    }
    uint32_t size = (header[9] << 0) | (header[8] << 7) | (header[7] << 14) | (header[6] << 21);
    //trace ("junklib: leading junk size %d\n", size);
    return size + 10 + 10 * footerpresent;
}

int
junk_iconv (const char *in, int inlen, char *out, int outlen, const char *cs_in, const char *cs_out) {
    iconv_t cd = iconv_open (cs_out, cs_in);
    if (cd == (iconv_t)-1) {
        return -1;
    }
#ifdef __linux__
    char *pin = (char*)in;
#else
    const char *pin = value;
#endif

    size_t inbytesleft = inlen;
    size_t outbytesleft = outlen;

    char *pout = out;
    memset (out, 0, outbytesleft);

    size_t res = iconv (cd, &pin, &inbytesleft, &pout, &outbytesleft);
    int err = errno;
    iconv_close (cd);

    trace ("iconv -f %s -t %s '%s': returned %d, inbytes %d/%d, outbytes %d/%d, errno=%d\n", cs_in, cs_out, in, res, inlen, inbytesleft, outlen, outbytesleft, err);
    if (res == -1) {
        return -1;
    }
    return pout - out;
}

int
junk_id3v2_unsync (uint8_t *out, int len, int maxlen) {
    uint8_t buf [maxlen];
    uint8_t *p = buf;
    int res = -1;
    for (int i = 0; i < len; i++) {
        *p++ = out[i];
        if (i < len - 1 && out[i] == 0xff && (out[i+1] & 0xe0) == 0xe0) {
            *p++ = 0;
            res = 0;
        }
    }
    if (!res) {
        res = p-buf;
        memcpy (out, buf, res);
    }
    return res;
}

int
junk_id3v2_remove_frames (DB_id3v2_tag_t *tag, const char *frame_id) {
    DB_id3v2_frame_t *prev = NULL;
    for (DB_id3v2_frame_t *f = tag->frames; f; ) {
        DB_id3v2_frame_t *next = f->next;
        if (!strcmp (f->id, frame_id)) {
            if (prev) {
                prev->next = f->next;
            }
            else {
                tag->frames = f->next;
            }
            free (f);
        }
        else {
            prev = f;
        }
        f = next;
    }
    return 0;
}

DB_id3v2_frame_t *
junk_id3v2_add_text_frame_23 (DB_id3v2_tag_t *tag, const char *frame_id, const char *value) {
    // convert utf8 into ucs2_le
    size_t inlen = strlen (value);
    size_t outlen = inlen * 3;
    uint8_t out[outlen];

    trace ("junklib: setting text frame '%s' = '%s'\n", frame_id, value);

    int unsync = 0;
    if (tag->flags & (1<<7)) {
        unsync = 1;
    }

    int encoding = 0;

    int res;
    res = junk_iconv (value, inlen, out, outlen, UTF8, "ISO-8859-1");
    if (res == -1) {
        res = junk_iconv (value, inlen, out+2, outlen-2, UTF8, "UCS-2LE");
        if (res == -1) {
            return NULL;
        }
        out[0] = 0xff;
        out[1] = 0xfe;
        outlen = res + 2;
        trace ("successfully converted to ucs-2le (size=%d, bom: %x %x)\n", res, out[0], out[1]);
        encoding = 1;
    }
    else {
        trace ("successfully converted to iso8859-1 (size=%d)\n", res);
        outlen = res;
    }

    // add unsync bytes
    if (unsync) {
        trace ("adding unsynchronization\n");
        res = junk_id3v2_unsync (out, res, outlen);
        if (res >= 0) {
            trace ("unsync successful\n");
            outlen = res;
        }
        else {
            trace ("unsync not needed\n");
        }
    }

    // make a frame
    int size = outlen + 1 + encoding + 1;
    trace ("calculated frame size = %d\n", size);
    DB_id3v2_frame_t *f = malloc (size + sizeof (DB_id3v2_frame_t));
    memset (f, 0, sizeof (DB_id3v2_frame_t));
    strcpy (f->id, frame_id);
    f->size = size;
    f->data[0] = encoding;
    memcpy (&f->data[1], out, outlen);
    f->data[outlen+1] = 0;
    if (encoding == 1) {
        f->data[outlen+2] = 0;
    }
    // append to tag
    DB_id3v2_frame_t *tail;
    for (tail = tag->frames; tail && tail->next; tail = tail->next);
    if (tail) {
        tail->next = f;
    }
    else {
        tag->frames = f;
    }

    return f;
}

DB_id3v2_frame_t *
junk_id3v2_add_text_frame_24 (DB_id3v2_tag_t *tag, const char *frame_id, const char *value) {
    trace ("junklib: setting 2.4 text frame '%s' = '%s'\n", frame_id, value);

    // make a frame
    int outlen = strlen (value);
    int size = outlen + 1 + 1;
    trace ("calculated frame size = %d\n", size);
    DB_id3v2_frame_t *f = malloc (size + sizeof (DB_id3v2_frame_t));
    memset (f, 0, sizeof (DB_id3v2_frame_t));
    strcpy (f->id, frame_id);
    // flags are all zero
    f->size = size;
    f->data[0] = 3; // encoding=utf8
    memcpy (&f->data[1], value, outlen);
    f->data[outlen+1] = 0;
//    if (encoding == 1) { // we don't write ucs2
//        f->data[outlen+2] = 0;
//    }
    // append to tag
    DB_id3v2_frame_t *tail;
    for (tail = tag->frames; tail && tail->next; tail = tail->next);
    if (tail) {
        tail->next = f;
    }
    else {
        tag->frames = f;
    }

    return f;
}
// [+] TODO: remove all unsync bytes during conversion, where appropriate
// TODO: some non-Txxx frames might still need charset conversion
// TODO: 2.4 TDTG frame (tagging time) should not be converted, but might be useful to create it
int
junk_id3v2_convert_24_to_23 (DB_id3v2_tag_t *tag24, DB_id3v2_tag_t *tag23) {
    DB_id3v2_frame_t *f24;
    DB_id3v2_frame_t *tail = NULL;

    const char *copy_frames[] = {
        "AENC", "APIC",
        "COMM", "COMR", "ENCR",
        "ETCO", "GEOB", "GRID",
        "LINK", "MCDI", "MLLT", "OWNE", "PRIV",
        "POPM", "POSS", "RBUF",
        "RVRB",
        "SYLT", "SYTC",
        "UFID", "USER", "USLT",
        NULL
    };

    // NOTE: 2.4 ASPI, EQU2, RVA2, SEEK, SIGN are discarded for 2.3
    // NOTE: 2.4 PCNT is discarded because it is useless
    // NOTE: all Wxxx frames are copy_frames, handled as special case


    // "TDRC" TDAT with conversion from ID3v2-strct timestamp to DDMM format
    // "TDOR" TORY with conversion from ID3v2-strct timestamp to year
    // TODO: "TIPL" IPLS with conversion to non-text format

    const char *text_frames[] = {
        "TALB", "TBPM", "TCOM", "TCON", "TCOP", "TDLY", "TENC", "TEXT", "TFLT", "TIT1", "TIT2", "TIT3", "TKEY", "TLAN", "TLEN", "TMED", "TOAL", "TOFN", "TOLY", "TOPE", "TOWN", "TPE1", "TPE2", "TPE3", "TPE4", "TPOS", "TPUB", "TRCK", "TRSN", "TRSO", "TSRC", "TSSE", "TXXX", "TDRC", NULL
    };

    // NOTE: 2.4 TMCL (musician credits list) is discarded for 2.3
    // NOTE: 2.4 TMOO (mood) is discarded for 2.3
    // NOTE: 2.4 TPRO (produced notice) is discarded for 2.3
    // NOTE: 2.4 TSOA (album sort order) is discarded for 2.3
    // NOTE: 2.4 TSOP (performer sort order) is discarded for 2.3
    // NOTE: 2.4 TSOT (title sort order) is discarded for 2.3
    // NOTE: 2.4 TSST (set subtitle) is discarded for 2.3

    for (f24 = tag24->frames; f24; f24 = f24->next) {
        DB_id3v2_frame_t *f23 = NULL;
        // we are altering the tag, so check for tag alter preservation
        if (tag24->flags & (1<<6)) {
            continue; // discard the frame
        }

        int simplecopy = 0; // means format is the same in 2.3 and 2.4
        int text = 0; // means this is a text frame

        int i;

        if (f24->id[0] == 'W') { // covers all W000..WZZZ tags
            simplecopy = 1;
        }

        if (!simplecopy) {
            for (i = 0; copy_frames[i]; i++) {
                if (!strcmp (f24->id, copy_frames[i])) {
                    simplecopy = 1;
                    break;
                }
            }
        }

        if (!simplecopy) {
            // check if this is a text frame
            for (i = 0; text_frames[i]; i++) {
                if (!strcmp (f24->id, text_frames[i])) {
                    text = 1;
                    break;
                }
            }
        }


        if (!simplecopy && !text) {
            continue; // unknown frame
        }

        // convert flags
        uint8_t flags[2];
        // 1st byte (status flags) is the same, but shifted by 1 bit to the left
        flags[0] = f24->flags[0] << 1;
        
        // 2nd byte (format flags) is quite different
        // 2.4 format is %0h00kmnp (grouping, compression, encryption, unsync)
        // 2.3 format is %ijk00000 (compression, encryption, grouping)
        flags[1] = 0;
        if (f24->flags[1] & (1 << 6)) {
            flags[1] |= (1 << 4);
        }
        if (f24->flags[1] & (1 << 3)) {
            flags[1] |= (1 << 7);
        }
        if (f24->flags[1] & (1 << 2)) {
            flags[1] |= (1 << 6);
        }
        if (f24->flags[1] & (1 << 1)) {
            flags[1] |= (1 << 5);
        }
        if (f24->flags[1] & 1) {
            // 2.3 doesn't support per-frame unsyncronyzation
            // let's ignore it
        }

        if (simplecopy) {
            f23 = malloc (sizeof (DB_id3v2_frame_t) + f24->size);
            memset (f23, 0, sizeof (DB_id3v2_frame_t) + f24->size);
            strcpy (f23->id, f24->id);
            f23->size = f24->size;
            memcpy (f23->data, f24->data, f24->size);
            f23->flags[0] = flags[0];
            f23->flags[1] = flags[1];
        }
        else if (text) {
            // decode text into utf8
            char str[f24->size+2];

            int unsync = 0;
            if (tag24->flags & (1<<7)) {
                unsync = 1;
            }
            if (f24->flags[1] & 1) {
                unsync = 1;
            }
            id3v2_string_read (4, str, f24->size, unsync, f24->data);
            char *decoded = convstr_id3v2_4 (str, f24->size);
            if (!decoded) {
                trace ("junk_id3v2_convert_24_to_23: failed to decode text frame %s\n", f24->id);
                continue; // failed, discard it
            }
            if (!strcmp (f24->id, "TDRC")) {
                trace ("junk_id3v2_convert_24_to_23: TDRC text: %s\n", decoded);
                int year, month, day;
                int c = sscanf (decoded, "%4d-%2d-%2d", &year, &month, &day);
                if (c >= 1) {
                    char s[5];
                    snprintf (s, sizeof (s), "%04d", year);
                    f23 = junk_id3v2_add_text_frame_23 (tag23, "TYER", s);
                    if (f23) {
                        tail = f23;
                        f23 = NULL;
                    }
                }
                if (c == 3) {
                    char s[5];
                    snprintf (s, sizeof (s), "%02d%02d", month, day);
                    f23 = junk_id3v2_add_text_frame_23 (tag23, "TDAT", s);
                    if (f23) {
                        tail = f23;
                        f23 = NULL;
                    }
                }
                else {
                    trace ("junk_id3v2_add_text_frame_23: 2.4 TDRC doesn't have month/day info; discarded\n");
                }
            }
            else if (!strcmp (f24->id, "TDOR")) {
                trace ("junk_id3v2_convert_24_to_23: TDOR text: %s\n", decoded);
                int year;
                int c = sscanf (decoded, "%4d", &year);
                if (c == 1) {
                    char s[5];
                    snprintf (s, sizeof (s), "%04d", &year);
                    f23 = junk_id3v2_add_text_frame_23 (tag23, "TORY", s);
                    if (f23) {
                        tail = f23;
                        f23 = NULL;
                    }
                }
                else {
                    trace ("junk_id3v2_add_text_frame_23: 2.4 TDOR doesn't have month/day info; discarded\n");
                }
            }
            else {
                // encode for 2.3
                f23 = junk_id3v2_add_text_frame_23 (tag23, f24->id, decoded);
                if (f23) {
                    tail = f23;
                    f23 = NULL;
                }
            }
            free (decoded);
        }
        if (f23) {
            if (tail) {
                tail->next = f23;
            }
            else {
                tag23->frames = f23;
            }
            tail = f23;
        }
    }

    // convert tag header
    tag23->version[0] = 3;
    tag23->version[1] = 0;
    tag23->flags = tag24->flags;
    tag23->flags &= ~(1<<4); // no footer (unsupported in 2.3)
    tag23->flags &= ~(1<<7); // no unsync

    return 0;
}

int
junk_id3v2_convert_23_to_24 (DB_id3v2_tag_t *tag23, DB_id3v2_tag_t *tag24) {
    DB_id3v2_frame_t *f23;
    DB_id3v2_frame_t *tail = NULL;

    const char *copy_frames[] = {
        "AENC", "APIC",
        "COMM", "COMR", "ENCR",
        "ETCO", "GEOB", "GRID",
        "LINK", "MCDI", "MLLT", "OWNE", "PRIV",
        "POPM", "POSS", "RBUF",
        "RVRB",
        "SYLT", "SYTC",
        "UFID", "USER", "USLT",
        NULL
    };

    // NOTE: all Wxxx frames are copy_frames, handled as special case

    // "TDRC" TDAT with conversion from ID3v2-strct timestamp to DDMM format
    // "TDOR" TORY with conversion from ID3v2-strct timestamp to year
    // TODO: "TIPL" IPLS with conversion to non-text format

    const char *text_frames[] = {
        "TALB", "TBPM", "TCOM", "TCON", "TCOP", "TDLY", "TENC", "TEXT", "TFLT", "TIT1", "TIT2", "TIT3", "TKEY", "TLAN", "TLEN", "TMED", "TOAL", "TOFN", "TOLY", "TOPE", "TOWN", "TPE1", "TPE2", "TPE3", "TPE4", "TPOS", "TPUB", "TRCK", "TRSN", "TRSO", "TSRC", "TSSE", "TXXX", "TDRC", NULL
    };

    for (f23 = tag23->frames; f23; f23 = f23->next) {
        // we are altering the tag, so check for tag alter preservation
        if (tag23->flags & (1<<7)) {
            continue; // discard the frame
        }

        int simplecopy = 0; // means format is the same in 2.3 and 2.4
        int text = 0; // means this is a text frame

        int i;

        if (f23->id[0] == 'W') { // covers all W000..WZZZ tags
            simplecopy = 1;
        }

        if (!simplecopy) {
            for (i = 0; copy_frames[i]; i++) {
                if (!strcmp (f23->id, copy_frames[i])) {
                    simplecopy = 1;
                    break;
                }
            }
        }

        if (!simplecopy) {
            // check if this is a text frame
            for (i = 0; text_frames[i]; i++) {
                if (!strcmp (f23->id, text_frames[i])) {
                    text = 1;
                    break;
                }
            }
        }


        if (!simplecopy && !text) {
            continue; // unknown frame
        }

        // convert flags
        uint8_t flags[2];
        // 1st byte (status flags) is the same, but shifted by 1 bit to the
        // right
        flags[0] = f23->flags[0] >> 1;
        
        // 2nd byte (format flags) is quite different
        // 2.4 format is %0h00kmnp (grouping, compression, encryption, unsync)
        // 2.3 format is %ijk00000 (compression, encryption, grouping)
        flags[1] = 0;
        if (f23->flags[1] & (1 << 4)) {
            flags[1] |= (1 << 6);
        }
        if (f23->flags[1] & (1 << 7)) {
            flags[1] |= (1 << 3);
        }
        if (f23->flags[1] & (1 << 6)) {
            flags[1] |= (1 << 2);
        }
        if (f23->flags[1] & (1 << 5)) {
            flags[1] |= (1 << 1);
        }

        DB_id3v2_frame_t *f24 = NULL;
        if (simplecopy) {
            f24 = malloc (sizeof (DB_id3v2_frame_t) + f23->size);
            memset (f24, 0, sizeof (DB_id3v2_frame_t) + f23->size);
            strcpy (f24->id, f23->id);
            f24->size = f23->size;
            memcpy (f24->data, f23->data, f23->size);
            f24->flags[0] = flags[0];
            f24->flags[1] = flags[1];
        }
        else if (text) {
            // decode text into utf8
            char str[f23->size+2];

            int unsync = 0;
            if (tag23->flags & (1<<7)) {
                unsync = 1;
            }
            if (f23->flags[1] & 1) {
                unsync = 1;
            }
            id3v2_string_read (4, str, f23->size, unsync, f23->data);
            char *decoded = convstr_id3v2_4 (str, f23->size);
            if (!decoded) {
                trace ("junk_id3v2_convert_23_to_24: failed to decode text frame %s\n", f23->id);
                continue; // failed, discard it
            }
            if (!strcmp (f23->id, "TDRC")) {
                trace ("junk_id3v2_convert_23_to_24: TDRC text: %s\n", decoded);
                int year, month, day;
                int c = sscanf (decoded, "%4d-%2d-%2d", &year, &month, &day);
                if (c >= 1) {
                    char s[5];
                    snprintf (s, sizeof (s), "%04d", year);
                    f24 = junk_id3v2_add_text_frame_24 (tag24, "TYER", s);
                    if (f24) {
                        tail = f24;
                        f24 = NULL;
                    }
                }
                if (c == 3) {
                    char s[5];
                    snprintf (s, sizeof (s), "%02d%02d", month, day);
                    f24 = junk_id3v2_add_text_frame_24 (tag24, "TDAT", s);
                    if (f24) {
                        tail = f24;
                        f24 = NULL;
                    }
                }
                else {
                    trace ("junk_id3v2_add_text_frame_24: 2.4 TDRC doesn't have month/day info; discarded\n");
                }
            }
            else if (!strcmp (f23->id, "TORY")) {
                trace ("junk_id3v2_convert_23_to_24: TDOR text: %s\n", decoded);
                int year;
                int c = sscanf (decoded, "%4d", &year);
                if (c == 1) {
                    char s[5];
                    snprintf (s, sizeof (s), "%04d", &year);
// FIXME                    f24 = junk_id3v2_add_text_frame_24 (tag24, "TDOR", s);
                    if (f24) {
                        tail = f24;
                        f24 = NULL;
                    }
                }
                else {
                    trace ("junk_id3v2_add_text_frame_24: 2.4 TDOR doesn't have month/day info; discarded\n");
                }
            }
            else {
                // encode for 2.3
                f24 = junk_id3v2_add_text_frame_24 (tag24, f23->id, decoded);
                if (f24) {
                    tail = f24;
                    f24 = NULL;
                }
            }
            free (decoded);
        }
        if (f24) {
            if (tail) {
                tail->next = f24;
            }
            else {
                tag24->frames = f24;
            }
            tail = f24;
        }
    }

    // convert tag header
    tag24->version[0] = 4;
    tag24->version[1] = 0;
    tag24->flags = tag23->flags;
    tag24->flags &= ~(1<<4); // no footer (unsupported in 2.3)
    tag24->flags &= ~(1<<7); // no unsync

    return 0;
}

int
junk_write_id3v2 (const char *fname, DB_id3v2_tag_t *tag) {
/*
steps:
1. write tag to a new file
2. open source file, and skip id3v2 tag
3. copy remaining part of source file into new file
4. move new file in place of original file
*/
    if (tag->version[0] < 3) {
        fprintf (stderr, "junk_write_id3v2: writing id3v2.2 is not supported\n");
        return -1;
    }

    FILE *out = NULL;
    FILE *fp = NULL;
    char *buffer = NULL;
    int err = -1;

    char tmppath[PATH_MAX];
    snprintf (tmppath, sizeof (tmppath), "%s.temp.mp3", fname);
    fprintf (stderr, "going to write tags to %s\n", tmppath);

    out = fopen (tmppath, "w+b");
    if (!out) {
        fprintf (stderr, "junk_write_id3v2: failed to fdopen temp file\n");
        goto error;
    }

    // write tag header
    if (fwrite ("ID3", 1, 3, out) != 3) {
        fprintf (stderr, "junk_write_id3v2: failed to write ID3 signature\n");
        goto error;
    }

    if (fwrite (tag->version, 1, 2, out) != 2) {
        fprintf (stderr, "junk_write_id3v2: failed to write tag version\n");
        goto error;
    }
    uint8_t flags = tag->flags;
    flags &= ~(1<<6); // we don't (yet?) write ext header
    flags &= ~(1<<4); // we don't write footer

    if (fwrite (&flags, 1, 1, out) != 1) {
        fprintf (stderr, "junk_write_id3v2: failed to write tag flags\n");
        goto error;
    }
    // run through list of frames, and calculate size
    uint32_t sz = 0;
    int frm = 0;
    for (DB_id3v2_frame_t *f = tag->frames; f; f = f->next) {
        // each tag has 10 bytes header
        if (tag->version[0] > 2) {
            sz += 10;
        }
        else {
            sz += 6;
        }
        sz += f->size;
    }

    trace ("calculated tag size: %d bytes\n", sz);
    uint8_t tagsize[4];
    tagsize[0] = (sz >> 21) & 0x7f;
    tagsize[1] = (sz >> 14) & 0x7f;
    tagsize[2] = (sz >> 7) & 0x7f;
    tagsize[3] = sz & 0x7f;
    if (fwrite (tagsize, 1, 4, out) != 4) {
        fprintf (stderr, "junk_write_id3v2: failed to write tag size\n");
        goto error;
    }

    trace ("writing frames\n");
    // write frames
    for (DB_id3v2_frame_t *f = tag->frames; f; f = f->next) {
        trace ("writing frame %s size %d\n", f->id, f->size);
        int id_size = 3;
        uint8_t frame_size[4];
        if (tag->version[0] > 2) {
            id_size = 4;
        }
        if (tag->version[0] == 3) {
            frame_size[0] = (f->size >> 24) & 0xff;
            frame_size[1] = (f->size >> 16) & 0xff;
            frame_size[2] = (f->size >> 8) & 0xff;
            frame_size[3] = f->size & 0xff;
        }
        else if (tag->version[0] == 4) {
            frame_size[0] = (f->size >> 21) & 0x7f;
            frame_size[1] = (f->size >> 14) & 0x7f;
            frame_size[2] = (f->size >> 7) & 0x7f;
            frame_size[3] = f->size & 0x7f;
        }
        if (fwrite (f->id, 1, 4, out) != 4) {
            fprintf (stderr, "junk_write_id3v2: failed to write frame id %s\n", f->id);
            goto error;
        }
        if (fwrite (frame_size, 1, 4, out) != 4) {
            fprintf (stderr, "junk_write_id3v2: failed to write frame size, id %s, size %d\n", f->id, f->size);
            goto error;
        }
        if (fwrite (f->flags, 1, 2, out) != 2) {
            fprintf (stderr, "junk_write_id3v2: failed to write frame header flags, id %s, size %s\n", f->id, f->size);
            goto error;
        }
        if (fwrite (f->data, 1, f->size, out) != f->size) {
            fprintf (stderr, "junk_write_id3v2: failed to write frame data, id %s, size %s\n", f->id, f->size);
            goto error;
        }
        sz += f->size;
    }

    // skip id3v2 tag
    fp = fopen (fname, "rb");
    if (!fp) {
        fprintf (stderr, "junk_write_id3v2: failed to open source file %s\n", fname);
        goto error;
    }
    int skip= junk_get_leading_size_stdio (fp);
    if (skip > 0) {
        fseek (fp, skip, SEEK_SET);
    }
    else {
        rewind (fp);
    }

    buffer = malloc (8192);
    int rb = 0;
    for (;;) {
        rb = fread (buffer, 1, 8192, fp);
        if (rb < 0) {
            fprintf (stderr, "junk_write_id3v2: error reading input data\n");
            goto error;
        }
        if (fwrite (buffer, 1, rb, out) != rb) {
            fprintf (stderr, "junk_write_id3v2: error writing output file\n");
            goto error;
        }
        if (rb == 0) {
            break; // eof
        }
    }

    err = 0;

error:
    if (out) {
        fclose (out);
    }
    if (fp) {
        fclose (fp);
    }
    if (buffer) {
        free (buffer);
    }
    return err;
}

void
junk_free_id3v2 (DB_id3v2_tag_t *tag) {
    while (tag->frames) {
        DB_id3v2_frame_t *next = tag->frames->next;
        free (tag->frames);
        tag->frames = next;
    }
}

int
junklib_id3v2_sync_frame (uint8_t *data, int size) {
    char *writeptr = data;
    int skipnext = 0;
    int written = 0;
    while (size > 0) {
        *writeptr = *data;
        if (data[0] == 0xff && size >= 2 && data[1] == 0) {
            data++;
            size--;
        }
        writeptr++;
        data++;
        size--;
        written++;
    }
    return written;
}

int
junk_read_id3v2_full (playItem_t *it, DB_id3v2_tag_t *tag_store, DB_FILE *fp) {
    DB_id3v2_frame_t *tail = NULL;
    int title_added = 0;
    if (!fp) {
        trace ("bad call to junk_read_id3v2!\n");
        return -1;
    }
    deadbeef->rewind (fp);
    uint8_t header[10];
    if (deadbeef->fread (header, 1, 10, fp) != 10) {
        return -1; // too short
    }
    if (strncmp (header, "ID3", 3)) {
        return -1; // no tag
    }
    uint8_t version_major = header[3];
    uint8_t version_minor = header[4];
    if (version_major > 4 || version_major < 2) {
        trace ("id3v2.%d.%d is unsupported\n", version_major, version_minor);
        return -1; // unsupported
    }
    uint8_t flags = header[5];
    if (flags & 15) {
        trace ("unrecognized flags: one of low 15 bits is set, value=0x%x\n", (int)flags);
        return -1; // unsupported
    }
    int unsync = (flags & (1<<7)) ? 1 : 0;
    int extheader = (flags & (1<<6)) ? 1 : 0;
    int expindicator = (flags & (1<<5)) ? 1 : 0;
    int footerpresent = (flags & (1<<4)) ? 1 : 0;
    // check for bad size
    if ((header[9] & 0x80) || (header[8] & 0x80) || (header[7] & 0x80) || (header[6] & 0x80)) {
        trace ("bad header size\n");
        return -1; // bad header
    }
    uint32_t size = (header[9] << 0) | (header[8] << 7) | (header[7] << 14) | (header[6] << 21);
    // FIXME: that should be accounted for
//    int startoffset = size + 10 + 10 * footerpresent;
//    if (startoffset > it->startoffset) {
//        it->startoffset = startoffset;
//    }

    trace ("tag size: %d\n", size);
    if (tag_store) {
        tag_store->version[0] = version_major;
        tag_store->version[1] = version_minor;
        tag_store->flags = flags;
        // remove unsync flag
        tag_store->flags &= ~ (1<<7);
    }

    uint8_t *tag = malloc (size);
    if (!tag) {
        fprintf (stderr, "junklib: out of memory while reading id3v2, tried to alloc %d bytes\n", size);
    }
    if (deadbeef->fread (tag, 1, size, fp) != size) {
        goto error; // bad size
    }
    uint8_t *readptr = tag;
    int crcpresent = 0;
    trace ("version: 2.%d.%d, unsync: %d, extheader: %d, experimental: %d\n", version_major, version_minor, unsync, extheader, expindicator);
    
    if (extheader) {
        uint32_t sz = (readptr[3] << 0) | (header[2] << 8) | (header[1] << 16) | (header[0] << 24);
        //if (size < 6) {
        //    goto error; // bad size
        //}
        readptr += sz;
        if (size < sz) {
            return -1; // bad size
        }
#if 0
        uint16_t extflags = (readptr[1] << 0) | (readptr[0] << 8);
        readptr += 2;
        uint32_t pad = (readptr[3] << 0) | (header[2] << 8) | (header[1] << 16) | (header[0] << 24);
        readptr += 4;
        if (extflags & 0x8000) {
            crcpresent = 1;
        }
        if (crcpresent && sz != 10) {
            return -1; // bad header
        }
        readptr += 4; // skip crc
#endif
    }
    char * (*convstr)(const unsigned char *, int);
    if (version_major == 3) {
        convstr = convstr_id3v2_2to3;
    }
    else {
        convstr = convstr_id3v2_4;
    }
    char *artist = NULL;
    char *album = NULL;
    char *band = NULL;
    char *track = NULL;
    char *title = NULL;
    char *year = NULL;
    char *vendor = NULL;
    char *comment = NULL;
    char *copyright = NULL;
    char *genre = NULL;
    char *performer = NULL;
    char *composer = NULL;
    char *numtracks = NULL;
    char *disc = NULL;
    int err = 0;
    while (readptr - tag <= size - 4) {
        if (version_major == 3 || version_major == 4) {
            char frameid[5];
            memcpy (frameid, readptr, 4);
            frameid[4] = 0;
            readptr += 4;
            if (readptr - tag >= size - 4) {
                break;
            }
            uint32_t sz;
            if (version_major == 4) {
                sz = (readptr[3] << 0) | (readptr[2] << 7) | (readptr[1] << 14) | (readptr[0] << 21);
            }
            else if (version_major == 3) {
                sz = (readptr[3] << 0) | (readptr[2] << 8) | (readptr[1] << 16) | (readptr[0] << 24);
            }
            else {
                trace ("unknown id3v2 version (2.%d.%d)\n", version_major, version_minor);
                return -1;
            }
            readptr += 4;
            trace ("got frame %s, size %d, pos %d, tagsize %d\n", frameid, sz, readptr-tag, size);
            if (readptr - tag >= size - sz) {
                trace ("frame is out of tag bounds\n");
                err = 1;
                break; // size of frame is more than size of tag
            }
            if (sz < 1) {
//                err = 1;
                break; // frame must be at least 1 byte long
            }
            uint8_t flags1 = readptr[0];
            uint8_t flags2 = readptr[1];
            readptr += 2;
            if (tag_store) {
                DB_id3v2_frame_t *frm = malloc (sizeof (DB_id3v2_frame_t) + sz);
                if (!frm) {
                    fprintf (stderr, "junklib: failed to alloc %d bytes for id3v2 frame %s\n", sizeof (DB_id3v2_frame_t) + sz, frameid);
                    goto error;
                }
                memset (frm, 0, sizeof (DB_id3v2_frame_t));
                if (tail) {
                    tail->next = frm;
                }
                tail = frm;
                if (!tag_store->frames) {
                    tag_store->frames = frm;
                }
                strcpy (frm->id, frameid);
                memcpy (frm->data, readptr, sz);
                if (unsync) {
                    frm->size = junklib_id3v2_sync_frame (frm->data, sz);
                    trace ("frame %s size change %d -> %d after sync\n", frameid, sz, frm->size);
                }
                else
                {
                    frm->size = sz;
                }

                frm->flags[0] = flags1;
                frm->flags[1] = flags2;
            }
            if (version_major == 4) {
                if (flags1 & 0x8f) {
                    // unknown flags
                    trace ("unknown status flags: %02x\n", flags1);
                    readptr += sz;
                    continue;
                }
                if (flags2 & 0xb0) {
                    // unknown flags
                    trace ("unknown format flags: %02x\n", flags2);
                    readptr += sz;
                    continue;
                }

                if (flags2 & 0x40) { // group id
                    trace ("frame has group id\n");
                    readptr++; // skip id
                    sz--;
                }
                if (flags2 & 0x08) { // compressed frame, ignore
                    trace ("frame is compressed, skipping\n");
                    readptr += sz;
                    continue;
                }
                if (flags2 & 0x04) { // encrypted frame, skip
                    trace ("frame is encrypted, skipping\n");
                    readptr += sz;
                    continue;
                }
                if (flags2 & 0x02) { // unsync, just do nothing
                }
                if (flags2 & 0x01) { // data size
                    uint32_t size = extract_i32 (readptr);
                    trace ("frame has extra size field = %d\n", size);
                    readptr += 4;
                    sz -= 4;
                }
            }
            else if (version_major == 3) {
                if (flags1 & 0x1F) {
                    trace ("unknown status flags: %02x\n", flags1);
                    readptr += sz;
                    continue;
                }
                if (flags2 & 0x1F) {
                    trace ("unknown format flags: %02x\n", flags2);
                    readptr += sz;
                    continue;
                }
                if (flags2 & 0x80) {
                    trace ("frame is compressed, skipping\n");
                    readptr += sz;
                    continue;
                }
                if (flags2 & 0x40) {
                    trace ("frame is encrypted, skipping\n");
                    readptr += sz;
                    continue;
                }
                if (flags2 & 0x20) {
                    trace ("frame has group id\n");
                    readptr++; // skip id
                    sz--;
                }
            }
            if (!strcmp (frameid, "TPE1")) {
                if (sz > 1000) {
                    err = 1;
                    break; // too large
                }
                char str[sz+2];
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                artist = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TPE2")) {
                if (sz > 1000) {
                    err = 1;
                    break; // too large
                }
                char str[sz+2];
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                band = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TRCK")) {
                if (sz > 1000) {
                    err = 1;
                    break; // too large
                }
                char str[sz+2];
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);

                track = convstr (str, sz);
                if (track) {
                    char *slash = strchr (track, '/');
                    if (slash) {
                        // split into track/number
                        *slash = 0;
                        slash++;
                        numtracks = strdup (slash);
                    }
                }
            }
            else if (!strcmp (frameid, "TPOS")) {
                if (sz > 1000) {
                    err = 1;
                    break; // too large
                }
                char str[sz+2];
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                disc = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TIT2")) {
                trace ("parsing TIT2...\n");
                if (sz > 1000) {
                    err = 1;
                    break; // too large
                }
                trace ("parsing TIT2....\n");
                char str[sz+2];
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                title = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TALB")) {
                if (sz > 1000) {
                    err = 1;
                    break; // too large
                }
                char str[sz+2];
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                album = convstr (str, sz);
            }
            else if (version_major == 3 && !strcmp (frameid, "TYER")) { // this frame is 2.3-only
                if (sz > 1000) {
                    err = 1;
                    break; // too large
                }
                char str[sz+2];
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                year = convstr (str, sz);
            }
            else if (version_major == 4 && !strcmp (frameid, "TDRC")) { // this frame is 2.3-only
                if (sz > 1000) {
                    err = 1;
                    break; // too large
                }
                char str[sz+2];
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                year = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TCOP")) {
                if (sz > 1000) {
                    err = 1;
                    break; // too large
                }
                char str[sz+2];
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                copyright = convstr (str, sz);
                trace ("TCOP: %s\n", copyright);
            }
            else if (!strcmp (frameid, "TCON")) {
                if (sz > 1000) {
                    err = 1;
                    break; // too large
                }
                char str[sz+2];
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                genre = convstr (str, sz);

                trace ("TCON: %s\n", genre);
            }
            else if (!strcmp (frameid, "COMM")) {
                if (sz < 4) {
                    trace ("COMM frame is too short, skipped\n");
                    readptr += sz; // bad tag
                    continue;
                }
                uint8_t enc = readptr[0];
                char lang[4] = {readptr[1], readptr[2], readptr[3], 0};
                if (strcmp (lang, "eng")) {
                    trace ("non-english comment, skip\n");
                    readptr += sz;
                    continue;
                }
                trace ("COMM enc is: %d\n", (int)enc);
                trace ("COMM language is: %s\n", lang);
                char *descr = readptr+4;
                trace ("COMM descr: %s\n", descr);
                if (!strcmp (descr, "iTunNORM")) {
                    // ignore itunes normalization metadata
                    readptr += sz;
                    continue;
                }
                int dlen = strlen(descr)+1;
                int s = sz - 4 - dlen;
                char str[s + 3];
                id3v2_string_read (version_major, &str[1], s, unsync, descr+dlen);
                str[0] = enc;
                comment = convstr (str, s+1);
                trace ("COMM text: %s\n", comment);
            }
            else if (!strcmp (frameid, "TENC")) {
                if (sz > 1000) {
                    err = 1;
                    break; // too large
                }
                char str[sz+2];
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                vendor = convstr (str, sz);
                trace ("TENC: %s\n", vendor);
            }
            else if (!strcmp (frameid, "TPE3")) {
                if (sz > 1000) {
                    err = 1;
                    break; // too large
                }
                char str[sz+2];
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                performer = convstr (str, sz);
                trace ("TPE3: %s\n", performer);
            }
            else if (!strcmp (frameid, "TCOM")) {
                if (sz > 1000) {
                    err = 1;
                    break; // too large
                }
                char str[sz+2];
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                composer = convstr (str, sz);
                trace ("TCOM: %s\n", composer);
            }
            else if (!strcmp (frameid, "TXXX")) {
                if (sz < 2) {
                    trace ("TXXX frame is too short, skipped\n");
                    readptr += sz; // bad tag
                    continue;
                }
                uint8_t *p = readptr;
                uint8_t encoding = *p;
                p++;
                uint8_t *desc = p;
                int desc_sz = 0;
                while (*p && p - readptr < sz) {
                    p++;
                    desc_sz++;
                }
                p++;
                if (p - readptr >= sz) {
                    trace ("bad TXXX frame, skipped\n");
                    readptr += sz; // bad tag
                    continue;
                }
                char desc_s[desc_sz+2];
                id3v2_string_read (version_major, desc_s, desc_sz, unsync, desc);
                //trace ("desc=%s\n", desc_s);
                char value_s[readptr+sz-p+2];
                id3v2_string_read (version_major, value_s, readptr+sz-p, unsync, p);
                //trace ("value=%s\n", value_s);
                if (!strcasecmp (desc_s, "replaygain_album_gain")) {
                    it->replaygain_album_gain = atof (value_s);
                    trace ("%s=%s (%f)\n", desc_s, value_s, it->replaygain_album_gain);
                }
                else if (!strcasecmp (desc_s, "replaygain_album_peak")) {
                    it->replaygain_album_peak = atof (value_s);
                    trace ("%s=%s (%f)\n", desc_s, value_s, it->replaygain_album_peak);
                }
                else if (!strcasecmp (desc_s, "replaygain_track_gain")) {
                    it->replaygain_track_gain = atof (value_s);
                    trace ("%s=%s (%f)\n", desc_s, value_s, it->replaygain_track_gain);
                }
                else if (!strcasecmp (desc_s, "replaygain_track_peak")) {
                    it->replaygain_track_peak = atof (value_s);
                    trace ("%s=%s (%f)\n", desc_s, value_s, it->replaygain_track_peak);
                }
            }
            readptr += sz;
        }
        else if (version_major == 2) {
            char frameid[4];
            memcpy (frameid, readptr, 3);
            frameid[3] = 0;
            readptr += 3;
            if (readptr - tag >= size - 3) {
                break;
            }
            uint32_t sz = (readptr[2] << 0) | (readptr[1] << 8) | (readptr[0] << 16);
            readptr += 3;
            if (readptr - tag >= size - sz) {
                break; // size of frame is less than size of tag
            }
            if (sz < 1) {
                break; // frame must be at least 1 byte long
            }
            if (tag_store) {
                DB_id3v2_frame_t *frm = malloc (sizeof (DB_id3v2_frame_t) + sz);
                if (!frm) {
                    fprintf (stderr, "junklib: failed to alloc %d bytes for id3v2.2 frame %s\n", sizeof (DB_id3v2_frame_t) + sz, frameid);
                    goto error;
                }
                if (tail) {
                    tail->next = frm;
                }
                tail = frm;
                if (!tag_store->frames) {
                    tag_store->frames = frm;
                }
                strcpy (frm->id, frameid);
                memcpy (frm->data, readptr, sz);
                frm->size = sz;
            }
//            trace ("found id3v2.2 frame: %s, size=%d\n", frameid, sz);
            if (!strcmp (frameid, "TEN")) {
                char str[sz+2];
                //memcpy (str, readptr, sz);
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                str[sz] = 0;
                vendor = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TT2")) {
                if (sz > 1000) {
                    readptr += sz;
                    continue;
                }
                char str[sz+2];
                //memcpy (str, readptr, sz);
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                str[sz] = 0;
                title = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TAL")) {
                if (sz > 1000) {
                    readptr += sz;
                    continue;
                }
                char str[sz+2];
                //memcpy (str, readptr, sz);
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                str[sz] = 0;
                album = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TP1")) {
                if (sz > 1000) {
                    readptr += sz;
                    continue;
                }
                char str[sz+2];
                //memcpy (str, readptr, sz);
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                str[sz] = 0;
                artist = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TP2")) {
                if (sz > 1000) {
                    readptr += sz;
                    continue;
                }
                char str[sz+2];
                //memcpy (str, readptr, sz);
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                str[sz] = 0;
                band = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TP3")) {
                if (sz > 1000) {
                    readptr += sz;
                    continue;
                }
                char str[sz+2];
                //memcpy (str, readptr, sz);
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                str[sz] = 0;
                performer = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TCM")) {
                if (sz > 1000) {
                    readptr += sz;
                    continue;
                }
                char str[sz+2];
                //memcpy (str, readptr, sz);
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                str[sz] = 0;
                composer = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TPA")) {
                if (sz > 1000) {
                    readptr += sz;
                    continue;
                }
                char str[sz+2];
                //memcpy (str, readptr, sz);
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                str[sz] = 0;
                disc = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TRK")) {
                if (sz > 1000) {
                    readptr += sz;
                    continue;
                }
                char str[sz+2];
                //memcpy (str, readptr, sz);
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                str[sz] = 0;
                track = convstr (str, sz);
                if (track) {
                    char *slash = strchr (track, '/');
                    if (slash) {
                        // split into track/number
                        *slash = 0;
                        slash++;
                        numtracks = strdup (slash);
                    }
                }
            }
            else if (!strcmp (frameid, "TYE")) {
                if (sz > 1000) {
                    readptr += sz;
                    continue;
                }
                char str[sz+2];
                //memcpy (str, readptr, sz);
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                str[sz] = 0;
                year = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TCR")) {
                if (sz > 1000) {
                    readptr += sz;
                    continue;
                }
                char str[sz+2];
                //memcpy (str, readptr, sz);
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                str[sz] = 0;
                copyright = convstr (str, sz);
            }
            else if (!strcmp (frameid, "TCO")) {
                if (sz > 1000) {
                    readptr += sz;
                    continue;
                }
                char str[sz+2];
                //memcpy (str, readptr, sz);
                id3v2_string_read (version_major, &str[0], sz, unsync, readptr);
                str[sz] = 0;
                genre = convstr (str, sz);
            }
            else if (!strcmp (frameid, "COM")) {
                if (sz > 1000) {
                    readptr += sz;
                    continue;
                }
                uint8_t enc = readptr[0];
                char lang[4] = {readptr[1], readptr[2], readptr[3], 0};
                if (strcmp (lang, "eng")) {
                    trace ("non-english comment, skip\n");
                    readptr += sz;
                    continue;
                }
                trace ("COM enc is: %d\n", (int)enc);
                trace ("COM language is: %s\n", lang);
                char *descr = readptr+4;
                trace ("COM descr: %s\n", descr);
                int dlen = strlen(descr)+1;
                int s = sz - 4 - dlen;
                char str[s + 3];
                id3v2_string_read (version_major, &str[1], s, unsync, descr+dlen);
                str[0] = enc;
                comment = convstr (str, s+1);
                trace ("COM text: %s\n", comment);
            }
            readptr += sz;
        }
        else {
            trace ("id3v2.%d (unsupported!)\n", version_minor);
        }
    }
    if (!err && it) {
        if (artist) {
            pl_add_meta (it, "artist", artist);
            free (artist);
        }
        if (album) {
            pl_add_meta (it, "album", album);
            free (album);
        }
        if (band) {
            pl_add_meta (it, "band", band);
            free (band);
        }
        if (performer) {
            pl_add_meta (it, "performer", performer);
            free (performer);
        }
        if (composer) {
            pl_add_meta (it, "composer", composer);
            free (composer);
        }
        if (track) {
            pl_add_meta (it, "track", track);
            free (track);
        }
        if (numtracks) {
            pl_add_meta (it, "numtracks", numtracks);
            free (numtracks);
        }
        if (title) {
            pl_add_meta (it, "title", title);
            free (title);
        }
        if (genre) {
            if (genre[0] == '(') {
                // find matching parenthesis
                const char *p = &genre[1];
                while (*p && *p != ')') {
                    if (!isdigit (*p)) {
                        break;
                    }
                    p++;
                }
                if (*p == ')' && p[1] == 0) {
                    memmove (genre, genre+1, p-genre-1);
                }
            }
            // check if it is numeric
            if (genre) {
                const char *p = genre;
                while (*p) {
                    if (!isdigit (*p)) {
                        break;
                    }
                    p++;
                }
                if (*p == 0) {
                    int genre_id = atoi (genre);
                    if (genre_id >= 0) {
                        const char *genre_str = NULL;
                        if (genre_id <= 147) {
                            genre_str = junk_genretbl[genre_id];
                        }
                        else if (genre_id == 0xff) {
                            genre_str = "None";
                        }
                        if (genre_str) {
                            free (genre);
                            genre = strdup (genre_str);
                        }
                    }
                }
                else if (!strcmp (genre, "CR")) {
                    free (genre);
                    genre = strdup ("Cover");
                }
                else if (!strcmp (genre, "RX")) {
                    free (genre);
                    genre = strdup ("Remix");
                }
            }

            pl_add_meta (it, "genre", genre);
            free (genre);
        }
        if (year) {
            pl_add_meta (it, "year", year);
            free (year);
        }
        if (copyright) {
            pl_add_meta (it, "copyright", copyright);
            free (copyright);
        }
        if (vendor) {
            pl_add_meta (it, "vendor", vendor);
            free (vendor);
        }
        if (comment) {
            pl_add_meta (it, "comment", comment);
            free (comment);
        }
        if (disc) {
            pl_add_meta (it, "disc", disc);
            free (disc);
        }
        char new_tags[100] = "";
        const char *tags = pl_find_meta (it, "tags");
        if (tags) {
            strcpy (new_tags, tags);
        }
        if (version_major == 2) {
            strcat (new_tags, "ID3v2.2 ");
        }
        else if (version_major == 3) {
            strcat (new_tags, "ID3v2.3 ");
        }
        else if (version_major == 4) {
            strcat (new_tags, "ID3v2.4 ");
        }
        pl_replace_meta (it, "tags", new_tags);
        if (!title) {
            pl_add_meta (it, "title", NULL);
        }
        return 0;
    }
    else if (err) {
        trace ("error parsing id3v2\n");
    }

    return 0;

error:
    if (tag) {
        free (tag);
    }
    if (tag_store) {
        while (tag_store->frames) {
            DB_id3v2_frame_t *next = tag_store->frames->next;
            free (tag_store->frames);
            tag_store->frames = next;
        }
    }
    return -1;
}

int
junk_read_id3v2 (playItem_t *it, DB_FILE *fp) {
    return junk_read_id3v2_full (it, NULL, fp);
}

const char *
junk_detect_charset (const char *s) {
    // check if that's already utf8
    if (u8_valid (s, strlen (s), NULL)) {
        return NULL; // means no recoding required
    }
    // check if that could be non-latin1 (too many nonascii chars)
    if (can_be_russian (s)) {
        return "cp1251";
    }
    return "iso8859-1";
}

void
junk_recode (const char *in, int inlen, char *out, int outlen, const char *cs) {
    iconv_t cd = iconv_open (UTF8, cs);
    if (cd == (iconv_t)-1) {
        trace ("iconv can't recode from %s to utf8\n", cs);
        memcpy (out, in, min(inlen, outlen));
        return;
    }
    else {
        size_t inbytesleft = inlen;
        size_t outbytesleft = outlen;
#ifdef __linux__
        char *pin = (char*)in;
#else
        const char *pin = in;
#endif
        char *pout = out;
        memset (out, 0, outlen);
        size_t res = iconv (cd, &pin, &inbytesleft, &pout, &outbytesleft);
        iconv_close (cd);
    }
}

