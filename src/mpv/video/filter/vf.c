/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <libavutil/common.h>
#include <libavutil/mem.h>

#include "config.h"

#include "core/mp_msg.h"
#include "core/m_option.h"
#include "core/m_struct.h"

#include "core/options.h"

#include "video/img_format.h"
#include "video/mp_image.h"
#include "video/mp_image_pool.h"
#include "vf.h"

#include "video/memcpy_pic.h"

extern const vf_info_t vf_info_vo;
extern const vf_info_t vf_info_crop;
extern const vf_info_t vf_info_expand;
extern const vf_info_t vf_info_pp;
extern const vf_info_t vf_info_scale;
extern const vf_info_t vf_info_format;
extern const vf_info_t vf_info_noformat;
extern const vf_info_t vf_info_flip;
extern const vf_info_t vf_info_rotate;
extern const vf_info_t vf_info_mirror;
extern const vf_info_t vf_info_noise;
extern const vf_info_t vf_info_eq;
extern const vf_info_t vf_info_gradfun;
extern const vf_info_t vf_info_unsharp;
extern const vf_info_t vf_info_swapuv;
extern const vf_info_t vf_info_down3dright;
extern const vf_info_t vf_info_hqdn3d;
extern const vf_info_t vf_info_ilpack;
extern const vf_info_t vf_info_dsize;
extern const vf_info_t vf_info_softpulldown;
extern const vf_info_t vf_info_pullup;
extern const vf_info_t vf_info_delogo;
extern const vf_info_t vf_info_phase;
extern const vf_info_t vf_info_divtc;
extern const vf_info_t vf_info_softskip;
extern const vf_info_t vf_info_screenshot;
extern const vf_info_t vf_info_sub;
extern const vf_info_t vf_info_yadif;
extern const vf_info_t vf_info_stereo3d;
extern const vf_info_t vf_info_dlopen;
extern const vf_info_t vf_info_lavfi;

// list of available filters:
static const vf_info_t *const filter_list[] = {
    &vf_info_crop,
    &vf_info_expand,
    &vf_info_scale,
    &vf_info_vo,
    &vf_info_format,
    &vf_info_noformat,
    &vf_info_flip,
    &vf_info_rotate,
    &vf_info_mirror,

#ifdef CONFIG_LIBPOSTPROC
    &vf_info_pp,
#endif
#ifdef CONFIG_VF_LAVFI
    &vf_info_lavfi,
#endif

    &vf_info_screenshot,

    &vf_info_noise,
    &vf_info_eq,
    &vf_info_gradfun,
    &vf_info_unsharp,
    &vf_info_swapuv,
    &vf_info_down3dright,
    &vf_info_hqdn3d,
    &vf_info_ilpack,
    &vf_info_dsize,
    &vf_info_softpulldown,
    &vf_info_pullup,
    &vf_info_delogo,
    &vf_info_phase,
    &vf_info_divtc,
    &vf_info_sub,
    &vf_info_yadif,
    &vf_info_stereo3d,
    &vf_info_dlopen,
    NULL
};

// For the vf option
const m_obj_list_t vf_obj_list = {
    (void **)filter_list,
    M_ST_OFF(vf_info_t, name),
    M_ST_OFF(vf_info_t, info),
    M_ST_OFF(vf_info_t, opts)
};

// Get a new image for filter output, with size and pixel format according to
// the last vf_config call.
struct mp_image *vf_alloc_out_image(struct vf_instance *vf)
{
    assert(vf->fmt_out.configured);
    return mp_image_pool_get(vf->out_pool, vf->fmt_out.fmt,
                             vf->fmt_out.w, vf->fmt_out.h);
}

void vf_make_out_image_writeable(struct vf_instance *vf, struct mp_image *img)
{
    assert(vf->fmt_out.configured);
    assert(vf->fmt_out.fmt == img->imgfmt);
    assert(vf->fmt_out.w == img->w && vf->fmt_out.h == img->h);
    mp_image_pool_make_writeable(vf->out_pool, img);
}

//============================================================================

static int vf_default_query_format(struct vf_instance *vf, unsigned int fmt)
{
    return vf_next_query_format(vf, fmt);
}


