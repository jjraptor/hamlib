/*
 *  Hamlib Tuner backend - Video4Linux description
 *  Copyright (c) 2004 by Stephane Fillod
 *
 *	$Id: v4l.c,v 1.2 2004-09-25 14:33:52 fillods Exp $
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdlib.h>
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <math.h>
#include <errno.h>

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#include "hamlib/rig.h"
#include "misc.h"

#include "tuner.h"	/* include config.h */

#ifdef V4L_IOCTL

#include <stdlib.h>
#include "idx_builtin.h"


#define V4L_FUNC (RIG_FUNC_MUTE)

#define V4L_LEVEL_ALL (RIG_LEVEL_AF|RIG_LEVEL_RAWSTR)

#define V4L_PARM_ALL (RIG_PARM_NONE)

#define V4L_VFO (RIG_VFO_A)

/* FIXME: per card measures? */
#define V4L_STR_CAL { 2, { \
	                {     0, -60 }, \
	                { 65535, 60 }, \
	        } }

static int v4l_init(RIG *rig);
static int v4l_open(RIG *rig);
static int v4l_set_freq(RIG *rig, vfo_t vfo, freq_t freq);
static int v4l_get_freq(RIG *rig, vfo_t vfo, freq_t *freq);
static int v4l_set_func(RIG *rig, vfo_t vfo, setting_t func, int status);
static int v4l_get_func(RIG *rig, vfo_t vfo, setting_t func, int *status);
static int v4l_set_level(RIG *rig, vfo_t vfo, setting_t level, value_t val);
static int v4l_get_level(RIG *rig, vfo_t vfo, setting_t level, value_t *val);
static const char *v4l_get_info(RIG *rig);

/*
 * v4l rig capabilities.
 *
 *
 */
const struct rig_caps v4l_caps = {
.rig_model =  RIG_MODEL_V4L,
.model_name = "SW/FM radio",
.mfg_name =  "Video4Linux",
.version =  "0.1",
.copyright =  "LGPL",
.status =  RIG_STATUS_UNTESTED,
.rig_type =  RIG_TYPE_PCRECEIVER,
.ptt_type =  RIG_PTT_NONE,
.dcd_type =  RIG_DCD_NONE,
.port_type =  RIG_PORT_DEVICE,
.serial_rate_min =  1200,
.serial_rate_max =  9600,
.serial_data_bits =  8,
.serial_stop_bits =  1,
.serial_parity =  RIG_PARITY_NONE,
.serial_handshake =  RIG_HANDSHAKE_NONE,
.write_delay =  0,
.post_write_delay =  0,
.timeout =  2000,
.retry =  1,

.has_get_func =  V4L_FUNC,
.has_set_func =  V4L_FUNC,
.has_get_level =  V4L_LEVEL_ALL,
.has_set_level =  RIG_LEVEL_SET(V4L_LEVEL_ALL),
.has_get_parm =  V4L_PARM_ALL,
.has_set_parm =  RIG_PARM_SET(V4L_PARM_ALL),
.vfo_ops =  RIG_OP_NONE,
.level_gran = {
	[LVL_RAWSTR] = { .min = { .i = 0 }, .max = { .i = 65535 } },
},
.preamp =   { RIG_DBLST_END },
.attenuator =   { RIG_DBLST_END },
.max_rit =  Hz(0),
.max_xit =  Hz(0),
.max_ifshift =  Hz(0),
.targetable_vfo =  0,
.transceive =  RIG_TRN_OFF,
.bank_qty =   0,
.chan_desc_sz =  0,

.chan_list =  { RIG_CHAN_END, },

	/* will be rewritten at runtime */
.rx_range_list1 =  {
	{MHz(87.9),MHz(108.9),RIG_MODE_WFM,-1,-1,V4L_VFO},
	RIG_FRNG_END,
  },
.tx_range_list1 =  { RIG_FRNG_END, },
.rx_range_list2 =  {
	{MHz(87.9),MHz(108.9),RIG_MODE_WFM,-1,-1,V4L_VFO},
	RIG_FRNG_END,
  },
.tx_range_list2 =  { RIG_FRNG_END, },
.tuning_steps =  {
	 {RIG_MODE_WFM,100},
	 RIG_TS_END,
	},
        /* mode/filter list, remember: order matters! */
.filters =  {
		{RIG_MODE_WFM, kHz(230)},	/* guess */
		{RIG_MODE_AM, kHz(8)},	/* guess */
		RIG_FLT_END,
	},
.str_cal = V4L_STR_CAL,

.rig_init =  v4l_init,
.rig_open =  v4l_open,

.set_freq =  v4l_set_freq,
.get_freq =  v4l_get_freq,
.set_func =  v4l_set_func,
.get_func =  v4l_get_func,
.set_level =  v4l_set_level,
.get_level =  v4l_get_level,

.get_info =  v4l_get_info,

};

