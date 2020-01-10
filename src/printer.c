/**
 * @file printer.c
 * @author Radek Krejci <rkrejci@cesnet.cz>
 * @brief Generic libyang printers functions.
 *
 * Copyright (c) 2015 - 2019 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include "common.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"
#include "printer_internal.h"

/**
 * @brief informational structure shared by printers
 */
struct ext_substmt_info_s ext_substmt_info[] = {
  {NULL, NULL, 0},                              /**< LYEXT_SUBSTMT_SELF */
  {"argument", "name", SUBST_FLAG_ID},          /**< LYEXT_SUBSTMT_ARGUMENT */
  {"base", "name", SUBST_FLAG_ID},              /**< LYEXT_SUBSTMT_BASE */
  {"belongs-to", "module", SUBST_FLAG_ID},      /**< LYEXT_SUBSTMT_BELONGSTO */
  {"contact", "text", SUBST_FLAG_YIN},          /**< LYEXT_SUBSTMT_CONTACT */
  {"default", "value", 0},                      /**< LYEXT_SUBSTMT_DEFAULT */
  {"description", "text", SUBST_FLAG_YIN},      /**< LYEXT_SUBSTMT_DESCRIPTION */
  {"error-app-tag", "value", 0},                /**< LYEXT_SUBSTMT_ERRTAG */
  {"error-message", "value", SUBST_FLAG_YIN},   /**< LYEXT_SUBSTMT_ERRMSG */
  {"key", "value", 0},                          /**< LYEXT_SUBSTMT_KEY */
  {"namespace", "uri", 0},                      /**< LYEXT_SUBSTMT_NAMESPACE */
  {"organization", "text", SUBST_FLAG_YIN},     /**< LYEXT_SUBSTMT_ORGANIZATION */
  {"path", "value", 0},                         /**< LYEXT_SUBSTMT_PATH */
  {"prefix", "value", SUBST_FLAG_ID},           /**< LYEXT_SUBSTMT_PREFIX */
  {"presence", "value", 0},                     /**< LYEXT_SUBSTMT_PRESENCE */
  {"reference", "text", SUBST_FLAG_YIN},        /**< LYEXT_SUBSTMT_REFERENCE */
  {"revision-date", "date", SUBST_FLAG_ID},     /**< LYEXT_SUBSTMT_REVISIONDATE */
  {"units", "name", 0},                         /**< LYEXT_SUBSTMT_UNITS */
  {"value", "value", SUBST_FLAG_ID},            /**< LYEXT_SUBSTMT_VALUE */
  {"yang-version", "value", SUBST_FLAG_ID},     /**< LYEXT_SUBSTMT_VERSION */
  {"modifier", "value", SUBST_FLAG_ID},         /**< LYEXT_SUBSTMT_MODIFIER */
  {"require-instance", "value", SUBST_FLAG_ID}, /**< LYEXT_SUBSTMT_REQINST */
  {"yin-element", "value", SUBST_FLAG_ID},      /**< LYEXT_SUBSTMT_YINELEM */
  {"config", "value", SUBST_FLAG_ID},           /**< LYEXT_SUBSTMT_CONFIG */
  {"mandatory", "value", SUBST_FLAG_ID},        /**< LYEXT_SUBSTMT_MANDATORY */
  {"ordered-by", "value", SUBST_FLAG_ID},       /**< LYEXT_SUBSTMT_ORDEREDBY */
  {"status", "value", SUBST_FLAG_ID},           /**< LYEXT_SUBSTMT_STATUS */
  {"fraction-digits", "value", SUBST_FLAG_ID},  /**< LYEXT_SUBSTMT_DIGITS */
  {"max-elements", "value", SUBST_FLAG_ID},     /**< LYEXT_SUBSTMT_MAX */
  {"min-elements", "value", SUBST_FLAG_ID},     /**< LYEXT_SUBSTMT_MIN */
  {"position", "value", SUBST_FLAG_ID},         /**< LYEXT_SUBSTMT_POSITION */
  {"unique", "tag", 0},                         /**< LYEXT_SUBSTMT_UNIQUE */
};

API LYP_OUT_TYPE
lyp_out_type(const struct lyp_out *out)
{
    LY_CHECK_ARG_RET(NULL, out, LYP_OUT_ERROR);
    return out->type;
}

API struct lyp_out *
lyp_new_clb(ssize_t (*writeclb)(void *arg, const void *buf, size_t count), void *arg)
{
    struct lyp_out *out;

    out = calloc(1, sizeof *out);
    LY_CHECK_ERR_RET(!out, LOGMEM(NULL), NULL);

    out->type = LYP_OUT_CALLBACK;
    out->method.clb.func = writeclb;
    out->method.clb.arg = arg;

    return out;
}