static struct mp_image *vf_default_filter(struct vf_instance *vf,
                                          struct mp_image *mpi)
{
    assert(!vf->filter_ext);
    return mpi;
}

static void print_fmt(int msglevel, struct vf_format *fmt)
{
    if (fmt && fmt->configured) {
        mp_msg(MSGT_VFILTER, msglevel, "%dx%d", fmt->w, fmt->h);
        if (fmt->w != fmt->dw || fmt->h != fmt->dh)
            mp_msg(MSGT_VFILTER, msglevel, "->%dx%d", fmt->dw, fmt->dh);
        mp_msg(MSGT_VFILTER, msglevel, " %s %#x", mp_imgfmt_to_name(fmt->fmt),
               fmt->flags);
    } else {
        mp_msg(MSGT_VFILTER, msglevel, "???");
    }
}

void vf_print_filter_chain(int msglevel, struct vf_instance *vf)
{
    if (!mp_msg_test(MSGT_VFILTER, msglevel))
        return;

    for (vf_instance_t *f = vf; f; f = f->next) {
        mp_msg(MSGT_VFILTER, msglevel, " [%s] ", f->info->name);
        print_fmt(msglevel, &f->fmt_in);
        if (f->next) {
            mp_msg(MSGT_VFILTER, msglevel, " -> ");
            print_fmt(msglevel, &f->fmt_out);
        }
        mp_msg(MSGT_VFILTER, msglevel, "\n");
    }
}

struct vf_instance *vf_open_plugin_noerr(struct MPOpts *opts,
                                         const vf_info_t *const *filter_list,
                                         vf_instance_t *next, const char *name,
                                         char **args, int *retcode)
{
    vf_instance_t *vf;
    int i;
    for (i = 0;; i++) {
        if (!filter_list[i]) {
            mp_tmsg(MSGT_VFILTER, MSGL_ERR,
                    "Couldn't find video filter '%s'.\n", name);
            return NULL; // no such filter!
        }
        if (!strcmp(filter_list[i]->name, name))
            break;
    }
    vf = talloc_zero(NULL, struct vf_instance);
    vf->opts = opts;
    vf->info = filter_list[i];
    vf->next = next;
    vf->config = vf_next_config;
    vf->control = vf_next_control;
    vf->query_format = vf_default_query_format;
    vf->filter = vf_default_filter;
    vf->out_pool = talloc_steal(vf, mp_image_pool_new(16));
    if (vf->info->opts) { // vf_vo get some special argument
        const m_struct_t *st = vf->info->opts;
        void *vf_priv = m_struct_alloc(st);
        int n;
        for (n = 0; args && args[2 * n]; n++)
            m_struct_set(st, vf_priv, args[2 * n], bstr0(args[2 * n + 1]));
        vf->priv = vf_priv;
        args = NULL;
    } else // Otherwise we should have the '_oldargs_'
    if (args && !strcmp(args[0], "_oldargs_"))
        args = (char **)args[1];
    else
        args = NULL;
    *retcode = vf->info->vf_open(vf, (char *)args);
    if (*retcode > 0)
        return vf;
    talloc_free(vf);
    return NULL;
}

struct vf_instance *vf_open_plugin(struct MPOpts *opts,
                                   const vf_info_t *const *filter_list,
                                   vf_instance_t *next, const char *name,
                                   char **args)
{
    struct vf_instance *vf = vf_open_plugin_noerr(opts, filter_list, next,
                                                  name, args, &(int){0});
    if (!vf)
        mp_tmsg(MSGT_VFILTER, MSGL_ERR, "Couldn't open video filter '%s'.\n",
                name);
    return vf;
}

