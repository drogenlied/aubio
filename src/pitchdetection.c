/*
   Copyright (C) 2003 Paul Brossier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "aubio_priv.h"
#include "sample.h"
#include "phasevoc.h"
#include "mathutils.h"
//#include "filter.h"
#include "pitchmcomb.h"
#include "pitchyin.h"
#include "pitchfcomb.h"
#include "pitchschmitt.h"
#include "pitchdetection.h"

smpl_t freqconvpass(smpl_t f);
smpl_t freqconvpass(smpl_t f){
        return f;
}

typedef smpl_t (*aubio_pitchdetection_func_t)(aubio_pitchdetection_t *p, 
                fvec_t * ibuf);
typedef smpl_t (*aubio_pitchdetection_conv_t)(smpl_t value);
void aubio_pitchdetection_slideblock(aubio_pitchdetection_t *p, fvec_t *ibuf);

struct _aubio_pitchdetection_t {
	aubio_pitchdetection_type type;
	aubio_pitchdetection_mode mode;
	uint_t srate;
	uint_t bufsize;
	/* for mcomb */	
	aubio_pvoc_t * pv;
	cvec_t * fftgrain; 
	aubio_pitchmcomb_t * mcomb;
	aubio_pitchfcomb_t * fcomb;
	aubio_pitchschmitt_t * schmitt;
	//aubio_filter_t * filter;
	/* for yin */
	fvec_t * buf;
	fvec_t * yin;
        aubio_pitchdetection_func_t callback;
        aubio_pitchdetection_conv_t freqconv;
};

aubio_pitchdetection_t * new_aubio_pitchdetection(uint_t bufsize, 
		uint_t hopsize, 
		uint_t channels,
		uint_t samplerate,
		aubio_pitchdetection_type type,
		aubio_pitchdetection_mode mode)
{
	aubio_pitchdetection_t *p = AUBIO_NEW(aubio_pitchdetection_t);
	p->srate = samplerate;
	p->type = type;
	p->mode = mode;
	p->bufsize = bufsize;
	switch(p->type) {
		case aubio_pitch_yin:
			p->buf      = new_fvec(bufsize,channels);
			p->yin      = new_fvec(bufsize/2,channels);
                        p->callback = aubio_pitchdetection_yin;
			break;
		case aubio_pitch_mcomb:
			p->pv       = new_aubio_pvoc(bufsize, hopsize, channels);
			p->fftgrain = new_cvec(bufsize, channels);
			p->mcomb    = new_aubio_pitchmcomb(bufsize,channels);
                        p->callback = aubio_pitchdetection_mcomb;
			break;
                case aubio_pitch_fcomb:
			p->buf      = new_fvec(bufsize,channels);
                        p->fcomb    = new_aubio_pitchfcomb(bufsize,samplerate);
                        p->callback = aubio_pitchdetection_fcomb;
                        break;
                case aubio_pitch_schmitt:
			p->buf      = new_fvec(bufsize,channels);
                        p->schmitt  = new_aubio_pitchschmitt(bufsize,samplerate);
                        p->callback = aubio_pitchdetection_schmitt;
                        break;
                default:
                        break;
	}
	switch(p->mode) {
		case aubio_pitchm_freq:
                        p->freqconv = freqconvpass;
                        break;
		case aubio_pitchm_midi:
                        p->freqconv = aubio_freqtomidi;
                        break;
		case aubio_pitchm_cent:
                        /** bug: not implemented */
                        p->freqconv = freqconvpass;
                        break;
		case aubio_pitchm_bin:
                        /** bug: not implemented */
                        p->freqconv = freqconvpass;
                        break;
                default:
                        break;
        }
	return p;
}

void del_aubio_pitchdetection(aubio_pitchdetection_t * p) {
	switch(p->type) {
		case aubio_pitch_yin:
			del_fvec(p->yin);
			del_fvec(p->buf);
			break;
		case aubio_pitch_mcomb:
			del_aubio_pvoc(p->pv);
			del_cvec(p->fftgrain);
			del_aubio_pitchmcomb(p->mcomb);
			break;
                case aubio_pitch_schmitt:
			del_fvec(p->buf);
                        del_aubio_pitchschmitt(p->schmitt);
                        break;
                case aubio_pitch_fcomb:
			del_fvec(p->buf);
                        del_aubio_pitchfcomb(p->fcomb);
                        break;
		default:
			break;
	}
	AUBIO_FREE(p);
}

void aubio_pitchdetection_slideblock(aubio_pitchdetection_t *p, fvec_t *ibuf){
        uint_t i,j = 0, overlap_size = 0;
        overlap_size = p->buf->length-ibuf->length;
        for (i=0;i<p->buf->channels;i++){
                for (j=0;j<overlap_size;j++){
                        p->buf->data[i][j] = 
                                p->buf->data[i][j+ibuf->length];
                }
        }
        for (i=0;i<ibuf->channels;i++){
                for (j=0;j<ibuf->length;j++){
                        p->buf->data[i][j+overlap_size] = 
                                ibuf->data[i][j];
                }
        }
}

smpl_t aubio_pitchdetection(aubio_pitchdetection_t *p, fvec_t * ibuf) {
        return p->freqconv(p->callback(p,ibuf));
}

smpl_t aubio_pitchdetection_mcomb(aubio_pitchdetection_t *p, fvec_t *ibuf) {
	smpl_t pitch = 0.;
        aubio_pvoc_do(p->pv,ibuf,p->fftgrain);
        pitch = aubio_pitchmcomb_detect(p->mcomb,p->fftgrain);
        /** \bug should move the >0 check within aubio_bintofreq */
        if (pitch>0.) {
                pitch = aubio_bintofreq(pitch,p->srate,p->bufsize);
        } else {
                pitch = 0.;
        }
        return pitch;
}

smpl_t aubio_pitchdetection_yin(aubio_pitchdetection_t *p, fvec_t *ibuf) {
	smpl_t pitch = 0.;
        aubio_pitchdetection_slideblock(p,ibuf);
        pitch = aubio_pitchyin_getpitchfast(p->buf,p->yin, 0.5);
        if (pitch>0) {
                pitch = p->srate/(pitch+0.);
        } else {
                pitch = 0.;
        }
        return pitch;
}


smpl_t aubio_pitchdetection_fcomb(aubio_pitchdetection_t *p, fvec_t *ibuf){
        aubio_pitchdetection_slideblock(p,ibuf);
        return aubio_pitchfcomb_detect(p->fcomb,p->buf);
}

smpl_t aubio_pitchdetection_schmitt(aubio_pitchdetection_t *p, fvec_t *ibuf){
        aubio_pitchdetection_slideblock(p,ibuf);
        return aubio_pitchschmitt_detect(p->schmitt,p->buf);
}