API ssize_t (*lyp_clb(struct lyp_out *out, ssize_t (*writeclb)(void *arg, const void *buf, size_t count)))(void *arg, const void *buf, size_t count)
{
    void *prev_clb;

    LY_CHECK_ARG_RET(NULL, out, out->type == LYP_OUT_CALLBACK, NULL);

    prev_clb = out->method.clb.func;

    if (writeclb) {
        out->method.clb.func = writeclb;
    }

    return prev_clb;
}

API void *
lyp_clb_arg(struct lyp_out *out, void *arg)
{
    void *prev_arg;

    LY_CHECK_ARG_RET(NULL, out, out->type == LYP_OUT_CALLBACK, NULL);

    prev_arg = out->method.clb.arg;

    if (arg) {
        out->method.clb.arg = arg;
    }

    return prev_arg;
}

API struct lyp_out *
lyp_new_fd(int fd)
{
    struct lyp_out *out;

    out = calloc(1, sizeof *out);
    LY_CHECK_ERR_RET(!out, LOGMEM(NULL), NULL);

#ifdef HAVE_VDPRINTF
    out->type = LYP_OUT_FD;
    out->method.fd = fd;
#else
    /* Without vdfprintf(), change the printing method to printing to a FILE stream.
     * To preserve the original file descriptor, duplicate it and use it to open file stream. */
    out->type = LYP_OUT_FDSTREAM;
    out->method.fdstream.fd = fd;

    fd = dup(out->method.fdstream.fd);
    if (fd < 0) {
        LOGERR(NULL, LY_ESYS, "Unable to duplicate provided file descriptor (%d) for printing the output (%s).",
               out->method.fdstream.fd, strerror(errno));
        free(out);
        return NULL;
    }
    out->method.fdstream.f = fdopen(fd, "a");
    if (!out->method.fdstream.f) {
        LOGERR(NULL, LY_ESYS, "Unable to open provided file descriptor (%d) for printing the output (%s).",
               out->method.fdstream.fd, strerror(errno));
        free(out);
        fclose(fd);
        return NULL;
    }
#endif

    return out;
}

API int
lyp_fd(struct lyp_out *out, int fd)
{
    int prev_fd;

    LY_CHECK_ARG_RET(NULL, out, out->type <= LYP_OUT_FDSTREAM, -1);

    if (out->type == LYP_OUT_FDSTREAM) {
        prev_fd = out->method.fdstream.fd;
    } else { /* LYP_OUT_FD */
        prev_fd = out->method.fd;
    }

    if (fd != -1) {
        /* replace output stream */
        if (out->type == LYP_OUT_FDSTREAM) {
            int streamfd;
            FILE *stream;

            streamfd = dup(fd);
            if (streamfd < 0) {
                LOGERR(NULL, LY_ESYS, "Unable to duplicate provided file descriptor (%d) for printing the output (%s).", fd, strerror(errno));
                return -1;
            }
            stream = fdopen(streamfd, "a");
            if (!stream) {
                LOGERR(NULL, LY_ESYS, "Unable to open provided file descriptor (%d) for printing the output (%s).", fd, strerror(errno));
                close(streamfd);
                return -1;
            }
            /* close only the internally created stream, file descriptor is returned and supposed to be closed by the caller */
            fclose(out->method.fdstream.f);
            out->method.fdstream.f = stream;
            out->method.fdstream.fd = streamfd;
        } else { /* LYP_OUT_FD */
            out->method.fd = fd;
        }
    }

    return prev_fd;
}

API struct lyp_out *
lyp_new_file(FILE *f)
{
    struct lyp_out *out;

    out = calloc(1, sizeof *out);
    LY_CHECK_ERR_RET(!out, LOGMEM(NULL), NULL);

    out->type = LYP_OUT_FILE;
    out->method.f = f;

    return out;
}

API FILE *
lyp_file(struct lyp_out *out, FILE *f)
{
    FILE *prev_f;

    LY_CHECK_ARG_RET(NULL, out, out->type == LYP_OUT_FILE, NULL);

    prev_f = out->method.f;

    if (f) {
        out->method.f = f;
    }

    return prev_f;
}

