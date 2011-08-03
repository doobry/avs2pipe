/* 
 * Copyright (C) 2010-2011 Chris Beswick <chris.beswick@gmail.com>
 *
 * This file is part of avs2pipe.
 *
 * avs2pipe is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * avs2pipe is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with avs2pipe.  If not, see <http://www.gnu.org/licenses/>.
 *
 * YUV4MPEG2 output derived from Avs2YUV by Loren Merritt
 * AviSynth 2.6.0 Alpha 2 Color Spaces from Chikuzen @ Doom9 Forums
 *
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <string.h>
#ifdef A2P_AVS26
    #include "avisynth26/avisynth_c.h"
#else
    #include "avisynth/avisynth_c.h"
#endif
#include "common.h"
#include "wave.h"


AVS_Clip *
a2p_avs_invoke(AVS_ScriptEnvironment *env, const char *name, AVS_Value *arg)
{
    AVS_Value val_array, val_return;
    AVS_Clip *clip;
    
    val_array = avs_new_value_array(arg, 1);
    val_return = avs_invoke(env, name, val_array, 0);
    
    if(avs_is_error(val_return)) {
        a2p_log(A2P_LOG_ERROR, "%s\n", avs_as_string(val_return));
    } else if(!avs_is_clip(val_return)) {
        a2p_log(A2P_LOG_ERROR, "%s return value is not a clip.\n", name);
    }
    
    clip = avs_take_clip(val_return, env);
    
    avs_release_value(val_array);
    avs_release_value(val_return);
    
    return clip;
}

AVS_Clip *
a2p_avs_filter(AVS_ScriptEnvironment *env, const char *filter, AVS_Clip *clip)
{
    AVS_Value val_clip;
    
    val_clip = avs_new_value_clip(clip);
    avs_release_clip(clip);
    
    clip = a2p_avs_invoke(env, filter, &val_clip);
    
    avs_release_value(val_clip);
    
    return clip;
}

AVS_Clip *
a2p_avs_source(AVS_ScriptEnvironment *env, char *file)
{
    AVS_Clip *clip;
    AVS_Value val_string;
    char *ext, *import;
    
    import = "Import";
    ext = strrchr(file, '.');
    if(!strcmp(ext, ".avsg")) {
        if(avs_function_exists(env, "GImport")) {
            a2p_log(A2P_LOG_INFO, "Enabling GScript support for GImport.\n");
            import = "GImport";
        } else {
            a2p_log(A2P_LOG_WARNING, "GScript support missing, trying standard Import.\n");
        }
    }
    
    val_string = avs_new_value_string(file);
    
    clip = a2p_avs_invoke(env, import, &val_string);
    
    avs_release_value(val_string);
    
    return clip;
}

void
a2p_do_audio(AVS_ScriptEnvironment *env, AVS_Clip *clip)
{
    const AVS_VideoInfo *info;
    WaveRiffHeader *header;
    WaveFormatType format;
    void *buff;
    size_t size, count, step;
    uint64_t i, wrote, target;
    
    info = avs_get_video_info(clip);
    
    if(!avs_has_audio(info)) {
        a2p_log(A2P_LOG_ERROR, "source has no audio.\n");
    }
    
    // AviSynth only supports AVS_SAMPLE_FLOAT & AVS_SAMPLE_INT*
    switch(info->sample_type) {
        case AVS_SAMPLE_FLOAT:
            format = WAVE_FORMAT_IEEE_FLOAT;
            break;
        default:
            a2p_log(A2P_LOG_WARNING, "audio format unknown trying PCM.\n");
        case AVS_SAMPLE_INT8:
        case AVS_SAMPLE_INT16:
        case AVS_SAMPLE_INT24:
        case AVS_SAMPLE_INT32:
            format = WAVE_FORMAT_PCM;
            break;
    }
    
    if(_setmode(_fileno(stdout), _O_BINARY) == -1) {
        a2p_log(A2P_LOG_ERROR, "cannot switch stdout to binary mode.\n");
    }
    
    a2p_log(A2P_LOG_INFO, "writing %I64d seconds of %d Hz, %d channel audio.\n",
            (info->num_audio_samples / info->audio_samples_per_second),
            info->audio_samples_per_second, info->nchannels);
    
    header = wave_create_riff_header(format, info->nchannels,
                                     info->audio_samples_per_second,
                                     avs_bytes_per_channel_sample(info),
                                     info->num_audio_samples);
    fwrite(header, sizeof(*header), 1, stdout);
    fflush(stdout);
    free(header); // free the wav header
    
    count = info->audio_samples_per_second;
    wrote = 0;
    target = info->num_audio_samples;
    size = avs_bytes_per_channel_sample(info) * info->nchannels;
    buff = malloc(count * size);
    if(buff == NULL) { // some idiot (me) forgot to check malloc return before
        a2p_log(A2P_LOG_ERROR, "could not allocate sample buffer.\n");
    }
    for (i = 0; i < target; i += count) {
        if(target - i < count) count = (size_t) (target - i);
        avs_get_audio(clip, buff, i, count);
        step = fwrite(buff, size, count, stdout);
        wrote += step;
        // fail early if there is a problem instead of end of input
        if(step != count) break;
    }
    fflush(stdout);
    free(buff);
    
    a2p_log(A2P_LOG_INFO, "finished, wrote %I64u seconds [%I64u%%].\n", 
        wrote / info->audio_samples_per_second,
        (100 * wrote) / target);
    
    if(wrote != target) {
        a2p_log(A2P_LOG_ERROR, "only wrote %I64u of %I64u samples.\n",
                wrote, target);
    }
}

void
a2p_do_video(AVS_ScriptEnvironment *env, AVS_Clip *clip)
{
    #define MAX_PLANES 3
    static const int planes[] = {AVS_PLANAR_Y, AVS_PLANAR_U, AVS_PLANAR_V};
    static const char *FRAME_HEADER = "FRAME\n";
    
    const AVS_VideoInfo *info;
    AVS_VideoFrame *frame;
    
    int32_t width[MAX_PLANES], height[MAX_PLANES];
    int32_t width_sft, height_sft, planes_num;
    char *yuv_csp;

    BYTE *buff, *buff_ptr, *buff_0;
    int32_t buff_inc[MAX_PLANES], buff_sz;
    
    int32_t p, wrote; // plane and frame for loop counts
    size_t step, count;
    
    //const BYTE *read_ptr;
    //int32_t r, pitch;
    
    info = avs_get_video_info(clip);
    
    if(!avs_has_video(info)) {
        a2p_log(A2P_LOG_ERROR, "source has no video.\n");
    }
    
    // Default number of planes to MAX_PLANES
    planes_num = MAX_PLANES;
    
    // Setup correct color space handling, tnx Chikuzen
    // If np > 3 is ever needed increase MAX_PLANES define
    switch(info->pixel_type) {
        #ifdef A2P_AVS26
        case AVS_CS_BGR32:
        case AVS_CS_BGR24:
            a2p_log(A2P_LOG_INFO, "converting video to yv24.\n");
            clip = a2p_avs_filter(env, "ConvertToYV24", clip);
            info = avs_get_video_info(clip);
        case AVS_CS_YV24:
            yuv_csp = "444";
            count = info->width * info->height * 3;
            width_sft = 0;
            height_sft = 0;
            break;
        case AVS_CS_YUY2:
            a2p_log(A2P_LOG_INFO, "converting video to yv16.\n");
            clip = a2p_avs_filter(env, "ConvertToYV16", clip);
            info = avs_get_video_info(clip);
        case AVS_CS_YV16:
            yuv_csp = "422";
            count = info->width * info->height * 2;
            width_sft = 1;
            height_sft = 0;
            break;
        case AVS_CS_YV411:
            yuv_csp = "411";
            count = info->width * info->height * 3 / 2;
            width_sft = 2;
            height_sft = 0;
            break;
        case AVS_CS_Y8:
            yuv_csp = "mono";
            count = info->width * info->height;
            width_sft = 0;
            height_sft = 0;
            planes_num = 1; // special case only one plane for mono
            break;
        #endif        
        default:
            a2p_log(A2P_LOG_INFO, "converting video to yv12.\n");
            clip = a2p_avs_filter(env, "ConvertToYV12", clip);
            info = avs_get_video_info(clip);
        case AVS_CS_I420:
        case AVS_CS_YV12:
            yuv_csp = "420";
            count = info->width * info->height * 3 / 2;
            width_sft = 1;
            height_sft = 1;
    }
    
    if(_setmode(_fileno(stdout), _O_BINARY) == -1) {
        a2p_log(A2P_LOG_ERROR, "cannot switch stdout to binary mode.\n");
    }
    
    a2p_log(A2P_LOG_INFO, "writing %d frames of %d/%d fps, %dx%d YUV%s %s video.\n",
            info->num_frames, info->fps_numerator, info->fps_denominator,
            info->width, info->height, yuv_csp, !avs_is_field_based(info) ?
             "progressive" : !avs_is_bff(info) ? "tff" : "bff"); // default tff
    
    // YUV4MPEG2 header http://wiki.multimedia.cx/index.php?title=YUV4MPEG2
    fprintf(stdout, "YUV4MPEG2 W%d H%d F%u:%u I%s A0:0 C%s\n", info->width,
            info->height, info->fps_numerator, info->fps_denominator,
            !avs_is_field_based(info) ? "p" : !avs_is_bff(info) ? "t" : "b",
            yuv_csp);
    fflush(stdout);
    
    // avs2yuv method changed to c with malloc, memcpy, avs_bit_blt, more csps
    // calculate output buffer planes pitches
    buff_sz = strlen(FRAME_HEADER) * sizeof(char); // space for FRAME header
    count += buff_sz / sizeof(BYTE); // increase count to add FRAME
    for(p = 0; p < planes_num; p++) {
        width[p] = info->width >> (p ? width_sft : 0);
        height[p] = info->height >> (p? height_sft : 0);
        buff_inc[p] = width[p] * height[p] * sizeof(BYTE);
        buff_sz += buff_inc[p];
        //buff_inc[p] = width[p] * sizeof(BYTE);
        //buff_sz += buff_inc[p] * height[p];
    }
    // check buff size to be sure on spec
    if(buff_sz / sizeof(BYTE) != count) {
        a2p_log(A2P_LOG_ERROR, "buffer size %d does not match count %d.\n",
                buff_sz / sizeof(BYTE), count);
    }
    buff = (BYTE *) malloc(buff_sz);
    if(buff == NULL) { // some idiot (me) forgot to check malloc return before
        a2p_log(A2P_LOG_ERROR, "could not allocate frame buffer.\n");
    }
    // copy FRAME header to buffer and offset past it
    memcpy(buff, FRAME_HEADER, strlen(FRAME_HEADER) * sizeof(char));
    buff_0 = buff + (strlen(FRAME_HEADER) * sizeof(char));
    wrote = 0;
    while(wrote < info->num_frames) {
        frame = avs_get_frame(clip, wrote);
        buff_ptr = buff_0; // reset buff pointer
        for(p = 0; p < planes_num; p++) {
            // use avs_bit_blt to perform copy
            avs_bit_blt(env, buff_ptr, width[p], avs_get_read_ptr_p(frame, planes[p]),
                        avs_get_pitch_p(frame, planes[p]),
                        avs_get_row_size_p(frame, planes[p]),
                        avs_get_height_p(frame, planes[p]));
            buff_ptr += buff_inc[p];
            
            // use memcpy to perform copy
            /*pitch = avs_get_pitch_p(frame, planes[p]);
            read_ptr = avs_get_read_ptr_p(frame, planes[p]);
            for(r = 0; r < height[p]; r++) {
                memcpy(buff_ptr, read_ptr, buff_inc[p]);
                read_ptr += pitch;
                buff_ptr += buff_inc[p];
            }*/
        }
        step = fwrite(buff, sizeof(BYTE), count, stdout);
        avs_release_frame(frame);
        // fail early if there is a problem instead of end of input
        if(step != count) break;
        wrote++;
    }
    fflush(stdout);
    free(buff);
    
    if(wrote != info->num_frames) {
        a2p_log(A2P_LOG_ERROR, "failed, only wrote %d of %d frames.\n",
                wrote, info->num_frames);
    } else {
        a2p_log(A2P_LOG_INFO, "finished, wrote %d frames [%d%%].\n", 
                wrote, (100 * wrote) / info->num_frames);
    }
}