vf_instance_t *vf_open_filter(struct MPOpts *opts, vf_instance_t *next,
                              const char *name, char **args)
{
    if (args && strcmp(args[0], "_oldargs_")) {
        int i, l = 0;
        for (i = 0; args && args[2 * i]; i++)
            l += 1 + strlen(args[2 * i]) + 1 + strlen(args[2 * i + 1]);
        l += strlen(name);
        {
            char str[l + 1];
            char *p = str;
            p += sprintf(str, "%s", name);
            for (i = 0; args && args[2 * i]; i++)
                p += sprintf(p, " %s=%s", args[2 * i], args[2 * i + 1]);
            mp_msg(MSGT_VFILTER, MSGL_INFO, "%s[%s]\n",
                   mp_gtext("Opening video filter: "), str);
        }
    } else if (strcmp(name, "vo")) {
        if (args && strcmp(args[0], "_oldargs_") == 0)
            mp_msg(MSGT_VFILTER, MSGL_INFO, "%s[%s=%s]\n",
                   mp_gtext("Opening video filter: "), name, args[1]);
        else
            mp_msg(MSGT_VFILTER, MSGL_INFO, "%s[%s]\n",
                   mp_gtext("Opening video filter: "), name);
    }
    return vf_open_plugin(opts, filter_list, next, name, args);
}

/**
 * \brief adds a filter before the last one (which should be the vo filter).
 * \param vf start of the filter chain.
 * \param name name of the filter to add.
 * \param args argument list for the filter.
 * \return pointer to the filter instance that was created.
 */
vf_instance_t *vf_add_before_vo(vf_instance_t **vf, char *name, char **args)
{
    struct MPOpts *opts = (*vf)->opts;
    vf_instance_t *vo, *prev = NULL, *new;
    // Find the last filter (should be vf_vo)
    for (vo = *vf; vo->next; vo = vo->next)
        prev = vo;
    new = vf_open_filter(opts, vo, name, args);
    if (prev)
        prev->next = new;
    else
        *vf = new;
    return new;
}

//============================================================================

unsigned int vf_match_csp(vf_instance_t **vfp, const unsigned int *list,
                          unsigned int preferred)
{
    vf_instance_t *vf = *vfp;
    struct MPOpts *opts = vf->opts;
    const unsigned int *p;
    unsigned int best = 0;
    int ret;
    if ((p = list))
        while (*p) {
            ret = vf->query_format(vf, *p);
            mp_msg(MSGT_VFILTER, MSGL_V, "[%s] query(%s) -> %x\n",
                   vf->info->name, vo_format_name(*p), ret);
            if (ret & VFCAP_CSP_SUPPORTED_BY_HW) {
                best = *p;
                break;
            }
            if (ret & VFCAP_CSP_SUPPORTED && !best)
                best = *p;
            ++p;
        }
    if (best)
        return best;      // bingo, they have common csp!
    // ok, then try with scale:
    if (vf->info == &vf_info_scale)
        return 0;     // avoid infinite recursion!
    vf = vf_open_filter(opts, vf, "scale", NULL);
    if (!vf)
        return 0;     // failed to init "scale"
    // try the preferred csp first:
    if (preferred && vf->query_format(vf, preferred))
        best = preferred;
    else
        // try the list again, now with "scaler" :
        if ((p = list))
            while (*p) {
                ret = vf->query_format(vf, *p);
                mp_msg(MSGT_VFILTER, MSGL_V, "[%s] query(%s) -> %x\n",
                       vf->info->name, vo_format_name(*p), ret);
                if (ret & VFCAP_CSP_SUPPORTED_BY_HW) {
                    best = *p;
                    break;
                }
                if (ret & VFCAP_CSP_SUPPORTED && !best)
                    best = *p;
            ++p;
        }
    if (best)
        *vfp = vf;    // else uninit vf  !FIXME!
    return best;
}

// Used by filters to add a filtered frame to the output queue.
// Ownership of img is transferred from caller to the filter chain.
void vf_add_output_frame(struct vf_instance *vf, struct mp_image *img)
{
    if (img)
        MP_TARRAY_APPEND(vf, vf->out_queued, vf->num_out_queued, img);
}

static struct mp_image *vf_dequeue_output_frame(struct vf_instance *vf)
{
    struct mp_image *res = NULL;
    if (vf->num_out_queued) {
        res = vf->out_queued[0];
        MP_TARRAY_REMOVE_AT(vf->out_queued, vf->num_out_queued, 0);
    }
    return res;
}