/*
 * Function definitions below
 */


#include "videodev.h"


#define DEFAULT_V4L_PATH "/dev/radio0"

int v4l_init(RIG *rig)
{
  rig->state.rigport.type.rig = RIG_PORT_DEVICE;
  strncpy(rig->state.rigport.pathname, DEFAULT_V4L_PATH, FILPATHLEN);

  return RIG_OK;
}

int v4l_open(RIG *rig)
{
	int ret, i;
	struct video_tuner vt;
	struct rig_state *rs = &rig->state;
	double fact;

	for (i=0; i<8; i++) {
		vt.tuner = i;
		ret = ioctl(rig->state.rigport.fd, VIDIOCGTUNER, &vt);
		if (ret < 0)
			break;

	 	fact = (vt.flags & VIDEO_TUNER_LOW) == 0 ? 16 : 16000;
	 	rs->rx_range_list[i].start = vt.rangelow/fact;
	 	rs->rx_range_list[i].end = vt.rangehigh/fact;
 		rs->rx_range_list[i].modes = vt.rangehigh/fact < MHz(30) ? RIG_MODE_AM : RIG_MODE_WFM;
		/* hack! hack! store the resolution in low_power! */
		rs->rx_range_list[i].low_power = rint(fact);
	}
	return RIG_OK;
}


int v4l_set_freq(RIG *rig, vfo_t vfo, freq_t freq)
{
	struct rig_state *rs = &rig->state;
	struct		video_tuner vt;
	const freq_range_t *range;
	unsigned long f;
	double fact;
	int ret;
	
	/* AM or WFM */
	range = rig_get_range (rs->rx_range_list, freq, RIG_MODE_AM|RIG_MODE_WFM);
	if (!range)
		return -RIG_ECONF;

	/* at this point, we are trying to tune to a frequency */

	vt.tuner = (rs->rx_range_list-range)/sizeof(freq_range_t);

	ret = ioctl(rig->state.rigport.fd, VIDIOCSTUNER, &vt);	/* set tuner # */
	if (ret < 0) {
		rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOCSTUNER: %s\n",
				strerror(errno));
		return -RIG_EIO;
	}
	fact = range->low_power;

	f = rint(freq * fact);	/* rounding to nearest int */

	ret = ioctl(rig->state.rigport.fd, VIDIOCSFREQ, &freq);
	if (ret < 0) {
		rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOCSFREQ: %s\n",
				strerror(errno));
		return -RIG_EIO;
	}

	return RIG_OK;
}

int v4l_get_freq(RIG *rig, vfo_t vfo, freq_t *freq)
{
	struct rig_state *rs = &rig->state;
	const freq_range_t *range;
	unsigned long f;
	double fact;
	int ret;

	ret = ioctl(rig->state.rigport.fd, VIDIOCGFREQ, &f);
	if (ret < 0) {
		rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOCGFREQ: %s\n",
				strerror(errno));
		return -RIG_EIO;
	}

	/* FIXME: remember tuner and current *fact* */
	range = rig_get_range (rs->rx_range_list, f/16, RIG_MODE_AM|RIG_MODE_WFM);
	if (!range)
		return -RIG_ECONF;
	fact = range->low_power;


	*freq = f/fact;

	return RIG_OK;
}