void
a2p_do_info(AVS_ScriptEnvironment *env, AVS_Clip *clip)
{
    const AVS_VideoInfo *info;
    
    info = avs_get_video_info(clip);
    
    if(avs_has_video(info)) {
        fprintf(stdout, "v:width       %d\n", info->width);
        fprintf(stdout, "v:height      %d\n", info->height);
        fprintf(stdout, "v:fps         %d/%d\n",
                info->fps_numerator, info->fps_denominator);
        fprintf(stdout, "v:frames      %d\n", info->num_frames);
        fprintf(stdout, "v:duration    %d\n",
                info->num_frames * info->fps_denominator / info->fps_numerator);
        fprintf(stdout, "v:interlaced  %s\n", !avs_is_field_based(info) ?
                "no" : !avs_is_bff(info) ? "tff" : "bff");
        fprintf(stdout, "v:pixel_type  %x\n", info->pixel_type);
    }
    if(avs_has_audio(info)) {
        fprintf(stdout, "a:sample_rate %d\n", info->audio_samples_per_second);
        fprintf(stdout, "a:format      %s\n",
                info->sample_type == AVS_SAMPLE_FLOAT ? "float" : "pcm");
        fprintf(stdout, "a:bit_depth   %d\n",
                avs_bytes_per_channel_sample(info) * 8);
        fprintf(stdout, "a:channels    %d\n", info->nchannels);
        fprintf(stdout, "a:samples     %I64d\n", info->num_audio_samples);
        fprintf(stdout, "a:duration    %I64d\n",
                info->num_audio_samples / info->audio_samples_per_second);
    }
}

