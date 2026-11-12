#pragma once
#include <chiaki/session.h>
/* Adapter-only profile mapping. Upstream streaming code remains unmodified. */
static inline bool relay_stream_profile(ChiakiConnectVideoProfile *profile,int height,int bitrate,int fps)
{
 if(bitrate<100||bitrate>50000||(fps!=30&&fps!=60))return false;
 ChiakiVideoResolutionPreset preset;
 switch(height){
 case 360:preset=CHIAKI_VIDEO_RESOLUTION_PRESET_360p;break;
 case 540:preset=CHIAKI_VIDEO_RESOLUTION_PRESET_540p;break;
 case 720:preset=CHIAKI_VIDEO_RESOLUTION_PRESET_720p;break;
 case 1080:preset=CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;break;
 default:return false;
 }
 chiaki_connect_video_profile_preset(profile,preset,fps==30?CHIAKI_VIDEO_FPS_PRESET_30:CHIAKI_VIDEO_FPS_PRESET_60);
 profile->codec=CHIAKI_CODEC_H264;profile->bitrate=(unsigned)bitrate;
 return true;
}