API struct lyp_out *
lyp_new_memory(char **strp, size_t size)
{
    struct lyp_out *out;

    out = calloc(1, sizeof *out);
    LY_CHECK_ERR_RET(!out, LOGMEM(NULL), NULL);

    out->type = LYP_OUT_MEMORY;
    out->method.mem.buf = strp;
    if (!size) {
        /* buffer is supposed to be allocated */
        *strp = NULL;
    } else if (*strp) {
        /* there is already buffer to use */
        out->method.mem.size = size;
    }

    return out;
}

char *
lyp_memory(struct lyp_out *out, char **strp, size_t size)
{
    char *data;

    LY_CHECK_ARG_RET(NULL, out, out->type == LYP_OUT_MEMORY, NULL);

    data = *out->method.mem.buf;

    if (strp) {
        out->method.mem.buf = strp;
        out->method.mem.len = out->method.mem.size = 0;
        out->printed = 0;
        if (!size) {
            /* buffer is supposed to be allocated */
            *strp = NULL;
        } else if (*strp) {
            /* there is already buffer to use */
            out->method.mem.size = size;
        }
    }

    return data;
}

API LY_ERR
lyp_out_reset(struct lyp_out *out)
{
    LY_CHECK_ARG_RET(NULL, out, LY_EINVAL);

    switch(out->type) {
    case LYP_OUT_ERROR:
        LOGINT(NULL);
        return LY_EINT;
    case LYP_OUT_FD:
        if ((lseek(out->method.fd, 0, SEEK_SET) == -1) && errno != ESPIPE) {
            LOGERR(NULL, LY_ESYS, "Seeking output file descriptor failed (%s).", strerror(errno));
            return LY_ESYS;
        }
        break;
    case LYP_OUT_FDSTREAM:
    case LYP_OUT_FILE:
    case LYP_OUT_FILEPATH:
        if ((fseek(out->method.f, 0, SEEK_SET) == -1) && errno != ESPIPE) {
            LOGERR(NULL, LY_ESYS, "Seeking output file stream failed (%s).", strerror(errno));
            return LY_ESYS;
        }
        break;
    case LYP_OUT_MEMORY:
        out->printed = 0;
        out->method.mem.len = 0;
        break;
    case LYP_OUT_CALLBACK:
        /* nothing to do (not seekable) */
        break;
    }

    return LY_SUCCESS;
}

API struct lyp_out *
lyp_new_filepath(const char *filepath)
{
    struct lyp_out *out;

    out = calloc(1, sizeof *out);
    LY_CHECK_ERR_RET(!out, LOGMEM(NULL), NULL);

    out->type = LYP_OUT_FILEPATH;
    out->method.fpath.f = fopen(filepath, "w");
    if (!out->method.fpath.f) {
        LOGERR(NULL, LY_ESYS, "Failed to open file \"%s\" (%s).", filepath, strerror(errno));
        return NULL;
    }
    out->method.fpath.filepath = strdup(filepath);
    return out;
}

API const char *
lyp_filepath(struct lyp_out *out, const char *filepath)
{
    FILE *f;

    LY_CHECK_ARG_RET(NULL, out, out->type == LYP_OUT_FILEPATH, filepath ? NULL : ((void *)-1));

    if (!filepath) {
        return out->method.fpath.filepath;
    }

    /* replace filepath */
    f = out->method.fpath.f;
    out->method.fpath.f = fopen(filepath, "w");
    if (!out->method.fpath.f) {
        LOGERR(NULL, LY_ESYS, "Failed to open file \"%s\" (%s).", filepath, strerror(errno));
        out->method.fpath.f = f;
        return ((void *)-1);
    }
    fclose(f);
    free(out->method.fpath.filepath);
    out->method.fpath.filepath = strdup(filepath);

    return NULL;
}

API void
lyp_free(struct lyp_out *out, void (*clb_arg_destructor)(void *arg), int destroy)
{
    if (!out) {
        return;
    }

    switch (out->type) {
    case LYP_OUT_CALLBACK:
        if (clb_arg_destructor) {
            clb_arg_destructor(out->method.clb.arg);
        }
        break;
    case LYP_OUT_FDSTREAM:
        fclose(out->method.fdstream.f);
        if (destroy) {
            close(out->method.fdstream.fd);
        }
        break;
    case LYP_OUT_FD:
        if (destroy) {
            close(out->method.fd);
        }
        break;
    case LYP_OUT_FILE:
        if (destroy) {
            fclose(out->method.f);
        }
        break;
    case LYP_OUT_MEMORY:
        if (destroy) {
            free(*out->method.mem.buf);
        }
        break;
    case LYP_OUT_FILEPATH:
        free(out->method.fpath.filepath);
        if (destroy) {
            fclose(out->method.fpath.f);
        }
        break;
    case LYP_OUT_ERROR:
        LOGINT(NULL);
    }
    free(out);
}