void
a2p_do_x264bd(AVS_ScriptEnvironment *env, AVS_Clip *clip)
{
    // x264 arguments from http://sites.google.com/site/x264bluray/
    // resolutions, fps... http://forum.doom9.org/showthread.php?t=154533
    //   vbv-maxrate reduced to 15000 from 40000 for DVD playback
    //   ref added to all settings for simplicity
    
    const AVS_VideoInfo *info;
    enum res {
        A2P_RES_1080,
        A2P_RES_720,
        A2P_RES_576,
        A2P_RES_480,
        A2P_RES_UNKNOWN
    } res;
    enum fps {
        A2P_FPS_59, // 59.94
        A2P_FPS_50, // 50
        A2P_FPS_30, // 30
        A2P_FPS_29, // 29.97
        A2P_FPS_25, // 25
        A2P_FPS_24, // 24
        A2P_FPS_23, // 23.976
        A2P_FPS_UNKNOWN
    } fps;
    
    int keyint;
    int ref;
    char * args;
    char * color;
    
    info = avs_get_video_info(clip);
    
    // Initial format guess, setting ref and color.
    // max safe width or height = 32767
    // width << 16         | height
    // 0000 0000 0000 0000   0000 0000  0000 0000
    switch((info->width << 16) | info->height) {
        case (1920 << 16) | 1080: // HD1080
        case (1440 << 16) | 1080:
            res = A2P_RES_1080;
            ref = 4;
            color = "bt709";
            break;
        case (1280 << 16) | 720: // HD720
            res = A2P_RES_720;
            ref = 6;
            color = "bt709";
            break;
        case (720 << 16) | 576: // PAL
            res = A2P_RES_576;
            ref = 6;
            color = "bt470bg";
            break;
        case (720 << 16) | 480: // NTSC
            res = A2P_RES_480;
            ref = 6;
            color = "smpte170m";
            break;
        default:
            res = A2P_RES_UNKNOWN;
            ref = 0;
            color = "";
            break;
    }
    
    // See http://avisynth.org/mediawiki/FPS
    // fps_numerator << 16 | fps_denominator
    // 0000 0000 0000 0000   0000 0000  0000 0000
    switch((info->fps_numerator << 16) | info->fps_denominator) {
        case ((24000 << 16) | 1001): // ntsc_film ~23.976
        case ((2997 << 16) | 125): // ntsc_round_film =23.976
            fps = A2P_FPS_23;
            keyint = 24;
            break;
        case ((24 << 16) | 1): // film =24
            fps = A2P_FPS_24;
            keyint = 24;
            break;
        case ((25 << 16) | 1): // pal_film, pal_video =25
            fps = A2P_FPS_25;
            keyint = 25;
            break;
        case ((30000 << 16) | 1001): // ntsc_video ~29.970
        case ((2997 << 16) | 100): // ntsc_round_video =29.97
            fps = A2P_FPS_29;
            keyint = 30;
            break;
        case ((30 << 16) | 1): // 1080i30 exists in enc guide =30
            fps = A2P_FPS_30;
            keyint = 30;
            break;
        case ((50 << 16) | 1): // pal_double =50
            fps = A2P_FPS_50;
            keyint = 50;
            break;
        case ((60000 << 16) | 1001): // ntsc_double ~59.940
        case ((2997 << 16) | 50): // ntsc_round_double =59.94
            fps = A2P_FPS_59;
            keyint = 60;
            break;
            break;
        default:
            fps = A2P_FPS_UNKNOWN;
            keyint = 0;
            break;
    }
    
    // Ensure video is supported... this is messy
    // and set any special case arguments.
    // res << 16           | fps << 8  | avs_is_field_based
    // 0000 0000 0000 0000   0000 0000   0000 0000
    switch((res << 16) | fps << 8 | avs_is_field_based(info)) {
        case ((A2P_RES_1080 << 16) | (A2P_FPS_25 << 8) | 1):
        case ((A2P_RES_1080 << 16) | (A2P_FPS_29 << 8) | 1):
        case ((A2P_RES_1080 << 16) | (A2P_FPS_30 << 8) | 1):
        case ((A2P_RES_576 << 16) | (A2P_FPS_25 << 8) | 1):
        case ((A2P_RES_480 << 16) | (A2P_FPS_29 << 8) | 1):
            args = !avs_is_bff(info) ? "--tff" : "--bff"; // default to tff
            break;
        case ((A2P_RES_1080 << 16) | (A2P_FPS_29 << 8) | 0):
        case ((A2P_RES_1080 << 16) | (A2P_FPS_25 << 8) | 0):
        case ((A2P_RES_576 << 16) | (A2P_FPS_25 << 8) | 0):
            args = "--fake-interlaced --pic-struct";
            break;
        case ((A2P_RES_720 << 16) | (A2P_FPS_29 << 8) | 0):
        case ((A2P_RES_720 << 16) | (A2P_FPS_25 << 8) | 0):
            args = "--pulldown double";
            break;
        case ((A2P_RES_480 << 16) | (A2P_FPS_29 << 8) | 0):
            args = "--pulldown 32 --fake-interlaced";
            break;
        case ((A2P_RES_1080 << 16) | (A2P_FPS_23 << 8) | 0):
        case ((A2P_RES_1080 << 16) | (A2P_FPS_24 << 8) | 0):
        case ((A2P_RES_720 << 16) | (A2P_FPS_23 << 8) | 0):
        case ((A2P_RES_720 << 16) | (A2P_FPS_24 << 8) | 0):
        case ((A2P_RES_720 << 16) | (A2P_FPS_50 << 8) | 0):
        case ((A2P_RES_720 << 16) | (A2P_FPS_59 << 8) | 0):
            args = "";
            break;
        default:
            args = "";
            a2p_log(A2P_LOG_ERROR, "%dx%d @ %d/%d fps not supported.\n",
                    info->width, info->height,
                    info->fps_numerator, info->fps_denominator);
            break;
    }
    
    fprintf(stdout, "--weightp 1 --bframes 3 --nal-hrd vbr --vbv-maxrate 15000"
            " --vbv-bufsize 30000 --level 4.1 --keyint %d --b-pyramid strict"
            " --open-gop bluray --slices 4 --ref %d %s --aud --colorprim "
            "\"%s\" --transfer \"%s\" --colormatrix \"%s\"",
            keyint, ref, args, color, color, color);
}