int v4l_set_func(RIG *rig, vfo_t vfo, setting_t func, int status)
{
  struct video_audio va;
  int ret;

  switch ( func ) {
  case RIG_FUNC_MUTE:
		ret = ioctl(rig->state.rigport.fd, VIDIOCGAUDIO, &va);
		if (ret < 0) {
			rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOCGAUDIO: %s\n",
				strerror(errno));
			return -RIG_EIO;
		}
		va.flags = status ? VIDEO_AUDIO_MUTE : 0;
		ret = ioctl(rig->state.rigport.fd, VIDIOCSAUDIO, &va);
		if (ret < 0) {
			rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOCSAUDIO: %s\n",
				strerror(errno));
			return -RIG_EIO;
		}
		break;

  default:
    return -RIG_EINVAL;
  }
  return RIG_OK;
}

int v4l_get_func(RIG *rig, vfo_t vfo, setting_t func, int *status)
{
  struct video_audio va;
  int ret;

  switch ( func ) {
  case RIG_FUNC_MUTE:
		ret = ioctl(rig->state.rigport.fd, VIDIOCGAUDIO, &va);
		if (ret < 0) {
			rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOCGAUDIO: %s\n",
				strerror(errno));
			return -RIG_EIO;
		}
		*status = (va.flags & VIDEO_AUDIO_MUTE) == VIDEO_AUDIO_MUTE ;
		break;
  default:
    return -RIG_EINVAL;
  }    
  return RIG_OK;
}


int v4l_set_level(RIG *rig, vfo_t vfo, setting_t level, value_t val)
{
  struct video_audio va;
  int ret;

  ret = ioctl(rig->state.rigport.fd, VIDIOCGAUDIO, &va);
  if (ret < 0) {
	rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOCGAUDIO: %s\n",
			strerror(errno));
	return -RIG_EIO;
  }

  switch ( level ) {
  case RIG_LEVEL_AF:
    va.volume = val.f * 65535;
    break;

  default:
    return -RIG_EINVAL;
  }    

  ret = ioctl(rig->state.rigport.fd, VIDIOCSAUDIO, &va);
  if (ret < 0) {
	rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOCSAUDIO: %s\n",
		strerror(errno));
	return -RIG_EIO;
  }
  return RIG_OK;
}

int v4l_get_level(RIG *rig, vfo_t vfo, setting_t level, value_t *val)
{
  struct video_audio va;
  struct video_tuner vt;
  int ret;

  switch ( level ) {
  case RIG_LEVEL_AF:
    ret = ioctl(rig->state.rigport.fd, VIDIOCGAUDIO, &va);
    if (ret < 0) {
	rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOCGAUDIO: %s\n",
			strerror(errno));
	return -RIG_EIO;
    }
    val->f = (float)va.volume / 65535.;
    break;

  case RIG_LEVEL_RAWSTR:
    ret = ioctl(rig->state.rigport.fd, VIDIOCGTUNER, &vt);	/* get info */
    if (ret < 0) {
	rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOCGTUNER: %s\n",
			strerror(errno));
	return -RIG_EIO;
    }
    val->i = vt.signal;
    break;

  default:
    return -RIG_EINVAL;
  }    

  return RIG_OK;
}

/*
 * FIXME: static buf does not allow reentrancy!
 */
const char *v4l_get_info(RIG *rig)
{
	static struct video_tuner vt;
	int ret;

	vt.tuner = 0;
	ret = ioctl(rig->state.rigport.fd, VIDIOCGTUNER, &vt);
	if (ret < 0) {
		rig_debug(RIG_DEBUG_ERR, "ioctl VIDIOCGTUNER: %s\n",
				strerror(errno));
		return "Get info failed";
	}
	return vt.name;
}

#endif	/* V4L_IOCTL */