API LY_ERR
lyp_print(struct lyp_out *out, const char *format, ...)
{
    int count = 0;
    char *msg = NULL, *aux;
    va_list ap;

    LYOUT_CHECK(out, out->status);

    va_start(ap, format);

    switch (out->type) {
    case LYP_OUT_FD:
#ifdef HAVE_VDPRINTF
        count = vdprintf(out->method.fd, format, ap);
        break;
#else
        /* never should be here since lyp_fd() is supposed to set type to LYP_OUT_FDSTREAM in case vdprintf() is missing */
        LOGINT(NULL);
        return LY_EINT;
#endif
    case LYP_OUT_FDSTREAM:
    case LYP_OUT_FILEPATH:
    case LYP_OUT_FILE:
        count = vfprintf(out->method.f, format, ap);
        break;
    case LYP_OUT_MEMORY:
        if ((count = vasprintf(&msg, format, ap)) < 0) {
            break;
        }
        if (out->method.mem.len + count + 1 > out->method.mem.size) {
            aux = ly_realloc(*out->method.mem.buf, out->method.mem.len + count + 1);
            if (!aux) {
                out->method.mem.buf = NULL;
                out->method.mem.len = 0;
                out->method.mem.size = 0;
                LOGMEM(NULL);
                va_end(ap);
                return LY_EMEM;
            }
            *out->method.mem.buf = aux;
            out->method.mem.size = out->method.mem.len + count + 1;
        }
        memcpy(&(*out->method.mem.buf)[out->method.mem.len], msg, count);
        out->method.mem.len += count;
        (*out->method.mem.buf)[out->method.mem.len] = '\0';
        free(msg);
        break;
    case LYP_OUT_CALLBACK:
        if ((count = vasprintf(&msg, format, ap)) < 0) {
            break;
        }
        count = out->method.clb.func(out->method.clb.arg, msg, count);
        free(msg);
        break;
    case LYP_OUT_ERROR:
        LOGINT(NULL);
    }

    va_end(ap);

    if (count < 0) {
        LOGERR(out->ctx, LY_ESYS, "%s: writing data failed (%s).", __func__, strerror(errno));
        out->status = LY_ESYS;
        return LY_ESYS;
    } else {
        if (out->type == LYP_OUT_FDSTREAM) {
            /* move the original file descriptor to the end of the output file */
            lseek(out->method.fdstream.fd, 0, SEEK_END);
        }
        out->printed += count;
        return LY_SUCCESS;
    }
}

void
ly_print_flush(struct lyp_out *out)
{
    switch (out->type) {
    case LYP_OUT_FDSTREAM:
        /* move the original file descriptor to the end of the output file */
        lseek(out->method.fdstream.fd, 0, SEEK_END);
        fflush(out->method.fdstream.f);
        break;
    case LYP_OUT_FILEPATH:
    case LYP_OUT_FILE:
        fflush(out->method.f);
        break;
    case LYP_OUT_FD:
        fsync(out->method.fd);
        break;
    case LYP_OUT_MEMORY:
    case LYP_OUT_CALLBACK:
        /* nothing to do */
        break;
    case LYP_OUT_ERROR:
        LOGINT(NULL);
    }

    free(out->buffered);
    out->buf_size = out->buf_len = 0;
}

