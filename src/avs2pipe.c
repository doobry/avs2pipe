/* 
 * Copyright (C) 2010 Chris Beswick <chris.beswick@gmail.com>
 *
 * YUV4MPEG2 output inspired by Avs2YUV by Loren Merritt
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
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <string.h>
#include "avisynth_c.h"
#include "common.h"
#include "wave.h"

AVS_Clip *
avisynth_filter(AVS_Clip *clip, AVS_ScriptEnvironment *env, const char *filter)
{
    AVS_Value val_clip, val_array, val_return;

    val_clip = avs_new_value_clip(clip);
    avs_release_clip(clip);

    val_array = avs_new_value_array(&val_clip, 1);
    val_return = avs_invoke(env, filter, val_array, 0);
    clip = avs_take_clip(val_return, env);

    avs_release_value(val_array);
    avs_release_value(val_clip);
    avs_release_value(val_return);

    return clip;
}

AVS_Clip *
avisynth_source(char *file, AVS_ScriptEnvironment *env)
{
    AVS_Clip *clip;
    AVS_Value val_string, val_array, val_return;

    val_string = avs_new_value_string(file);
    val_array = avs_new_value_array(&val_string, 1);
    val_return = avs_invoke(env, "Import", val_array, 0);
    avs_release_value(val_array);
    avs_release_value(val_string);

    if(!avs_is_clip(val_return)) {
        a2p_log(A2P_LOG_ERROR, "avs files return value is not a clip.\n", 0);
        exit(2);
    }

    clip = avs_take_clip(val_return, env);

    avs_release_value(val_return);

    return clip;
}

void
audio_write(AVS_Clip *clip, AVS_ScriptEnvironment *env)
{
    const AVS_VideoInfo *info;
    WaveRiffHeader *header;
    WaveFormatType format;
    void *buff;
    size_t size, count, step;
    uint64_t i, wrote, target;

    info = avs_get_video_info(clip);

    if(!avs_has_audio(info)) {
        a2p_log(A2P_LOG_ERROR, "clip has no audio.\n", 0);
        exit(2);
    }

    if(info->sample_type == AVS_SAMPLE_FLOAT) {
        format = WAVE_FORMAT_IEEE_FLOAT;
    } else {
        format = WAVE_FORMAT_PCM;
    }

    if(_setmode(_fileno(stdout), _O_BINARY) == -1) {
        a2p_log(A2P_LOG_ERROR, "cannot switch stdout to binary mode.\n", 0);
        exit(2);
    }

    a2p_log(A2P_LOG_INFO, "writing %lld seconds of %d Hz, %d channel audio.\n",
            info->num_audio_samples / info->audio_samples_per_second,
            info->audio_samples_per_second, info->nchannels);

    header = wave_create_riff_header(format, info->nchannels,
                                     info->audio_samples_per_second,
                                     avs_bytes_per_channel_sample(info),
                                     info->num_audio_samples);
    fwrite(header, sizeof(*header), 1, stdout);

    count = info->audio_samples_per_second;
    wrote = 0;
    target = info->num_audio_samples;
    size = avs_bytes_per_channel_sample(info) * info->nchannels;
    buff = malloc(count * size);
    for (i = 0; i < target; i += count) {
        if(target - i < count) count = (size_t) (target - i);
        avs_get_audio(clip, buff, i, count);
        step = fwrite(buff, size, count, stdout);
        // fail early if there is a problem instead of end of input
        if(step != count) break;
        wrote += count;
        //a2p_log(A2P_LOG_REPEAT, "written %lld seconds [%lld%%]... ", 
        //    wrote / info->audio_samples_per_second,
        //    (100 * wrote) / target);
    }
    fflush(stdout); // clear buffers before we exit
    a2p_log(A2P_LOG_REPEAT, "finished, wrote %lld seconds [%lld%%].\n", 
        wrote / info->audio_samples_per_second,
        (100 * wrote) / target);
    free(buff);
    free(header);
    if(wrote != target) {
        a2p_log(A2P_LOG_ERROR, "only wrote %lld of %lld samples.\n",
                wrote, target);
        exit(2);
    }
}

void
info_write(AVS_Clip *clip, AVS_ScriptEnvironment *env)
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
                "no" : avs_is_tff(info) ? "tff" : "bff");
        fprintf(stdout, "v:pixel_type  %x\n", info->pixel_type);
    }
    if(avs_has_audio(info)) {
        fprintf(stdout, "a:sample_rate %d\n", info->audio_samples_per_second);
        fprintf(stdout, "a:format      %s\n",
                info->sample_type == AVS_SAMPLE_FLOAT ? "float" : "pcm");
        fprintf(stdout, "a:bit_depth   %d\n",
                avs_bytes_per_channel_sample(info) * 8);
        fprintf(stdout, "a:channels    %d\n", info->nchannels);
        fprintf(stdout, "a:samples     %lld\n", info->num_audio_samples);
        fprintf(stdout, "a:duration    %lld\n",
                info->num_audio_samples / info->audio_samples_per_second);
    }
}

void
video_write(AVS_Clip *clip, AVS_ScriptEnvironment *env)
{
    const AVS_VideoInfo *info;
    AVS_VideoFrame *frame;
    static const int planes[] = {AVS_PLANAR_Y, AVS_PLANAR_U, AVS_PLANAR_V};
    const BYTE *buff; // BYTE from avisynth_c.h not windows headers
    size_t count, step;
    int32_t w, h, f, p, i, pitch;
    int32_t wrote, target;

    info = avs_get_video_info(clip);

    if(!avs_has_video(info)) {
        a2p_log(A2P_LOG_ERROR, "clip has no video.\n");
        exit(2);
    }

    // ensure video is yv12
    if(avs_has_video(info) && !avs_is_yv12(info)) {
        a2p_log(A2P_LOG_INFO, "converting video to yv12.\n", 0);
        clip = avisynth_filter(clip, env, "ConvertToYV12");
        info = avs_get_video_info(clip);
        if(!avs_is_yv12(info)) {
            a2p_log(A2P_LOG_ERROR, "failed to convert video to yv12.\n", 0);
            exit(2);
        }
    }

    if(_setmode(_fileno(stdout), _O_BINARY) == -1) {
        a2p_log(A2P_LOG_ERROR, "cannot switch stdout to binary mode.\n");
        exit(2);
    }

    a2p_log(A2P_LOG_INFO, "writing %d frames of %d/%d fps, %dx%d %s video.\n",
            info->num_frames, info->fps_numerator, info->fps_denominator,
            info->width, info->height, !avs_is_field_based(info) ?
            "progressive" : avs_is_tff(info) ? "tff" : "bff");

    // YUV4MPEG2 header http://wiki.multimedia.cx/index.php?title=YUV4MPEG2
    fprintf(stdout, "YUV4MPEG2 W%d H%d F%ld:%ld I%s A0:0 C420\n", info->width,
            info->height, info->fps_numerator, info->fps_denominator,
            !avs_is_field_based(info) ? "p" : avs_is_tff(info) ? "t" : "b");
    fflush(stdout);

    // method from avs2yuv converted to c
    if(avs_is_yv12(info)) {
        count = info->width * info->height * 3 / 2;
    } else {
        a2p_log(A2P_LOG_ERROR, "unknown color space.\n");
        exit(2);
    }
    target = info->num_frames;
    wrote = 0;
    for(f = 0; f < target; f++) {
        step = 0;
        frame = avs_get_frame(clip, f);
        fprintf(stdout, "FRAME\n");
        for(p = 0; p < 3; p++) {
            w = info->width >> (p ? 1 : 0);
            h = info->height >> (p ? 1 : 0);
            pitch = avs_get_pitch_p(frame, planes[p]);
            buff = avs_get_read_ptr_p(frame, planes[p]);
            for(i = 0; i < h; i++) {
                step += fwrite(buff, sizeof(BYTE), w, stdout);
                buff += pitch;
            }
        }
        // not sure release is needed, but it doesn't cause an error
        avs_release_frame(frame);
        // fail early if there is a problem instead of end of input
        if(step != count) break;
        wrote++;
        //a2p_log(A2P_LOG_REPEAT, "written %d frames [%d%%]... ", 
        //    wrote, (100 * wrote) / target);
    }
    fflush(stdout); // clear buffers before we exit
    a2p_log(A2P_LOG_REPEAT, "finished, wrote %d frames [%d%%].\n", 
        wrote, (100 * wrote) / target);
    if(wrote != target) {
        a2p_log(A2P_LOG_ERROR, "only wrote %d of %d frames.\n", wrote, target);
        exit(2);
    }
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
        A2P_ACTION_NOTHING    
    } action;

    action = A2P_ACTION_NOTHING;

    if(argc == 3) {
        if(strcmp(argv[1], "--audio") == 0) {
            action = A2P_ACTION_AUDIO;
        } else if(strcmp(argv[1], "--video") == 0) {
            action = A2P_ACTION_VIDEO;
        }
        if(strcmp(argv[1], "--info") == 0) {
            action = A2P_ACTION_INFO;
        }
        input = argv[2];
    }

    if(action == A2P_ACTION_NOTHING) {
        fprintf(stderr, "Usage: avs2pipe --[audio|video|info] input.avs\n");
        fprintf(stderr, "   --audio - output wav extensible format audio to stdout.\n");
        fprintf(stderr, "   --video - output yuv4mpeg2 format video to stdout.\n");
        fprintf(stderr, "   --info  - output information about aviscript clip.\n");
        exit(2);
    }

    env = avs_create_script_environment(AVISYNTH_INTERFACE_VERSION);
    clip = avisynth_source(input, env);

    if(action == A2P_ACTION_AUDIO) {
        audio_write(clip, env);
    } else if(action == A2P_ACTION_VIDEO) {
        video_write(clip, env);
    } else if(action == A2P_ACTION_INFO) {
        info_write(clip, env);
    }

    avs_release_clip(clip);
    avs_delete_script_environment(env);

    exit(0);
}
