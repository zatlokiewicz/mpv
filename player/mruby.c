/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/variable.h>

#include "common/msg.h"
#include "options/path.h"
#include "player/core.h"
#include "player/client.h"
#include "libmpv/client.h"
#include "talloc.h"

struct script_ctx {
    mrb_state *state;

    const char *name;
    const char *filename;
    struct mp_log *log;
    struct mpv_handle *client;
    struct MPContext *mpctx;
};

static struct script_ctx *get_ctx(mrb_state *mrb)
{
    mrb_sym sym = mrb_intern_cstr(mrb, "mpctx");
    mrb_value mrbctx = mrb_vm_const_get(mrb, sym);;
    return mrb_cptr(mrbctx);
}

static mrb_value _log(mrb_state *mrb, mrb_value self)
{
    struct script_ctx *ctx = get_ctx(mrb);
    char *string;
    int len;
    mrb_get_args(mrb, "s", &string, &len);
    MP_ERR(ctx, "%s", string);
    return mrb_nil_value();
}

static void define_module(mrb_state *mrb)
{
    struct RClass *mod = mrb_define_module(mrb, "M");
    mrb_define_module_function(mrb, mod, "log", _log, MRB_ARGS_REQ(1));
}

static void load_script(mrb_state *mrb, const char *fname)
{
    struct script_ctx *ctx = get_ctx(mrb);
    char *file_path = mp_get_user_path(NULL, ctx->mpctx->global, fname);
    FILE *fp = fopen(file_path, "r");
    mrb_load_file(mrb, fp);
    fclose(fp);
    talloc_free(file_path);
}

static int load_mruby(struct mpv_handle *client, const char *fname)
{
    struct MPContext *mpctx = mp_client_get_core(client);
    int r = -1;

    struct script_ctx *ctx = talloc_ptrtype(NULL, ctx);
    *ctx = (struct script_ctx) {
        .name     = mpv_client_name(client),
        .filename = fname,
        .log      = mp_client_get_log(client),
        .client   = client,
        .mpctx    = mpctx,
    };

    mrb_state *mrb = ctx->state = mrb_open();
    mrb_sym sym = mrb_intern_cstr(mrb, "mpctx");
    mrb_vm_const_set(mrb, sym, mrb_cptr_value(mrb, ctx));
    define_module(mrb);

    if (!mrb)
        goto err_out;

    load_script(mrb, fname);
    mrb_close(ctx->state);
    r = 0;

err_out:
    if (ctx->state)
        talloc_free(ctx);
    return r;
}


const struct mp_scripting mp_scripting_mruby = {
    .file_ext = "mrb",
    .load = load_mruby,
};