int __cdecl
main (int argc, char *argv[])
{
    AVS_ScriptEnvironment *env;
    AVS_Clip *clip;
    char * input;
    enum {
        A2P_ACTION_AUDIO,
        A2P_ACTION_VIDEO,
        A2P_ACTION_INFO,
        A2P_ACTION_X264BD,
        A2P_ACTION_NOTHING    
    } action;
    
    action = A2P_ACTION_NOTHING;
    
    if(argc == 3) {
        if(strcmp(argv[1], "audio") == 0) {
            action = A2P_ACTION_AUDIO;
        } else if(strcmp(argv[1], "video") == 0) {
            action = A2P_ACTION_VIDEO;
        } else if(strcmp(argv[1], "info") == 0) {
            action = A2P_ACTION_INFO;
        } else if(strcmp(argv[1], "x264bd") == 0) {
            action = A2P_ACTION_X264BD;
        }
        input = argv[2];
    }
    
    if(action == A2P_ACTION_NOTHING) {
       
        #ifdef A2P_AVS26
            fprintf(stderr, "avs2pipe for AviSynth 2.6.0 Alpha 2\n");
        #else
            fprintf(stderr, "avs2pipe for AviSynth 2.5.8\n");
        #endif
        fprintf(stderr, "Usage: avs2pipe [audio|video|info|x264] input.avs\n");
        fprintf(stderr, "   audio  - output wav extensible format audio to stdout.\n");
        fprintf(stderr, "   video  - output yuv4mpeg2 format video to stdout.\n");
        fprintf(stderr, "   info   - output information about aviscript clip.\n");
        fprintf(stderr, "   x264bd - suggest x264 arguments for bluray disc encoding.\n");
        exit(2);
    }
    
    env = avs_create_script_environment(AVISYNTH_INTERFACE_VERSION);
    clip = a2p_avs_source(env, input);
    
    switch(action) {
        case A2P_ACTION_AUDIO:
            a2p_do_audio(env, clip);
            break;
        case A2P_ACTION_VIDEO:
            a2p_do_video(env, clip);
            break;
        case A2P_ACTION_INFO:
            a2p_do_info(env, clip);
            break;
        case A2P_ACTION_X264BD:
            a2p_do_x264bd(env, clip);
            break;
        case A2P_ACTION_NOTHING: // Removing GCC warning, this action is handled above
            break;
    }
    
    avs_release_clip(clip);
    avs_delete_script_environment(env);
    
    exit(0);
}