API LY_ERR
lyp_write(struct lyp_out *out, const char *buf, size_t len)
{
    int written = 0;

    LYOUT_CHECK(out, out->status);

    if (out->hole_count) {
        /* we are buffering data after a hole */
        if (out->buf_len + len > out->buf_size) {
            out->buffered = ly_realloc(out->buffered, out->buf_len + len);
            if (!out->buffered) {
                out->buf_len = 0;
                out->buf_size = 0;
                LOGMEM_RET(NULL);
            }
            out->buf_size = out->buf_len + len;
        }

        memcpy(&out->buffered[out->buf_len], buf, len);
        out->buf_len += len;
        return LY_SUCCESS;
    }

repeat:
    switch (out->type) {
    case LYP_OUT_MEMORY:
        if (out->method.mem.len + len + 1 > out->method.mem.size) {
            *out->method.mem.buf = ly_realloc(*out->method.mem.buf, out->method.mem.len + len + 1);
            if (!*out->method.mem.buf) {
                out->method.mem.len = 0;
                out->method.mem.size = 0;
                LOGMEM_RET(NULL);
            }
            out->method.mem.size = out->method.mem.len + len + 1;
        }
        memcpy(&(*out->method.mem.buf)[out->method.mem.len], buf, len);
        out->method.mem.len += len;
        (*out->method.mem.buf)[out->method.mem.len] = '\0';

        out->printed += len;
        return LY_SUCCESS;
    case LYP_OUT_FD:
        written = write(out->method.fd, buf, len);
        break;
    case LYP_OUT_FDSTREAM:
    case LYP_OUT_FILEPATH:
    case LYP_OUT_FILE:
        written =  fwrite(buf, sizeof *buf, len, out->method.f);
        break;
    case LYP_OUT_CALLBACK:
        written = out->method.clb.func(out->method.clb.arg, buf, len);
        break;
    case LYP_OUT_ERROR:
        LOGINT(NULL);
    }

    if (written < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            goto repeat;
        }
        LOGERR(out->ctx, LY_ESYS, "%s: writing data failed (%s).", __func__, strerror(errno));
        out->status = LY_ESYS;
        return LY_ESYS;
    } else if ((size_t)written != len) {
        LOGERR(out->ctx, LY_ESYS, "%s: writing data failed (unable to write %u from %u data).", __func__, len - (size_t)written, len);
        out->status = LY_ESYS;
        return LY_ESYS;
    } else {
        if (out->type == LYP_OUT_FDSTREAM) {
            /* move the original file descriptor to the end of the output file */
            lseek(out->method.fdstream.fd, 0, SEEK_END);
        }
        out->printed += written;
        return LY_SUCCESS;
    }
}

LY_ERR
ly_write_skip(struct lyp_out *out, size_t count, size_t *position)
{
    LYOUT_CHECK(out, out->status);

    switch (out->type) {
    case LYP_OUT_MEMORY:
        if (out->method.mem.len + count > out->method.mem.size) {
            *out->method.mem.buf = ly_realloc(*out->method.mem.buf, out->method.mem.len + count);
            if (!(*out->method.mem.buf)) {
                out->method.mem.len = 0;
                out->method.mem.size = 0;
                out->status = LY_ESYS;
                LOGMEM_RET(NULL);
            }
            out->method.mem.size = out->method.mem.len + count;
        }

        /* save the current position */
        *position = out->method.mem.len;

        /* skip the memory */
        out->method.mem.len += count;

        /* update printed bytes counter despite we actually printed just a hole */
        out->printed += count;
        break;
    case LYP_OUT_FD:
    case LYP_OUT_FDSTREAM:
    case LYP_OUT_FILEPATH:
    case LYP_OUT_FILE:
    case LYP_OUT_CALLBACK:
        /* buffer the hole */
        if (out->buf_len + count > out->buf_size) {
            out->buffered = ly_realloc(out->buffered, out->buf_len + count);
            if (!out->buffered) {
                out->buf_len = 0;
                out->buf_size = 0;
                out->status = LY_ESYS;
                LOGMEM_RET(NULL);
            }
            out->buf_size = out->buf_len + count;
        }

        /* save the current position */
        *position = out->buf_len;

        /* skip the memory */
        out->buf_len += count;

        /* increase hole counter */
        ++out->hole_count;

        break;
    case LYP_OUT_ERROR:
        LOGINT(NULL);
    }

    return LY_SUCCESS;
}

LY_ERR
ly_write_skipped(struct lyp_out *out, size_t position, const char *buf, size_t count)
{
    LY_ERR ret = LY_SUCCESS;

    LYOUT_CHECK(out, out->status);

    switch (out->type) {
    case LYP_OUT_MEMORY:
        /* write */
        memcpy(&(*out->method.mem.buf)[position], buf, count);
        break;
    case LYP_OUT_FD:
    case LYP_OUT_FDSTREAM:
    case LYP_OUT_FILEPATH:
    case LYP_OUT_FILE:
    case LYP_OUT_CALLBACK:
        if (out->buf_len < position + count) {
            out->status = LY_ESYS;
            LOGMEM_RET(NULL);
        }

        /* write into the hole */
        memcpy(&out->buffered[position], buf, count);

        /* decrease hole counter */
        --out->hole_count;

        if (!out->hole_count) {
            /* all holes filled, we can write the buffer,
             * printed bytes counter is updated by ly_write() */
            ret = lyp_write(out, out->buffered, out->buf_len);
            out->buf_len = 0;
        }
        break;
    case LYP_OUT_ERROR:
        LOGINT(NULL);
    }

    if (out->type == LYP_OUT_FILEPATH) {
        /* move the original file descriptor to the end of the output file */
        lseek(out->method.fdstream.fd, 0, SEEK_END);
    }
    return ret;
}