// Input a frame into the filter chain.
// Return >= 0 on success, < 0 on failure (even if output frames were produced)
int vf_filter_frame(struct vf_instance *vf, struct mp_image *img)
{
    assert(vf->fmt_in.configured);
    assert(img->w == vf->fmt_in.w && img->h == vf->fmt_in.h);
    assert(img->imgfmt == vf->fmt_in.fmt);

    if (vf->filter_ext) {
        return vf->filter_ext(vf, img);
    } else {
        vf_add_output_frame(vf, vf->filter(vf, img));
        return 0;
    }
}

// Output the next queued image (if any) from the full filter chain.
struct mp_image *vf_chain_output_queued_frame(struct vf_instance *vf)
{
    while (1) {
        struct vf_instance *last = NULL;
        for (struct vf_instance * cur = vf; cur; cur = cur->next) {
            if (cur->num_out_queued)
                last = cur;
        }
        if (!last)
            return NULL;
        struct mp_image *img = vf_dequeue_output_frame(last);
        if (!last->next)
            return img;
        vf_filter_frame(last->next, img);
    }
}

static void vf_forget_frames(struct vf_instance *vf)
{
    for (int n = 0; n < vf->num_out_queued; n++)
        talloc_free(vf->out_queued[n]);
    vf->num_out_queued = 0;
}

void vf_chain_seek_reset(struct vf_instance *vf)
{
    vf->control(vf, VFCTRL_SEEK_RESET, NULL);
    for (struct vf_instance *cur = vf; cur; cur = cur->next)
        vf_forget_frames(cur);
}

int vf_config_wrapper(struct vf_instance *vf,
                      int width, int height, int d_width, int d_height,
                      unsigned int flags, unsigned int outfmt)
{
    vf_forget_frames(vf);
    mp_image_pool_clear(vf->out_pool);

    vf->fmt_in = vf->fmt_out = (struct vf_format){0};

    int r = vf->config(vf, width, height, d_width, d_height, flags, outfmt);
    if (r) {
        vf->fmt_in = (struct vf_format) {
            .configured = 1,
            .w = width,    .h = height,
            .dw = d_width, .dh = d_height,
            .flags = flags,
            .fmt = outfmt,
        };
        vf->fmt_out = vf->next ? vf->next->fmt_in : (struct vf_format){0};
    }
    return r;
}

int vf_next_config(struct vf_instance *vf,
                   int width, int height, int d_width, int d_height,
                   unsigned int voflags, unsigned int outfmt)
{
    struct MPOpts *opts = vf->opts;
    int flags = vf->next->query_format(vf->next, outfmt);
    if (!flags) {
        // hmm. colorspace mismatch!!!
        // let's insert the 'scale' filter, it does the job for us:
        vf_instance_t *vf2;
        if (vf->next->info == &vf_info_scale)
            return 0;                                // scale->scale
        vf2 = vf_open_filter(opts, vf->next, "scale", NULL);
        if (!vf2)
            return 0;      // shouldn't happen!
        vf->next = vf2;
        flags = vf->next->query_format(vf->next, outfmt);
        if (!flags) {
            mp_tmsg(MSGT_VFILTER, MSGL_ERR, "Cannot find matching colorspace, "
                    "even by inserting 'scale' :(\n");
            return 0; // FAIL
        }
    }
    return vf_config_wrapper(vf->next, width, height, d_width, d_height,
                             voflags, outfmt);
}

int vf_next_control(struct vf_instance *vf, int request, void *data)
{
    return vf->next->control(vf->next, request, data);
}

int vf_next_query_format(struct vf_instance *vf, unsigned int fmt)
{
    return vf->next->query_format(vf->next, fmt);
}

//============================================================================

vf_instance_t *append_filters(vf_instance_t *last,
                              struct m_obj_settings *vf_settings)
{
    struct MPOpts *opts = last->opts;
    vf_instance_t *vf;
    int i;

