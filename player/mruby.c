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
#include <mruby/array.h>
#include <mruby/compile.h>
#include <mruby/error.h>
#include <mruby/string.h>
#include <mruby/variable.h>

#include "common/msg.h"
#include "options/m_property.h"
#include "options/path.h"
#include "player/command.h"
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

static mrb_value _property_list(mrb_state *mrb, mrb_value self)
{
    const struct m_property *props = mp_get_property_list();
    mrb_value mrb_props = mrb_ary_new(mrb);
    for (int i = 0; props[i].name; i++) {
        mrb_value name = mrb_str_new_cstr(mrb, props[i].name);
        mrb_ary_push(mrb, mrb_props, name);
    }
    return mrb_props;
}

#define MRB_FN(a,b) \
    mrb_define_module_function(mrb, mod, #a, _ ## a, MRB_ARGS_REQ(b));
static void define_module(mrb_state *mrb)
{
    struct RClass *mod = mrb_define_module(mrb, "M");
    MRB_FN(log, 1);
    MRB_FN(property_list, 0);
}
#undef MRB_FN

static void print_backtrace(mrb_state *mrb)
{
    if (!mrb->exc)
        return;

    mrb_value exc = mrb_obj_value(mrb->exc);
    mrb_value bt  = mrb_exc_backtrace(mrb, exc);

    char *err = talloc_strdup(NULL, "");
    mrb_value exc_str = mrb_inspect(mrb, exc);
    err = talloc_asprintf_append(err, "%s\n", RSTRING_PTR(exc_str));

    mrb_int bt_len = mrb_ary_len(mrb, bt);
    err = talloc_asprintf_append(err, "backtrace:\n");
    for (int i = 0; i < bt_len; i++) {
        mrb_value s = mrb_ary_entry(bt, i);
        err = talloc_asprintf_append(err, "\t[%d] => %s\n", i, RSTRING_PTR(s));
    }

    struct script_ctx *ctx = get_ctx(mrb);
    MP_ERR(ctx, "%s", err);
    talloc_free(err);
}

static void load_script(mrb_state *mrb, const char *fname)
{
    struct script_ctx *ctx = get_ctx(mrb);
    char *file_path = mp_get_user_path(NULL, ctx->mpctx->global, fname);
    FILE *fp = fopen(file_path, "r");
    mrbc_context *mrb_ctx = mrbc_context_new(mrb);
    mrbc_filename(mrb, mrb_ctx, file_path);

    mrb_load_file_cxt(mrb, fp, mrb_ctx);
    print_backtrace(mrb);

    mrbc_context_free(mrb, mrb_ctx);

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

    r = 0;

err_out:
    if (ctx->state)
        mrb_close(ctx->state);
    talloc_free(ctx);
    return r;
}


const struct mp_scripting mp_scripting_mruby = {
    .file_ext = "mrb",
    .load = load_mruby,
};