    if (vf_settings) {
        // We want to add them in the 'right order'
        for (i = 0; vf_settings[i].name; i++)
            /* NOP */;
        for (i--; i >= 0; i--) {
            //printf("Open filter %s\n",vf_settings[i].name);
            vf = vf_open_filter(opts, last, vf_settings[i].name,
                                vf_settings[i].attribs);
            if (vf)
                last = vf;
        }
    }
    return last;
}

//============================================================================

void vf_uninit_filter(vf_instance_t *vf)
{
    if (vf->uninit)
        vf->uninit(vf);
    vf_forget_frames(vf);
    talloc_free(vf);
}

void vf_uninit_filter_chain(vf_instance_t *vf)
{
    while (vf) {
        vf_instance_t *next = vf->next;
        vf_uninit_filter(vf);
        vf = next;
    }
}

// When changing the size of an image that had old_w/old_h with
// DAR *d_width/*d_height to the new size new_w/new_h, adjust
// *d_width/*d_height such that the new image has the same pixel aspect ratio.
void vf_rescale_dsize(int *d_width, int *d_height, int old_w, int old_h,
                      int new_w, int new_h)
{
    *d_width  = *d_width  * new_w / old_w;
    *d_height = *d_height * new_h / old_h;
}

void vf_detc_init_pts_buf(struct vf_detc_pts_buf *p)
{
    p->inpts_prev = MP_NOPTS_VALUE;
    p->outpts_prev = MP_NOPTS_VALUE;
    p->lastdelta = 0;
}

static double vf_detc_adjust_pts_internal(struct vf_detc_pts_buf *p,
                                          double pts, bool reset_pattern,
                                          bool skip_frame, double delta,
                                          double boundfactor_minus,
                                          double increasefactor,
                                          double boundfactor_plus)
{
    double newpts;

    if (pts == MP_NOPTS_VALUE)
        return pts;

    if (delta <= 0) {
        if (p->inpts_prev == MP_NOPTS_VALUE)
            delta = 0;
        else if (pts == p->inpts_prev)
            delta = p->lastdelta;
        else
            delta = pts - p->inpts_prev;
    }
    p->inpts_prev = pts;
    p->lastdelta = delta;

    if (skip_frame)
        return MP_NOPTS_VALUE;

    /* detect bogus deltas and then passthru pts (possibly caused by seeking,
     * or bad input) */
    if (p->outpts_prev == MP_NOPTS_VALUE || reset_pattern || delta <= 0.0 ||
            delta >= 0.5)
        newpts = pts;
    else {
        // turn 5 frames into 4
        newpts = p->outpts_prev + delta * increasefactor;

        // bound to input pts in a sensible way; these numbers come because we
        // map frames the following way when ivtc'ing:
        // 0/30 -> 0/24   diff=0
        // 1/30 -> 1/24   diff=1/120
        // 2/30 -> -
        // 3/30 -> 2/24   diff=-1/60
        // 4/30 -> 3/24   diff=-1/120
        if (newpts < pts - delta * boundfactor_minus)
            newpts = pts - delta * boundfactor_minus;
        if (newpts > pts + delta * boundfactor_plus)
            newpts = pts + delta * boundfactor_plus;
        if (newpts < p->outpts_prev)
            newpts = p->outpts_prev;  // damage control
    }
    p->outpts_prev = newpts;

    return newpts;
}

double vf_detc_adjust_pts(struct vf_detc_pts_buf *p, double pts,
                          bool reset_pattern, bool skip_frame)
{
    // standard telecine (see above)
    return vf_detc_adjust_pts_internal(p, pts, reset_pattern, skip_frame,
                                       0, 0.5, 1.25, 0.25);
}

double vf_softpulldown_adjust_pts(struct vf_detc_pts_buf *p, double pts,
                                  bool reset_pattern, bool skip_frame,
                                  int last_frame_duration)
{
    // for the softpulldown filter we get:
    // 0/60 -> 0/30
    // 2/60 -> 1/30
    // 5/60 -> 2/30
    // 7/60 -> 3/30, 4/30
    return vf_detc_adjust_pts_internal(p, pts, reset_pattern, skip_frame,
                                       0, 1.0 / last_frame_duration,
                                       2.0 / last_frame_duration,
                                       1.0 / last_frame_duration);
}